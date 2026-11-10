// source/core/ui_core.h
// Core context / state / build / runtime declarations for the Apex UI library
// https://github.com/is-nobody/apex-ui
// MIT license

#ifndef UI_CORE_H
#define UI_CORE_H

#include "ui_internal.h"

// context lifecycle
UiContext* apex_ui_context_create(void);
void       apex_ui_context_free(UiContext* ctx);

// widget pool + tree construction helpers
Widget* ui_widget_alloc(UiContext* ctx);
void    ui_widgets_reset(UiContext* ctx);
void    widget_add_child(UiContext* ctx, Widget* p, Widget* c);
bool    widget_focusable(WidgetKind k);
void    collect_focusable(Widget* w, Widget** arr, int* n, int cap);
const char* widget_kind_name(WidgetKind k);

// descriptor -> tree
Widget* ui_build_tree(UiContext* ctx, Value descr, Widget* parent, int idx);

// text measurement memo
float ui_text_measure_cached(UiContext* ctx, const char* s, int len, float size);

// descriptor hash of a whole tree
uint64_t hash_descr_recursive(UiContext* ctx, Value descr);

// state accessor + gc
WidgetState* ui_state_get(UiContext* ctx, uint64_t id);
void         state_gc_mark_root(UiContext* ctx, Widget* root);
void         state_gc_sweep(UiContext* ctx);

// event queue + table conversion
void  ui_event_push(UiContext* ctx, const UiEvent* ev);
Value ui_event_to_table(UiContext* ctx, const UiEvent* ev);
void  apex_ui_fire_click(UiContext* ctx, Widget* w);
void  apex_ui_fire_click_b(UiContext* ctx, Widget* w, int button);

// animation
void ui_tick(UiContext* ctx, double dt);
bool ui_animating(UiContext* ctx);

// soft keyboard sync
void ui_ime_sync(UiContext* ctx);

// message loop
bool apex_ui_pump(UiContext* ctx);

// builtin entry points (used by the public api in apex_ui.c)
Value apex_ui_impl_init(UiContext* ctx, int argc, Value* argv);
Value apex_ui_impl_render(UiContext* ctx, int argc, Value* argv);
Value apex_ui_impl_next_event(UiContext* ctx, int argc, Value* argv);
Value apex_ui_impl_shutdown(UiContext* ctx, int argc, Value* argv);
Value apex_ui_impl_set_title(UiContext* ctx, int argc, Value* argv);
Value apex_ui_impl_window_size(UiContext* ctx, int argc, Value* argv);
Value apex_ui_impl_open_file(UiContext* ctx, int argc, Value* argv);
Value apex_ui_impl_open_folder(UiContext* ctx, int argc, Value* argv);
Value apex_ui_impl_save_as(UiContext* ctx, int argc, Value* argv);
Value apex_ui_impl_fullscreen(UiContext* ctx, int argc, Value* argv);

// message loop driven by a host callback; the callback receives each event
// table and returns a widget tree table (or none) which takes over rendering
void ui_run_loop(UiContext* ctx, apex_ui_run_fn fn, void* user);

// host paint hook
void ui_window_paint_cb(void* user, int w, int h);

// rebuild/redraw helpers
void rebuild_and_draw(UiContext* ctx);
void redraw_only(UiContext* ctx);

#endif