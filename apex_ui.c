// apex_ui.c
// Public C API implementation of the Apex UI library
// https://github.com/is-nobody/apex-ui
// MIT license

#include "apex_ui.h"

#include "ui_internal.h"
#include "ui_core.h"
#include "ui_table.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// sets a string-keyed entry using a fresh (non-interned) key string
static void kset(Table* t, const char* k, Value v) {
    Value kk = MAKE_STRING(uival_string_create(k, (int)strlen(k)));
    uival_table_set(t, kk, v);                 // table_set takes its own references
    uival_value_decref(kk);                    // drop the local key reference
}

// merges every field of an options table into a descriptor table
static void merge_opts(Table* t, Value opts) {
    if (!IS_TABLE(opts)) return;
    Table* o = AS_TABLE(opts);
    int nk;
    Value* keys = uival_table_keys(o, &nk);    // snapshot of option keys
    if (!keys) return;
    for (int i = 0; i < nk; i++) {
        Value vv;
        if (uival_table_get(o, keys[i], &vv)) {
            uival_table_set(t, keys[i], vv);   // copy each option into the descriptor
            uival_value_decref(vv);
        }
        uival_value_decref(keys[i]);           // release each key from the snapshot
    }
    free(keys);
}

// converts a c string to a string value, or none when the pointer is null
static Value str_or_none(const char* s) {
    if (!s) return MAKE_NONE();
    return MAKE_STRING(uival_string_create(s, (int)strlen(s)));
}

// builds a leaf widget descriptor of the given kind
static Value make_leaf(const char* type, Value content, Value opts) {
    Table* t = uival_table_create(8);          // descriptor table for the leaf
    if (!t) return MAKE_NONE();
    Value tv = MAKE_STRING(uival_string_create(type, (int)strlen(type)));
    kset(t, "__t", tv);                       // kind discriminator
    uival_value_decref(tv);                    // kset retained its own reference
    if (!IS_NONE(content)) kset(t, "content", content);  // label / content
    if (IS_TABLE(opts)) kset(t, "opts", opts);           // keep options table too
    merge_opts(t, opts);                 // merge option fields into the descriptor
    return MAKE_TABLE(t);                // caller owns the descriptor
}

// builds a container widget descriptor of the given kind; borrows its inputs
static Value make_container(const char* type, Value children, Value opts) {
    Table* t = uival_table_create(8);
    if (!t) return MAKE_NONE();
    Value tv = MAKE_STRING(uival_string_create(type, (int)strlen(type)));
    kset(t, "__t", tv);
    uival_value_decref(tv);                    // kset retained its own reference
    if (IS_TABLE(children)) kset(t, "children", children);
    if (IS_TABLE(opts)) kset(t, "opts", opts);
    merge_opts(t, opts);
    return MAKE_TABLE(t);                // caller owns the descriptor
}

// builds a statusbar descriptor; borrows opts
static Value make_statusbar(Value opts) {
    Table* t = uival_table_create(8);
    if (!t) return MAKE_NONE();
    Value tv = MAKE_STRING(uival_string_create("statusbar", 9));
    kset(t, "__t", tv);
    uival_value_decref(tv);                    // kset retained its own reference
    if (IS_TABLE(opts)) kset(t, "opts", opts);
    merge_opts(t, opts);
    return MAKE_TABLE(t);                // caller owns the descriptor
}

// constructs the none value
apexui_value apexui_make_none(void)   { return MAKE_NONE(); }

// constructs a boolean value
apexui_value apexui_make_bool(bool v) { return MAKE_BOOL(v); }

// constructs a numeric value
apexui_value apexui_make_number(double v) { return MAKE_NUMBER(v); }

// constructs a string value from a byte range; caller owns the reference
apexui_value apexui_make_string(const char* s, int len) {
    if (!s) { len = 0; s = ""; }
    if (len < 0) len = 0;
    StringObject* str = uival_string_create(s, len);
    if (!str) return MAKE_NONE();
    return MAKE_STRING(str);             // caller owns the fresh object
}

// constructs a string value from a null-terminated c string
apexui_value apexui_make_string_cstr(const char* s) {
    return apexui_make_string(s, s ? (int)strlen(s) : 0);
}

// constructs a fresh hash table (also used as an array); caller owns the reference
apexui_value apexui_make_table(int hash_capacity) {
    Table* t = uival_table_create(hash_capacity > 0 ? hash_capacity : 8);
    if (!t) return MAKE_NONE();
    return MAKE_TABLE(t);                // caller owns the fresh table
}

// bumps the reference count of a heap-tagged value
void apexui_value_retain(apexui_value v)  { uival_value_incref((Value)v); }

// drops one reference; frees the object when the count hits zero
void apexui_value_release(apexui_value v) { uival_value_decref((Value)v); }

// true when the value holds the none tag
bool apexui_is_none(apexui_value v)   { return IS_NONE(v); }

// true when the value holds a boolean
bool apexui_is_bool(apexui_value v)   { return IS_BOOL(v); }

// true when the value holds a number
bool apexui_is_number(apexui_value v) { return IS_NUMBER(v); }

// true when the value holds a string
bool apexui_is_string(apexui_value v) { return IS_STRING(v); }

// true when the value holds a table
bool apexui_is_table(apexui_value v)  { return IS_TABLE(v); }

// unwraps a number; caller must check apexui_is_number first
double apexui_get_number(apexui_value v) { return AS_NUMBER(v); }

// unwraps a boolean; caller must check apexui_is_bool first
bool   apexui_get_bool(apexui_value v)   { return AS_BOOL(v); }

// borrowed pointer to the string bytes, or null; valid while v is alive
const char* apexui_get_string_ptr(apexui_value v) {
    if (!IS_STRING(v)) return NULL;
    return AS_STRING(v)->chars;
}

// byte length of the string, or zero
int apexui_get_string_len(apexui_value v) {
    if (!IS_STRING(v)) return 0;
    return AS_STRING(v)->length;
}

// malloc'd copy of the string, null-terminated; free() it
char* apexui_string_dup_cstr(apexui_value v) {
    if (!IS_STRING(v)) return NULL;
    StringObject* s = AS_STRING(v);
    char* out = (char*)malloc((size_t)s->length + 1);
    if (!out) return NULL;
    memcpy(out, s->chars, (size_t)s->length);
    out[s->length] = '\0';
    return out;
}

// inserts or updates key -> value; table takes its own reference to value
bool apexui_table_set(apexui_value t, apexui_value key, apexui_value value) {
    if (!IS_TABLE(t)) return false;
    return uival_table_set(AS_TABLE(t), (Value)key, (Value)value);
}

// looks a key up; on hit *out receives a new reference the caller must release
bool apexui_table_get(apexui_value t, apexui_value key, apexui_value* out) {
    if (!IS_TABLE(t) || !out) return false;
    Value v;
    if (!uival_table_get(AS_TABLE(t), (Value)key, &v)) return false;
    *out = (apexui_value)v;              // table_get handed us a new reference
    return true;
}

