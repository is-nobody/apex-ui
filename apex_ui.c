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
    kset(t, "__t", MAKE_STRING(uival_string_create(type, (int)strlen(type))));
    if (!IS_NONE(content)) kset(t, "content", content);  // label / content
    if (IS_TABLE(opts)) kset(t, "opts", opts);           // keep options table too
    merge_opts(t, opts);                 // merge option fields into the descriptor
    return MAKE_TABLE(t);                // caller owns the descriptor
}

// builds a container widget descriptor of the given kind; borrows its inputs
static Value make_container(const char* type, Value children, Value opts) {
    Table* t = uival_table_create(8);
    if (!t) return MAKE_NONE();
    kset(t, "__t", MAKE_STRING(uival_string_create(type, (int)strlen(type))));
    if (IS_TABLE(children)) kset(t, "children", children);
    if (IS_TABLE(opts)) kset(t, "opts", opts);
    merge_opts(t, opts);
    return MAKE_TABLE(t);                // caller owns the descriptor
}

// builds a statusbar descriptor; borrows opts
static Value make_statusbar(Value opts) {
    Table* t = uival_table_create(8);
    if (!t) return MAKE_NONE();
    kset(t, "__t", MAKE_STRING(uival_string_create("statusbar", 9)));
    if (IS_TABLE(opts)) kset(t, "opts", opts);
    merge_opts(t, opts);
    return MAKE_TABLE(t);                // caller owns the descriptor
}

apexui_value apexui_make_none(void)   { return MAKE_NONE(); }

apexui_value apexui_make_bool(bool v) { return MAKE_BOOL(v); }

apexui_value apexui_make_number(double v) { return MAKE_NUMBER(v); }

apexui_value apexui_make_string(const char* s, int len) {
    if (!s) { len = 0; s = ""; }
    if (len < 0) len = 0;
    StringObject* str = uival_string_create(s, len);
    if (!str) return MAKE_NONE();
    return MAKE_STRING(str);             // caller owns the fresh object
}

apexui_value apexui_make_string_cstr(const char* s) {
    return apexui_make_string(s, s ? (int)strlen(s) : 0);
}

apexui_value apexui_make_table(int hash_capacity) {
    Table* t = uival_table_create(hash_capacity > 0 ? hash_capacity : 8);
    if (!t) return MAKE_NONE();
    return MAKE_TABLE(t);                // caller owns the fresh table
}

void apexui_value_retain(apexui_value v)  { uival_value_incref((Value)v); }
void apexui_value_release(apexui_value v) { uival_value_decref((Value)v); }

bool apexui_is_none(apexui_value v)   { return IS_NONE(v); }
bool apexui_is_bool(apexui_value v)   { return IS_BOOL(v); }
bool apexui_is_number(apexui_value v) { return IS_NUMBER(v); }
bool apexui_is_string(apexui_value v) { return IS_STRING(v); }
bool apexui_is_table(apexui_value v)  { return IS_TABLE(v); }

double apexui_get_number(apexui_value v) { return AS_NUMBER(v); }
bool   apexui_get_bool(apexui_value v)   { return AS_BOOL(v); }

const char* apexui_get_string_ptr(apexui_value v) {
    if (!IS_STRING(v)) return NULL;
    return AS_STRING(v)->chars;
}

int apexui_get_string_len(apexui_value v) {
    if (!IS_STRING(v)) return 0;
    return AS_STRING(v)->length;
}

char* apexui_string_dup_cstr(apexui_value v) {
    if (!IS_STRING(v)) return NULL;
    StringObject* s = AS_STRING(v);
    char* out = (char*)malloc((size_t)s->length + 1);
    if (!out) return NULL;
    memcpy(out, s->chars, (size_t)s->length);
    out[s->length] = '\0';
    return out;
}

bool apexui_table_set(apexui_value t, apexui_value key, apexui_value value) {
    if (!IS_TABLE(t)) return false;
    return uival_table_set(AS_TABLE(t), (Value)key, (Value)value);
}

bool apexui_table_get(apexui_value t, apexui_value key, apexui_value* out) {
    if (!IS_TABLE(t) || !out) return false;
    Value v;
    if (!uival_table_get(AS_TABLE(t), (Value)key, &v)) return false;
    *out = (apexui_value)v;              // table_get handed us a new reference
    return true;
}

