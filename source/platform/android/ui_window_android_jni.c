// source/platform/android/ui_window_android_jni.c
// JNI bridges to the android clipboard and immersive chrome for Apex UI
// https://github.com/is-nobody/apex-ui
// MIT license

#ifdef __ANDROID__
#include "ui_window_android.h"
#include "ui_theme.h"
#include <string.h>
#include <stdlib.h>

// art's AttachCurrentThread rejects jni 1.1
#ifndef JNI_VERSION_1_6
#define JNI_VERSION_1_6 0x00010006
#endif

// returns a JNIEnv* for the calling thread, attaching it on first use
JNIEnv* android_attach_env(void) {
    JavaVM* vm = android_state.vm;
    if (!vm) return NULL;
    JNIEnv* env = NULL;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) == JNI_OK && env)
        return env;                                      // already attached
    JavaVMAttachArgs args;
    memset(&args, 0, sizeof(args));
    args.version = JNI_VERSION_1_6;
    args.name    = (char*)"apex-ui";
    if ((*vm)->AttachCurrentThread(vm, &env, &args) == JNI_OK)
        return env;
    return NULL;
}

// local-ref cleanup helper: releases every object a bridge step produced
static void release_refs(JNIEnv* env, jobject* refs, int n) {
    for (int i = 0; i < n; i++)
        if (refs[i]) (*env)->DeleteLocalRef(env, refs[i]);
}

// pushes the desired immersive chrome to the decor view
void android_apply_immersive(void) {
    JNIEnv* env = android_attach_env();
    if (!env || !android_state.activity) return;

    pthread_mutex_lock(&android_state.lock);
    bool want = android_state.want_immersive;
    pthread_mutex_unlock(&android_state.lock);
    if (want == android_state.immersive) return;         // already applied

    jobject activity = android_state.activity->clazz;
    jobject window = NULL, decor = NULL;
    jclass activity_cls = NULL, window_cls = NULL, view_cls = NULL;

    activity_cls = (*env)->GetObjectClass(env, activity);
    if (!activity_cls) goto done;
    jmethodID get_window = (*env)->GetMethodID(env, activity_cls, "getWindow",
                                               "()Landroid/view/Window;");
    if (!get_window) goto done;
    window = (*env)->CallObjectMethod(env, activity, get_window);
    if (!window) goto done;
    window_cls = (*env)->GetObjectClass(env, window);
    if (!window_cls) goto done;
    jmethodID get_decor = (*env)->GetMethodID(env, window_cls, "getDecorView",
                                              "()Landroid/view/View;");
    if (!get_decor) goto done;
    decor = (*env)->CallObjectMethod(env, window, get_decor);
    if (!decor) goto done;
    view_cls = (*env)->GetObjectClass(env, decor);
    if (!view_cls) goto done;
    jmethodID set_suv = (*env)->GetMethodID(env, view_cls, "setSystemUiVisibility", "(I)V");
    if (!set_suv) goto done;

    if (want) {
        // layout stable + hide navigation + immersive sticky
        int flags = 0x100   // SYSTEM_UI_FLAG_LAYOUT_STABLE
                  | 0x200   // SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                  | 0x400   // SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                  | 0x2     // SYSTEM_UI_FLAG_HIDE_NAVIGATION
                  | 0x4     // SYSTEM_UI_FLAG_FULLSCREEN
                  | 0x1000; // SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        (*env)->CallVoidMethod(env, decor, set_suv, flags);
    } else {
        (*env)->CallVoidMethod(env, decor, set_suv, 0);  // restore system chrome
    }
    if ((*env)->ExceptionCheck(env)) {                   // keep the vm alive on failure
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        APEXUI_LOG("setSystemUiVisibility failed");
    } else {
        android_state.immersive = want;
    }

done:
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    release_refs(env, (jobject[]){ activity_cls ? (jobject)activity_cls : NULL,
                                   window, decor, window_cls ? (jobject)window_cls : NULL,
                                   view_cls ? (jobject)view_cls : NULL }, 5);
    (void)activity;
}

