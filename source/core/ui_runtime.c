// source/core/ui_runtime.c
// Message loop and builtin entry points for the Apex UI library
// https://github.com/is-nobody/apex-ui
// MIT license

#include "ui_core.h"
#include "ui_hash.h"
#include "ui_table.h"
#include "ui_widgets.h"
#include "ui_interaction.h"
#include "ui_render.h"
#include "ui_window.h"
#include "ui_text.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// rebuilds the widget tree from the last descriptor and paints it
void rebuild_and_draw(UiContext* ctx) {
    if (!ctx->window) {                                           // window is gone: nothing to draw into
        ctx->redraw_needed = false;
        ctx->full_repaint  = false;
        return;
    }

    // decide whether we can reuse the tree from the previous frame
    bool can_reuse = false;
    uint64_t new_hash = 0;

    if (ctx->has_tree && ctx->descr_hash_valid && ctx->root)
    {
        if (ctx->last_tree == ctx->hashed_tree) {
            new_hash = ctx->last_descr_hash;                      // same Value, cached hash
        } else {
            new_hash = hash_descr_recursive(ctx, ctx->last_tree);
        }
        if (new_hash == ctx->last_descr_hash) can_reuse = true;
    }

    if (!can_reuse) {
        ui_widgets_reset(ctx);                                    // drop the previous tree
        if (ctx->has_tree) {
            if (IS_TABLE(ctx->last_tree)) {
                Table* t = AS_TABLE(ctx->last_tree);
                Value first;
                if (uival_table_get(t, MAKE_NUMBER(1.0), &first)) {     // array-style: multiple root children
                    uival_value_decref(first);
                    Widget* root = ui_widget_alloc(ctx);          // synthesize a vbox root
                    root->kind = WK_VBOX;
                    root->id = ui_hash_id(0, "__root__", 0);
                    root->padding = UI_PAD_SCREEN;
                    root->gap = UI_GAP;
                    for (int i = 1; ; i++) {                      // walk 1-based array children
                        Value vv;
                        if (!uival_table_get(t, MAKE_NUMBER((double)i), &vv)) break;
                        Widget* ch = ui_build_tree(ctx, vv, root, i - 1);
                        if (ch) widget_add_child(ctx, root, ch);
                        uival_value_decref(vv);
                    }
                    ctx->root = root;
                } else {                                          // single descriptor root
                    ctx->root = ui_build_tree(ctx, ctx->last_tree, NULL, 0);
                }
            } else {
                ctx->root = ui_build_tree(ctx, ctx->last_tree, NULL, 0);  // non-table descriptor
            }
        }
        if (!ctx->root) {                                         // no tree: synthesize an empty root
            Widget* root = ui_widget_alloc(ctx);
            root->kind = WK_VBOX;
            root->id = ui_hash_id(0, "__root__", 0);
            root->padding = UI_PAD_SCREEN;
            ctx->root = root;
        }
        // remember what we just built so the next frame can compare
        ctx->last_descr_hash = new_hash;
        ctx->hashed_tree     = ctx->last_tree;
        ctx->descr_hash_valid = true;
    }

    int top       = ctx->custom_chrome ? UI_TITLEBAR_H : 0;       // titlebar reserved at the top
    int mb_h      = ui_menubar_height(ctx);                       // menu bar reserved below it
    int content_t = top + mb_h;                                   // top edge of the widget tree

    int pad = 0;
#ifdef __ANDROID__
    pad = UI_PAD_SCREEN;                                          // physical screen margins
#endif

    ctx->root->x = (float)pad;
    ctx->root->y = (float)(content_t + pad);
    ctx->root->w = (float)(ctx->win_w - pad * 2);
    ctx->root->h = (float)(ctx->win_h - content_t - pad * 2);
    if (ctx->geometry_dirty) {                                    // skip when nothing moved
        ui_layout(ctx);                                           // compute widget geometry
        ctx->geometry_dirty = false;
    }

    // snapshot the freshly laid-out tree, diff against the previous frame to compute damage
    ui_snapshot_capture(ctx, ctx->root);
    ui_damage_begin(ctx);

    // the modal dialog is painted on top of everything
    bool dlg_animating =
        ctx->dialog.active &&
        ((ctx->dialog.closing && ctx->dialog.anim_t > 0.0) ||
         (!ctx->dialog.closing && ctx->dialog.anim_t < 1.0));

    if (ctx->full_repaint || dlg_animating) {
        ui_damage_add(ctx, 0, 0, ctx->win_w, ctx->win_h);
        ctx->full_repaint = false;
    } else {
        ui_damage_diff(ctx);
        if (ctx->custom_chrome)
            ui_damage_add(ctx, 0, 0, ctx->win_w, UI_TITLEBAR_H);
    }
    ui_damage_finalize(ctx);

    if (ctx->damage_count > 0) {
        long total = 0, area = (long)ctx->win_w * (long)ctx->win_h;
        for (int i = 0; i < ctx->damage_count; i++)
            total += (long)ctx->damage[i].w * (long)ctx->damage[i].h;

        ui_render_frame_damage(ctx, ctx->damage, ctx->damage_count);
        if (ctx->dialog.active || total * 2 >= area) {
            // a modal dialog dims the whole window and paints on top of everything
            ui_window_present(ctx->window);                       // whole-window blit
        } else {
            ui_window_present_rects(ctx->window,                  // small damage: per-rect blits
                                    (const int*)ctx->damage,
                                    ctx->damage_count);
        }
    }
    if (ctx->window) ui_window_sync_ack(ctx->window);             // ack the pending resize
    ui_damage_swap(ctx);
    ctx->redraw_needed = false;                                   // repaint satisfied

    // state gc is only needed when the tree was actually rebuilt
    if (!can_reuse) {
        state_gc_mark_root(ctx, ctx->root);                       // mark reachable state
        state_gc_sweep(ctx);                                      // drop the rest

        // clear interaction ids that no longer match a live widget after the rebuild
        if (ctx->focused_id) {
            bool found = false;
            for (int i = 0; i < ctx->pool_used && !found; i++)
                if (ctx->pool[i].id == ctx->focused_id) found = true;
            if (!found) ctx->focused_id = 0;
        }
        if (ctx->hovered_id) {
            bool found = false;
            for (int i = 0; i < ctx->pool_used && !found; i++)
                if (ctx->pool[i].id == ctx->hovered_id) found = true;
            if (!found) ctx->hovered_id = 0;
        }
        if (ctx->drag_widget_id) {
            bool found = false;
            for (int i = 0; i < ctx->pool_used && !found; i++)
                if (ctx->pool[i].id == ctx->drag_widget_id) found = true;
            if (!found) ctx->drag_widget_id = 0;
        }
    }
}