void apexui_table_remove(apexui_value t, apexui_value key) {
    if (!IS_TABLE(t)) return;
    uival_table_remove(AS_TABLE(t), (Value)key);
}

int apexui_table_size(apexui_value t) {
    if (!IS_TABLE(t)) return 0;
    return uival_table_size(AS_TABLE(t));
}

apexui_value* apexui_table_keys(apexui_value t, int* out_count) {
    if (out_count) *out_count = 0;
    if (!IS_TABLE(t) || !out_count) return NULL;
    int n = 0;
    Value* keys = uival_table_keys(AS_TABLE(t), &n);   // snapshot holds key references
    if (!keys) return NULL;
    *out_count = n;
    return (apexui_value*)keys;
}

bool apexui_table_set_cstr(apexui_value t, const char* key, apexui_value value) {
    if (!IS_TABLE(t) || !key) return false;
    Value k = MAKE_STRING(uival_string_create(key, (int)strlen(key)));
    if (!IS_STRING(k)) return false;
    bool ok = uival_table_set(AS_TABLE(t), k, (Value)value);
    uival_value_decref(k);
    return ok;
}

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

bool apexui_table_set_number(apexui_value t, const char* key, double v) {
    return apexui_table_set_cstr(t, key, MAKE_NUMBER(v));
}

bool apexui_table_set_bool(apexui_value t, const char* key, bool v) {
    return apexui_table_set_cstr(t, key, MAKE_BOOL(v));
}

bool apexui_table_set_string(apexui_value t, const char* key, const char* v) {
    return apexui_table_set_cstr(t, key, str_or_none(v));
}

bool apexui_table_get_number(apexui_value t, const char* key, double* out) {
    apexui_value v;
    if (!out || !apexui_table_get_cstr(t, key, &v)) return false;
    if (!IS_NUMBER(v)) { uival_value_decref(v); return false; }
    *out = AS_NUMBER(v);
    uival_value_decref(v);
    return true;
}

bool apexui_table_get_bool(apexui_value t, const char* key, bool* out) {
    apexui_value v;
    if (!out || !apexui_table_get_cstr(t, key, &v)) return false;
    if (!IS_BOOL(v)) { uival_value_decref(v); return false; }
    *out = AS_BOOL(v);
    uival_value_decref(v);
    return true;
}

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

bool apexui_array_push(apexui_value arr, apexui_value v) {
    if (!IS_TABLE(arr)) return false;
    Table* t = AS_TABLE(arr);
    return uival_table_set_int(t, t->array_count, (Value)v);  // 0-based index == next slot
}

apexui_value apexui_array_get(apexui_value arr, int index) {
    if (!IS_TABLE(arr) || index < 1) return MAKE_NONE();
    Value v;
    if (!uival_table_get(AS_TABLE(arr), MAKE_NUMBER((double)index), &v)) return MAKE_NONE();
    return (apexui_value)v;              // caller owns the reference
}

int apexui_array_count(apexui_value arr) {
    if (!IS_TABLE(arr)) return 0;
    Table* t = AS_TABLE(arr);
    return t->array_part ? t->array_count : 0;
}

void apexui_context_destroy(ApexUiContext* ctx) {
    apexui_context_free((UiContext*)ctx);
}

apexui_value apexui_init(ApexUiContext* ctx, apexui_value opts) {
    Value argv[1];
    int argc = 0;
    if (IS_TABLE(opts)) { argv[0] = (Value)opts; argc = 1; }
    return (apexui_value)apexui_impl_init((UiContext*)ctx, argc, argv);
}

void apexui_shutdown(ApexUiContext* ctx) {
    apexui_impl_shutdown((UiContext*)ctx, 0, NULL);
}

void apexui_render(ApexUiContext* ctx, apexui_value tree) {
    Value argv[1];
    argv[0] = (Value)tree;               // impl retains heap-tagged trees
    apexui_impl_render((UiContext*)ctx, 1, argv);
}

apexui_value apexui_next_event(ApexUiContext* ctx) {
    return (apexui_value)apexui_impl_next_event((UiContext*)ctx, 0, NULL);
}

