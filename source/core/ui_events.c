// source/core/ui_events.c
// Event queue and table conversion for the Apex UI library
// https://github.com/is-nobody/apex-ui
// MIT license

#include "ui_core.h"
#include "ui_table.h"
#include "ui_hash.h"
#include "ui_widgets.h"
#include <stdlib.h>
#include <string.h>

// appends an event to the pending event queue
void ui_event_push(UiContext* ctx, const UiEvent* ev) {
    if (ev->kind == EV_CHANGE && ev->widget_id != 0) {
        for (int i = 0; i < ctx->queue.count; i++) {
            UiEvent* q = &ctx->queue.events[i];
            if (q->kind == EV_CHANGE && q->widget_id == ev->widget_id) {
                free(q->str_value);            // release the superseded payload
                *q = *ev;                      // take ownership of the new one
                return;
            }
        }
    }

    if (ctx->queue.count >= ctx->queue.cap) {             // event queue is full
        ctx->queue.cap = ctx->queue.cap ? ctx->queue.cap * 2 : 32;  // start at 32, then double
        ctx->queue.events = (UiEvent*)realloc(ctx->queue.events,
                                sizeof(UiEvent) * ctx->queue.cap);
    }
    ctx->queue.events[ctx->queue.count++] = *ev;          // copy event into the queue
}

// converts a native ui event into a value table for the host layer
Value ui_event_to_table(UiContext* ctx, const UiEvent* ev) {
    Table* t = uival_table_create(14);                          // event table with room for ~10 fields
    Value k, v;
    const char* ty = "none";                              // default type string
    switch (ev->kind) {
        case EV_CLOSE:       ty = "close";       break;
        case EV_CLICK:       ty = "click";       break;
        case EV_RIGHT_CLICK: ty = "right_click"; break;
        case EV_CHANGE:      ty = "change";      break;
        case EV_KEY:         ty = "key";         break;
        case EV_SCROLL:      ty = "scroll";      break;
        case EV_HOVER:       ty = "hover";       break;
        case EV_RESIZE:      ty = "resize";      break;
        case EV_DIALOG:      ty = "dialog";      break;
        default: break;
    }

    k = ui_intern(ctx, "type");                               // "type" = event kind string
    v = ui_intern(ctx, ty);
    uival_table_set(t, k, v); uival_value_decref(k); uival_value_decref(v);

    k = ui_intern(ctx, "key");                                // "key" = user key string
    if (ev->key_name[0]) {
        v = MAKE_STRING(uival_string_create(ev->key_name, (int)strlen(ev->key_name)));
        uival_table_set(t, k, v); uival_value_decref(k); uival_value_decref(v);
    } else {
        v = ui_intern(ctx, "");                               // empty string when no user key
        uival_table_set(t, k, v); uival_value_decref(k); uival_value_decref(v);
    }

    k = ui_intern(ctx, "widget");                             // "widget" = widget type name
    v = ui_intern(ctx, ev->widget_type);
    uival_table_set(t, k, v); uival_value_decref(k); uival_value_decref(v);

    k = ui_intern(ctx, "value");                              // "value" = event payload, type varies
    if (strcmp(ev->widget_type, "number_input") == 0 ||
        strcmp(ev->widget_type, "tab_move")     == 0 ||
        ev->kind == EV_SCROLL) {
        v = MAKE_NUMBER(ev->num_value);                   // numeric widget: value is a number
    } else if (ev->str_value && strcmp(ev->widget_type, "tab_close") != 0) {
        v = MAKE_STRING(uival_string_create(ev->str_value, (int)strlen(ev->str_value)));
    } else if (ev->payload_skipped) {
        // huge text_area, undo/redo path: do not allocate a full copy
        v = MAKE_STRING(uival_string_create("", 0));
    } else if (ev->str_value == NULL &&
               ev->kind == EV_CHANGE &&
               (strcmp(ev->widget_type, "input")     == 0 ||
                strcmp(ev->widget_type, "text_area") == 0)) {
        WidgetState* ws = NULL;
        UiContext*   cx = ctx;
        if (cx) {
            for (int i = 0; i < cx->state.count; i++) {
                if (cx->state.entries[i].id == ev->widget_id) {
                    ws = &cx->state.entries[i];
                    break;
                }
            }
        }
        if (ws && ws->input_buf && ws->input_len > 0)
            v = MAKE_STRING(uival_string_create(ws->input_buf, ws->input_len));
        else
            v = MAKE_STRING(uival_string_create("", 0));
    } else {
        v = MAKE_BOOL(ev->bool_value);
    }
    uival_table_set(t, k, v); uival_value_decref(k);
    if ((v & QNAN) == QNAN) uival_value_decref(v);              // release heap-tagged payload

    // key of the tab that should take over selection
    if (strcmp(ev->widget_type, "tab_close") == 0) {
        k = ui_intern(ctx, "next");
        const char* nxt = ev->str_value ? ev->str_value : "";
        v = MAKE_STRING(uival_string_create(nxt, (int)strlen(nxt)));
        uival_table_set(t, k, v); uival_value_decref(k); uival_value_decref(v);
    }

    if (ev->kind == EV_KEY) {                             // extra fields for key events
        k = ui_intern(ctx, "key_code"); v = MAKE_NUMBER((double)ev->key_code);
        uival_table_set(t, k, v); uival_value_decref(k);
        k = ui_intern(ctx, "mods"); v = MAKE_NUMBER((double)ev->mods);
        uival_table_set(t, k, v); uival_value_decref(k);
    }
    if (ev->kind == EV_SCROLL) {                          // extra field for scroll events
        k = ui_intern(ctx, "scroll_dy"); v = MAKE_NUMBER(ev->scroll_dy);
        uival_table_set(t, k, v); uival_value_decref(k);
    }
    if (strcmp(ev->widget_type, "tab_move") == 0) {       // source slot for tab reorders
        k = ui_intern(ctx, "from"); v = MAKE_NUMBER(ev->scroll_dy);
        uival_table_set(t, k, v); uival_value_decref(k);
    }
    if (ev->kind == EV_CLICK || ev->kind == EV_HOVER) {   // extra fields for pointer events
        k = ui_intern(ctx, "x"); v = MAKE_NUMBER((double)ev->mouse_x);
        uival_table_set(t, k, v); uival_value_decref(k);
        k = ui_intern(ctx, "y"); v = MAKE_NUMBER((double)ev->mouse_y);
        uival_table_set(t, k, v); uival_value_decref(k);
    }
    if (ev->kind == EV_DIALOG) {                          // "action" = ok / cancel / close
        k = ui_intern(ctx, "action");
        const char* act = (ev->num_value == 1.0) ? "ok" : "cancel";
        v = ui_intern(ctx, act);
        uival_table_set(t, k, v); uival_value_decref(k); uival_value_decref(v);
    }
    return MAKE_TABLE(t);                                 // hand the event table to the caller
}