// repaints the existing widget tree without rebuilding it
void redraw_only(UiContext* ctx) {
    if (!ctx->root) return;                                       // nothing to draw
    if (!ctx->window) {                                           // window is gone: nothing to blit
        ctx->redraw_needed = false;
        ctx->full_repaint  = false;
        return;
    }

    int top       = ctx->custom_chrome ? UI_TITLEBAR_H : 0;       // titlebar reserved at the top
    int mb_h      = ui_menubar_height(ctx);                       // menu bar reserved below it
    int content_t = top + mb_h;                                   // top edge of the widget tree

    int pad = 0;
#ifdef __ANDROID__
    pad = UI_PAD_SCREEN;
#endif

    ctx->root->x = (float)pad;
    ctx->root->y = (float)(content_t + pad);
    ctx->root->w = (float)(ctx->win_w - pad * 2);
    ctx->root->h = (float)(ctx->win_h - content_t - pad * 2);

    if (ctx->geometry_dirty) {                                    // skip when nothing moved
        ui_layout(ctx);                                           // recompute geometry
        ctx->geometry_dirty = false;
    }
    ui_render_frame(ctx);                                         // redraw into the pixel buffer
    ui_window_present(ctx->window);                               // blit to the screen
    if (ctx->window) ui_window_sync_ack(ctx->window);             // ack the pending resize
    ctx->redraw_needed = false;                                   // repaint satisfied
    ctx->full_repaint  = false;                                   // full repaint satisfied
}

