// source/platform/android/ui_window_android.c
// Android native activity glue and window core for the Apex UI library
// https://github.com/is-nobody/apex-ui
// MIT license

#ifdef __ANDROID__
#include "ui_window_android.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <malloc.h>

// android 14 tags native heap pointers by default (mte)
__attribute__((constructor))
static void apex_ui_disable_mte(void) {
#ifdef M_BIONIC_SET_HEAP_TAGGING_LEVEL
    mallopt(M_BIONIC_SET_HEAP_TAGGING_LEVEL, M_HEAP_TAGGING_LEVEL_NONE);
#endif
}

AndroidState android_state;

// the one window this backend hosts; input translation needs the scale
UiWindow* android_main_window = NULL;

// the host application entry
extern int main(int argc, char** argv);

// runs the host application; finishes the activity when main() returns
static void* app_thread_entry(void* arg) {
    (void)arg;
    main(0, NULL);
    // finish() is pointless while the system is already tearing us down
    if (android_state.activity && !android_state.destroyed)
        ANativeActivity_finish(android_state.activity);
    return NULL;
}

// looper callback for the app-thread wake fd
static int wake_cb(int fd, int events, void* data) {
    (void)events; (void)data;
    uint64_t n = 0;
    ssize_t got = read(fd, &n, sizeof(n));               // clear the wakeup
    if (got < 0) {}                                      // EAGAIN: nothing to drain
    return 1;                                            // keep the registration
}

// looper callback running on the ui thread
static int ui_thread_cb(int fd, int events, void* data) {
    (void)events; (void)data;
    uint64_t n = 0;
    ssize_t got = read(fd, &n, sizeof(n));               // clear the wakeup
    if (got < 0) {}
    android_apply_immersive();                           // safe here: ui thread
    android_apply_ime();                                 // creates the hidden EditText if needed
    android_apply_ime_sync();                            // drain pending text/cursor sync
    return 1;                                            // keep the registration
}

static void on_destroy(ANativeActivity* activity) {
    (void)activity;
    pthread_mutex_lock(&android_state.lock);
    android_state.destroyed = true;                      // stops further event queueing
    if (android_main_window) android_main_window->open = false;
    pthread_mutex_unlock(&android_state.lock);

    // the event queue drops everything once destroyed
    pthread_mutex_lock(&android_state.lock);
    if (android_state.wake_fd >= 0) {
        uint64_t one = 1;
        ssize_t ignored = write(android_state.wake_fd, &one, sizeof(one));
        if (ignored < 0) {}
    }
    pthread_mutex_unlock(&android_state.lock);

    if (android_state.app_started)                       // wait for main() to return
        pthread_join(android_state.app_thread, NULL);
    APEXUI_LOG("activity destroyed");
}

static void on_native_window_created(ANativeActivity* activity, ANativeWindow* window) {
    (void)activity;
    pthread_mutex_lock(&android_state.lock);
    android_state.surface = window;
    pthread_mutex_unlock(&android_state.lock);
    BEvent ev; memset(&ev, 0, sizeof(ev));
    ev.internal = BMSG_SURFACE_CREATED;
    bevent_push(&ev);
}

static void on_native_window_resized(ANativeActivity* activity, ANativeWindow* window) {
    (void)activity;
    BEvent ev; memset(&ev, 0, sizeof(ev));
    ev.internal        = BMSG_SURFACE_RESIZED;
    ev.we.w            = ANativeWindow_getWidth(window);   // physical dims for the handler
    ev.we.h            = ANativeWindow_getHeight(window);
    bevent_push(&ev);
}

static void on_native_window_redraw_needed(ANativeActivity* activity, ANativeWindow* window) {
    (void)activity; (void)window;
    BEvent ev; memset(&ev, 0, sizeof(ev));
    ev.internal = BMSG_REDRAW;
    bevent_push(&ev);
}

static void on_native_window_destroyed(ANativeActivity* activity, ANativeWindow* window) {
    (void)activity; (void)window;
    pthread_mutex_lock(&android_state.lock);
    android_state.surface = NULL;                        // stop presenting immediately
    pthread_mutex_unlock(&android_state.lock);
    BEvent ev; memset(&ev, 0, sizeof(ev));
    ev.internal = BMSG_SURFACE_DESTROYED;
    bevent_push(&ev);
}