apexui_value apexui_run(ApexUiContext* ctx, apexui_run_fn fn, void* user) {
    ui_run_loop((UiContext*)ctx, fn, user);
    return MAKE_NONE();
}

void apexui_set_title(ApexUiContext* ctx, const char* title) {
    if (!title) return;
    Value v = MAKE_STRING(uival_string_create(title, (int)strlen(title)));
    if (!IS_STRING(v)) return;
    apexui_impl_set_title((UiContext*)ctx, 1, &v);
    uival_value_decref(v);
}

apexui_value apexui_window_size(ApexUiContext* ctx) {
    return (apexui_value)apexui_impl_window_size((UiContext*)ctx, 0, NULL);
}

apexui_value apexui_open_file(ApexUiContext* ctx, apexui_value opts) {
    Value argv[1];
    int argc = 0;
    if (IS_TABLE(opts)) { argv[0] = (Value)opts; argc = 1; }
    return (apexui_value)apexui_impl_open_file((UiContext*)ctx, argc, argv);
}

apexui_value apexui_open_folder(ApexUiContext* ctx, apexui_value opts) {
    Value argv[1];
    int argc = 0;
    if (IS_TABLE(opts)) { argv[0] = (Value)opts; argc = 1; }
    return (apexui_value)apexui_impl_open_folder((UiContext*)ctx, argc, argv);
}

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

bool apexui_set_fullscreen(ApexUiContext* ctx, bool enable) {
    Value argv[1];
    argv[0] = MAKE_BOOL(enable);
    Value out = apexui_impl_fullscreen((UiContext*)ctx, 1, argv);
    bool res = IS_BOOL(out) && AS_BOOL(out);
    return res;
}

bool apexui_toggle_fullscreen(ApexUiContext* ctx) {
    Value out = apexui_impl_fullscreen((UiContext*)ctx, 0, NULL);
    bool res = IS_BOOL(out) && AS_BOOL(out);
    return res;
}

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

void apexui_context_menu_open(ApexUiContext* ctx, apexui_value items) {
    UiContext* c = (UiContext*)ctx;
    if (!c) return;
    if (!IS_TABLE(items)) {
        ui_context_menu_close(c);
        return;
    }
    ui_context_menu_set(c, (Value)items, c->input.x, c->input.y);
}

void apexui_context_menu_open_at(ApexUiContext* ctx, apexui_value items, int x, int y) {
    UiContext* c = (UiContext*)ctx;
    if (!c) return;
    if (!IS_TABLE(items)) {
        ui_context_menu_close(c);
        return;
    }
    ui_context_menu_set(c, (Value)items, x, y);
}

void apexui_context_menu_close(ApexUiContext* ctx) {
    UiContext* c = (UiContext*)ctx;
    if (!c) return;
    ui_context_menu_close(c);
}

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

apexui_value apexui_text(const char* content, apexui_value opts) {
    Value c = str_or_none(content);
    Value v = make_leaf("text", c, (Value)opts);
    uival_value_decref(c);
    return (apexui_value)v;
}

apexui_value apexui_heading(const char* content, apexui_value opts) {
    Value c = str_or_none(content);
    Value v = make_leaf("heading", c, (Value)opts);
    uival_value_decref(c);
    return (apexui_value)v;
}

apexui_value apexui_separator(apexui_value opts) {
    return (apexui_value)make_leaf("separator", MAKE_NONE(), (Value)opts);
}

apexui_value apexui_button(const char* label, apexui_value opts) {
    Value c = str_or_none(label);
    Value v = make_leaf("button", c, (Value)opts);
    uival_value_decref(c);
    return (apexui_value)v;
}

apexui_value apexui_checkbox(const char* label, bool checked, apexui_value opts) {
    Value c = str_or_none(label);
    Value v = make_leaf("checkbox", c, (Value)opts);
    uival_value_decref(c);
    if (IS_TABLE(v)) kset(AS_TABLE(v), "checked", MAKE_BOOL(checked));
    return (apexui_value)v;
}

apexui_value apexui_input(const char* value_text, apexui_value opts) {
    Value c = str_or_none(value_text);
    Value v = make_leaf("input", c, (Value)opts);
    if (IS_TABLE(v) && !IS_NONE(c)) kset(AS_TABLE(v), "value_text", c);
    uival_value_decref(c);
    return (apexui_value)v;
}

