// source/platform/android/ui_window_android.h
// Internal android backend declarations for the Apex UI library
// https://github.com/is-nobody/apex-ui
// MIT license

#ifndef UI_WINDOW_ANDROID_H
#define UI_WINDOW_ANDROID_H

#ifdef __ANDROID__

#include "ui_window.h"
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

// logcat tag + helper for backend diagnostics
#define APEXUI_LOG(...) __android_log_print(ANDROID_LOG_INFO, "apex-ui", __VA_ARGS__)

// process-wide android backend state
typedef struct {
    ANativeActivity*  activity;        // hosting activity, set before the app thread runs
    ANativeWindow*    surface;         // current native surface, null while backgrounded
    JavaVM*           vm;              // jvm used for clipboard / fullscreen jni calls
    pthread_mutex_t   lock;            // guards the event queue and the flags below
    int               wake_fd;         // eventfd that wakes the app-thread looper
    int               ui_fd;           // eventfd that pokes the ui-thread looper
    ALooper*          looper;          // app-thread looper, null until prepared
    ALooper*          main_looper;     // ui-thread looper captured by a lifecycle callback
    AInputQueue*      input_queue;     // pending input queue, attached on the app thread
    bool              input_attached;  // true once the queue is attached to our looper
    bool              wake_registered; // true once wake_fd is registered with our looper
    bool              main_registered; // true once ui_fd is registered with the main looper
    bool              want_immersive;  // desired immersive (fullscreen chrome) state
    bool              immersive;       // last applied immersive state
    bool              want_ime;        // desired soft keyboard state
    bool              ime_shown;       // last applied soft keyboard state
    bool              destroyed;       // activity is going away; drop further events
    int               density_milli;   // system density * 1000, 0 = unknown
    pthread_t         app_thread;      // thread that runs the host's main()
    bool              app_started;     // app thread was spawned

    // hidden android.widget.EditText that owns the ime connection
    jobject           ime_view;        // global ref to the EditText, or NULL
    bool              ime_view_ready;  // true once it has been added to the decor view
    bool              ime_multiline;   // input type currently applied to ime_view

    // text mirror used by the app thread to diff against what the ime has committed
    char*             ime_last_text;
    int               ime_last_text_len;
    int               ime_last_sync_cursor;   // last cursor byte pushed to the IME
    int               ime_last_sync_sel;      // last selection anchor byte
    uint32_t          ime_poll_gen;           // bumped whenever ime_last_text is
                                              // rewritten from the UI thread, so a
                                              // racing poll can drop a stale read

    // pending ui-thread work
    bool              ime_pending;
    char*             ime_pending_text;       // malloc'd UTF-8, or NULL
    int               ime_pending_text_len;
    bool              ime_pending_text_dirty;
    int               ime_pending_cursor;
    int               ime_pending_sel;
    bool              ime_pending_cursor_dirty;
    bool              ime_pending_multiline;
    bool              ime_pending_ml_dirty;
} AndroidState;

extern AndroidState android_state;

// the one live window this backend hosts; input translation needs its scale
extern UiWindow* android_main_window;

// window state for the android backend
struct UiWindow {
    int       w, h;            // logical client size in pixels (framebuffer size)
    int       buf_w, buf_h;    // backing buffer size; equals w/h on android
    int       surf_w, surf_h;  // physical surface size in pixels
    int       scale;           // integer upscale factor applied on present
    bool      open;            // false once the window is closed
    bool      surface_ok;      // true while a native surface is available
    uint32_t* pixels;          // logical cpu framebuffer
    char*     clipboard_text;  // owned copy of the string we last set
    int       clipboard_len;   // length of that copy
};

// internal backend messages queued by the ui-thread lifecycle callbacks
enum {
    BMSG_SURFACE_CREATED = 1,   // surface is ready for rendering
    BMSG_SURFACE_RESIZED,       // surface changed size; w/h carry physical dims
    BMSG_SURFACE_DESTROYED,     // surface is gone; stop presenting until it returns
    BMSG_REDRAW,                // the system asks for a repaint
    BMSG_INPUT_ATTACH,          // attach the pending input queue to our looper
    BMSG_INPUT_DETACH,          // the input queue went away
    BMSG_DESTROY,               // activity is being destroyed
};

// one queued backend event; internal messages carry payload in we.w / we.h
typedef struct {
    UiWinEvent we;              // translated window event, valid when internal == 0
    int        internal;        // 0 = window event, else a BMSG_* id
} BEvent;

// event queue (ui_window_android_event.c)
bool bevent_push(const BEvent* ev);        // thread-safe; wakes the app thread
bool bevent_pop(BEvent* out);              // app thread only

// input plumbing (ui_window_android_event.c)
void backend_attach_input(void);           // attach the pending queue to our looper
void backend_detach_input(void);           // release the current input queue

// surface handling (ui_window_android.c), called on the app thread from poll()
void android_surface_ready(UiWindow* w);               // adopt the current surface
void android_surface_resized(UiWindow* w, int pw, int ph);  // adopt new surface dims

// jni + system integration (ui_window_android_jni.c)
JNIEnv* android_attach_env(void);          // env for the calling thread, attaches on demand
void    android_apply_immersive(void);     // push the immersive flags to the decor view
void    android_apply_ime(void);           // create the EditText if needed, then show/hide it
void    android_apply_ime_sync(void);      // drain the pending text/cursor sync on the ui thread
int     android_density_milli(void);       // displaymetrics density * 1000, 0 = unknown

// reads the hidden EditText, diffs it against the last polled text
void    android_ime_poll(void);

// releases the global ref to the hidden EditText and every pending buffer.
void    android_ime_destroy(void);

bool    android_clipboard_set_text(const char* text, int len);
char*   android_clipboard_get_text(int* out_len);   // malloc'd utf-8, free() it
int     android_translate_key(int keyCode, int metaState, int repeatCount, int scanCode, int flags, int source, int deviceId, int64_t downTime, int64_t eventTime, int action);

#endif // __ANDROID__
#endif // UI_WINDOW_ANDROID_H