// removes a key; no-op when absent
void apexui_table_remove(apexui_value t, apexui_value key) {
    if (!IS_TABLE(t)) return;
    uival_table_remove(AS_TABLE(t), (Value)key);
}

// number of live entries (array part + hash part)
int apexui_table_size(apexui_value t) {
    if (!IS_TABLE(t)) return 0;
    return uival_table_size(AS_TABLE(t));
}

// snapshot of all keys; free() the array and release each key value
apexui_value* apexui_table_keys(apexui_value t, int* out_count) {
    if (out_count) *out_count = 0;
    if (!IS_TABLE(t) || !out_count) return NULL;
    int n = 0;
    Value* keys = uival_table_keys(AS_TABLE(t), &n);   // snapshot holds key references
    if (!keys) return NULL;
    *out_count = n;
    return (apexui_value*)keys;
}

// set by plain c string key
bool apexui_table_set_cstr(apexui_value t, const char* key, apexui_value value) {
    if (!IS_TABLE(t) || !key) return false;
    Value k = MAKE_STRING(uival_string_create(key, (int)strlen(key)));
    if (!IS_STRING(k)) return false;
    bool ok = uival_table_set(AS_TABLE(t), k, (Value)value);
    uival_value_decref(k);
    return ok;
}

// get by plain c string key; on hit *out receives a new reference
bool apexui_table_get_cstr(apexui_value t, const char* key, apexui_value* out) {
    if (!IS_TABLE(t) || !key || !out) return false;
    Value k = MAKE_STRING(uival_string_create(key, (int)strlen(key)));
    if (!IS_STRING(k)) return false;
    Value v;
    bool ok = uival_table_get(AS_TABLE(t), k, &v);
    uival_value_decref(k);
    if (!ok) return false;
    *out = (apexui_value)v;
    return true;
}

// set a numeric field by c string key
bool apexui_table_set_number(apexui_value t, const char* key, double v) {
    return apexui_table_set_cstr(t, key, MAKE_NUMBER(v));
}

// set a boolean field by c string key
bool apexui_table_set_bool(apexui_value t, const char* key, bool v) {
    return apexui_table_set_cstr(t, key, MAKE_BOOL(v));
}

// set a string field by c string key; copies the bytes
bool apexui_table_set_string(apexui_value t, const char* key, const char* v) {
    Value sv = str_or_none(v);            // fresh string for the value
    bool ok = apexui_table_set_cstr(t, key, sv);
    uival_value_decref(sv);               // the table kept its own reference
    return ok;
}

// read a numeric field; false on miss or type mismatch
bool apexui_table_get_number(apexui_value t, const char* key, double* out) {
    apexui_value v;
    if (!out || !apexui_table_get_cstr(t, key, &v)) return false;
    if (!IS_NUMBER(v)) { uival_value_decref(v); return false; }
    *out = AS_NUMBER(v);
    uival_value_decref(v);
    return true;
}

// read a boolean field; false on miss or type mismatch
bool apexui_table_get_bool(apexui_value t, const char* key, bool* out) {
    apexui_value v;
    if (!out || !apexui_table_get_cstr(t, key, &v)) return false;
    if (!IS_BOOL(v)) { uival_value_decref(v); return false; }
    *out = AS_BOOL(v);
    uival_value_decref(v);
    return true;
}

// read a string field; returns a borrowed pointer valid while the table holds it
bool apexui_table_get_string(apexui_value t, const char* key,
                             const char** out_ptr, int* out_len) {
    apexui_value v;
    if (!out_ptr || !apexui_table_get_cstr(t, key, &v)) return false;
    if (!IS_STRING(v)) { uival_value_decref(v); return false; }
    StringObject* s = AS_STRING(v);
    *out_ptr = s->chars;                 // borrowed: valid while the table holds it
    if (out_len) *out_len = s->length;
    uival_value_decref(v);               // the table keeps its own reference alive
    return true;
}

// append a value to the 1-based array part; returns success
bool apexui_array_push(apexui_value arr, apexui_value v) {
    if (!IS_TABLE(arr)) return false;
    Table* t = AS_TABLE(arr);
    return uival_table_set_int(t, t->array_count, (Value)v);  // 0-based index == next slot
}

// fetch a 1-based array element; caller owns the reference, none if out of range
apexui_value apexui_array_get(apexui_value arr, int index) {
    if (!IS_TABLE(arr) || index < 1) return MAKE_NONE();
    Value v;
    if (!uival_table_get(AS_TABLE(arr), MAKE_NUMBER((double)index), &v)) return MAKE_NONE();
    return (apexui_value)v;              // caller owns the reference
}

// length of the 1-based array part
int apexui_array_count(apexui_value arr) {
    if (!IS_TABLE(arr)) return 0;
    Table* t = AS_TABLE(arr);
    return t->array_part ? t->array_count : 0;
}

// destroys the context, closing the window if still open
void apexui_context_destroy(ApexUiContext* ctx) {
    apexui_context_free((UiContext*)ctx);
}

// opens the native window from an options table; returns the resulting bool
apexui_value apexui_init(ApexUiContext* ctx, apexui_value opts) {
    Value argv[1];
    int argc = 0;
    if (!ctx) return MAKE_BOOL(false);          // null context: no window
    if (IS_TABLE(opts)) { argv[0] = (Value)opts; argc = 1; }
    return (apexui_value)apexui_impl_init((UiContext*)ctx, argc, argv);
}

// value-free window bootstrap; returns false when the window cannot be created
bool apexui_open(ApexUiContext* ctx, const char* title, int width, int height) {
    Table* t;
    Value argv[1];
    Value out;
    if (!ctx) return false;                     // null context: no window
    t = uival_table_create(8);                  // transient opts table
    if (!t) return false;
    if (title) {
        Value tv = str_or_none(title);
        kset(t, "title", tv);
        uival_value_decref(tv);                // kset retained its own reference
    }
    if (width  > 0) kset(t, "width",  MAKE_NUMBER(width));
    if (height > 0) kset(t, "height", MAKE_NUMBER(height));
    argv[0] = MAKE_TABLE(t);
    out = apexui_impl_init((UiContext*)ctx, 1, argv);
    uival_value_decref(argv[0]);               // drop our table reference
    return IS_BOOL(out) && AS_BOOL(out);
}

// closes the native window; the context stays usable for a new apexui_init
void apexui_shutdown(ApexUiContext* ctx) {
    apexui_impl_shutdown((UiContext*)ctx, 0, NULL);
}

// value-level render: stages a widget descriptor tree for the next repaint
void apexui_render_value(ApexUiContext* ctx, apexui_value tree) {
    Value argv[1];
    argv[0] = (Value)tree;               // impl retains heap-tagged trees
    apexui_impl_render((UiContext*)ctx, 1, argv);
}