apexui_value apexui_number_input(double value, apexui_value opts) {
    Value v = make_leaf("number_input", MAKE_NUMBER(value), (Value)opts);
    if (IS_TABLE(v)) kset(AS_TABLE(v), "value", MAKE_NUMBER(value));
    return (apexui_value)v;
}

apexui_value apexui_text_area(const char* value_text, apexui_value opts) {
    Value c = str_or_none(value_text);
    Value v = make_leaf("text_area", c, (Value)opts);
    if (IS_TABLE(v) && !IS_NONE(c)) kset(AS_TABLE(v), "value_text", c);
    uival_value_decref(c);
    return (apexui_value)v;
}

apexui_value apexui_dropdown(const char* selected, apexui_value options, apexui_value opts) {
    Value c = str_or_none(selected);
    Value v = make_leaf("dropdown", c, (Value)opts);
    if (IS_TABLE(v) && IS_TABLE(options)) kset(AS_TABLE(v), "options", options);
    uival_value_decref(c);
    return (apexui_value)v;
}

apexui_value apexui_table_widget(apexui_value columns, apexui_value rows, apexui_value opts) {
    Value v = make_leaf("table", MAKE_NONE(), (Value)opts);
    if (IS_TABLE(v)) {
        if (IS_TABLE(columns)) kset(AS_TABLE(v), "columns", columns);
        if (IS_TABLE(rows))    kset(AS_TABLE(v), "rows", rows);
    }
    return (apexui_value)v;
}

apexui_value apexui_tree(apexui_value nodes, apexui_value opts) {
    Value v = make_leaf("tree", MAKE_NONE(), (Value)opts);
    if (IS_TABLE(v) && IS_TABLE(nodes)) kset(AS_TABLE(v), "nodes", nodes);
    return (apexui_value)v;
}

apexui_value apexui_tabs(apexui_value tabs, apexui_value opts) {
    Value v = make_leaf("tabs", MAKE_NONE(), (Value)opts);
    if (IS_TABLE(v) && IS_TABLE(tabs)) kset(AS_TABLE(v), "tabs", tabs);
    return (apexui_value)v;
}

apexui_value apexui_spacer_fixed(double pixels) {
    Table* t = uival_table_create(4);
    if (!t) return MAKE_NONE();
    kset(t, "__t", MAKE_STRING(uival_string_create("spacer", 6)));
    kset(t, "min_width", MAKE_NUMBER(pixels));
    kset(t, "min_height", MAKE_NUMBER(pixels));
    return MAKE_TABLE(t);                // caller owns the descriptor
}

apexui_value apexui_spacer_flex(double weight) {
    Table* t = uival_table_create(4);
    if (!t) return MAKE_NONE();
    kset(t, "__t", MAKE_STRING(uival_string_create("spacer", 6)));
    kset(t, "weight", MAKE_NUMBER(weight));
    return MAKE_TABLE(t);                // caller owns the descriptor
}

apexui_value apexui_vbox(apexui_value children, apexui_value opts) {
    return (apexui_value)make_container("vbox", (Value)children, (Value)opts);
}

apexui_value apexui_hbox(apexui_value children, apexui_value opts) {
    return (apexui_value)make_container("hbox", (Value)children, (Value)opts);
}

apexui_value apexui_panel(apexui_value children, apexui_value opts) {
    return (apexui_value)make_container("panel", (Value)children, (Value)opts);
}

apexui_value apexui_vscroll(apexui_value children, apexui_value opts) {
    return (apexui_value)make_container("vscroll", (Value)children, (Value)opts);
}

apexui_value apexui_hscroll(apexui_value children, apexui_value opts) {
    return (apexui_value)make_container("hscroll", (Value)children, (Value)opts);
}

apexui_value apexui_statusbar(apexui_value opts) {
    return (apexui_value)make_statusbar((Value)opts);
}

static Value ctor_button_argv(int argc, Value* argv) {
    return make_leaf("button",
                     argc >= 1 ? argv[0] : MAKE_NONE(),
                     argc >= 2 ? argv[1] : MAKE_NONE());
}

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

