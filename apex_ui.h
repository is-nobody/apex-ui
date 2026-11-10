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
typedef uint64_t apex_ui_value;

// the always-available none value (compile-time constant)
#define APEXUI_NONE ((apex_ui_value)0x7FF8000000000000ULL)

// opaque ui context; one context owns at most one native window
typedef struct UiContext ApexUIContext;

// constructors; string constructors copy the bytes
apex_ui_value apex_ui_make_none(void);
apex_ui_value apex_ui_make_bool(bool v);
apex_ui_value apex_ui_make_number(double v);
apex_ui_value apex_ui_make_string(const char* s, int len);        // len bytes copied
apex_ui_value apex_ui_make_string_cstr(const char* s);            // null-terminated
apex_ui_value apex_ui_make_table(int hash_capacity);              // also used as array

// reference counting; every value you receive is one reference
void apex_ui_value_retain(apex_ui_value v);
void apex_ui_value_release(apex_ui_value v);

// type queries
bool apex_ui_is_none(apex_ui_value v);
bool apex_ui_is_bool(apex_ui_value v);
bool apex_ui_is_number(apex_ui_value v);
bool apex_ui_is_string(apex_ui_value v);
bool apex_ui_is_table(apex_ui_value v);

// accessors; call the matching type query first (undefined otherwise)
double      apex_ui_get_number(apex_ui_value v);
bool        apex_ui_get_bool(apex_ui_value v);
const char* apex_ui_get_string_ptr(apex_ui_value v);   // valid while v is alive
int         apex_ui_get_string_len(apex_ui_value v);
char*       apex_ui_string_dup_cstr(apex_ui_value v);  // malloc'd copy, free() it

// inserts or updates key -> value; table keeps its own reference to value
bool apex_ui_table_set(apex_ui_value t, apex_ui_value key, apex_ui_value value);

// looks a key up; on hit *out receives a new reference the caller must release
bool apex_ui_table_get(apex_ui_value t, apex_ui_value key, apex_ui_value* out);

// removes a key; no-op when absent
void apex_ui_table_remove(apex_ui_value t, apex_ui_value key);

// number of live entries (array part + hash part)
int  apex_ui_table_size(apex_ui_value t);

// snapshot of all keys; free() the returned array and release each key value
apex_ui_value* apex_ui_table_keys(apex_ui_value t, int* out_count);

// same as apex_ui_table_set/get but with a plain c string key
bool apex_ui_table_set_cstr(apex_ui_value t, const char* key, apex_ui_value value);
bool apex_ui_table_get_cstr(apex_ui_value t, const char* key, apex_ui_value* out);

// typed convenience setters (c string key)
bool apex_ui_table_set_number(apex_ui_value t, const char* key, double v);
bool apex_ui_table_set_bool(apex_ui_value t, const char* key, bool v);
bool apex_ui_table_set_string(apex_ui_value t, const char* key, const char* v);

// typed convenience getters (c string key); return false on miss/type mismatch
bool apex_ui_table_get_number(apex_ui_value t, const char* key, double* out);
bool apex_ui_table_get_bool(apex_ui_value t, const char* key, bool* out);
bool apex_ui_table_get_string(apex_ui_value t, const char* key,
                             const char** out_ptr, int* out_len);  // borrowed ptr

// array helpers (1-based integer keys, like the apex language)
bool apex_ui_array_push(apex_ui_value arr, apex_ui_value v);   // append, returns success
apex_ui_value apex_ui_array_get(apex_ui_value arr, int index); // 1-based; none if out of range
int  apex_ui_array_count(apex_ui_value arr);                  // length of the array part

// creates an empty ui context (no window yet); null on allocation failure
ApexUIContext* apex_ui_context_create(void);

// destroys the context, closing the window if still open
void apex_ui_context_destroy(ApexUIContext* ctx);

// opens the native window
apex_ui_value apex_ui_init(ApexUIContext* ctx, apex_ui_value opts);

// value-free convenience for apex_ui_init
bool apex_ui_open(ApexUIContext* ctx, const char* title, int width, int height);

// closes the native window; the context stays usable for a new apex_ui_init
void apex_ui_shutdown(ApexUIContext* ctx);

// value-level render
void apex_ui_render_value(ApexUIContext* ctx, apex_ui_value tree);

// blocks until the next event and returns it as a table
apex_ui_value apex_ui_next_event(ApexUIContext* ctx);

// host callback for apex_ui_run
typedef apex_ui_value (*apex_ui_run_fn)(void* user, apex_ui_value event);

// drives the message loop with batched repaints until the window closes
apex_ui_value apex_ui_run(ApexUIContext* ctx, apex_ui_run_fn fn, void* user);

// sets the native window title (and the custom titlebar text)
void apex_ui_set_title(ApexUIContext* ctx, const char* title);

// current client area size as a {w, h} table
apex_ui_value apex_ui_window_size(ApexUIContext* ctx);

// native "open file" / "open folder" pickers
apex_ui_value apex_ui_open_file(ApexUIContext* ctx, apex_ui_value opts);
apex_ui_value apex_ui_open_folder(ApexUIContext* ctx, apex_ui_value opts);

// native "save file" picker
apex_ui_value apex_ui_save_as(ApexUIContext* ctx, const char* content,
                            int content_len, apex_ui_value opts);

// fullscreen helpers; return the resulting fullscreen state
bool apex_ui_set_fullscreen(ApexUIContext* ctx, bool enable);
bool apex_ui_toggle_fullscreen(ApexUIContext* ctx);

// modal dialog. descr fields: key, title, message (strings)
void apex_ui_dialog(ApexUIContext* ctx, apex_ui_value descr);

