// apex_ui.h
// Public C API of the Apex UI library
// https://github.com/is-nobody/apex-ui
// MIT license

#ifndef APEX_UI_H
#define APEX_UI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// library version
#define APEXUI_VERSION_MAJOR 26
#define APEXUI_VERSION_MINOR 11
#define APEXUI_VERSION       "26.11"

// nan-boxed value cell: none / bool / number / string / table
typedef uint64_t apexui_value;

// the always-available none value (compile-time constant)
#define APEXUI_NONE ((apexui_value)0x7FF8000000000000ULL)

// opaque ui context; one context owns at most one native window
typedef struct UiContext ApexUiContext;

// constructors; string constructors copy the bytes
apexui_value apexui_make_none(void);
apexui_value apexui_make_bool(bool v);
apexui_value apexui_make_number(double v);
apexui_value apexui_make_string(const char* s, int len);        // len bytes copied
apexui_value apexui_make_string_cstr(const char* s);            // null-terminated
apexui_value apexui_make_table(int hash_capacity);              // also used as array

// reference counting; every value you receive is one reference
void apexui_value_retain(apexui_value v);
void apexui_value_release(apexui_value v);

// type queries
bool apexui_is_none(apexui_value v);
bool apexui_is_bool(apexui_value v);
bool apexui_is_number(apexui_value v);
bool apexui_is_string(apexui_value v);
bool apexui_is_table(apexui_value v);

// accessors; call the matching type query first (undefined otherwise)
double      apexui_get_number(apexui_value v);
bool        apexui_get_bool(apexui_value v);
const char* apexui_get_string_ptr(apexui_value v);   // valid while v is alive
int         apexui_get_string_len(apexui_value v);
char*       apexui_string_dup_cstr(apexui_value v);  // malloc'd copy, free() it

// inserts or updates key -> value; table keeps its own reference to value
bool apexui_table_set(apexui_value t, apexui_value key, apexui_value value);

// looks a key up; on hit *out receives a new reference the caller must release
bool apexui_table_get(apexui_value t, apexui_value key, apexui_value* out);

// removes a key; no-op when absent
void apexui_table_remove(apexui_value t, apexui_value key);

// number of live entries (array part + hash part)
int  apexui_table_size(apexui_value t);

// snapshot of all keys; free() the returned array and release each key value
apexui_value* apexui_table_keys(apexui_value t, int* out_count);

// same as apexui_table_set/get but with a plain c string key
bool apexui_table_set_cstr(apexui_value t, const char* key, apexui_value value);
bool apexui_table_get_cstr(apexui_value t, const char* key, apexui_value* out);

// typed convenience setters (c string key)
bool apexui_table_set_number(apexui_value t, const char* key, double v);
bool apexui_table_set_bool(apexui_value t, const char* key, bool v);
bool apexui_table_set_string(apexui_value t, const char* key, const char* v);

// typed convenience getters (c string key); return false on miss/type mismatch
bool apexui_table_get_number(apexui_value t, const char* key, double* out);
bool apexui_table_get_bool(apexui_value t, const char* key, bool* out);
bool apexui_table_get_string(apexui_value t, const char* key,
                             const char** out_ptr, int* out_len);  // borrowed ptr

// array helpers (1-based integer keys, like the apex language)
bool apexui_array_push(apexui_value arr, apexui_value v);   // append, returns success
apexui_value apexui_array_get(apexui_value arr, int index); // 1-based; none if out of range
int  apexui_array_count(apexui_value arr);                  // length of the array part

// creates an empty ui context (no window yet); null on allocation failure
ApexUiContext* apexui_context_create(void);

// destroys the context, closing the window if still open
void apexui_context_destroy(ApexUiContext* ctx);

// opens the native window
apexui_value apexui_init(ApexUiContext* ctx, apexui_value opts);

// closes the native window; the context stays usable for a new apexui_init
void apexui_shutdown(ApexUiContext* ctx);

// stages a widget descriptor tree (or an array of trees) for the next repaint
void apexui_render(ApexUiContext* ctx, apexui_value tree);

// blocks until the next event
apexui_value apexui_next_event(ApexUiContext* ctx);

// host callback for apexui_run
typedef apexui_value (*apexui_run_fn)(void* user, apexui_value event);

// drives the message loop with batched repaints until the window closes
apexui_value apexui_run(ApexUiContext* ctx, apexui_run_fn fn, void* user);

// sets the native window title (and the custom titlebar text)
void apexui_set_title(ApexUiContext* ctx, const char* title);

// current client area size as a {w, h} table
apexui_value apexui_window_size(ApexUiContext* ctx);

// native "open file" / "open folder" pickers
apexui_value apexui_open_file(ApexUiContext* ctx, apexui_value opts);
apexui_value apexui_open_folder(ApexUiContext* ctx, apexui_value opts);