static void on_input_queue_created(ANativeActivity* activity, AInputQueue* queue) {
    (void)activity;
    pthread_mutex_lock(&android_state.lock);
    android_state.input_queue  = queue;
    android_state.input_attached = false;                // attach happens on the app thread
    pthread_mutex_unlock(&android_state.lock);
    BEvent ev; memset(&ev, 0, sizeof(ev));
    ev.internal = BMSG_INPUT_ATTACH;
    bevent_push(&ev);
}

static void on_input_queue_destroyed(ANativeActivity* activity, AInputQueue* queue) {
    (void)activity; (void)queue;
    BEvent ev; memset(&ev, 0, sizeof(ev));
    ev.internal = BMSG_INPUT_DETACH;
    bevent_push(&ev);
}

// captures the ui-thread looper once and applies the current immersive state
static void on_window_focus_changed(ANativeActivity* activity, int focused) {
    (void)activity;
    pthread_mutex_lock(&android_state.lock);
    if (!android_state.main_registered && focused) {     // one-time main looper capture
        ALooper* main_loop = ALooper_forThread();        // non-null on the ui thread
        if (main_loop && android_state.ui_fd >= 0) {
            ALooper_addFd(main_loop, android_state.ui_fd, 1001,
                          ALOOPER_EVENT_INPUT, ui_thread_cb, NULL);
            android_state.main_looper     = main_loop;
            android_state.main_registered = true;
        }
    }
    pthread_mutex_unlock(&android_state.lock);
    if (focused) android_apply_immersive();              // restore chrome after resume
}

static void on_pause(ANativeActivity* activity) { (void)activity; }
static void on_resume(ANativeActivity* activity) { (void)activity; }
static void on_start(ANativeActivity* activity) { (void)activity; }
static void on_stop(ANativeActivity* activity) { (void)activity; }

static void on_configuration_changed(ANativeActivity* activity) { (void)activity; }
static void on_low_memory(ANativeActivity* activity) { (void)activity; }
static void on_content_rect_changed(ANativeActivity* activity, const ARect* rect) {
    (void)activity; (void)rect;                          // surface resize events cover us
}
static void* on_save_instance_state(ANativeActivity* activity, size_t* out_len) {
    (void)activity;
    if (out_len) *out_len = 0;                           // no state to preserve
    return NULL;
}

// native activity entry point
void ANativeActivity_onCreate(ANativeActivity* activity,
                              void* saved_state, size_t saved_state_size) {
    (void)saved_state; (void)saved_state_size;

    memset(&android_state, 0, sizeof(android_state));    // fresh state per activity
    pthread_mutex_init(&android_state.lock, NULL);
    android_state.wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    android_state.ui_fd   = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    android_state.activity = activity;
    android_state.vm       = activity->vm;

    // android 14 sometimes ignores a wholesale pointer replacement of activity->callbacks
    if (activity->callbacks) {
        activity->callbacks->onStart                    = on_start;
        activity->callbacks->onResume                   = on_resume;
        activity->callbacks->onPause                    = on_pause;
        activity->callbacks->onStop                     = on_stop;
        activity->callbacks->onDestroy                  = on_destroy;
        activity->callbacks->onWindowFocusChanged       = on_window_focus_changed;
        activity->callbacks->onNativeWindowCreated      = on_native_window_created;
        activity->callbacks->onNativeWindowResized      = on_native_window_resized;
        activity->callbacks->onNativeWindowRedrawNeeded = on_native_window_redraw_needed;
        activity->callbacks->onNativeWindowDestroyed    = on_native_window_destroyed;
        activity->callbacks->onInputQueueCreated        = on_input_queue_created;
        activity->callbacks->onInputQueueDestroyed      = on_input_queue_destroyed;
        activity->callbacks->onContentRectChanged       = on_content_rect_changed;
        activity->callbacks->onConfigurationChanged     = on_configuration_changed;
        activity->callbacks->onLowMemory                = on_low_memory;
        activity->callbacks->onSaveInstanceState        = on_save_instance_state;
        APEXUI_LOG("onCreate: callbacks mutated, onStart=%p onNativeWindowCreated=%p",
                   (void*)activity->callbacks->onStart,
                   (void*)activity->callbacks->onNativeWindowCreated);
    } else {
        APEXUI_LOG("onCreate: activity->callbacks is NULL");
    }

    APEXUI_LOG("activity created; spawning the app thread");
    if (pthread_create(&android_state.app_thread, NULL, app_thread_entry, NULL) == 0) {
        android_state.app_started = true;
    } else {
        APEXUI_LOG("failed to spawn the app thread");
    }
}

