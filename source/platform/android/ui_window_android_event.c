// source/platform/android/ui_window_android_event.c
// Android event queue, input translation and polling for the Apex UI library
// https://github.com/is-nobody/apex-ui
// MIT license

#ifdef __ANDROID__
#include "ui_window_android.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

// the shared event queue
static BEvent* g_queue        = NULL;
static int     g_queue_count  = 0;
static int     g_queue_cap    = 0;

static int  input_queue_cb(int fd, int events, void* data);
static void queue_key_event(const AInputEvent* event);
static void queue_motion_event(const AInputEvent* event);

// translates one input event; returns 1 so android knows it was consumed
int backend_translate_input(const AInputEvent* event) {
    switch (AInputEvent_getType(event)) {
        case AINPUT_EVENT_TYPE_KEY:    queue_key_event(event);    return 1;
        case AINPUT_EVENT_TYPE_MOTION: queue_motion_event(event); return 1;
        default: return 0;
    }
}

// pushes one event and wakes the app-thread looper; thread-safe
bool bevent_push(const BEvent* ev) {
    pthread_mutex_lock(&android_state.lock);
    bool ok = !android_state.destroyed;                  // dying: drop everything
    if (ok) {
        if (g_queue_count >= g_queue_cap) {              // grow the ring
            int cap = g_queue_cap ? g_queue_cap * 2 : 64;
            BEvent* grown = (BEvent*)realloc(g_queue, sizeof(BEvent) * (size_t)cap);
            if (grown) {
                g_queue     = grown;
                g_queue_cap = cap;
            } else {
                ok = false;
            }
        }
        if (ok) g_queue[g_queue_count++] = *ev;
    }
    // wake the app thread while still holding the lock
    if (ok && android_state.wake_fd >= 0) {
        uint64_t one = 1;
        ssize_t ignored = write(android_state.wake_fd, &one, sizeof(one));
        if (ignored < 0) {}
    }
    pthread_mutex_unlock(&android_state.lock);
    return ok;
}

// pops the oldest event; app thread only
bool bevent_pop(BEvent* out) {
    pthread_mutex_lock(&android_state.lock);
    bool ok = g_queue_count > 0;
    if (ok) {
        *out = g_queue[0];
        memmove(&g_queue[0], &g_queue[1], sizeof(BEvent) * (size_t)(g_queue_count - 1));
        g_queue_count--;
    }
    pthread_mutex_unlock(&android_state.lock);
    return ok;
}

// queue occupancy probe used by wait/poll
static bool queue_nonempty(void) {
    pthread_mutex_lock(&android_state.lock);
    bool pending = g_queue_count > 0;
    pthread_mutex_unlock(&android_state.lock);
    return pending;
}

// monotonic clock in milliseconds, for poll deadlines
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

// attaches the pending input queue to the app-thread looper
void backend_attach_input(void) {
    pthread_mutex_lock(&android_state.lock);
    AInputQueue* queue   = android_state.input_queue;
    bool         already = android_state.input_attached;
    ALooper*     looper  = android_state.looper;
    pthread_mutex_unlock(&android_state.lock);
    if (!queue || already || !looper) return;
    AInputQueue_attachLooper(queue, looper, ALOOPER_POLL_CALLBACK, input_queue_cb, NULL);
    pthread_mutex_lock(&android_state.lock);
    android_state.input_attached = true;                 // only after a real attach
    pthread_mutex_unlock(&android_state.lock);
}

// releases the current input queue, if any
void backend_detach_input(void) {
    pthread_mutex_lock(&android_state.lock);
    AInputQueue* queue   = android_state.input_queue;
    bool         attached = android_state.input_attached;
    android_state.input_queue    = NULL;
    android_state.input_attached = false;
    pthread_mutex_unlock(&android_state.lock);
    if (queue && attached && android_state.looper)
        AInputQueue_detachLooper(queue);
}

// looper callback for the input queue: drains every pending input event
int input_queue_cb(int fd, int events, void* data) {
    (void)fd; (void)data;
    if (!(events & ALOOPER_EVENT_INPUT)) return 1;
    pthread_mutex_lock(&android_state.lock);
    AInputQueue* queue = android_state.input_queue;
    pthread_mutex_unlock(&android_state.lock);
    if (!queue) return 1;

    AInputEvent* event = NULL;
    while (AInputQueue_getEvent(queue, &event) >= 0) {
        if (AInputQueue_preDispatchEvent(queue, event)) continue;  // ime ate it
        int handled = backend_translate_input(event);
        AInputQueue_finishEvent(queue, event, handled);
    }
    return 1;                                            // keep the registration
}