// emits a click (button 1) or right-click (button 3) event for a widget
void apexui_fire_click_b(UiContext* ctx, Widget* w, int button) {
    if (button == 3) {                                            // right-click: never toggles state
        if (w->kind == WK_TREE) {                                 // per-row dispatch
            tree_handle_press(ctx, w, 3);
            return;
        }
        UiEvent ev; memset(&ev, 0, sizeof(ev));
        ev.kind = EV_RIGHT_CLICK;
        ev.widget_id = w->id;
        strncpy(ev.widget_type, widget_kind_name(w->kind), 15);
        if (w->key_str) strncpy(ev.key_name, w->key_str, 63);
        ev.mouse_x = ctx->input.x;
        ev.mouse_y = ctx->input.y;
        ui_event_push(ctx, &ev);
        ctx->redraw_needed = true;
        return;
    }

    if (w->kind == WK_TREE) {                                     // per-row dispatch
        tree_handle_press(ctx, w, 1);
        return;
    }
    if (w->kind == WK_TABS) {                                     // per-tab dispatch
        tabs_handle_click(ctx, w);
        return;
    }

    UiEvent ev; memset(&ev, 0, sizeof(ev));                       // zero the event
    ev.kind = EV_CLICK;                                           // click kind
    ev.widget_id = w->id;                                         // source widget id
    strncpy(ev.widget_type, widget_kind_name(w->kind), 15);       // widget type name
    if (w->key_str) strncpy(ev.key_name, w->key_str, 63);         // user key if present
    ev.mouse_x = ctx->input.x;                                    // record pointer position
    ev.mouse_y = ctx->input.y;
    ui_event_push(ctx, &ev);                                      // enqueue the click

    if (w->kind == WK_CHECKBOX) {                                  // fires change on toggle
        UiEvent cev; memset(&cev, 0, sizeof(cev));
        cev.kind = EV_CHANGE;
        cev.widget_id = w->id;
        strncpy(cev.widget_type, widget_kind_name(w->kind), 15);   // "checkbox"
        if (w->key_str) strncpy(cev.key_name, w->key_str, 63);
        cev.bool_value = !w->checked;                              // new value is the inverse
        ui_event_push(ctx, &cev);
    }
    ctx->redraw_needed = true;                                    // force a repaint
}

// emits a left-click event for a widget
void apexui_fire_click(UiContext* ctx, Widget* w) { apexui_fire_click_b(ctx, w, 1); }