// host window paint hook: refreshes buffers and repaints the existing tree
void ui_window_paint_cb(void* user, int w, int h) {
    UiContext* ctx = (UiContext*)user;
    if (!ctx || !ctx->window) return;                             // nothing to paint

    ctx->pixels = ui_window_pixels(ctx->window);                  // refresh pixel buffer
    if (w != ctx->win_w || h != ctx->win_h) {                     // client rect changed under us
        ctx->win_w = w;
        ctx->win_h = h;
        ctx->geometry_dirty = true;
    }

    redraw_only(ctx);                                             // repaint the current tree
}

// pumps the window event loop once, returning true if events were queued
bool apexui_pump(UiContext* ctx) {
    if (!ctx->window) return false;                               // no window: nothing to pump

    // advance animated state first, before any early return
    static double s_last_tick = 0.0;                              // previous wall-clock reading
    double now = apexui_now_seconds();
    double dt = now - s_last_tick;
    s_last_tick = now;
    if (dt < 0.0) dt = 0.0;                                       // clock went backwards
    if (dt > 0.25) dt = 0.25;                                     // clamp after a long pause
    ui_tick(ctx, dt);

    UiWinEvent we;
    while (ui_window_poll(ctx->window, &we)) {                    // drain pending events
        handle_win_event(ctx, &we);
        if (we.kind == UW_EV_CLOSE) break;                        // stop on close
    }
    ui_notification_pump(ctx);                                    // pump popup events too
    if (ctx->queue.count > 0) return true;                        // events already queued
    if (!ui_window_is_open(ctx->window)) return false;            // window closed

    int timeout;
    if (ctx->redraw_needed)        timeout = 8;                   // ~120 fps: repaint pending
    else if (ui_animating(ctx))    timeout = 8;                   // ~120 fps: scroll or notif ease
    else if (ctx->focused_id)      timeout = 62;                  // ~16 hz: only caret blink
    else if (ctx->dialog.active)   timeout = 62;                  // dialog animation / blink
    else if (ctx->notif_count > 0) timeout = 62;                  // idle notifications: poll close btn
    else                           timeout = 1000;                // fully idle
    if (ui_window_wait(ctx->window, timeout)) {                   // block briefly for input
        while (ui_window_poll(ctx->window, &we)) {                // drain newly arrived events
            handle_win_event(ctx, &we);
            if (we.kind == UW_EV_CLOSE) break;
        }
    }

    // deliver a single ev_resize after the user has stopped dragging
    #define RESIZE_DEBOUNCE_SEC 0.08
    if (ctx->pending_resize &&
        apexui_now_seconds() - ctx->pending_resize_at > RESIZE_DEBOUNCE_SEC) {
        UiEvent ev; memset(&ev, 0, sizeof(ev));
        ev.kind      = EV_RESIZE;
        ev.num_value = (double)ctx->pending_resize_w;
        ev.scroll_dy = (double)ctx->pending_resize_h;
        ui_event_push(ctx, &ev);
        ctx->pending_resize = false;
    }

    ui_ime_sync(ctx);                                             // track soft keyboard focus

    return ctx->queue.count > 0;                                  // true if anything was queued
}

// shows the platform soft keyboard and keeps it in sync with the focused editor
void ui_ime_sync(UiContext* ctx) {
    static uint64_t s_last_focus = 0;                             // focus id we synced for
    static bool     s_have_focus = false;                         // false before the first sync
    bool focus_changed = (!s_have_focus || ctx->focused_id != s_last_focus);
    s_last_focus = ctx->focused_id;
    s_have_focus = true;

    Widget* w = NULL;                                             // focused widget, if any
    for (int i = 0; i < ctx->pool_used; i++) {
        if (ctx->pool[i].id == ctx->focused_id) { w = &ctx->pool[i]; break; }
    }
    bool is_text = w && (w->kind == WK_INPUT || w->kind == WK_NUMBER_INPUT ||
                         w->kind == WK_TEXT_AREA);

    (void)focus_changed;
    ui_window_ime_show(ctx->window, is_text);

    if (!is_text) return;                                         // nothing to sync
    WidgetState* s = ui_state_get(ctx, w->id);
    int cur = s->cursor     < 0 ? w->input_value.len : s->cursor;
    int anc = s->sel_anchor < 0 ? cur                 : s->sel_anchor;
    if (cur > w->input_value.len) cur = w->input_value.len;
    if (anc > w->input_value.len) anc = w->input_value.len;
    ui_window_ime_sync(ctx->window, w->input_value.ptr, w->input_value.len,
                       cur, anc, w->kind == WK_TEXT_AREA);
}

