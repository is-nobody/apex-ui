// source/platform/android/ui_window_android_filedialog.c
// File picker stubs for the Apex UI library (android backend)
// https://github.com/is-nobody/apex-ui
// MIT license

#ifdef __ANDROID__
#include "ui_window_android.h"
#include <android/log.h>

// shows the native "open file" picker
char* ui_window_open_file(UiWindow* w, const char* title,
                          const char* default_path,
                          const UiFileFilter* filters) {
    (void)w; (void)title; (void)default_path; (void)filters;
    __android_log_print(ANDROID_LOG_WARN, "apex-ui",
                        "file pickers are not supported on android");
    return NULL;
}

// shows the native "open folder" picker
char* ui_window_open_folder(UiWindow* w, const char* title,
                            const char* default_path) {
    (void)w; (void)title; (void)default_path;
    return NULL;                                         // same limitation as files
}

// shows the native "save file" picker
char* ui_window_save_file(UiWindow* w, const char* title,
                          const char* default_path,
                          const UiFileFilter* filters) {
    (void)w; (void)title; (void)default_path; (void)filters;
    return NULL;
}

#endif // __ANDROID__