// maps an android keycode to the apex keycode (x11 keysym values)
static int translate_keycode(int akey) {
    switch (akey) {
        case AKEYCODE_ESCAPE:      return 0xFF1B;
        case AKEYCODE_ENTER:       return 0xFF0D;
        case AKEYCODE_NUMPAD_ENTER:return 0xFF0D;
        case AKEYCODE_TAB:         return 0xFF09;
        case AKEYCODE_DEL:         return 0xFF08;   // backspace
        case AKEYCODE_FORWARD_DEL: return 0xFFFF;
        case AKEYCODE_INSERT:      return 0xFF63;
        case AKEYCODE_MOVE_HOME:   return 0xFF50;
        case AKEYCODE_MOVE_END:    return 0xFF57;
        case AKEYCODE_PAGE_UP:     return 0xFF55;
        case AKEYCODE_PAGE_DOWN:   return 0xFF56;
        case AKEYCODE_DPAD_LEFT:   return 0xFF51;
        case AKEYCODE_DPAD_RIGHT:  return 0xFF53;
        case AKEYCODE_DPAD_UP:     return 0xFF52;
        case AKEYCODE_DPAD_DOWN:   return 0xFF54;
        default: break;
    }
    if (akey >= AKEYCODE_F1 && akey <= AKEYCODE_F12)     // function row
        return 0xFFBE + (akey - AKEYCODE_F1);
    return akey;                                         // printable: the code itself
}

// returns the printable character for a keycode + shift state, 0 if none
static char translate_char(int akey, bool shift) {
    if (akey >= AKEYCODE_A && akey <= AKEYCODE_Z) {      // letters
        char base = (char)('a' + (akey - AKEYCODE_A));
        return shift ? (char)(base - 'a' + 'A') : base;
    }
    if (akey >= AKEYCODE_0 && akey <= AKEYCODE_9) {      // digits + us shift symbols
        static const char shifted[] = ")!@#$%^&*(";
        int digit = akey - AKEYCODE_0;
        return shift ? shifted[digit] : (char)('0' + digit);
    }
    if (akey >= AKEYCODE_NUMPAD_0 && akey <= AKEYCODE_NUMPAD_9)
        return (char)('0' + (akey - AKEYCODE_NUMPAD_0));
    switch (akey) {                                      // punctuation, us layout
        case AKEYCODE_SPACE:         return shift ? ' '  : ' ';
        case AKEYCODE_COMMA:         return shift ? '<'  : ',';
        case AKEYCODE_PERIOD:        return shift ? '>'  : '.';
        case AKEYCODE_MINUS:         return shift ? '_'  : '-';
        case AKEYCODE_EQUALS:        return shift ? '+'  : '=';
        case AKEYCODE_LEFT_BRACKET:  return shift ? '{'  : '[';
        case AKEYCODE_RIGHT_BRACKET: return shift ? '}'  : ']';
        case AKEYCODE_BACKSLASH:     return shift ? '|'  : '\\';
        case AKEYCODE_SEMICOLON:     return shift ? ':'  : ';';
        case AKEYCODE_APOSTROPHE:    return shift ? '"'  : '\'';
        case AKEYCODE_SLASH:         return shift ? '?'  : '/';
        case AKEYCODE_GRAVE:         return shift ? '~'  : '`';
        case AKEYCODE_NUMPAD_DOT:        return '.';
        case AKEYCODE_NUMPAD_ADD:        return '+';
        case AKEYCODE_NUMPAD_SUBTRACT:   return '-';
        case AKEYCODE_NUMPAD_MULTIPLY:   return '*';
        case AKEYCODE_NUMPAD_DIVIDE:     return '/';
        default: break;
    }
    return 0;                                            // not printable
}

// declare the jni function from ui_window_android_jni.c
int android_translate_key(int keyCode, int metaState, int repeatCount, int scanCode, int flags, int source, int deviceId, int64_t downTime, int64_t eventTime, int action);

