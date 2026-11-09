// source/platform/android/ui_window_android_clipboard.c
// System clipboard exchange for the Apex UI library (android backend)
// https://github.com/is-nobody/apex-ui
// MIT license

#ifdef __ANDROID__
#include "ui_window_android.h"
#include <string.h>
#include <stdlib.h>

// stores a utf-8 string on the android system clipboard
void ui_window_clipboard_set(UiWindow* w, const char* text, int len) {
    if (!w) return;
    if (len < 0) len = 0;
    free(w->clipboard_text);                             // keep a local copy for fast
    w->clipboard_text = (char*)malloc((size_t)len + 1);  // self-reads
    if (w->clipboard_text) {
        if (text && len) memcpy(w->clipboard_text, text, (size_t)len);
        w->clipboard_text[len] = 0;
        w->clipboard_len = len;
    } else {
        w->clipboard_len = 0;
    }
    android_clipboard_set_text(text, len);               // binder call to ClipboardManager
}

// fetches the current clipboard contents as utf-8; the caller owns the copy
bool ui_window_clipboard_get(UiWindow* w, char** out_text, int* out_len) {
    if (out_text) *out_text = NULL;
    if (out_len)  *out_len = 0;
    if (!w || !out_text) return false;

    int len = 0;
    char* text = android_clipboard_get_text(&len);       // ask the system first
    if (text) {
        if (len <= 0) {                                  // empty clip: nothing to paste
            free(text);
            return false;
        }
        *out_text = text;
        if (out_len) *out_len = len;
        return true;
    }

    if (w->clipboard_text && w->clipboard_len > 0) {     // fall back to our last set
        char* copy = (char*)malloc((size_t)w->clipboard_len + 1);
        if (copy) {
            memcpy(copy, w->clipboard_text, (size_t)w->clipboard_len + 1);
            *out_text = copy;
            if (out_len) *out_len = w->clipboard_len;
            return true;
        }
    }
    return false;                                        // clipboard unavailable
}

#endif // __ANDROID__