// builtin: creates the native window if not already open
Value apexui_impl_init(UiContext* ctx, int argc, Value* argv) {
    if (ctx->window) return MAKE_BOOL(true);                      // already initialised
    int w = 640, h = 480;                                         // default window size
    int min_w = UI_MIN_W, min_h = UI_MIN_H;                       // default minimum size
    char* title = NULL;
    bool chrome = true;                                           // custom titlebar on by default
    if (argc >= 1 && IS_TABLE(argv[0])) {                         // optional options table
        Table* t = AS_TABLE(argv[0]);
        w = (int)ui_table_get_num(ctx, t, "width", 640);              // width override
        h = (int)ui_table_get_num(ctx, t, "height", 480);             // height override
        title  = ui_table_get_str_dup(ctx, t, "title");               // title override
        chrome = ui_table_get_bool(ctx, t, "titlebar", true);         // default: on
        min_w = (int)ui_table_get_num(ctx, t, "min_width",  UI_MIN_W);  // minimum width
        min_h = (int)ui_table_get_num(ctx, t, "min_height", UI_MIN_H);  // minimum height
    }
    const char* use_title = title ? title : "Apex UI";            // fall back to default title
    #ifdef __ANDROID__
        chrome = false;
    #endif
    ctx->custom_chrome = chrome;                                  // remember for the render pass
    ctx->tb_hot = ctx->tb_active = 0;                             // clear button state
    strncpy(ctx->title, use_title, sizeof(ctx->title) - 1);       // mirror the title
    ctx->title[sizeof(ctx->title) - 1] = 0;
    ctx->window = ui_window_create(w, h, use_title, !chrome);     // create the host window
    free(title);                                                  // release the temp title
    if (!ctx->window) return MAKE_BOOL(false);                    // creation failed
    ctx->win_w = w;                                               // cache window size
    ctx->win_h = h;
    ctx->pixels = ui_window_pixels(ctx->window);                  // cache pixel buffer

    ctx->redraw_needed = true;                                    // force first paint
    ctx->full_repaint  = true;                                    // first frame is undefined
    ui_window_set_min_size(ctx->window, min_w, min_h);            // enforce a floor on resize
    ui_window_set_paint_callback(ctx->window, ui_window_paint_cb, ctx);  // install paint hook

    return MAKE_BOOL(true);                                       // success
}

// builtin: records the current widget tree descriptor for the next repaint
Value apexui_impl_render(UiContext* ctx, int argc, Value* argv) {
    if (ctx->has_tree && (ctx->last_tree & QNAN) == QNAN)
        uival_value_decref(ctx->last_tree);                             // release previous tree reference
    if (argc >= 1 && (argv[0] & QNAN) == QNAN) {                  // new tree is heap-tagged
        uival_value_incref(argv[0]);                                    // take a reference
        ctx->last_tree = argv[0];
        ctx->has_tree = true;
    } else {
        ctx->last_tree = MAKE_NONE();                             // no tree or non-heap value
        ctx->has_tree = false;
    }
    ctx->redraw_needed = true;                                    // schedule a repaint
    return MAKE_NONE();
}