// blocks until the next event and returns it as a table
apexui_value apexui_next_event(ApexUiContext* ctx) {
    return (apexui_value)apexui_impl_next_event((UiContext*)ctx, 0, NULL);
}

// drives the message loop with batched repaints until the window closes
apexui_value apexui_run(ApexUiContext* ctx, apexui_run_fn fn, void* user) {
    ui_run_loop((UiContext*)ctx, fn, user);
    return MAKE_NONE();
}

// sets the native window title and mirrors it for the custom titlebar
void apexui_set_title(ApexUiContext* ctx, const char* title) {
    if (!title) return;
    Value v = MAKE_STRING(uival_string_create(title, (int)strlen(title)));
    if (!IS_STRING(v)) return;
    apexui_impl_set_title((UiContext*)ctx, 1, &v);
    uival_value_decref(v);
}

// current client area size as a {w, h} table
apexui_value apexui_window_size(ApexUiContext* ctx) {
    return (apexui_value)apexui_impl_window_size((UiContext*)ctx, 0, NULL);
}

// native "open file" picker; returns the picked path or none
apexui_value apexui_open_file(ApexUiContext* ctx, apexui_value opts) {
    Value argv[1];
    int argc = 0;
    if (IS_TABLE(opts)) { argv[0] = (Value)opts; argc = 1; }
    return (apexui_value)apexui_impl_open_file((UiContext*)ctx, argc, argv);
}

// native "open folder" picker; returns the picked path or none
apexui_value apexui_open_folder(ApexUiContext* ctx, apexui_value opts) {
    Value argv[1];
    int argc = 0;
    if (IS_TABLE(opts)) { argv[0] = (Value)opts; argc = 1; }
    return (apexui_value)apexui_impl_open_folder((UiContext*)ctx, argc, argv);
}

// native "save file" picker; writes the content and returns the path or none
apexui_value apexui_save_as(ApexUiContext* ctx, const char* content,
                            int content_len, apexui_value opts) {
    if (!content) { content = ""; content_len = 0; }
    if (content_len < 0) content_len = (int)strlen(content);
    Value argv[2];
    argv[0] = MAKE_STRING(uival_string_create(content, content_len));
    if (!IS_STRING(argv[0])) return MAKE_NONE();
    int argc = 1;
    if (IS_TABLE(opts)) { argv[1] = (Value)opts; argc = 2; }
    Value out = apexui_impl_save_as((UiContext*)ctx, argc, argv);
    uival_value_decref(argv[0]);               // release the local content copy
    return (apexui_value)out;
}

// set the window's fullscreen state; returns the resulting state
bool apexui_set_fullscreen(ApexUiContext* ctx, bool enable) {
    Value argv[1];
    argv[0] = MAKE_BOOL(enable);
    Value out = apexui_impl_fullscreen((UiContext*)ctx, 1, argv);
    bool res = IS_BOOL(out) && AS_BOOL(out);
    return res;
}

// flip the window's fullscreen state; returns the resulting state
bool apexui_toggle_fullscreen(ApexUiContext* ctx) {
    Value out = apexui_impl_fullscreen((UiContext*)ctx, 0, NULL);
    bool res = IS_BOOL(out) && AS_BOOL(out);
    return res;
}

// open a modal dialog from a descriptor table
void apexui_dialog(ApexUiContext* ctx, apexui_value descr) {
    UiContext* c = (UiContext*)ctx;
    if (!c || !IS_TABLE(descr)) return;  // descriptor table required

    Table* t = AS_TABLE(descr);

    char* key   = ui_table_get_str_dup(c, t, "key");               // script-visible id
    char* title = ui_table_get_str_dup(c, t, "title");             // heading text
    char* msg   = ui_table_get_str_dup(c, t, "message");           // body text
    dialog_begin(c, key ? key : "", title ? title : "", msg ? msg : "");
    free(key); free(title); free(msg);                          // dialog owns its copies

    // fields
    Value fk = MAKE_STRING(uival_string_create("fields", 6)), fv;
    if (IS_STRING(fk) && uival_table_get(t, fk, &fv)) {
        if (IS_TABLE(fv)) {
            Table* ft = AS_TABLE(fv);
            int n = ft->array_part ? ft->array_count : 0;
            for (int i = 0; i < n; i++) {
                Value item = ft->array_part[i];
                if (!IS_TABLE(item)) continue;
                Table* it = AS_TABLE(item);
                char* fkey  = ui_table_get_str_dup(c, it, "key");
                char* flbl  = ui_table_get_str_dup(c, it, "label");
                char* fph   = ui_table_get_str_dup(c, it, "placeholder");
                char* fdef  = ui_table_get_str_dup(c, it, "default");
                dialog_add_field(c,
                                 fkey  ? fkey  : "",
                                 flbl  ? flbl  : "",
                                 fph   ? fph   : "",
                                 fdef  ? fdef  : "");
                free(fkey); free(flbl); free(fph); free(fdef);
            }
        }
        uival_value_decref(fv);
    }
    if (IS_STRING(fk)) uival_value_decref(fk);

    // buttons
    Value bk = MAKE_STRING(uival_string_create("buttons", 7)), bv;
    if (IS_STRING(bk) && uival_table_get(t, bk, &bv)) {
        if (IS_TABLE(bv)) {
            Table* bt = AS_TABLE(bv);
            int n = bt->array_part ? bt->array_count : 0;
            for (int i = 0; i < n; i++) {
                Value item = bt->array_part[i];
                if (!IS_TABLE(item)) continue;
                Table* it = AS_TABLE(item);
                char* bid  = ui_table_get_str_dup(c, it, "id");
                char* blbl = ui_table_get_str_dup(c, it, "label");
                bool bprim = false;
                Value pk = MAKE_STRING(uival_string_create("primary", 7)), pv;
                if (IS_STRING(pk) && uival_table_get(it, pk, &pv)) {
                    if (IS_BOOL(pv)) bprim = AS_BOOL(pv);
                    uival_value_decref(pv);
                }
                if (IS_STRING(pk)) uival_value_decref(pk);
                dialog_add_button(c, bid ? bid : "", blbl ? blbl : "", bprim);
                free(bid); free(blbl);
            }
        }
        uival_value_decref(bv);
    }
    if (IS_STRING(bk)) uival_value_decref(bk);

    dialog_commit(c);                    // drop if empty, else publish
}

// show a desktop notification popup from a descriptor table
void apexui_notification(ApexUiContext* ctx, apexui_value descr) {
    UiContext* c = (UiContext*)ctx;
    if (!c || !IS_TABLE(descr)) return;

    Table* t = AS_TABLE(descr);
    char* key     = ui_table_get_str_dup(c, t, "key");
    char* title   = ui_table_get_str_dup(c, t, "title");
    char* message = ui_table_get_str_dup(c, t, "message");
    double timeout   = ui_table_get_num(c, t, "timeout", 4.0);
    bool show_close  = ui_table_get_bool(c, t, "close", true);

    ui_notification_show(c,
                         key     ? key     : "",
                         title   ? title   : "",
                         message ? message : "",
                         timeout, show_close);

    free(key); free(title); free(message);
}