// picks the integer upscale factor
static int android_pick_scale(int phys_w) {
    int d = android_state.density_milli ? android_state.density_milli
                                        : android_density_milli();
    android_state.density_milli = d;
    int scale = (d > 0) ? (d + 500) / 1000      // round(2.75) -> 3
                        : (phys_w + 210) / 420; // width heuristic fallback
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    return scale;
}

// (re)allocates the logical framebuffer for a new client size
static void android_realloc_framebuffer(UiWindow* w, int lw, int lh) {
    if (lw < 1) lw = 1;
    if (lh < 1) lh = 1;
    if (lw == w->w && lh == w->h && w->pixels) return;   // unchanged: keep the buffer
    free(w->pixels);
    w->pixels = (uint32_t*)calloc((size_t)lw * (size_t)lh, 4);
    w->w = lw;  w->h = lh;
    w->buf_w = lw; w->buf_h = lh;
}

// adopts the current surface
void android_surface_ready(UiWindow* w) {
    ANativeWindow* surf = android_state.surface;
    if (!surf || !w) return;
    int pw = ANativeWindow_getWidth(surf);
    int ph = ANativeWindow_getHeight(surf);
    if (pw < 1 || ph < 1) return;

    // cpu framebuffer -> surface blit: force a known rgba byte order
    ANativeWindow_setBuffersGeometry(surf, pw, ph, WINDOW_FORMAT_RGBA_8888);

    w->surf_w = pw; w->surf_h = ph;
    w->scale  = android_pick_scale(pw);
    android_realloc_framebuffer(w, pw / w->scale, ph / w->scale);
    w->surface_ok = true;

    BEvent ev; memset(&ev, 0, sizeof(ev));               // surface size wins over the
    ev.we.kind  = UW_EV_RESIZE;                          // size asked by ui.open()
    ev.we.w     = w->w;
    ev.we.h     = w->h;
    bevent_push(&ev);
}

// adopts new surface dimensions after a rotation or a split-screen move
void android_surface_resized(UiWindow* w, int pw, int ph) {
    if (!w || pw < 1 || ph < 1) return;
    if (pw == w->surf_w && ph == w->surf_h) return;      // no real change
    android_surface_ready(w);                            // same path: geometry + resize
}

// creates the host window. android ignores the requested size and title
UiWindow* ui_window_create(int w, int h, const char* title, bool decorated) {
    (void)title; (void)decorated;                        // the activity owns both
    if (!android_state.activity || android_state.destroyed) return NULL;
    if (android_main_window) return NULL;                // one window per process

    if (!android_state.looper)                           // app-thread message loop
        android_state.looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);

    UiWindow* win = (UiWindow*)calloc(1, sizeof(UiWindow));
    if (!win) return NULL;
    win->w = w;  win->h = h;                             // provisional size until the
    win->buf_w = w; win->buf_h = h;                      // first surface event lands
    win->scale = 1;
    win->open = true;
    win->pixels = (uint32_t*)calloc((size_t)w * (size_t)h, 4);
    android_main_window = win;

    if (android_state.wake_fd >= 0 && !android_state.wake_registered) {
        ALooper_addFd(android_state.looper, android_state.wake_fd, 1000,
                      ALOOPER_EVENT_INPUT, wake_cb, NULL);
        android_state.wake_registered = true;
    }

    // the surface usually exists by now; wait a moment in case we started early
    int waited_ms = 0;
    while (!android_state.surface && !android_state.destroyed && waited_ms < 5000) {
        ALooper_pollOnce(50, NULL, NULL, NULL);          // dispatch lifecycle wakeups
        waited_ms += 50;
    }
    if (android_state.surface) android_surface_ready(win);
    return win;
}