// desktop notification popup
void apex_ui_notification(ApexUIContext* ctx, apex_ui_value descr);

// installs a window menu bar from a descriptor array
void apex_ui_menubar(ApexUIContext* ctx, apex_ui_value descr);

// context menu at the current cursor position / at explicit coordinates
void apex_ui_context_menu_open(ApexUIContext* ctx, apex_ui_value items);
void apex_ui_context_menu_open_at(ApexUIContext* ctx, apex_ui_value items, int x, int y);
void apex_ui_context_menu_close(ApexUIContext* ctx);

// table of named key / modifier constants
apex_ui_value apex_ui_key_constants(void);

// name-based dispatcher over the value layer ("ui.button", "ui.init", ...)
bool apex_ui_call(ApexUIContext* ctx, const char* name,
                 int argc, const apex_ui_value* args, apex_ui_value* result);

// widget options for the node constructors
typedef struct {
    const char* id;         // stable widget identity, reported back by events
    const char* css_class;  // styling tag; stored on the descriptor for hosts
    double      width;      // explicit pixel width, 0 = auto
    double      height;     // explicit pixel height, 0 = auto
    bool        disabled;   // greyed out and input-ignoring
    bool        checked;    // initial checkbox state
} ApexUIOpts;

// opaque, context-owned widget handle (see the ownership model up top)
typedef struct ApexUINode ApexUINode;

// a push button with a text label
ApexUINode* apex_ui_button(ApexUIContext* ctx, const char* label);
ApexUINode* apex_ui_button_ex(ApexUIContext* ctx, const char* label, ApexUIOpts opts);

// a line of body text
ApexUINode* apex_ui_label(ApexUIContext* ctx, const char* text);
ApexUINode* apex_ui_label_ex(ApexUIContext* ctx, const char* text, ApexUIOpts opts);

// a line of heading text (larger font)
ApexUINode* apex_ui_heading(ApexUIContext* ctx, const char* text);
ApexUINode* apex_ui_heading_ex(ApexUIContext* ctx, const char* text, ApexUIOpts opts);

// a horizontal rule
ApexUINode* apex_ui_separator(ApexUIContext* ctx);
ApexUINode* apex_ui_separator_ex(ApexUIContext* ctx, ApexUIOpts opts);

// an empty flexible gap; opts.width / opts.height make it a fixed-size gap
ApexUINode* apex_ui_spacer(ApexUIContext* ctx);
ApexUINode* apex_ui_spacer_ex(ApexUIContext* ctx, ApexUIOpts opts);

// a checkbox with a text label; opts.checked sets the initial state
ApexUINode* apex_ui_checkbox(ApexUIContext* ctx, const char* label);
ApexUINode* apex_ui_checkbox_ex(ApexUIContext* ctx, const char* label, ApexUIOpts opts);

// a single-line text input; text is the initial content
ApexUINode* apex_ui_input(ApexUIContext* ctx, const char* text);
ApexUINode* apex_ui_input_ex(ApexUIContext* ctx, const char* text, ApexUIOpts opts);

// a dropdown ("select") showing `selected`; items is an array of n labels
ApexUINode* apex_ui_dropdown(ApexUIContext* ctx, const char* selected,
                            const char* const* items, size_t n);
ApexUINode* apex_ui_dropdown_ex(ApexUIContext* ctx, const char* selected,
                               const char* const* items, size_t n, ApexUIOpts opts);

// vertical stack of children
ApexUINode* apex_ui_column(ApexUIContext* ctx, ApexUINode* const* kids, size_t n);
ApexUINode* apex_ui_column_ex(ApexUIContext* ctx, ApexUINode* const* kids,
                             size_t n, ApexUIOpts opts);

// horizontal row of children
ApexUINode* apex_ui_row(ApexUIContext* ctx, ApexUINode* const* kids, size_t n);
ApexUINode* apex_ui_row_ex(ApexUIContext* ctx, ApexUINode* const* kids,
                          size_t n, ApexUIOpts opts);

// grid laying the children out row-major into `cols` equal-weight columns
ApexUINode* apex_ui_grid(ApexUIContext* ctx, int cols, ApexUINode* const* kids, size_t n);
ApexUINode* apex_ui_grid_ex(ApexUIContext* ctx, int cols, ApexUINode* const* kids,
                           size_t n, ApexUIOpts opts);

// atomically commits a new tree from the root nodes
void apex_ui_render(ApexUIContext* ctx, ApexUINode* const* roots, size_t n);

// convenience for a single root
void apex_ui_render_one(ApexUIContext* ctx, ApexUINode* root);

// releases the staged nodes and the committed tree without rendering
void apex_ui_clear(ApexUIContext* ctx);

// an event handed to an ApexUIHandler; only valid inside the callback
typedef struct ApexUIEvent ApexUIEvent;

// user callback; ev is owned by the framework and dies right after the call
typedef void (*ApexUIHandler)(const ApexUIEvent* ev, void* userdata);

// pumps the window once and hands every queued event to the handler in order
void apex_ui_poll_events(ApexUIContext* ctx, ApexUIHandler handler, void* userdata);

// event kind
const char* apex_ui_event_type(const ApexUIEvent* ev);

// identity (ApexUIOpts.id) of the widget that produced the event
const char* apex_ui_event_id(const ApexUIEvent* ev);

// numeric field lookup ("value", "x", "y", "key_code", "mods", "scroll_dy")
double apex_ui_event_num(const ApexUIEvent* ev, const char* key);

// string field lookup ("key", "value", "widget", "action", ...)
const char* apex_ui_event_str(const ApexUIEvent* ev, const char* key);

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