// install or clear the window menu bar from a descriptor array
void apexui_menubar(ApexUiContext* ctx, apexui_value descr) {
    UiContext* c = (UiContext*)ctx;
    if (!c) return;
    if (IS_TABLE(descr)) {
        ui_menubar_set(c, (Value)descr); // install the new bar
    } else {
        ui_menubar_free(c);              // clear any existing bar
        c->redraw_needed = true;
        c->full_repaint  = true;
    }
}

// open a context menu at the current cursor position
void apexui_context_menu_open(ApexUiContext* ctx, apexui_value items) {
    UiContext* c = (UiContext*)ctx;
    if (!c) return;
    if (!IS_TABLE(items)) {
        ui_context_menu_close(c);
        return;
    }
    ui_context_menu_set(c, (Value)items, c->input.x, c->input.y);
}

// open a context menu at explicit coordinates
void apexui_context_menu_open_at(ApexUiContext* ctx, apexui_value items, int x, int y) {
    UiContext* c = (UiContext*)ctx;
    if (!c) return;
    if (!IS_TABLE(items)) {
        ui_context_menu_close(c);
        return;
    }
    ui_context_menu_set(c, (Value)items, x, y);
}

// close any open context menu
void apexui_context_menu_close(ApexUiContext* ctx) {
    UiContext* c = (UiContext*)ctx;
    if (!c) return;
    ui_context_menu_close(c);
}

// table of named key / modifier constants
apexui_value apexui_key_constants(void) {
    Table* t = uival_table_create(32);
    if (!t) return MAKE_NONE();
    kset(t, "esc",       MAKE_NUMBER((double)UI_KEY_ESCAPE));
    kset(t, "enter",     MAKE_NUMBER((double)UI_KEY_ENTER));
    kset(t, "tab",       MAKE_NUMBER((double)UI_KEY_TAB));
    kset(t, "backspace", MAKE_NUMBER((double)UI_KEY_BACKSPACE));
    kset(t, "delete",    MAKE_NUMBER((double)UI_KEY_DELETE));
    kset(t, "insert",    MAKE_NUMBER((double)UI_KEY_INSERT));
    kset(t, "home",      MAKE_NUMBER((double)UI_KEY_HOME));
    kset(t, "end",       MAKE_NUMBER((double)UI_KEY_END));
    kset(t, "page_up",   MAKE_NUMBER((double)UI_KEY_PAGE_UP));
    kset(t, "page_down", MAKE_NUMBER((double)UI_KEY_PAGE_DOWN));
    kset(t, "left",      MAKE_NUMBER((double)UI_KEY_LEFT));
    kset(t, "right",     MAKE_NUMBER((double)UI_KEY_RIGHT));
    kset(t, "up",        MAKE_NUMBER((double)UI_KEY_UP));
    kset(t, "down",      MAKE_NUMBER((double)UI_KEY_DOWN));
    kset(t, "space",     MAKE_NUMBER((double)UI_KEY_SPACE));
    kset(t, "f1",        MAKE_NUMBER((double)UI_KEY_F1));
    kset(t, "f2",        MAKE_NUMBER((double)UI_KEY_F2));
    kset(t, "f3",        MAKE_NUMBER((double)UI_KEY_F3));
    kset(t, "f4",        MAKE_NUMBER((double)UI_KEY_F4));
    kset(t, "f5",        MAKE_NUMBER((double)UI_KEY_F5));
    kset(t, "f6",        MAKE_NUMBER((double)UI_KEY_F6));
    kset(t, "f7",        MAKE_NUMBER((double)UI_KEY_F7));
    kset(t, "f8",        MAKE_NUMBER((double)UI_KEY_F8));
    kset(t, "f9",        MAKE_NUMBER((double)UI_KEY_F9));
    kset(t, "f10",       MAKE_NUMBER((double)UI_KEY_F10));
    kset(t, "f11",       MAKE_NUMBER((double)UI_KEY_F11));
    kset(t, "f12",       MAKE_NUMBER((double)UI_KEY_F12));
    kset(t, "shift",     MAKE_NUMBER((double)UI_MOD_SHIFT));
    kset(t, "ctrl",      MAKE_NUMBER((double)UI_MOD_CTRL));
    kset(t, "alt",       MAKE_NUMBER((double)UI_MOD_ALT));
    return MAKE_TABLE(t);                // caller owns the table
}

// registers a freshly built descriptor with the context staging arena
static ApexuiNode* node_track(UiContext* c, Value descr) {
    if (!c) { uival_value_decref(descr); return NULL; }    // null context: no node
    if (!IS_TABLE(descr)) return NULL;
    ApexuiNode* n = c->node_free;                           // recycle a parked node
    if (n) {
        c->node_free = n->next_free;                       // pop from the free list
    } else {
        n = (ApexuiNode*)malloc(sizeof(ApexuiNode));
        if (!n) { uival_value_decref(descr); return NULL; } // oom: drop the descriptor
    }
    n->descr = descr;
    if (c->node_count >= c->node_cap) {                     // grow the pointer array
        int cap = c->node_cap ? c->node_cap * 2 : 16;
        ApexuiNode** nl = (ApexuiNode**)realloc(c->node_list,
                           sizeof(ApexuiNode*) * (size_t)cap);
        if (!nl) {
            if ((n->descr & QNAN) == QNAN) uival_value_decref(n->descr);
            n->descr = MAKE_NONE();
            n->next_free = c->node_free;
            c->node_free = n;                               // park it again
            return NULL;
        }
        c->node_list = nl;
        c->node_cap  = cap;
    }
    c->node_list[c->node_count++] = n;
    return n;
}

// releases every staged node
void ui_nodes_release_all(UiContext* c) {
    if (!c) return;
    for (int i = 0; i < c->node_count; i++) {
        ApexuiNode* n = c->node_list[i];
        if ((n->descr & QNAN) == QNAN) uival_value_decref(n->descr);
        n->descr = MAKE_NONE();         // a stale handle now renders nothing
        n->next_free = c->node_free;    // park the struct for reuse
        c->node_free = n;
    }
    c->node_count = 0;                  // keep the array capacity for reuse
}