// destroys the window and releases everything it owns
void ui_window_destroy(UiWindow* w) {
    if (!w) return;

    backend_detach_input();                              // release the input queue
    if (android_state.wake_registered && android_state.looper && android_state.wake_fd >= 0) {
        ALooper_removeFd(android_state.looper, android_state.wake_fd);
        android_state.wake_registered = false;
    }
    
    // only remove the main_looper fd if the ui thread is not blocked in on_destroy
    if (android_state.main_registered && android_state.main_looper && 
        android_state.ui_fd >= 0 && !android_state.destroyed) {
        ALooper_removeFd(android_state.main_looper, android_state.ui_fd);
        android_state.main_registered = false;
        android_state.main_looper = NULL;
    }
    // close both wake fds under the lock
    pthread_mutex_lock(&android_state.lock);
    if (android_state.wake_fd >= 0) { close(android_state.wake_fd); android_state.wake_fd = -1; }
    if (android_state.ui_fd >= 0)   { close(android_state.ui_fd);   android_state.ui_fd   = -1; }
    pthread_mutex_unlock(&android_state.lock);

    pthread_mutex_lock(&android_state.lock);
    android_state.surface = NULL;
    pthread_mutex_unlock(&android_state.lock);

    free(w->pixels);
    free(w->clipboard_text);
    if (android_main_window == w) android_main_window = NULL;
    android_ime_destroy();                               // release the hidden EditText
    free(w);
}

// paint callback is unused on android; repaints are driven by the event loop
void ui_window_set_paint_callback(UiWindow* w, UiPaintCallback cb, void* user) {
    (void)w; (void)cb; (void)user;
}

// returns the logical framebuffer the painter should target
uint32_t* ui_window_pixels(UiWindow* w) {
    return w ? w->pixels : NULL;
}

// row pitch of the framebuffer in pixels
int ui_window_stride(UiWindow* w) {
    return w ? w->buf_w : 0;
}

int ui_window_width(UiWindow* w)  { return w ? w->w : 0; }
int ui_window_height(UiWindow* w) { return w ? w->h : 0; }

// the system owns the window size on android; a resize request is a no-op
void ui_window_resize(UiWindow* w, int nw, int nh) {
    (void)w; (void)nw; (void)nh;
}

// marks the window as closed so callers stop polling it
void ui_window_close(UiWindow* w) {
    if (w) w->open = false;
}

// creates a frameless popup window used for notifications
UiWindow* ui_window_create_popup(int x, int y, int w, int h, const char* title) {
    (void)x; (void)y; (void)w; (void)h; (void)title;
    APEXUI_LOG("notification popups are not supported on android");
    return NULL;
}

// shows or hides the platform soft keyboard
void ui_window_ime_show(UiWindow* w, bool show) {
    (void)w;
    pthread_mutex_lock(&android_state.lock);
    bool changed   = (show != android_state.want_ime);
    // keep poking until ime_shown matches
    bool need_poke = changed || (show && !android_state.ime_shown);
    if (!need_poke) {
        pthread_mutex_unlock(&android_state.lock);
        return;
    }
    android_state.want_ime = show;
    if (!show) {
        free(android_state.ime_last_text);
        android_state.ime_last_text     = NULL;
        android_state.ime_last_text_len = 0;
        android_state.ime_last_sync_cursor = 0;
        android_state.ime_last_sync_sel    = 0;
    }
    if (android_state.main_registered && android_state.ui_fd >= 0) {
        uint64_t one = 1;
        ssize_t ignored = write(android_state.ui_fd, &one, sizeof(one));
        if (ignored < 0) {}
    }
    pthread_mutex_unlock(&android_state.lock);
}