// translates one android key event into a queued keydown
static void queue_key_event(const AInputEvent* event) {
    // ignore key releases, we only care about presses
    if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP) return;

    int akey = AKeyEvent_getKeyCode(event);
    int meta = AKeyEvent_getMetaState(event);

    BEvent ev; memset(&ev, 0, sizeof(ev));

    if (akey == AKEYCODE_BACK) {                         // back closes the window
        pthread_mutex_lock(&android_state.lock);
        if (android_main_window) android_main_window->open = false;
        pthread_mutex_unlock(&android_state.lock);
        ev.we.kind = UW_EV_CLOSE;
        bevent_push(&ev);
        return;
    }

    ev.we.kind = UW_EV_KEYDOWN;
    ev.we.key  = translate_keycode(akey);
    if (meta & AMETA_SHIFT_ON) ev.we.mods |= 1;
    if (meta & AMETA_CTRL_ON)  ev.we.mods |= 2;
    if (meta & AMETA_ALT_ON)   ev.we.mods |= 4;

    // use the java KeyCharacterMap to get the true unicode character
    int uc = android_translate_key(akey, meta, 
                                   AKeyEvent_getRepeatCount(event), 
                                   AKeyEvent_getScanCode(event), 
                                   AKeyEvent_getFlags(event), 
                                   AInputEvent_getSource(event), 
                                   AInputEvent_getDeviceId(event), 
                                   AKeyEvent_getDownTime(event), 
                                   AKeyEvent_getEventTime(event), 
                                   AKeyEvent_getAction(event));
    if (uc > 0 && uc < 0x110000) {
        if (uc < 0x80) {
            ev.we.utf8[0] = (char)uc;
            ev.we.utf8_len = 1;
        } else if (uc < 0x800) {
            ev.we.utf8[0] = (char)(0xC0 | (uc >> 6));
            ev.we.utf8[1] = (char)(0x80 | (uc & 0x3F));
            ev.we.utf8_len = 2;
        } else if (uc < 0x10000) {
            ev.we.utf8[0] = (char)(0xE0 | (uc >> 12));
            ev.we.utf8[1] = (char)(0x80 | ((uc >> 6) & 0x3F));
            ev.we.utf8[2] = (char)(0x80 | (uc & 0x3F));
            ev.we.utf8_len = 3;
        } else {
            ev.we.utf8[0] = (char)(0xF0 | (uc >> 18));
            ev.we.utf8[1] = (char)(0x80 | ((uc >> 12) & 0x3F));
            ev.we.utf8[2] = (char)(0x80 | ((uc >> 6) & 0x3F));
            ev.we.utf8[3] = (char)(0x80 | (uc & 0x3F));
            ev.we.utf8_len = 4;
        }
    } else {
        // fallback for cases where jni fails or keycode is completely unknown
        char ch = translate_char(akey, (meta & AMETA_SHIFT_ON) != 0);
        if (ch) {
            ev.we.utf8[0] = ch;
            ev.we.utf8_len = 1;
        }
    }

    bevent_push(&ev);
}

// translates a touch / mouse motion event
static void queue_motion_event(const AInputEvent* event) {
    UiWindow* win = android_main_window;
    int scale = win ? win->scale : 1;
    if (scale < 1) scale = 1;

    static float s_last_y = 0.0f;                        // two-finger pan state
    static bool  s_have_y = false;

    int action = AMotionEvent_getAction(event);
    int what   = action & AMOTION_EVENT_ACTION_MASK;
    size_t ptr_count = AMotionEvent_getPointerCount(event);

    switch (what) {
        case AMOTION_EVENT_ACTION_DOWN: {
            if (!win) return;
            float x = AMotionEvent_getX(event, 0) / (float)scale;
            float y = AMotionEvent_getY(event, 0) / (float)scale;
            BEvent ev; memset(&ev, 0, sizeof(ev));
            ev.we.kind = UW_EV_MOUSEDOWN;                // primary finger = left button
            ev.we.x = (int)x; ev.we.y = (int)y;
            ev.we.button = 1;
            bevent_push(&ev);
            return;
        }
        case AMOTION_EVENT_ACTION_POINTER_DOWN: {
            // a second finger lands
            if (!win) return;
            s_have_y = false;                            // re-arm the pan anchor
            BEvent ev; memset(&ev, 0, sizeof(ev));
            ev.we.kind = UW_EV_MOUSEUP;
            ev.we.button = 1;
            ev.we.x = (int)(AMotionEvent_getX(event, 0) / (float)scale);
            ev.we.y = (int)(AMotionEvent_getY(event, 0) / (float)scale);
            bevent_push(&ev);
            return;
        }
        case AMOTION_EVENT_ACTION_MOVE: {
            if (!win) return;
            BEvent ev; memset(&ev, 0, sizeof(ev));
            if (ptr_count >= 2) {                        // two-finger pan -> scroll
                float y = (AMotionEvent_getY(event, 0) + AMotionEvent_getY(event, 1))
                          / (float)scale;
                if (!s_have_y) {
                    s_last_y = y;
                    s_have_y  = true;
                    return;
                }
                float delta = y - s_last_y;              // content follows the fingers
                s_last_y = y;
                if (delta > -0.25f && delta < 0.25f) return;
                ev.we.kind      = UW_EV_SCROLL;
                ev.we.scroll_dy = (double)(-delta);      // swipe up scrolls down
                bevent_push(&ev);
                return;
            }
            ev.we.kind = UW_EV_MOUSEMOVE;                // single pointer: hover / drag
            ev.we.x = (int)(AMotionEvent_getX(event, 0) / (float)scale);
            ev.we.y = (int)(AMotionEvent_getY(event, 0) / (float)scale);
            bevent_push(&ev);
            return;
        }
        case AMOTION_EVENT_ACTION_POINTER_UP:
            return;                                      // one finger may still drag
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_CANCEL: {
            s_have_y = false;                            // gesture is over: re-arm pan
            if (!win) return;
            BEvent ev; memset(&ev, 0, sizeof(ev));
            ev.we.kind = UW_EV_MOUSEUP;
            ev.we.button = 1;
            ev.we.x = (int)(AMotionEvent_getX(event, 0) / (float)scale);
            ev.we.y = (int)(AMotionEvent_getY(event, 0) / (float)scale);
            bevent_push(&ev);
            return;
        }
        case AMOTION_EVENT_ACTION_HOVER_MOVE: {          // mouse / stylus hover
            if (!win) return;
            BEvent ev; memset(&ev, 0, sizeof(ev));
            ev.we.kind = UW_EV_MOUSEMOVE;
            ev.we.x = (int)(AMotionEvent_getX(event, 0) / (float)scale);
            ev.we.y = (int)(AMotionEvent_getY(event, 0) / (float)scale);
            bevent_push(&ev);
            return;
        }
        case AMOTION_EVENT_ACTION_SCROLL: {              // mouse wheel / touchpad
            if (!win) return;
            float vaxis = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0);
            BEvent ev; memset(&ev, 0, sizeof(ev));
            ev.we.kind      = UW_EV_SCROLL;
            ev.we.scroll_dy = -(double)vaxis;            // wheel up is negative
            bevent_push(&ev);
            return;
        }
        default:
            return;                                      // ignore exotic gestures
    }
}