// converts widget options into an opts table value; none when nothing is set
static Value opts_to_table(ApexuiOpts o) {
    bool any = o.id || o.css_class ||
               o.width != 0.0 || o.height != 0.0 || o.disabled || o.checked;
    if (!any) return MAKE_NONE();
    Table* t = uival_table_create(8);
    if (!t) return MAKE_NONE();
    if (o.id) {                          // identity lands on the "key" field,
        Value v = str_or_none(o.id);     // the name events report back
        kset(t, "key", v);
        uival_value_decref(v);
    }
    if (o.css_class) {
        Value v = str_or_none(o.css_class);
        kset(t, "css_class", v);         // stored for hosts; the core keeps it
        uival_value_decref(v);
    }
    if (o.width  != 0.0) kset(t, "width",  MAKE_NUMBER(o.width));
    if (o.height != 0.0) kset(t, "height", MAKE_NUMBER(o.height));
    if (o.disabled)      kset(t, "disabled", MAKE_BOOL(true));
    if (o.checked)       kset(t, "checked", MAKE_BOOL(true));
    return MAKE_TABLE(t);
}

// builds and stages a leaf descriptor of the given kind
static ApexuiNode* node_leaf(UiContext* c, const char* type,
                             const char* content, ApexuiOpts opts) {
    Value cv = str_or_none(content);
    Value ov = opts_to_table(opts);
    Value d = make_leaf(type, cv, ov);
    uival_value_decref(cv);
    uival_value_decref(ov);
    return node_track(c, d);
}

// builds and stages a container descriptor from live node handles
static ApexuiNode* node_container(UiContext* c, const char* type,
                                  ApexuiNode* const* kids, size_t n,
                                  ApexuiOpts opts, bool stretch) {
    Value ov = opts_to_table(opts);
    Table* arr = NULL;
    if (kids && n > 0) {
        arr = uival_table_create(n > 64 ? 64 : (int)n);
        if (!arr) { uival_value_decref(ov); return NULL; }
        int m = 0;
        for (size_t i = 0; i < n; i++) {
            if (!kids[i] || !IS_TABLE(kids[i]->descr)) continue;  // skip null / stale
            uival_table_set_int(arr, m, kids[i]->descr);          // array keeps a ref
            m++;
        }
    }
    Value kids_v = arr ? MAKE_TABLE(arr) : MAKE_NONE();
    Value d = make_container(type, kids_v, ov);
    if (arr) uival_value_decref(kids_v);
    uival_value_decref(ov);
    if (stretch && IS_TABLE(d)) kset(AS_TABLE(d), "weight", MAKE_NUMBER(1.0));
    return node_track(c, d);
}

// a push button with a text label
ApexuiNode* apexui_button(ApexUiContext* ctx, const char* label) {
    return node_leaf((UiContext*)ctx, "button", label, (ApexuiOpts){0});
}

// a push button with a text label and options
ApexuiNode* apexui_button_ex(ApexUiContext* ctx, const char* label, ApexuiOpts opts) {
    return node_leaf((UiContext*)ctx, "button", label, opts);
}

// a line of body text
ApexuiNode* apexui_label(ApexUiContext* ctx, const char* text) {
    return node_leaf((UiContext*)ctx, "text", text, (ApexuiOpts){0});
}

// a line of body text with options
ApexuiNode* apexui_label_ex(ApexUiContext* ctx, const char* text, ApexuiOpts opts) {
    return node_leaf((UiContext*)ctx, "text", text, opts);
}

// a line of heading text
ApexuiNode* apexui_heading(ApexUiContext* ctx, const char* text) {
    return node_leaf((UiContext*)ctx, "heading", text, (ApexuiOpts){0});
}

// a line of heading text with options
ApexuiNode* apexui_heading_ex(ApexUiContext* ctx, const char* text, ApexuiOpts opts) {
    return node_leaf((UiContext*)ctx, "heading", text, opts);
}

// a horizontal rule
ApexuiNode* apexui_separator(ApexUiContext* ctx) {
    return node_leaf((UiContext*)ctx, "separator", NULL, (ApexuiOpts){0});
}

// a horizontal rule with options
ApexuiNode* apexui_separator_ex(ApexUiContext* ctx, ApexuiOpts opts) {
    return node_leaf((UiContext*)ctx, "separator", NULL, opts);
}

// spacer: a flexible gap, or a fixed-size gap when opts.width / height are set
static ApexuiNode* node_spacer(UiContext* c, ApexuiOpts opts) {
    Value ov = opts_to_table(opts);
    Table* t = uival_table_create(8);
    if (!t) { uival_value_decref(ov); return NULL; }
    Value tv = MAKE_STRING(uival_string_create("spacer", 6));
    kset(t, "__t", tv);
    uival_value_decref(tv);                    // kset retained its own reference
    if (opts.width <= 0.0 && opts.height <= 0.0)
        kset(t, "weight", MAKE_NUMBER(1.0));       // no explicit size: flex gap
    if (IS_TABLE(ov)) {                             // width / height / id land here
        kset(t, "opts", ov);
        merge_opts(t, ov);
    }
    uival_value_decref(ov);
    return node_track(c, MAKE_TABLE(t));
}

// a flexible gap
ApexuiNode* apexui_spacer(ApexUiContext* ctx) {
    return node_spacer((UiContext*)ctx, (ApexuiOpts){0});
}

// a fixed-size gap via opts.width / opts.height
ApexuiNode* apexui_spacer_ex(ApexUiContext* ctx, ApexuiOpts opts) {
    return node_spacer((UiContext*)ctx, opts);
}

// a checkbox with a text label
ApexuiNode* apexui_checkbox(ApexUiContext* ctx, const char* label) {
    return node_leaf((UiContext*)ctx, "checkbox", label, (ApexuiOpts){0});
}

// a checkbox with a text label and options; opts.checked sets the initial state
ApexuiNode* apexui_checkbox_ex(ApexUiContext* ctx, const char* label, ApexuiOpts opts) {
    return node_leaf((UiContext*)ctx, "checkbox", label, opts);  // opts.checked flows through
}

// input: the text is the initial content of the field
static ApexuiNode* node_input(UiContext* c, const char* text, ApexuiOpts opts) {
    Value cv = str_or_none(text);
    Value ov = opts_to_table(opts);
    Value d = make_leaf("input", cv, ov);
    if (IS_TABLE(d) && !IS_NONE(cv)) kset(AS_TABLE(d), "value_text", cv);
    uival_value_decref(cv);
    uival_value_decref(ov);
    return node_track(c, d);
}

// a single-line text input
ApexuiNode* apexui_input(ApexUiContext* ctx, const char* text) {
    return node_input((UiContext*)ctx, text, (ApexuiOpts){0});
}

// a single-line text input with options
ApexuiNode* apexui_input_ex(ApexUiContext* ctx, const char* text, ApexuiOpts opts) {
    return node_input((UiContext*)ctx, text, opts);
}