// reads the system display density so present() can pick a sane upscale
int android_density_milli(void) {
    JNIEnv* env = android_attach_env();
    if (!env || !android_state.activity) return 0;

    jobject activity = android_state.activity->clazz;
    jclass activity_cls = (*env)->GetObjectClass(env, activity);
    if (!activity_cls) return 0;
    int out = 0;
    jmethodID get_res = (*env)->GetMethodID(env, activity_cls, "getResources",
                                            "()Landroid/content/res/Resources;");
    jobject res = get_res ? (*env)->CallObjectMethod(env, activity, get_res) : NULL;
    if (res) {
        jclass res_cls = (*env)->GetObjectClass(env, res);
        jmethodID get_dm = res_cls
            ? (*env)->GetMethodID(env, res_cls, "getDisplayMetrics",
                                  "()Landroid/util/DisplayMetrics;") : NULL;
        jobject dm = get_dm ? (*env)->CallObjectMethod(env, res, get_dm) : NULL;
        if (dm) {
            jclass dm_cls = (*env)->GetObjectClass(env, dm);
            jfieldID density_f = dm_cls
                ? (*env)->GetFieldID(env, dm_cls, "density", "F") : NULL;
            if (density_f) {
                float density = (*env)->GetFloatField(env, dm, density_f);
                out = (int)(density * 1000.0f + 0.5f);   // 2.75 -> 2750
            }
            if (dm_cls) (*env)->DeleteLocalRef(env, dm_cls);
            (*env)->DeleteLocalRef(env, dm);
        }
        if (res_cls) (*env)->DeleteLocalRef(env, res_cls);
        (*env)->DeleteLocalRef(env, res);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);                     // fall back to the heuristic
    }
    (*env)->DeleteLocalRef(env, activity_cls);
    return out;
}

bool android_clipboard_set_text(const char* text, int len) {
    JNIEnv* env = android_attach_env();
    if (!env || !android_state.activity || !text) return false;
    if (len < 0) len = 0;

    jobject activity = android_state.activity->clazz;
    jclass activity_cls = (*env)->GetObjectClass(env, activity);
    if (!activity_cls) return false;
    bool ok = false;
    jmethodID get_service = (*env)->GetMethodID(env, activity_cls, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;");
    if (get_service) {
        jstring name = (*env)->NewStringUTF(env, "clipboard");
        jobject cm = name ? (*env)->CallObjectMethod(env, activity, get_service, name) : NULL;
        if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); }
        if (cm) {
            jclass cm_cls = (*env)->GetObjectClass(env, cm);
            jmethodID set_clip = cm_cls
                ? (*env)->GetMethodID(env, cm_cls, "setPrimaryClip",
                                      "(Landroid/content/ClipData;)V") : NULL;
            jclass cd_cls = set_clip ? (*env)->FindClass(env, "android/content/ClipData") : NULL;
            jmethodID new_plain = cd_cls
                ? (*env)->GetStaticMethodID(env, cd_cls, "newPlainText",
                    "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)"
                    "Landroid/content/ClipData;") : NULL;
            if (new_plain) {
                char* nul = (char*)malloc((size_t)len + 1);   // jni wants c strings
                if (nul) {
                    memcpy(nul, text, (size_t)len);
                    nul[len] = 0;
                    jstring jtext = (*env)->NewStringUTF(env, nul);
                    if (jtext) {
                        jobject clip = (*env)->CallStaticObjectMethod(env, cd_cls,
                                                                      new_plain, NULL, jtext);
                        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
                        if (clip) {
                            (*env)->CallVoidMethod(env, cm, set_clip, clip);
                            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
                            else ok = true;
                            (*env)->DeleteLocalRef(env, clip);
                        }
                        (*env)->DeleteLocalRef(env, jtext);
                    }
                    free(nul);
                }
            }
            if (cd_cls) (*env)->DeleteLocalRef(env, cd_cls);
            if (cm_cls) (*env)->DeleteLocalRef(env, cm_cls);
            (*env)->DeleteLocalRef(env, cm);
        }
        if (name) (*env)->DeleteLocalRef(env, name);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, activity_cls);
    return ok;
}