// true while the window has not been closed
bool ui_window_is_open(UiWindow* w) {
    return w && w->open;
}

// blocks until input or a lifecycle event arrives, up to the timeout
bool ui_window_wait(UiWindow* w, int timeout_ms) {
    if (!w || !w->open) return false;
    if (android_state.destroyed) return true;            // dying: poll() reports close
    if (queue_nonempty()) return true;                   // already queued

    backend_attach_input();                              // first wait: attach the queue
    if (timeout_ms < 0 || timeout_ms > 5000) timeout_ms = 5000;
    double deadline = now_ms() + (double)timeout_ms;

    for (;;) {
        double left = deadline - now_ms();
        if (left < 0.0) left = 0.0;
        int ret = ALooper_pollOnce((int)left, NULL, NULL, NULL);
        if (android_state.destroyed) return true;        // teardown woke us up
        if (queue_nonempty()) return true;               // callbacks queued work
        if (ret < 0 || ret == ALOOPER_POLL_TIMEOUT) break;
    }
    return queue_nonempty();
}

// pops the next pending event, translating internal backend messages
bool ui_window_poll(UiWindow* w, UiWinEvent* out) {
    out->kind = UW_EV_NONE;
    if (!w) return false;

    // before draining the queue, read the hidden EditText
    android_ime_poll();

    // the activity is being torn down
    if (android_state.destroyed && w->open) {
        w->open = false;
        out->kind = UW_EV_CLOSE;
        out->w = w->w; out->h = w->h;
        return true;
    }

    BEvent ev;
    while (bevent_pop(&ev)) {                            // drain until a window event
        if (!ev.internal) {                              // plain translated input
            *out = ev.we;
            return true;
        }
        switch (ev.internal) {
            case BMSG_SURFACE_CREATED:
                android_surface_ready(w);                // geometry + framebuffer
                break;                                   // the resize event follows
            case BMSG_SURFACE_RESIZED:
                android_surface_resized(w, ev.we.w, ev.we.h);
                break;
            case BMSG_SURFACE_DESTROYED:
                w->surface_ok = false;                   // stop presenting
                break;
            case BMSG_REDRAW:
                out->kind = UW_EV_EXPOSE;                // ask for a repaint
                out->w = w->w; out->h = w->h;
                return true;
            case BMSG_INPUT_ATTACH:
                backend_attach_input();
                break;
            case BMSG_INPUT_DETACH:
                backend_detach_input();
                break;
            case BMSG_DESTROY:
                w->open = false;                         // activity is going away
                out->kind = UW_EV_CLOSE;
                return true;
            default:
                break;                                   // unknown: skip
        }
    }
    return false;                                        // nothing left this call
}

#endif // __ANDROID__