// tells the hidden EditText what text, caret and input type to use
void ui_window_ime_sync(UiWindow* w, const char* text, int text_len,
                        int cursor_byte, int sel_byte, bool multiline) {
    (void)w;
    if (android_state.destroyed) return;
    if (!text) { text = ""; text_len = 0; }
    if (cursor_byte < 0) cursor_byte = text_len;
    if (sel_byte    < 0) sel_byte    = cursor_byte;

    pthread_mutex_lock(&android_state.lock);
    if (!android_state.ime_view) {
        // no hidden EditText yet: nothing to sync
        pthread_mutex_unlock(&android_state.lock);
        return;
    }

    bool need_text = (text_len != android_state.ime_last_text_len) ||
                     (text_len > 0 &&
                      (!android_state.ime_last_text ||
                       memcmp(text, android_state.ime_last_text, (size_t)text_len) != 0));
    bool need_cur  = (cursor_byte != android_state.ime_last_sync_cursor) ||
                     (sel_byte    != android_state.ime_last_sync_sel);
    bool need_ml   = (multiline != android_state.ime_multiline);

    if (!need_text && !need_cur && !need_ml) {
        pthread_mutex_unlock(&android_state.lock);
        return;
    }

    if (need_text) {
        free(android_state.ime_pending_text);
        android_state.ime_pending_text = (char*)malloc((size_t)text_len + 1);
        if (text_len > 0 && android_state.ime_pending_text)
            memcpy(android_state.ime_pending_text, text, (size_t)text_len);
        if (android_state.ime_pending_text)
            android_state.ime_pending_text[text_len] = 0;
        android_state.ime_pending_text_len    = text_len;
        android_state.ime_pending_text_dirty  = true;
        android_state.ime_last_sync_cursor    = cursor_byte;
        android_state.ime_last_sync_sel       = sel_byte;
    }
    if (need_cur) {
        android_state.ime_pending_cursor       = cursor_byte;
        android_state.ime_pending_sel          = sel_byte;
        android_state.ime_pending_cursor_dirty = true;
        android_state.ime_last_sync_cursor     = cursor_byte;
        android_state.ime_last_sync_sel        = sel_byte;
    }
    if (need_ml) {
        android_state.ime_pending_multiline   = multiline;
        android_state.ime_pending_ml_dirty    = true;
        android_state.ime_multiline           = multiline;
    }
    android_state.ime_pending = true;
    if (android_state.main_registered && android_state.ui_fd >= 0) {
        uint64_t one = 1;
        ssize_t ignored = write(android_state.ui_fd, &one, sizeof(one));
        if (ignored < 0) {}
    }
    pthread_mutex_unlock(&android_state.lock);
}

// updates the window title
void ui_window_set_title(UiWindow* w, const char* t) {
    (void)w; (void)t;
}

// blits the whole framebuffer to the surface
void ui_window_present(UiWindow* w) {
    if (!w) return;
    int quad[4] = { 0, 0, w->w, w->h };
    ui_window_present_rects(w, quad, 1);
}

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

// converts one row from 0xaarrggbb words to the surface's rgba byte order
static void blit_swap_row(uint32_t* dst, const uint32_t* src, int n) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    const uint32x4_t keep   = vdupq_n_u32(0xFF00FF00u);  // alpha + green pass through
    const uint32x4_t ff     = vdupq_n_u32(0xFFu);
    const uint32x4_t opaque = vdupq_n_u32(0xFF000000u);
    int i = 0;
    for (; i + 4 <= n; i += 4) {                         // 4 pixels per pass
        uint32x4_t v = vld1q_u32(src + i);
        uint32x4_t r = vandq_u32(vshrq_n_u32(v, 16), ff);
        uint32x4_t b = vshlq_n_u32(vandq_u32(v, ff), 16);
        vst1q_u32(dst + i, vorrq_u32(vorrq_u32(vandq_u32(v, keep), vorrq_u32(r, b)), opaque));
    }
    for (; i < n; i++) {                                 // scalar tail
        uint32_t v = src[i];
        dst[i] = (v & 0xFF00FF00u) | ((v >> 16) & 0xFFu) | ((v & 0xFFu) << 16) | 0xFF000000u;
    }
#else
    for (int i = 0; i < n; i++) {
        uint32_t v = src[i];
        dst[i] = (v & 0xFF00FF00u) | ((v >> 16) & 0xFFu) | ((v & 0xFFu) << 16) | 0xFF000000u;
    }
#endif
}