// dropdown: selected is the shown label, items the n selectable labels
static ApexuiNode* node_dropdown(UiContext* c, const char* selected,
                                 const char* const* items, size_t n,
                                 ApexuiOpts opts) {
    Value sv = str_or_none(selected);
    Value ov = opts_to_table(opts);
    Value d = make_leaf("dropdown", sv, ov);
    uival_value_decref(sv);
    uival_value_decref(ov);
    if (!IS_TABLE(d)) return NULL;
    if (items && n > 0) {
        Table* arr = uival_table_create(n > 64 ? 64 : (int)n);
        if (!arr) { uival_value_decref(d); return NULL; }
        int m = 0;
        for (size_t i = 0; i < n; i++) {
            if (!items[i]) continue;                // null items are skipped
            Value iv = str_or_none(items[i]);
            uival_table_set_int(arr, m, iv);
            uival_value_decref(iv);
            m++;
        }
        Value av = MAKE_TABLE(arr);
        kset(AS_TABLE(d), "options", av);
        uival_value_decref(av);
    }
    return node_track(c, d);
}

// a dropdown showing `selected` with the given items
ApexuiNode* apexui_dropdown(ApexUiContext* ctx, const char* selected,
                            const char* const* items, size_t n) {
    return node_dropdown((UiContext*)ctx, selected, items, n, (ApexuiOpts){0});
}

// a dropdown showing `selected` with the given items and options
ApexuiNode* apexui_dropdown_ex(ApexUiContext* ctx, const char* selected,
                               const char* const* items, size_t n, ApexuiOpts opts) {
    return node_dropdown((UiContext*)ctx, selected, items, n, opts);
}

// a vertical stack of children
ApexuiNode* apexui_column(ApexUiContext* ctx, ApexuiNode* const* kids, size_t n) {
    return node_container((UiContext*)ctx, "vbox", kids, n, (ApexuiOpts){0}, false);
}

// a vertical stack of children with options
ApexuiNode* apexui_column_ex(ApexUiContext* ctx, ApexuiNode* const* kids,
                             size_t n, ApexuiOpts opts) {
    return node_container((UiContext*)ctx, "vbox", kids, n, opts, false);
}

// a horizontal row of children
ApexuiNode* apexui_row(ApexUiContext* ctx, ApexuiNode* const* kids, size_t n) {
    return node_container((UiContext*)ctx, "hbox", kids, n, (ApexuiOpts){0}, false);
}

// a horizontal row of children with options
ApexuiNode* apexui_row_ex(ApexUiContext* ctx, ApexuiNode* const* kids,
                          size_t n, ApexuiOpts opts) {
    return node_container((UiContext*)ctx, "hbox", kids, n, opts, false);
}

// grid: composes the children row-major into `cols` equal-weight columns
static ApexuiNode* node_grid(UiContext* c, int cols,
                             ApexuiNode* const* kids, size_t n, ApexuiOpts opts) {
    if (cols < 1) cols = 1;
    if (n == 0) return node_container(c, "hbox", NULL, 0, opts, false);

    size_t rows = (n + (size_t)cols - 1) / (size_t)cols;
    ApexuiNode** buf = (ApexuiNode**)malloc(sizeof(ApexuiNode*) *
                                            (rows + (size_t)cols));
    if (!buf) return NULL;
    ApexuiNode** col_kids = buf;                    // scratch: one column of kids
    ApexuiNode** col_nodes = buf + rows;            // the cols column handles
    int made = 0;

    for (int cix = 0; cix < cols; cix++) {
        size_t cnt = 0;
        for (size_t i = (size_t)cix; i < n; i += (size_t)cols)
            if (kids[i] && IS_TABLE(kids[i]->descr)) col_kids[cnt++] = kids[i];
        col_nodes[made] = node_container(c, "vbox", cnt ? col_kids : NULL, cnt,
                                         (ApexuiOpts){0}, true);
        if (!col_nodes[made]) { free(buf); return NULL; }
        made++;
    }

    ApexuiNode* row = node_container(c, "hbox", col_nodes, (size_t)made, opts, false);
    free(buf);
    return row;
}

// a row-major grid laid out into `cols` equal-width columns
ApexuiNode* apexui_grid(ApexUiContext* ctx, int cols, ApexuiNode* const* kids, size_t n) {
    return node_grid((UiContext*)ctx, cols, kids, n, (ApexuiOpts){0});
}

// a row-major grid laid out into `cols` equal-width columns with options
ApexuiNode* apexui_grid_ex(ApexUiContext* ctx, int cols, ApexuiNode* const* kids,
                           size_t n, ApexuiOpts opts) {
    return node_grid((UiContext*)ctx, cols, kids, n, opts);
}

// atomically commits a new tree from the given roots and releases every staged node
void apexui_render(ApexUiContext* ctx, ApexuiNode* const* roots, size_t n) {
    UiContext* c = (UiContext*)ctx;
    if (!c) return;
    if (!roots || n == 0) {                 // nothing to show: clear the window
        apexui_clear(ctx);
        return;
    }
    if (n == 1) {                           // single root: hand the descriptor
        Value d = (roots[0] && IS_TABLE(roots[0]->descr)) ? roots[0]->descr
                                                          : MAKE_NONE();
        apexui_impl_render(c, 1, &d);       // impl retains heap-tagged trees
    } else {                                // multiple roots: a 1-based array
        Table* arr = uival_table_create(n > 64 ? 64 : (int)n);
        if (!arr) { ui_nodes_release_all(c); return; }   // oom: keep the old tree
        int m = 0;
        for (size_t i = 0; i < n; i++)
            if (roots[i] && IS_TABLE(roots[i]->descr))
                uival_table_set_int(arr, m++, roots[i]->descr);
        Value v = MAKE_TABLE(arr);
        apexui_impl_render(c, 1, &v);
        uival_value_decref(v);
    }
    ui_nodes_release_all(c);                // the handles are consumed
}

// commits a single-root tree
void apexui_render_one(ApexUiContext* ctx, ApexuiNode* root) {
    apexui_render(ctx, &root, 1);
}

// releases the staged nodes and the committed tree without rendering
void apexui_clear(ApexUiContext* ctx) {
    UiContext* c = (UiContext*)ctx;
    if (!c) return;
    ui_nodes_release_all(c);                // drop staged handles
    Value none = MAKE_NONE();
    apexui_impl_render(c, 1, &none);        // drop the committed tree
}

// an event handed to the handler: the wrapped table lives for the callback
struct ApexuiEvent {
    Value table;
};

// pumps the window once and hands every queued event to the handler in order
void apexui_poll_events(ApexUiContext* ctx, ApexuiHandler handler, void* userdata) {
    UiContext* c = (UiContext*)ctx;
    if (!c || !handler) return;

    if (c->window && c->redraw_needed && !c->quit_requested)
        rebuild_and_draw(c);                // honour a render() made before polling

    if (c->window)
        apexui_pump(c);                     // translate native events into the queue

    UiEvent ue;
    while (ui_event_pop(c, &ue)) {
        Value tbl = ui_event_to_table(c, &ue);
        free(ue.str_value);                 // release the owned string payload
        if (IS_TABLE(tbl)) {
            ApexuiEvent ev;
            ev.table = tbl;
            handler(&ev, userdata);         // framework owns the event
            uival_value_decref(tbl);
        }
        if (c->quit_requested) break;       // stop the batch like ui_run_loop
    }

    if (c->window && c->redraw_needed && !c->quit_requested)
        rebuild_and_draw(c);                // one repaint per batch
}