// builtin: blocks until an event is available, returns it as a table
Value apexui_impl_next_event(UiContext* ctx, int argc, Value* argv) {
    (void)argc; (void)argv;                                       // no arguments
    if (!ctx->window) return MAKE_NONE();                         // no window: nothing to wait on

    int safety = 0;                                               // guard against a runaway loop
    while (ctx->queue.count == 0) {                               // wait for an event
        if (ctx->quit_requested || !ui_window_is_open(ctx->window)) {
            UiEvent ev; memset(&ev, 0, sizeof(ev));               // synthesize a close event
            ev.kind = EV_CLOSE;
            ui_event_push(ctx, &ev);
            break;
        }
        if (ctx->redraw_needed) rebuild_and_draw(ctx);            // repaint if requested
        apexui_pump(ctx);                                                // drain native events
        if (ctx->queue.count > 0) break;                          // event arrived
        if (++safety > 5000) break;                               // give up after too many empty spins
    }
    if (ctx->queue.count == 0) return MAKE_NONE();                // still nothing: return none

    UiEvent ev = ctx->queue.events[0];                            // peek the front event
    memmove(&ctx->queue.events[0], &ctx->queue.events[1],
            sizeof(UiEvent) * (ctx->queue.count - 1));            // shift the queue down
    ctx->queue.count--;                                           // one fewer queued event

    Value t = ui_event_to_table(ctx, &ev);                        // convert to a value table
    free(ev.str_value);                                           // release the event's owned string
    return t;                                                     // return the event table
}

// drive the message loop with batched repaints, delegating events to a host callback
void ui_run_loop(UiContext* ctx, apexui_run_fn fn, void* user) {
    if (!ctx->window) return;                         // no window: nothing to run
    if (!fn) return;                                  // callback required

    if (ctx->redraw_needed) rebuild_and_draw(ctx);    // honour a pre-render tree

    while (!ctx->quit_requested && ui_window_is_open(ctx->window)) {
        apexui_pump(ctx);                                    // block, drain native events

        bool rebuilt = false;                         // did any callback return a tree?

        while (ctx->queue.count > 0) {                // process the whole batch
            UiEvent ev = ctx->queue.events[0];        // peek front
            memmove(&ctx->queue.events[0], &ctx->queue.events[1],
                    sizeof(UiEvent) * (ctx->queue.count - 1));  // shift down
            ctx->queue.count--;                       // one fewer queued event

            Value ev_tbl = ui_event_to_table(ctx, &ev);  // convert to value table
            free(ev.str_value);                       // release the event's owned string

            // the callback owns the returned tree table (or none); the event
            // table stays owned by this loop and is released right after
            Value result = (Value)fn(user, (apexui_value)ev_tbl);
            uival_value_decref(ev_tbl);                     // release our local reference

            if (IS_TABLE(result)) {                   // callback staged a new tree
                if (ctx->has_tree && (ctx->last_tree & QNAN) == QNAN)
                    uival_value_decref(ctx->last_tree);     // release previous tree
                ctx->last_tree = result;              // take ownership of the new one
                ctx->has_tree  = true;
                ctx->redraw_needed = true;
                rebuilt = true;
            } else {
                uival_value_decref(result);                 // none or non-table: drop it
            }

            if (ctx->quit_requested) break;           // callback shut down: stop the batch
        }

        if (ctx->quit_requested) break;               // bail before any render pass

        if (rebuilt || ctx->redraw_needed) {          // one rebuild per batch
            // during a resize burst, an rbd that is not backed by a queue event
            bool recently_resized = (apexui_now_seconds() - ctx->last_resize_at) < 0.050;
            bool skip = (!rebuilt &&
                         recently_resized &&
                         ctx->last_rbd_w == ctx->win_w &&
                         ctx->last_rbd_h == ctx->win_h);
            if (!skip) {
                ctx->last_rbd_w = ctx->win_w;
                ctx->last_rbd_h = ctx->win_h;
                rebuild_and_draw(ctx);
            }
        }
    }
}