// fetches the system clipboard as utf-8; returns a malloc'd copy or null
char* android_clipboard_get_text(int* out_len) {
    if (out_len) *out_len = 0;
    JNIEnv* env = android_attach_env();
    if (!env || !android_state.activity) return NULL;

    jobject activity = android_state.activity->clazz;
    jclass activity_cls = (*env)->GetObjectClass(env, activity);
    if (!activity_cls) return NULL;
    char* out = NULL;
    jmethodID get_service = (*env)->GetMethodID(env, activity_cls, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;");
    if (get_service) {
        jstring name = (*env)->NewStringUTF(env, "clipboard");
        jobject cm = name ? (*env)->CallObjectMethod(env, activity, get_service, name) : NULL;
        if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); }
        if (cm) {
            jclass cm_cls = (*env)->GetObjectClass(env, cm);
            jmethodID get_clip = cm_cls
                ? (*env)->GetMethodID(env, cm_cls, "getPrimaryClip",
                                      "()Landroid/content/ClipData;") : NULL;
            jobject clip = get_clip ? (*env)->CallObjectMethod(env, cm, get_clip) : NULL;
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
            if (clip) {
                jclass clip_cls = (*env)->GetObjectClass(env, clip);
                jmethodID get_item = clip_cls
                    ? (*env)->GetMethodID(env, clip_cls, "getItemAt",
                        "(I)Landroid/content/ClipData$Item;") : NULL;
                jobject item = get_item ? (*env)->CallObjectMethod(env, clip, get_item, 0) : NULL;
                if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
                if (item) {
                    jclass item_cls = (*env)->GetObjectClass(env, item);
                    jmethodID coerce = item_cls
                        ? (*env)->GetMethodID(env, item_cls, "coerceToText",
                            "(Landroid/content/Context;)Ljava/lang/CharSequence;") : NULL;
                    jobject seq = coerce
                        ? (*env)->CallObjectMethod(env, item, coerce, activity) : NULL;
                    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
                    if (seq) {
                        jclass seq_cls = (*env)->GetObjectClass(env, seq);
                        jmethodID to_str = seq_cls
                            ? (*env)->GetMethodID(env, seq_cls, "toString",
                                                  "()Ljava/lang/String;") : NULL;
                        jstring jstr = to_str
                            ? (jstring)(*env)->CallObjectMethod(env, seq, to_str) : NULL;
                        if (jstr) {
                            const char* utf = (*env)->GetStringUTFChars(env, jstr, NULL);
                            if (utf) {
                                int n = (int)strlen(utf);
                                out = (char*)malloc((size_t)n + 1);
                                if (out) {
                                    memcpy(out, utf, (size_t)n + 1);
                                    if (out_len) *out_len = n;
                                }
                                (*env)->ReleaseStringUTFChars(env, jstr, utf);
                            }
                            (*env)->DeleteLocalRef(env, jstr);
                        }
                        if (seq_cls) (*env)->DeleteLocalRef(env, seq_cls);
                        (*env)->DeleteLocalRef(env, seq);
                    }
                    if (item_cls) (*env)->DeleteLocalRef(env, item_cls);
                    (*env)->DeleteLocalRef(env, item);
                }
                if (clip_cls) (*env)->DeleteLocalRef(env, clip_cls);
                (*env)->DeleteLocalRef(env, clip);
            }
            if (cm_cls) (*env)->DeleteLocalRef(env, cm_cls);
            (*env)->DeleteLocalRef(env, cm);
        }
        if (name) (*env)->DeleteLocalRef(env, name);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, activity_cls);
    return out;                                          // caller frees; may be null
}

