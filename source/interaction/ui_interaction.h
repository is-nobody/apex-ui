// source/interaction/ui_interaction.h
// Hit testing, damage tracking and native event handling for the Apex UI library
// https://github.com/is-nobody/apex-ui
// MIT license

#ifndef UI_INTERACTION_H
#define UI_INTERACTION_H

#include "ui_internal.h"
#include "ui_window.h"

// finds the topmost visible widget containing (x, y) via the uniform grid
Widget* hit_test(UiContext* ctx, int x, int y);

// translates a raw window event into ui-layer events and state changes
void handle_win_event(UiContext* ctx, const UiWinEvent* e);

#endif