// resamples one prepared row to the surface width (nearest neighbor)
static void blit_expand_row(uint32_t* dst, int dst_w, const uint32_t* src, int src_w) {
    if (dst_w == src_w) {                                // 1:1: plain copy
        memcpy(dst, src, (size_t)dst_w * 4);
        return;
    }
    uint32_t step = (uint32_t)(((uint64_t)src_w << 16) / (uint64_t)dst_w);
    uint32_t pos  = step / 2;                            // sample pixel centers
    for (int x = 0; x < dst_w; x++) {
        int sx = (int)(pos >> 16);
        if (sx >= src_w) sx = src_w - 1;
        dst[x] = src[sx];
        pos += step;
    }
}

// presents the framebuffer. android surfaces must be redrawn in full
void ui_window_present_rects(UiWindow* w, const int* quads, int n) {
    (void)quads; (void)n;
    if (!w || !w->surface_ok || !w->pixels || w->w < 1 || w->h < 1) return;
    ANativeWindow* surf = android_state.surface;         // may vanish mid-frame
    if (!surf) return;

    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(surf, &buf, NULL) != 0) return;

    int dst_w = buf.width;
    int dst_h = buf.height;
    if (dst_w > 0 && dst_h > 0) {
        uint32_t* row_tmp = (uint32_t*)malloc((size_t)w->w * 4);  // swapped source row
        if (row_tmp) {
            int last_sy = -1;                            // upscaled rows repeat
            for (int y = 0; y < dst_h; y++) {
                int sy = (int)(((int64_t)y * w->h) / dst_h);
                if (sy >= w->h) sy = w->h - 1;
                if (sy != last_sy) {
                    blit_swap_row(row_tmp, w->pixels + (size_t)sy * (size_t)w->buf_w, w->w);
                    last_sy = sy;
                }
                uint32_t* dst = (uint32_t*)buf.bits + (size_t)y * (size_t)buf.stride;
                blit_expand_row(dst, dst_w, row_tmp, w->w);
            }
            free(row_tmp);
        }
    }
    ANativeWindow_unlockAndPost(surf);
}

// flips the desired immersive state and pokes the ui thread to apply it
void ui_window_toggle_fullscreen(UiWindow* w) {
    (void)w;
    pthread_mutex_lock(&android_state.lock);
    android_state.want_immersive = !android_state.want_immersive;
    if (android_state.main_registered && android_state.ui_fd >= 0) {
        // write under the lock: the fd may be closed concurrently by destroy
        uint64_t one = 1;
        ssize_t ignored = write(android_state.ui_fd, &one, sizeof(one));
        if (ignored < 0) {}                              // callback applies it anyway
    }
    pthread_mutex_unlock(&android_state.lock);
    // without a registered main looper
}

bool ui_window_is_fullscreen(UiWindow* w) {
    (void)w;
    return android_state.want_immersive || android_state.immersive;
}

// window-manager gestures have no android equivalent
void ui_window_minimize(UiWindow* w)          { (void)w; }
void ui_window_toggle_maximize(UiWindow* w)   { (void)w; }
bool ui_window_is_maximized(UiWindow* w)      { (void)w; return false; }
void ui_window_begin_drag(UiWindow* w)        { (void)w; }
void ui_window_begin_resize(UiWindow* w, const char* edge) { (void)w; (void)edge; }
void ui_window_set_resize_cursor(UiWindow* w, const char* edge) { (void)w; (void)edge; }
void ui_window_set_min_size(UiWindow* w, int min_w, int min_h) { (void)w; (void)min_w; (void)min_h; }
void ui_window_sync_ack(UiWindow* w)          { (void)w; }
void ui_window_move(UiWindow* w, int x, int y) { (void)w; (void)x; (void)y; }

// the whole surface is the work area
void ui_window_work_area(UiWindow* w, int* ox, int* oy, int* ow, int* oh) {
    int lw = w ? w->w : 0, lh = w ? w->h : 0;
    if (ox) *ox = 0;
    if (oy) *oy = 0;
    if (ow) *ow = lw;
    if (oh) *oh = lh;
}

// nudge the event loop into a repaint
void ui_window_invalidate(UiWindow* w) {
    (void)w;
    BEvent ev; memset(&ev, 0, sizeof(ev));
    ev.internal = BMSG_REDRAW;
    bevent_push(&ev);
}

#endif // __ANDROID__