// creates the 1x1 invisible EditText on the ui thread and stores a global ref
static void android_ime_create_view(JNIEnv* env) {
    if (android_state.ime_view_ready || !android_state.activity) return;

    jobject activity = android_state.activity->clazz;

    jclass edit_cls = (*env)->FindClass(env, "android/widget/EditText");
    if (!edit_cls) { (*env)->ExceptionClear(env); return; }

    jmethodID ctor = (*env)->GetMethodID(env, edit_cls, "<init>",
                                         "(Landroid/content/Context;)V");
    jobject edit = ctor ? (*env)->NewObject(env, edit_cls, ctor, activity) : NULL;
    (*env)->DeleteLocalRef(env, edit_cls);
    if (!edit) { (*env)->ExceptionClear(env); return; }

    jclass view_cls = (*env)->GetObjectClass(env, edit);
    if (!view_cls) { (*env)->DeleteLocalRef(env, edit); return; }

    jmethodID set_foc  = (*env)->GetMethodID(env, view_cls, "setFocusable",            "(Z)V");
    jmethodID set_fit  = (*env)->GetMethodID(env, view_cls, "setFocusableInTouchMode", "(Z)V");
    jmethodID set_vis  = (*env)->GetMethodID(env, view_cls, "setVisibility",           "(I)V");
    jmethodID set_type = (*env)->GetMethodID(env, view_cls, "setInputType",            "(I)V");
    jmethodID set_bg   = (*env)->GetMethodID(env, view_cls, "setBackgroundColor",      "(I)V");
    if (set_foc)  (*env)->CallVoidMethod(env, edit, set_foc, JNI_TRUE);
    if (set_fit)  (*env)->CallVoidMethod(env, edit, set_fit, JNI_TRUE);
    if (set_vis)  (*env)->CallVoidMethod(env, edit, set_vis, 0 /* View.VISIBLE */);
    if (set_type) (*env)->CallVoidMethod(env, edit, set_type,
                                         1 /* TYPE_CLASS_TEXT */
                                       | 0x00020000 /* TYPE_TEXT_FLAG_MULTI_LINE */);
    if (set_bg)   (*env)->CallVoidMethod(env, edit, set_bg, 0 /* transparent */);

    // attach as a 1x1 content view
    jclass activity_cls = (*env)->GetObjectClass(env, activity);
    jmethodID add_content = (*env)->GetMethodID(env, activity_cls,
        "addContentView",
        "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");
    jclass lp_cls = (*env)->FindClass(env, "android/view/ViewGroup$LayoutParams");
    jmethodID lp_ctor = lp_cls
        ? (*env)->GetMethodID(env, lp_cls, "<init>", "(II)V") : NULL;
    jobject lp = lp_ctor ? (*env)->NewObject(env, lp_cls, lp_ctor, 1, 1) : NULL;

    if (add_content && lp) {
        (*env)->CallVoidMethod(env, activity, add_content, edit, lp);
    }
    if (lp)     (*env)->DeleteLocalRef(env, lp);
    if (lp_cls) (*env)->DeleteLocalRef(env, lp_cls);
    (*env)->DeleteLocalRef(env, activity_cls);
    (*env)->DeleteLocalRef(env, view_cls);

    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, edit);
        return;
    }

    pthread_mutex_lock(&android_state.lock);
    android_state.ime_view       = (*env)->NewGlobalRef(env, edit);
    android_state.ime_view_ready = true;
    android_state.ime_multiline  = true;
    pthread_mutex_unlock(&android_state.lock);

    (*env)->DeleteLocalRef(env, edit);
    APEXUI_LOG("hidden EditText installed");
}