static Value ctor_input_argv(int argc, Value* argv) {
    Value content = argc >= 1 ? argv[0] : MAKE_NONE();
    Value v = make_leaf("input", content, argc >= 2 ? argv[1] : MAKE_NONE());
    if (IS_TABLE(v) && !IS_NONE(content)) kset(AS_TABLE(v), "value_text", content);
    return v;
}

static Value ctor_text_area_argv(int argc, Value* argv) {
    Value content = argc >= 1 ? argv[0] : MAKE_NONE();
    Value v = make_leaf("text_area", content, argc >= 2 ? argv[1] : MAKE_NONE());
    if (IS_TABLE(v) && !IS_NONE(content)) kset(AS_TABLE(v), "value_text", content);
    return v;
}

static Value ctor_dropdown_argv(int argc, Value* argv) {
    Value v = make_leaf("dropdown",
                        argc >= 1 ? argv[0] : MAKE_NONE(),
                        argc >= 3 ? argv[2] : MAKE_NONE());
    if (IS_TABLE(v) && argc >= 2 && IS_TABLE(argv[1]))
        kset(AS_TABLE(v), "options", argv[1]);
    return v;
}

static Value ctor_table_argv(int argc, Value* argv) {
    Value v = make_leaf("table", MAKE_NONE(), argc >= 3 ? argv[2] : MAKE_NONE());
    if (IS_TABLE(v)) {
        if (argc >= 1 && IS_TABLE(argv[0])) kset(AS_TABLE(v), "columns", argv[0]);
        if (argc >= 2 && IS_TABLE(argv[1])) kset(AS_TABLE(v), "rows", argv[1]);
    }
    return v;
}

static Value ctor_tree_argv(int argc, Value* argv) {
    Value v = make_leaf("tree", MAKE_NONE(), argc >= 2 ? argv[1] : MAKE_NONE());
    if (IS_TABLE(v) && argc >= 1 && IS_TABLE(argv[0]))
        kset(AS_TABLE(v), "nodes", argv[0]);
    return v;
}

static Value ctor_tabs_argv(int argc, Value* argv) {
    Value v = make_leaf("tabs", MAKE_NONE(), argc >= 2 ? argv[1] : MAKE_NONE());
    if (IS_TABLE(v) && argc >= 1 && IS_TABLE(argv[0]))
        kset(AS_TABLE(v), "tabs", argv[0]);
    return v;
}

static Value ctor_number_input_argv(int argc, Value* argv) {
    Value v = make_leaf("number_input",
                        argc >= 1 ? argv[0] : MAKE_NONE(),
                        argc >= 2 ? argv[1] : MAKE_NONE());
    if (IS_TABLE(v) && argc >= 1) kset(AS_TABLE(v), "value", argv[0]);
    return v;
}

static Value ctor_spacer_argv(int argc, Value* argv) {
    Table* t = uival_table_create(4);
    if (!t) return MAKE_NONE();
    kset(t, "__t", MAKE_STRING(uival_string_create("spacer", 6)));
    if (argc >= 1 && IS_NUMBER(argv[0])) {          // numeric arg: fixed min size
        kset(t, "min_width", argv[0]);
        kset(t, "min_height", argv[0]);
    } else {                                        // default: flex spacer
        kset(t, "weight", MAKE_NUMBER(1.0));
    }
    if (argc >= 2 && IS_TABLE(argv[1])) {
        kset(t, "opts", argv[1]);
        merge_opts(t, argv[1]);
    }
    return MAKE_TABLE(t);                // caller owns the descriptor
}

bool apexui_call(ApexUiContext* ctx, const char* name,
                 int argc, const apexui_value* args, apexui_value* result) {
    UiContext* c = (UiContext*)ctx;
    Value* argv = (Value*)args;
    Value r = MAKE_NONE();

    if (!name || !result) return false;

    if (strcmp(name, "ui.key") == 0)               { r = (Value)apexui_key_constants(); }
    else if (strcmp(name, "ui.init") == 0)         { r = (Value)apexui_init(ctx, argc >= 1 ? argv[0] : MAKE_NONE()); }
    else if (strcmp(name, "ui.shutdown") == 0)     { apexui_shutdown(ctx); r = MAKE_NONE(); }
    else if (strcmp(name, "ui.render") == 0)       { apexui_render(ctx, argc >= 1 ? argv[0] : MAKE_NONE()); r = MAKE_NONE(); }
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