// reads a string field of the event table; borrowed pointer or null
static const char* event_str_field(const ApexuiEvent* ev, const char* key) {
    if (!ev || !key || !IS_TABLE(ev->table)) return NULL;
    const char* ptr;
    int len;
    if (!apexui_table_get_string(ev->table, key, &ptr, &len)) return NULL;
    return ptr;
}

// event kind: "click", "change", "key", "close", ... or null
const char* apexui_event_type(const ApexuiEvent* ev) {
    return event_str_field(ev, "type");
}

// identity (opts.id) of the widget that produced the event; "" when it has none
const char* apexui_event_id(const ApexuiEvent* ev) {
    return event_str_field(ev, "key");      // the opts.id the widget was built with
}

// numeric field lookup; booleans read as 1.0 / 0.0, misses as 0.0
double apexui_event_num(const ApexuiEvent* ev, const char* key) {
    if (!ev || !key || !IS_TABLE(ev->table)) return 0.0;
    apexui_value v;
    if (!apexui_table_get_cstr(ev->table, key, &v)) return 0.0;
    double r = 0.0;
    if (IS_NUMBER(v))      r = AS_NUMBER(v);
    else if (IS_BOOL(v))   r = AS_BOOL(v) ? 1.0 : 0.0;   // booleans read as 0 / 1
    uival_value_decref(v);
    return r;
}

// string field lookup; borrowed pointer, null on a miss
const char* apexui_event_str(const ApexuiEvent* ev, const char* key) {
    return event_str_field(ev, key);
}

// value-layer button constructor; argv is (content?, opts?)
static Value ctor_button_argv(int argc, Value* argv) {
    return make_leaf("button",
                     argc >= 1 ? argv[0] : MAKE_NONE(),
                     argc >= 2 ? argv[1] : MAKE_NONE());
}

// value-layer checkbox constructor; argv is (content?, checked?, opts?)
static Value ctor_checkbox_argv(int argc, Value* argv) {
    Value v = make_leaf("checkbox",
                        argc >= 1 ? argv[0] : MAKE_NONE(),
                        argc >= 3 ? argv[2] : MAKE_NONE());
    if (IS_TABLE(v)) {
        if (argc >= 2 && IS_BOOL(argv[1])) {
            kset(AS_TABLE(v), "checked", argv[1]);               // explicit bool state
        } else if (argc >= 2 && IS_NUMBER(argv[1])) {
            kset(AS_TABLE(v), "checked", MAKE_BOOL(AS_NUMBER(argv[1]) != 0));  // numeric truthiness
        }
    }
    return v;
}

// value-layer input constructor; argv is (content?, opts?)
static Value ctor_input_argv(int argc, Value* argv) {
    Value content = argc >= 1 ? argv[0] : MAKE_NONE();
    Value v = make_leaf("input", content, argc >= 2 ? argv[1] : MAKE_NONE());
    if (IS_TABLE(v) && !IS_NONE(content)) kset(AS_TABLE(v), "value_text", content);
    return v;
}

// value-layer text_area constructor; argv is (content?, opts?)
static Value ctor_text_area_argv(int argc, Value* argv) {
    Value content = argc >= 1 ? argv[0] : MAKE_NONE();
    Value v = make_leaf("text_area", content, argc >= 2 ? argv[1] : MAKE_NONE());
    if (IS_TABLE(v) && !IS_NONE(content)) kset(AS_TABLE(v), "value_text", content);
    return v;
}

// value-layer dropdown constructor; argv is (selected?, options?, opts?)
static Value ctor_dropdown_argv(int argc, Value* argv) {
    Value v = make_leaf("dropdown",
                        argc >= 1 ? argv[0] : MAKE_NONE(),
                        argc >= 3 ? argv[2] : MAKE_NONE());
    if (IS_TABLE(v) && argc >= 2 && IS_TABLE(argv[1]))
        kset(AS_TABLE(v), "options", argv[1]);
    return v;
}

// value-layer table constructor; argv is (columns?, rows?, opts?)
static Value ctor_table_argv(int argc, Value* argv) {
    Value v = make_leaf("table", MAKE_NONE(), argc >= 3 ? argv[2] : MAKE_NONE());
    if (IS_TABLE(v)) {
        if (argc >= 1 && IS_TABLE(argv[0])) kset(AS_TABLE(v), "columns", argv[0]);
        if (argc >= 2 && IS_TABLE(argv[1])) kset(AS_TABLE(v), "rows", argv[1]);
    }
    return v;
}

// value-layer tree constructor; argv is (nodes?, opts?)
static Value ctor_tree_argv(int argc, Value* argv) {
    Value v = make_leaf("tree", MAKE_NONE(), argc >= 2 ? argv[1] : MAKE_NONE());
    if (IS_TABLE(v) && argc >= 1 && IS_TABLE(argv[0]))
        kset(AS_TABLE(v), "nodes", argv[0]);
    return v;
}

// value-layer tabs constructor; argv is (tabs?, opts?)
static Value ctor_tabs_argv(int argc, Value* argv) {
    Value v = make_leaf("tabs", MAKE_NONE(), argc >= 2 ? argv[1] : MAKE_NONE());
    if (IS_TABLE(v) && argc >= 1 && IS_TABLE(argv[0]))
        kset(AS_TABLE(v), "tabs", argv[0]);
    return v;
}

// value-layer number_input constructor; argv is (value?, opts?)
static Value ctor_number_input_argv(int argc, Value* argv) {
    Value v = make_leaf("number_input",
                        argc >= 1 ? argv[0] : MAKE_NONE(),
                        argc >= 2 ? argv[1] : MAKE_NONE());
    if (IS_TABLE(v) && argc >= 1) kset(AS_TABLE(v), "value", argv[0]);
    return v;
}

// value-layer spacer constructor; argv is (min_size?, opts?)
static Value ctor_spacer_argv(int argc, Value* argv) {
    Table* t = uival_table_create(4);
    if (!t) return MAKE_NONE();
    Value tv = MAKE_STRING(uival_string_create("spacer", 6));
    kset(t, "__t", tv);
    uival_value_decref(tv);                   // kset retained its own reference
    if (argc >= 1 && IS_NUMBER(argv[0])) {    // numeric arg: fixed min size
        kset(t, "min_width", argv[0]);
        kset(t, "min_height", argv[0]);
    } else {                                  // default: flex spacer
        kset(t, "weight", MAKE_NUMBER(1.0));
    }
    if (argc >= 2 && IS_TABLE(argv[1])) {
        kset(t, "opts", argv[1]);
        merge_opts(t, argv[1]);
    }
    return MAKE_TABLE(t);                     // caller owns the descriptor
}