// native "save file" picker
apexui_value apexui_save_as(ApexUiContext* ctx, const char* content,
                            int content_len, apexui_value opts);

// fullscreen helpers; return the resulting fullscreen state
bool apexui_set_fullscreen(ApexUiContext* ctx, bool enable);
bool apexui_toggle_fullscreen(ApexUiContext* ctx);

// modal dialog. descr fields: key, title, message (strings)
void apexui_dialog(ApexUiContext* ctx, apexui_value descr);

// desktop notification popup
void apexui_notification(ApexUiContext* ctx, apexui_value descr);

// installs a window menu bar from a descriptor array
void apexui_menubar(ApexUiContext* ctx, apexui_value descr);

// context menu at the current cursor position / at explicit coordinates
void apexui_context_menu_open(ApexUiContext* ctx, apexui_value items);
void apexui_context_menu_open_at(ApexUiContext* ctx, apexui_value items, int x, int y);
void apexui_context_menu_close(ApexUiContext* ctx);

// table of named key / modifier constants
apexui_value apexui_key_constants(void);

// leaf widgets; content/label are strings
apexui_value apexui_text(const char* content, apexui_value opts);
apexui_value apexui_heading(const char* content, apexui_value opts);
apexui_value apexui_separator(apexui_value opts);
apexui_value apexui_button(const char* label, apexui_value opts);
apexui_value apexui_checkbox(const char* label, bool checked, apexui_value opts);
apexui_value apexui_input(const char* value_text, apexui_value opts);
apexui_value apexui_number_input(double value, apexui_value opts);
apexui_value apexui_text_area(const char* value_text, apexui_value opts);
apexui_value apexui_dropdown(const char* selected, apexui_value options, apexui_value opts);

// data widgets; columns / rows / nodes / tabs are descriptor tables or arrays
apexui_value apexui_table_widget(apexui_value columns, apexui_value rows, apexui_value opts);
apexui_value apexui_tree(apexui_value nodes, apexui_value opts);
apexui_value apexui_tabs(apexui_value tabs, apexui_value opts);

// spacers: fixed pixel gap or flexible weighted gap
apexui_value apexui_spacer_fixed(double pixels);
apexui_value apexui_spacer_flex(double weight);

// containers; children is an array of widget descriptors
apexui_value apexui_vbox(apexui_value children, apexui_value opts);
apexui_value apexui_hbox(apexui_value children, apexui_value opts);
apexui_value apexui_panel(apexui_value children, apexui_value opts);
apexui_value apexui_vscroll(apexui_value children, apexui_value opts);
apexui_value apexui_hscroll(apexui_value children, apexui_value opts);

// bottom status bar; opts may contain an items array of {text} descriptors
apexui_value apexui_statusbar(apexui_value opts);

// name-based dispatcher
bool apexui_call(ApexUiContext* ctx, const char* name,
                 int argc, const apexui_value* args, apexui_value* result);

// internal key codes used in key events
#define APEXUI_KEY_ESCAPE     0xFF1B
#define APEXUI_KEY_ENTER      0xFF0D
#define APEXUI_KEY_TAB        0xFF09
#define APEXUI_KEY_BACKSPACE  0xFF08
#define APEXUI_KEY_DELETE     0xFFFF
#define APEXUI_KEY_INSERT     0xFF63
#define APEXUI_KEY_HOME       0xFF50
#define APEXUI_KEY_END        0xFF57
#define APEXUI_KEY_PAGE_UP    0xFF55
#define APEXUI_KEY_PAGE_DOWN  0xFF56
#define APEXUI_KEY_LEFT       0xFF51
#define APEXUI_KEY_RIGHT      0xFF53
#define APEXUI_KEY_UP         0xFF52
#define APEXUI_KEY_DOWN       0xFF54
#define APEXUI_KEY_SPACE      0x20
#define APEXUI_KEY_F1         0xFFBE
#define APEXUI_KEY_F2         0xFFBF
#define APEXUI_KEY_F3         0xFFC0
#define APEXUI_KEY_F4         0xFFC1
#define APEXUI_KEY_F5         0xFFC2
#define APEXUI_KEY_F6         0xFFC3
#define APEXUI_KEY_F7         0xFFC4
#define APEXUI_KEY_F8         0xFFC5
#define APEXUI_KEY_F9         0xFFC6
#define APEXUI_KEY_F10        0xFFC7
#define APEXUI_KEY_F11        0xFFC8
#define APEXUI_KEY_F12        0xFFC9
#define APEXUI_MOD_SHIFT      1
#define APEXUI_MOD_CTRL       2
#define APEXUI_MOD_ALT        4

#ifdef __cplusplus
} // extern "C"
#endif

#endif // APEX_UI_H