// builtin: destroys the native window and marks the context as shut down
Value apexui_impl_shutdown(UiContext* ctx, int argc, Value* argv) {
    (void)argc; (void)argv;                                       // no arguments
    if (ctx->context_menu.active) ui_context_menu_close(ctx);     // drop any open menu first
    if (ctx->window) {                                            // tear down the window if present
        ui_window_destroy(ctx->window);                           // releases the framebuffer too
        ctx->window = NULL;                                       // drop the handle
        ctx->pixels = NULL;                                       // buffer is freed with the window
        ctx->redraw_needed = false;                               // no render pass should run now
        ctx->full_repaint  = false;
        ctx->geometry_dirty = false;
    }
    ctx->quit_requested = true;                                   // mark the context as quit
    return MAKE_NONE();
}

// builtin: sets the native window title
Value apexui_impl_set_title(UiContext* ctx, int argc, Value* argv) {
    if (!ctx->window) return MAKE_NONE();                         // no window: no-op
    if (argc >= 1 && IS_STRING(argv[0])) {                        // only string titles are applied
        const char* s = AS_STRING(argv[0])->chars;
        ui_window_set_title(ctx->window, s);
        strncpy(ctx->title, s, sizeof(ctx->title) - 1);           // mirror for the custom titlebar
        ctx->title[sizeof(ctx->title) - 1] = 0;
        ctx->redraw_needed = true;                                // schedule a repaint
    }
    return MAKE_NONE();
}

// builtin: returns the current window size as a {w, h} table
Value apexui_impl_window_size(UiContext* ctx, int argc, Value* argv) {
    (void)argc; (void)argv;                                       // no arguments
    Table* t = uival_table_create(4);                                   // small result table
    Value k, v;
    k = ui_intern(ctx, "w"); v = MAKE_NUMBER(ctx->win_w);          // width field
    uival_table_set(t, k, v); uival_value_decref(k);
    k = ui_intern(ctx, "h"); v = MAKE_NUMBER(ctx->win_h);          // height field
    uival_table_set(t, k, v); uival_value_decref(k);
    return MAKE_TABLE(t);                                         // hand the table to the caller
}

// parses the optional options table shared by the two pickers
static void parse_picker_opts(UiContext* ctx, int argc, Value* argv,
                              char** out_title, char** out_default,
                              UiFileFilter** out_filters, int* out_filter_count) {
    *out_title = NULL;
    *out_default = NULL;
    *out_filters = NULL;
    *out_filter_count = 0;
    if (argc < 1 || !IS_TABLE(argv[0])) return;

    Table* t = AS_TABLE(argv[0]);
    *out_title   = ui_table_get_str_dup(ctx, t, "title");
    *out_default = ui_table_get_str_dup(ctx, t, "default");

    Value fv;
    Value k_filters = ui_intern(ctx, "filters");
    if (!uival_table_get(t, k_filters, &fv)) { uival_value_decref(k_filters); return; }
    uival_value_decref(k_filters);

    if (IS_TABLE(fv)) {
        Table* ft = AS_TABLE(fv);
        int n = ft->array_part ? ft->array_count : 0;
        UiFileFilter* arr = (UiFileFilter*)calloc((size_t)n + 1, sizeof(UiFileFilter));
        if (arr) {
            for (int i = 0; i < n; i++) {
                Value item = ft->array_part[i];
                if (!IS_TABLE(item)) continue;
                Table* it = AS_TABLE(item);
                char* name = ui_table_get_str_dup(ctx, it, "name");
                char* pat  = ui_table_get_str_dup(ctx, it, "pattern");
                if (name && pat) {
                    arr[*out_filter_count].name    = name;
                    arr[*out_filter_count].pattern = pat;
                    (*out_filter_count)++;
                } else {
                    free(name); free(pat);
                }
            }
            *out_filters = arr;
        }
    }
    uival_value_decref(fv);
}

static void free_picker_opts(char* title, char* def,
                             UiFileFilter* filters, int count) {
    for (int i = 0; i < count; i++) {
        free((void*)filters[i].name);
        free((void*)filters[i].pattern);
    }
    free(filters);
    free(title);
    free(def);
}