// name-based dispatcher over the value layer ("ui.button", "ui.init", ...)
bool apexui_call(ApexUiContext* ctx, const char* name,
                 int argc, const apexui_value* args, apexui_value* result) {
    UiContext* c = (UiContext*)ctx;
    Value* argv = (Value*)args;
    Value r = MAKE_NONE();

    if (!name || !result) return false;

    if (strcmp(name, "ui.key") == 0)               { r = (Value)apexui_key_constants(); }
    else if (strcmp(name, "ui.init") == 0)         { r = (Value)apexui_init(ctx, argc >= 1 ? argv[0] : MAKE_NONE()); }
    else if (strcmp(name, "ui.shutdown") == 0)     { apexui_shutdown(ctx); r = MAKE_NONE(); }
    else if (strcmp(name, "ui.render") == 0)       { apexui_render_value(ctx, argc >= 1 ? argv[0] : MAKE_NONE()); r = MAKE_NONE(); }
    else if (strcmp(name, "ui.next_event") == 0)   { r = (Value)apexui_next_event(ctx); }
    else if (strcmp(name, "ui.run") == 0)          { return false; }  // needs a host callback: use apexui_run
    else if (strcmp(name, "ui.set_title") == 0)    {
        if (argc >= 1 && IS_STRING(argv[0])) apexui_set_title(ctx, AS_STRING(argv[0])->chars);
        r = MAKE_NONE();
    }
    else if (strcmp(name, "ui.window_size") == 0)  { r = (Value)apexui_window_size(ctx); }
    else if (strcmp(name, "ui.open_file") == 0)    { r = (Value)apexui_open_file(ctx, argc >= 1 ? argv[0] : MAKE_NONE()); }
    else if (strcmp(name, "ui.open_folder") == 0)  { r = (Value)apexui_open_folder(ctx, argc >= 1 ? argv[0] : MAKE_NONE()); }
    else if (strcmp(name, "ui.save_as") == 0)      {
        if (argc >= 1 && IS_STRING(argv[0])) {
            StringObject* s = AS_STRING(argv[0]);
            r = (Value)apexui_save_as(ctx, s->chars, s->length,
                                      argc >= 2 ? argv[1] : MAKE_NONE());
        } else {
            r = MAKE_NONE();
        }
    }
    else if (strcmp(name, "ui.fullscreen") == 0)   {
        bool want = (argc >= 1 && IS_BOOL(argv[0]))
                        ? apexui_set_fullscreen(ctx, AS_BOOL(argv[0]))
                        : apexui_toggle_fullscreen(ctx);
        r = MAKE_BOOL(want);
    }
    else if (strcmp(name, "ui.dialog") == 0)       { apexui_dialog(ctx, argc >= 1 ? argv[0] : MAKE_NONE()); r = MAKE_NONE(); }
    else if (strcmp(name, "ui.notification") == 0) { apexui_notification(ctx, argc >= 1 ? argv[0] : MAKE_NONE()); r = MAKE_NONE(); }
    else if (strcmp(name, "ui.menubar") == 0)      { apexui_menubar(ctx, argc >= 1 ? argv[0] : MAKE_NONE()); r = MAKE_NONE(); }
    else if (strcmp(name, "ui.context_menu") == 0) {
        if (argc < 1 || !IS_TABLE(argv[0])) {
            ui_context_menu_close(c);
        } else if (argc >= 3 && IS_NUMBER(argv[1]) && IS_NUMBER(argv[2])) {
            ui_context_menu_set(c, argv[0], (int)AS_NUMBER(argv[1]), (int)AS_NUMBER(argv[2]));
        } else {
            ui_context_menu_set(c, argv[0], c->input.x, c->input.y);
        }
        r = MAKE_NONE();
    }
    else if (strcmp(name, "ui.statusbar") == 0)    { r = make_statusbar(argc >= 1 ? argv[0] : MAKE_NONE()); }

    else if (strcmp(name, "ui.text") == 0)         { r = make_leaf("text", argc >= 1 ? argv[0] : MAKE_NONE(), argc >= 2 ? argv[1] : MAKE_NONE()); }
    else if (strcmp(name, "ui.heading") == 0)      { r = make_leaf("heading", argc >= 1 ? argv[0] : MAKE_NONE(), argc >= 2 ? argv[1] : MAKE_NONE()); }
    else if (strcmp(name, "ui.separator") == 0)    { r = make_leaf("separator", MAKE_NONE(), argc >= 1 ? argv[0] : MAKE_NONE()); }
    else if (strcmp(name, "ui.button") == 0)       { r = ctor_button_argv(argc, argv); }
    else if (strcmp(name, "ui.checkbox") == 0)     { r = ctor_checkbox_argv(argc, argv); }
    else if (strcmp(name, "ui.input") == 0)        { r = ctor_input_argv(argc, argv); }
    else if (strcmp(name, "ui.number_input") == 0) { r = ctor_number_input_argv(argc, argv); }
    else if (strcmp(name, "ui.text_area") == 0)    { r = ctor_text_area_argv(argc, argv); }
    else if (strcmp(name, "ui.dropdown") == 0)     { r = ctor_dropdown_argv(argc, argv); }
    else if (strcmp(name, "ui.table") == 0)        { r = ctor_table_argv(argc, argv); }
    else if (strcmp(name, "ui.tree") == 0)         { r = ctor_tree_argv(argc, argv); }
    else if (strcmp(name, "ui.tabs") == 0)         { r = ctor_tabs_argv(argc, argv); }
    else if (strcmp(name, "ui.spacer") == 0)       { r = ctor_spacer_argv(argc, argv); }

    else if (strcmp(name, "ui.vbox") == 0)         { r = make_container("vbox", argc >= 1 ? argv[0] : MAKE_NONE(), argc >= 2 ? argv[1] : MAKE_NONE()); }
    else if (strcmp(name, "ui.hbox") == 0)         { r = make_container("hbox", argc >= 1 ? argv[0] : MAKE_NONE(), argc >= 2 ? argv[1] : MAKE_NONE()); }
    else if (strcmp(name, "ui.panel") == 0)        { r = make_container("panel", argc >= 1 ? argv[0] : MAKE_NONE(), argc >= 2 ? argv[1] : MAKE_NONE()); }
    else if (strcmp(name, "ui.vscroll") == 0)      { r = make_container("vscroll", argc >= 1 ? argv[0] : MAKE_NONE(), argc >= 2 ? argv[1] : MAKE_NONE()); }
    else if (strcmp(name, "ui.hscroll") == 0)      { r = make_container("hscroll", argc >= 1 ? argv[0] : MAKE_NONE(), argc >= 2 ? argv[1] : MAKE_NONE()); }

    else { return false; }               // not a ui builtin

    *result = (apexui_value)r;           // caller owns the reference
    return true;
}