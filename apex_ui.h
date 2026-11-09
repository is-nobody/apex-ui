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

// value-free convenience for apexui_init
bool apexui_open(ApexUiContext* ctx, const char* title, int width, int height);

// closes the native window; the context stays usable for a new apexui_init
void apexui_shutdown(ApexUiContext* ctx);

// value-level render
void apexui_render_value(ApexUiContext* ctx, apexui_value tree);

// blocks until the next event and returns it as a table
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

// name-based dispatcher over the value layer ("ui.button", "ui.init", ...)
bool apexui_call(ApexUiContext* ctx, const char* name,
                 int argc, const apexui_value* args, apexui_value* result);

// widget options for the node constructors
typedef struct {
    const char* id;         // stable widget identity, reported back by events
    const char* css_class;  // styling tag; stored on the descriptor for hosts
    double      width;      // explicit pixel width, 0 = auto
    double      height;     // explicit pixel height, 0 = auto
    bool        disabled;   // greyed out and input-ignoring
    bool        checked;    // initial checkbox state
} ApexuiOpts;

// opaque, context-owned widget handle (see the ownership model up top)
typedef struct ApexuiNode ApexuiNode;

// a push button with a text label
ApexuiNode* apexui_button(ApexUiContext* ctx, const char* label);
ApexuiNode* apexui_button_ex(ApexUiContext* ctx, const char* label, ApexuiOpts opts);

// a line of body text
ApexuiNode* apexui_label(ApexUiContext* ctx, const char* text);
ApexuiNode* apexui_label_ex(ApexUiContext* ctx, const char* text, ApexuiOpts opts);

// a line of heading text (larger font)
ApexuiNode* apexui_heading(ApexUiContext* ctx, const char* text);
ApexuiNode* apexui_heading_ex(ApexUiContext* ctx, const char* text, ApexuiOpts opts);

// a horizontal rule
ApexuiNode* apexui_separator(ApexUiContext* ctx);
ApexuiNode* apexui_separator_ex(ApexUiContext* ctx, ApexuiOpts opts);

// an empty flexible gap; opts.width / opts.height make it a fixed-size gap
ApexuiNode* apexui_spacer(ApexUiContext* ctx);
ApexuiNode* apexui_spacer_ex(ApexUiContext* ctx, ApexuiOpts opts);

// a checkbox with a text label; opts.checked sets the initial state
ApexuiNode* apexui_checkbox(ApexUiContext* ctx, const char* label);
ApexuiNode* apexui_checkbox_ex(ApexUiContext* ctx, const char* label, ApexuiOpts opts);

// a single-line text input; text is the initial content
ApexuiNode* apexui_input(ApexUiContext* ctx, const char* text);
ApexuiNode* apexui_input_ex(ApexUiContext* ctx, const char* text, ApexuiOpts opts);

// a dropdown ("select") showing `selected`; items is an array of n labels
ApexuiNode* apexui_dropdown(ApexUiContext* ctx, const char* selected,
                            const char* const* items, size_t n);
ApexuiNode* apexui_dropdown_ex(ApexUiContext* ctx, const char* selected,
                               const char* const* items, size_t n, ApexuiOpts opts);

// vertical stack of children
ApexuiNode* apexui_column(ApexUiContext* ctx, ApexuiNode* const* kids, size_t n);
ApexuiNode* apexui_column_ex(ApexUiContext* ctx, ApexuiNode* const* kids,
                             size_t n, ApexuiOpts opts);

// horizontal row of children
ApexuiNode* apexui_row(ApexUiContext* ctx, ApexuiNode* const* kids, size_t n);
ApexuiNode* apexui_row_ex(ApexUiContext* ctx, ApexuiNode* const* kids,
                          size_t n, ApexuiOpts opts);

// grid laying the children out row-major into `cols` equal-weight columns
ApexuiNode* apexui_grid(ApexUiContext* ctx, int cols, ApexuiNode* const* kids, size_t n);
ApexuiNode* apexui_grid_ex(ApexUiContext* ctx, int cols, ApexuiNode* const* kids,
                           size_t n, ApexuiOpts opts);

// atomically commits a new tree from the root nodes
void apexui_render(ApexUiContext* ctx, ApexuiNode* const* roots, size_t n);

// convenience for a single root
void apexui_render_one(ApexUiContext* ctx, ApexuiNode* root);

// releases the staged nodes and the committed tree without rendering
void apexui_clear(ApexUiContext* ctx);

// an event handed to an ApexuiHandler; only valid inside the callback
typedef struct ApexuiEvent ApexuiEvent;

// user callback; ev is owned by the framework and dies right after the call
typedef void (*ApexuiHandler)(const ApexuiEvent* ev, void* userdata);

// pumps the window once and hands every queued event to the handler in order
void apexui_poll_events(ApexUiContext* ctx, ApexuiHandler handler, void* userdata);

// event kind
const char* apexui_event_type(const ApexuiEvent* ev);

// identity (ApexuiOpts.id) of the widget that produced the event
const char* apexui_event_id(const ApexuiEvent* ev);

// numeric field lookup ("value", "x", "y", "key_code", "mods", "scroll_dy")
double apexui_event_num(const ApexuiEvent* ev, const char* key);

// string field lookup ("key", "value", "widget", "action", ...)
const char* apexui_event_str(const ApexuiEvent* ev, const char* key);

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