// creates the EditText if needed and shows or hides the soft keyboard
void android_apply_ime(void) {
    JNIEnv* env = android_attach_env();
    if (!env || !android_state.activity) return;

    pthread_mutex_lock(&android_state.lock);
    bool want = android_state.want_ime;
    pthread_mutex_unlock(&android_state.lock);

    if (want && !android_state.ime_view_ready) android_ime_create_view(env);

    if (want == android_state.ime_shown) return;

    jobject activity   = android_state.activity->clazz;
    jclass  activity_c = (*env)->GetObjectClass(env, activity);
    if (!activity_c) return;
    jmethodID get_svc = (*env)->GetMethodID(env, activity_c, "getSystemService",
                                            "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring   name    = (*env)->NewStringUTF(env, "input_method");
    jobject   imm     = name ? (*env)->CallObjectMethod(env, activity, get_svc, name) : NULL;

    pthread_mutex_lock(&android_state.lock);
    jobject view = android_state.ime_view;
    pthread_mutex_unlock(&android_state.lock);

    if (imm) {
        jclass imm_c = (*env)->GetObjectClass(env, imm);
        if (view) {
            if (want) {
                jclass view_c = (*env)->GetObjectClass(env, view);
                jmethodID req_focus = (*env)->GetMethodID(env, view_c, "requestFocus", "()Z");
                if (req_focus) (*env)->CallBooleanMethod(env, view, req_focus);
                (*env)->DeleteLocalRef(env, view_c);

                jmethodID show = (*env)->GetMethodID(env, imm_c, "showSoftInput",
                                                     "(Landroid/view/View;I)Z");
                jboolean shown = JNI_FALSE;
                if (show) shown = (*env)->CallBooleanMethod(env, imm, show, view, 0);

                if (!shown) {
                    // showSoftInput can return false on the first frame after the view is attached
                    jmethodID toggle = (*env)->GetMethodID(env, imm_c, "toggleSoftInput",
                                                           "(II)V");
                    if (toggle) {
                        (*env)->CallVoidMethod(env, imm, toggle, 2 /* SHOW_FORCED */, 0);
                        shown = JNI_TRUE;
                    }
                }
                APEXUI_LOG("ime show: view=%p shown=%d", (void*)view, (int)shown);
                if (shown) android_state.ime_shown = true;
            } else {
                jclass view_c = (*env)->GetObjectClass(env, view);
                jmethodID get_tok = (*env)->GetMethodID(env, view_c, "getWindowToken",
                                                        "()Landroid/os/IBinder;");
                jobject token = get_tok
                    ? (*env)->CallObjectMethod(env, view, get_tok) : NULL;
                if (token) {
                    jmethodID hide = (*env)->GetMethodID(env, imm_c,
                        "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z");
                    if (hide) (*env)->CallBooleanMethod(env, imm, hide, token, 0);
                    (*env)->DeleteLocalRef(env, token);
                }
                (*env)->DeleteLocalRef(env, view_c);
                android_state.ime_shown = false;
            }
        } else if (want) {
            // last-resort fallback with no InputConnection (latin only)
            jmethodID toggle = (*env)->GetMethodID(env, imm_c, "toggleSoftInput", "(II)V");
            if (toggle) (*env)->CallVoidMethod(env, imm, toggle, 2, 0);
        }
        if (imm_c) (*env)->DeleteLocalRef(env, imm_c);
    }
    if (imm)  (*env)->DeleteLocalRef(env, imm);
    if (name) (*env)->DeleteLocalRef(env, name);
    (*env)->DeleteLocalRef(env, activity_c);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}

// counts utf-16 code units needed for a utf-8 buffer
static int utf8_len_to_utf16(const char* s, int len) {
    int n = 0;
    for (int i = 0; i < len; ) {
        unsigned char b = (unsigned char)s[i];
        if (b < 0x80)       { n += 1; i += 1; }
        else if (b < 0xE0)  { n += 1; i += 2; }
        else if (b < 0xF0)  { n += 1; i += 3; }
        else                { n += 2; i += 4; }
    }
    return n;
}

// builds a jcharArray from a utf-8 buffer
static jcharArray build_utf16_array(JNIEnv* env, const char* s, int len) {
    int n = utf8_len_to_utf16(s, len);
    jcharArray arr = (*env)->NewCharArray(env, n);
    if (!arr) return NULL;
    jchar* chars = (*env)->GetCharArrayElements(env, arr, NULL);
    if (!chars) { (*env)->DeleteLocalRef(env, arr); return NULL; }
    int p = 0;
    for (int i = 0; i < len; ) {
        unsigned char b = (unsigned char)s[i];
        uint32_t cp; int adv;
        if (b < 0x80)       { cp = b; adv = 1; }
        else if (b < 0xE0)  { cp = ((b & 0x1F) << 6) | ((unsigned char)s[i+1] & 0x3F); adv = 2; }
        else if (b < 0xF0)  { cp = ((b & 0x0F) << 12) | (((unsigned char)s[i+1] & 0x3F) << 6)
                                 | ((unsigned char)s[i+2] & 0x3F); adv = 3; }
        else                { cp = ((b & 0x07) << 18) | (((unsigned char)s[i+1] & 0x3F) << 12)
                                 | (((unsigned char)s[i+2] & 0x3F) << 6)
                                 | ((unsigned char)s[i+3] & 0x3F); adv = 4; }
        if (cp < 0x10000) {
            chars[p++] = (jchar)cp;
        } else {
            cp -= 0x10000;
            chars[p++] = (jchar)(0xD800 | (cp >> 10));
            chars[p++] = (jchar)(0xDC00 | (cp & 0x3FF));
        }
        i += adv;
    }
    (*env)->ReleaseCharArrayElements(env, arr, chars, 0);
    return arr;
}

// converts a byte offset in a utf-8 buffer to a utf-16 index
static int utf8_byte_to_utf16(const char* s, int byte_off) {
    if (!s) return 0;
    int u16 = 0, i = 0;
    while (i < byte_off && s[i]) {
        unsigned char b = (unsigned char)s[i];
        if (b < 0x80)       { u16 += 1; i += 1; }
        else if (b < 0xE0)  { u16 += 1; i += 2; }
        else if (b < 0xF0)  { u16 += 1; i += 3; }
        else                { u16 += 2; i += 4; }
    }
    return u16;
}

// reads the hidden EditText into a malloc'd utf-8 buffer; caller frees
static char* ime_read_text(JNIEnv* env, jobject view, int* out_len) {
    *out_len = 0;
    jclass view_cls = (*env)->GetObjectClass(env, view);
    if (!view_cls) return NULL;

    // TextView.getText() is declared to return CharSequence, not Editable
    jmethodID get_text = (*env)->GetMethodID(env, view_cls, "getText",
                                             "()Ljava/lang/CharSequence;");
    jobject cs = NULL;
    if (get_text) {
        cs = (*env)->CallObjectMethod(env, view, get_text);
    } else {
        (*env)->ExceptionClear(env);
        jmethodID to_str_view = (*env)->GetMethodID(env, view_cls, "toString",
                                                    "()Ljava/lang/String;");
        if (to_str_view) cs = (*env)->CallObjectMethod(env, view, to_str_view);
    }
    (*env)->DeleteLocalRef(env, view_cls);
    if (!cs) return NULL;

    jclass cs_cls = (*env)->GetObjectClass(env, cs);
    jmethodID to_str = cs_cls
        ? (*env)->GetMethodID(env, cs_cls, "toString", "()Ljava/lang/String;") : NULL;
    jstring jstr = to_str ? (jstring)(*env)->CallObjectMethod(env, cs, to_str) : NULL;
    if (cs_cls) (*env)->DeleteLocalRef(env, cs_cls);
    (*env)->DeleteLocalRef(env, cs);
    if (!jstr) return NULL;

    const jchar* chars = (*env)->GetStringChars(env, jstr, NULL);
    jsize n = (*env)->GetStringLength(env, jstr);
    if (!chars) { (*env)->DeleteLocalRef(env, jstr); return NULL; }

    // worst case: 3 bytes per utf-16 unit
    size_t cap = (size_t)n * 3 + 1;
    char* out = (char*)malloc(cap);
    if (!out) {
        (*env)->ReleaseStringChars(env, jstr, chars);
        (*env)->DeleteLocalRef(env, jstr);
        return NULL;
    }
    int p = 0;
    for (jsize i = 0; i < n; i++) {
        uint32_t cp = chars[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n) {
            uint32_t lo = chars[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i++;
            }
        }
        if (cp < 0x80) {
            out[p++] = (char)cp;
        } else if (cp < 0x800) {
            out[p++] = (char)(0xC0 | (cp >> 6));
            out[p++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out[p++] = (char)(0xE0 | (cp >> 12));
            out[p++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[p++] = (char)(0x80 | (cp & 0x3F));
        } else {
            out[p++] = (char)(0xF0 | (cp >> 18));
            out[p++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[p++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[p++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[p] = 0;
    (*env)->ReleaseStringChars(env, jstr, chars);
    (*env)->DeleteLocalRef(env, jstr);
    *out_len = p;
    return out;
}

// drains android_state.ime_pending_* on the ui thread
void android_apply_ime_sync(void) {
    pthread_mutex_lock(&android_state.lock);
    if (!android_state.ime_pending || !android_state.ime_view) {
        android_state.ime_pending = false;
        pthread_mutex_unlock(&android_state.lock);
        return;
    }
    jobject view        = android_state.ime_view;
    bool    do_text     = android_state.ime_pending_text_dirty;
    char*   text        = do_text ? android_state.ime_pending_text : NULL;
    int     text_len    = android_state.ime_pending_text_len;
    bool    do_cur      = android_state.ime_pending_cursor_dirty;
    int     cur_byte    = android_state.ime_pending_cursor;
    int     sel_byte    = android_state.ime_pending_sel;
    bool    do_ml       = android_state.ime_pending_ml_dirty;
    bool    ml          = android_state.ime_pending_multiline;

    // detach ownership so the app thread can fill them again
    android_state.ime_pending              = false;
    android_state.ime_pending_text         = NULL;
    android_state.ime_pending_text_dirty   = false;
    android_state.ime_pending_cursor_dirty = false;
    android_state.ime_pending_ml_dirty     = false;
    pthread_mutex_unlock(&android_state.lock);

    JNIEnv* env = android_attach_env();
    if (!env) { free(text); return; }

    if (do_ml) {
        jclass cls = (*env)->GetObjectClass(env, view);
        if (cls) {
            jmethodID set_type = (*env)->GetMethodID(env, cls, "setInputType", "(I)V");
            if (set_type) {
                jint type = 1 /* TYPE_CLASS_TEXT */;
                if (ml) type |= 0x00020000 /* TYPE_TEXT_FLAG_MULTI_LINE */;
                (*env)->CallVoidMethod(env, view, set_type, type);
            }
            (*env)->DeleteLocalRef(env, cls);
        }
    }

    if (do_text && text) {
        jcharArray chars = build_utf16_array(env, text, text_len);
        if (chars) {
            jclass str_cls = (*env)->FindClass(env, "java/lang/String");
            jmethodID ctor = str_cls
                ? (*env)->GetMethodID(env, str_cls, "<init>", "([C)V") : NULL;
            jstring jstr = (str_cls && ctor)
                ? (jstring)(*env)->NewObject(env, str_cls, ctor, chars) : NULL;
            if (jstr) {
                jclass view_cls = (*env)->GetObjectClass(env, view);
                if (view_cls) {
                    jmethodID set_text = (*env)->GetMethodID(env, view_cls, "setText",
                                                            "(Ljava/lang/CharSequence;)V");
                    if (set_text) (*env)->CallVoidMethod(env, view, set_text, jstr);
                    (*env)->DeleteLocalRef(env, view_cls);
                }
                (*env)->DeleteLocalRef(env, jstr);
            }
            if (str_cls) (*env)->DeleteLocalRef(env, str_cls);
            (*env)->DeleteLocalRef(env, chars);
        }
        // adopt the text as the new poll baseline
        pthread_mutex_lock(&android_state.lock);
        free(android_state.ime_last_text);
        android_state.ime_last_text     = text;
        android_state.ime_last_text_len = text_len;
        android_state.ime_poll_gen++;
        pthread_mutex_unlock(&android_state.lock);
        text = NULL;
    }
    free(text);

    if (do_cur) {
        pthread_mutex_lock(&android_state.lock);
        const char* base = android_state.ime_last_text ? android_state.ime_last_text : "";
        pthread_mutex_unlock(&android_state.lock);

        int u16_cur = utf8_byte_to_utf16(base, cur_byte);
        int u16_sel = utf8_byte_to_utf16(base, sel_byte);
        if (u16_cur > u16_sel) { int t = u16_cur; u16_cur = u16_sel; u16_sel = t; }

        jclass cls = (*env)->GetObjectClass(env, view);
        if (cls) {
            jmethodID set_sel = (*env)->GetMethodID(env, cls, "setSelection", "(II)V");
            if (set_sel) (*env)->CallVoidMethod(env, view, set_sel, u16_cur, u16_sel);
            (*env)->DeleteLocalRef(env, cls);
        }
    }

    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
    }
}

// reads the hidden EditText
void android_ime_poll(void) {
    if (android_state.destroyed) return;

    pthread_mutex_lock(&android_state.lock);
    jobject view = android_state.ime_view;
    bool    want = android_state.want_ime;
    pthread_mutex_unlock(&android_state.lock);
    if (!view || !want) return;

    JNIEnv* env = android_attach_env();
    if (!env) return;

    int cur_len = 0;
    char* cur = ime_read_text(env, view, &cur_len);
    if (!cur) return;

    // take a snapshot of the previous text + generation atomically
    pthread_mutex_lock(&android_state.lock);
    char* old        = android_state.ime_last_text;
    int   old_len    = android_state.ime_last_text_len;
    uint32_t gen     = android_state.ime_poll_gen;
    pthread_mutex_unlock(&android_state.lock);

    // publish our read
    pthread_mutex_lock(&android_state.lock);
    if (android_state.ime_poll_gen == gen) {
        android_state.ime_last_text     = cur;
        android_state.ime_last_text_len = cur_len;
        android_state.ime_poll_gen++;
    } else {
        // ui thread already moved past our snapshot: drop the read
        free(cur);
        cur = NULL;
        cur_len = 0;
    }
    pthread_mutex_unlock(&android_state.lock);
    if (!cur) return;

    // common prefix in bytes, then back off to a codepoint boundary in `cur`
    int pre = 0;
    while (pre < old_len && pre < cur_len && old[pre] == cur[pre]) pre++;
    while (pre > 0 && ((unsigned char)cur[pre] & 0xC0) == 0x80) pre--;

    // common suffix in bytes, then back off so it does not cut a codepoint
    int suf = 0;
    while (suf < old_len - pre && suf < cur_len - pre &&
           old[old_len - 1 - suf] == cur[cur_len - 1 - suf]) suf++;
    while (suf > 0 && ((unsigned char)cur[cur_len - suf] & 0xC0) == 0x80) suf--;

    int removed = old_len - pre - suf;
    int added   = cur_len - pre - suf;

    // one backspace per codepoint removed
    if (removed > 0) {
        int n_cp = 0;
        for (int i = 0; i < removed; ) {
            unsigned char b = (unsigned char)old[pre + i];
            int adv = 1;
            if (b >= 0x80) {
                if      ((b & 0xE0) == 0xC0) adv = 2;
                else if ((b & 0xF0) == 0xE0) adv = 3;
                else if ((b & 0xF8) == 0xF0) adv = 4;
            }
            i += adv;
            n_cp++;
        }
        for (int i = 0; i < n_cp; i++) {
            BEvent ev; memset(&ev, 0, sizeof(ev));
            ev.we.kind = UW_EV_KEYDOWN;
            ev.we.key  = UI_KEY_BACKSPACE;
            bevent_push(&ev);
        }
    }

    // the added suffix, chunked at codepoint boundaries to fit utf8[32]
    const int cap = (int)sizeof(((UiWinEvent*)0)->utf8);
    int off = pre, left = added;
    while (left > 0) {
        int chunk = left > cap ? cap : left;
        if (chunk < left) {
            while (chunk > 0 && ((unsigned char)cur[off + chunk] & 0xC0) == 0x80) chunk--;
            if (chunk == 0) chunk = cap;  // defensive: never happens with cap >= 4
        }
        BEvent ev; memset(&ev, 0, sizeof(ev));
        ev.we.kind = UW_EV_KEYDOWN;
        ev.we.key  = 0;
        memcpy(ev.we.utf8, cur + off, chunk);
        ev.we.utf8_len = chunk;
        bevent_push(&ev);
        off  += chunk;
        left -= chunk;
    }

    free(old);
}

// teardown
void android_ime_destroy(void) {
    JNIEnv* env = android_attach_env();
    pthread_mutex_lock(&android_state.lock);
    jobject view = android_state.ime_view;
    android_state.ime_view       = NULL;
    android_state.ime_view_ready = false;
    android_state.ime_shown      = false;
    free(android_state.ime_last_text);
    android_state.ime_last_text     = NULL;
    android_state.ime_last_text_len = 0;
    free(android_state.ime_pending_text);
    android_state.ime_pending_text      = NULL;
    android_state.ime_pending_text_dirty = false;
    android_state.ime_pending            = false;
    pthread_mutex_unlock(&android_state.lock);
    if (env && view) (*env)->DeleteGlobalRef(env, view);
}

// converts an android keycode + metastate into a unicode character
int android_translate_key(int keyCode, int metaState, int repeatCount, int scanCode, int flags, int source, int deviceId, int64_t downTime, int64_t eventTime, int action) {
    JNIEnv* env = android_attach_env();
    if (!env) return 0;

    int result = 0;
    jclass ke_cls = (*env)->FindClass(env, "android/view/KeyEvent");
    if (ke_cls) {
        jmethodID ctor = (*env)->GetMethodID(env, ke_cls, "<init>", "(JJIIIIIIII)V");
        if (ctor) {
            jobject ke = (*env)->NewObject(env, ke_cls, ctor, 
                                           downTime, eventTime, action, keyCode, 
                                           repeatCount, metaState, deviceId, scanCode, 
                                           flags, source);
            if (ke) {
                jmethodID get_uc = (*env)->GetMethodID(env, ke_cls, "getUnicodeChar", "(I)I");
                if (get_uc) {
                    result = (*env)->CallIntMethod(env, ke, get_uc, metaState);
                    result &= 0x7FFFFFFF; // clear the combining-character bit
                }
                (*env)->DeleteLocalRef(env, ke);
            }
        }
        (*env)->DeleteLocalRef(env, ke_cls);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    return result;
}

#endif // __ANDROID__