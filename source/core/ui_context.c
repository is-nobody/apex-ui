// source/core/ui_context.c
// Context lifecycle for the Apex UI library
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// creates a fresh ui context with its own intern table and key cache
UiContext* apexui_context_create(void) {
    UiContext* ctx = (UiContext*)calloc(1, sizeof(UiContext));    // fresh zeroed context
    if (!ctx) return NULL;                                        // allocation failed
    ctx->focused_id = 0;                                          // nothing focused
    ctx->last_tree = MAKE_NONE();                                 // no tree rendered yet
    ctx->title_end_x = 14;                                        // left_pad

    ctx->font = ui_font_load_system();                            // try to load a default font
    if (!ctx->font) {
        fprintf(stderr, "[ui] no system font available; text will not render\n");
    }

    // pre-intern every descriptor field name once; saves a hash + lookup per field per widget
    static const char* const kKeyNames[UK_COUNT] = {
        "__t", "key", "align", "justify", "gap", "padding", "weight",
        "min_width", "max_width", "disabled", "visible", "on_top", "bare",
        "width", "height", "content", "label", "checked",
        "value", "min", "max", "value_text", "children", "options",
        "columns", "rows", "stripe", "header",
        "nodes", "selected", "indent", "expanded",
        "tabs", "closeable", "add_button", "reorder",
        "tooltip", "tooltip_delay", "left", "right", "text", "title", "collapsed", "gutter", "resizable"
    };
    if (!uival_string_intern_table_init(&ctx->intern_table)) {          // intern storage for field names
        free(ctx);
        return NULL;
    }
    for (int i = 0; i < UK_COUNT; i++)
        ctx->key_cache[i] = ui_intern(ctx, kKeyNames[i]);

    return ctx;
}

// releases every heap allocation owned by one persistent state entry
static void free_state_entry(WidgetState* s) {
    for (int j = 0; j < s->undo_count; j++) {
        free(s->undo_stack[j].removed);
        free(s->undo_stack[j].inserted);
    }
    for (int j = 0; j < s->redo_count; j++) {
        free(s->redo_stack[j].removed);
        free(s->redo_stack[j].inserted);
    }
    free(s->undo_stack);
    free(s->redo_stack);
    free(s->input_buf);
    free(s->tab_anim_x);
    free(s->tab_anim_w);
    free(s->line_starts);
}

// tears down the ui context and releases every owned allocation
void apexui_context_free(UiContext* ctx) {
    if (!ctx) return;                                             // null guard
    ui_nodes_release_all(ctx);                                    // drop staged declarative-api nodes
    ui_widgets_reset(ctx);                                        // free all per-widget allocations
    ui_context_menu_free(ctx);                                    // release any open context menu
    ui_menubar_free(ctx);                                         // release the menu bar

    for (int i = 0; i < ctx->state.count; i++)                    // release per-state history
        free_state_entry(&ctx->state.entries[i]);

    ui_notification_destroy(ctx);                                 // tear down notification popups
    free(ctx->pool);                                              // free the widget pool array
    free(ctx->state.entries);                                     // free the persistent state table
    free(ctx->text_cache);                                        // free the text measurement cache
    free(ctx->state.index);                                       // free the state lookup index
    dialog_clear(&ctx->dialog);                                   // release any open dialog strings
    ui_wrap_cache_free(ctx);                                      // free the wrap cache and its line arrays
    for (int i = 0; i < ctx->queue.count; i++) free(ctx->queue.events[i].str_value);  // release queued event strings
    free(ctx->queue.events);                                      // free the event queue array
    if (ctx->font)   ui_font_free(ctx->font);                     // release the font
    if (ctx->window) ui_window_destroy(ctx->window);              // release the host window
    if (ctx->has_tree && (ctx->last_tree & QNAN) == QNAN)
        uival_value_decref(ctx->last_tree);                       // release the committed tree
    ctx->last_tree = MAKE_NONE();                                 // clear the cached tree
    ctx->has_tree = false;
    free(ctx->node_list);                                         // free the staged-node pointer array
    while (ctx->node_free) {                                      // free every parked node struct
        struct ApexuiNode* n = ctx->node_free;
        ctx->node_free = n->next_free;
        free(n);
    }
    hit_grid_free(ctx);                                           // release hit-test grid
    ui_painter_shutdown();                                        // release the corner atlas
    ui_damage_free(ctx);                                          // free ui damage
    uival_string_intern_table_free(&ctx->intern_table);                 // release interned field names
    free(ctx);                                                    // free the context itself
}