// builtin: ui.open_file(opts?) — native "open file" dialog
Value apexui_impl_open_file(UiContext* ctx, int argc, Value* argv) {
    if (!ctx->window) return MAKE_NONE();                 // no native window: nothing to anchor to

    char* title = NULL;
    char* def   = NULL;
    UiFileFilter* filters = NULL;
    int filter_count = 0;
    parse_picker_opts(ctx, argc, argv, &title, &def, &filters, &filter_count);

    char* picked = ui_window_open_file(ctx->window, title, def, filters);
    free_picker_opts(title, def, filters, filter_count);

    if (!picked) return MAKE_NONE();                      // cancelled or unavailable
    Value v = MAKE_STRING(uival_string_create(picked, (int)strlen(picked)));
    free(picked);
    return v;
}

// builtin: ui.open_folder(opts?) — native "open folder" dialog
Value apexui_impl_open_folder(UiContext* ctx, int argc, Value* argv) {
    if (!ctx->window) return MAKE_NONE();

    char* title = NULL;
    char* def   = NULL;
    UiFileFilter* filters = NULL;
    int filter_count = 0;
    parse_picker_opts(ctx, argc, argv, &title, &def, &filters, &filter_count);

    // filters are meaningless for a folder picker
    char* picked = ui_window_open_folder(ctx->window, title, def);
    free_picker_opts(title, def, filters, filter_count);

    if (!picked) return MAKE_NONE();
    Value v = MAKE_STRING(uival_string_create(picked, (int)strlen(picked)));
    free(picked);
    return v;
}

// writes `len` bytes to `path` verbatim; returns true on success
static bool save_write_file(const char* path, const char* content, int len) {
    FILE* f = fopen(path, "wb");                    // binary: exact bytes, no newline translation
    if (!f) return false;
    size_t got = len > 0 ? fwrite(content, 1, (size_t)len, f) : 0;
    int    rc  = fclose(f);                         // 0 on success, EOF on flush error
    return rc == 0 && got == (size_t)len;
}

// builtin: ui.save_as(content, opts?) — native "save file" dialog + write
Value apexui_impl_save_as(UiContext* ctx, int argc, Value* argv) {
    if (!ctx->window) return MAKE_NONE();                 // no native window: nothing to anchor to

    if (argc < 1 || !IS_STRING(argv[0])) return MAKE_NONE();  // content must be a string
    StringObject* content = AS_STRING(argv[0]);

    char* title = NULL;
    char* def   = NULL;
    UiFileFilter* filters = NULL;
    int filter_count = 0;
    if (argc >= 2 && IS_TABLE(argv[1]))                   // argv[1] is the opts table
        parse_picker_opts(ctx, 1, argv + 1, &title, &def, &filters, &filter_count);

    char* picked = ui_window_save_file(ctx->window, title, def, filters);
    free_picker_opts(title, def, filters, filter_count);

    if (!picked) return MAKE_NONE();                      // cancelled or unavailable

    bool ok = save_write_file(picked, content->chars, content->length);
    if (!ok) {                                            // write failed: do not leak the path
        free(picked);
        return MAKE_NONE();
    }

    Value v = MAKE_STRING(uival_string_create(picked, (int)strlen(picked)));
    free(picked);
    return v;                                             // caller owns the path string
}

// builtin: ui.fullscreen(opts?) — toggle or set the window's fullscreen state
Value apexui_impl_fullscreen(UiContext* ctx, int argc, Value* argv) {
    if (!ctx->window) return MAKE_BOOL(false);            // no window: nothing to do

    bool want = !ui_window_is_fullscreen(ctx->window);    // default: flip the current state
    if (argc >= 1 && IS_BOOL(argv[0]))                    // explicit state wins
        want = AS_BOOL(argv[0]);

    if (want != ui_window_is_fullscreen(ctx->window))     // only toggle when the state differs
        ui_window_toggle_fullscreen(ctx->window);

    ctx->geometry_dirty = true;                           // client area changed
    ctx->redraw_needed  = true;                           // schedule a repaint
    ctx->full_repaint   = true;                           // no prior pixels for the new size

    return MAKE_BOOL(ui_window_is_fullscreen(ctx->window));
}