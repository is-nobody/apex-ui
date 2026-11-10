// apex_ui.cpp
// Public C++ API implementation of the Apex UI library
// https://github.com/is-nobody/apex-ui
// MIT license

#include "apex_ui.hpp"

#include <vector>

namespace apex {

namespace {

// flattens node handles into the pointer array the c api expects
std::vector<ApexUINode*> to_handles(std::initializer_list<Node> kids) {
    std::vector<ApexUINode*> out;
    out.reserve(kids.size());
    for (const Node& n : kids)
        out.push_back(n.c_node());
    return out;
}

} // namespace

// a push button with a text label
Node Ui::button(const char* label) {
    return Node(apex_ui_button(ctx_, label));
}

// a push button with a text label and options
Node Ui::button(const char* label, Opts opts) {
    return Node(apex_ui_button_ex(ctx_, label, opts.to_c()));
}

// a line of body text
Node Ui::label(const char* text) {
    return Node(apex_ui_label(ctx_, text));
}

// a line of body text with options
Node Ui::label(const char* text, Opts opts) {
    return Node(apex_ui_label_ex(ctx_, text, opts.to_c()));
}

// a line of heading text
Node Ui::heading(const char* text) {
    return Node(apex_ui_heading(ctx_, text));
}

// a line of heading text with options
Node Ui::heading(const char* text, Opts opts) {
    return Node(apex_ui_heading_ex(ctx_, text, opts.to_c()));
}

// a horizontal rule
Node Ui::separator() {
    return Node(apex_ui_separator(ctx_));
}

// a horizontal rule with options
Node Ui::separator(Opts opts) {
    return Node(apex_ui_separator_ex(ctx_, opts.to_c()));
}

// a flexible gap
Node Ui::spacer() {
    return Node(apex_ui_spacer(ctx_));
}

// a fixed-size gap via with_width / with_height
Node Ui::spacer(Opts opts) {
    return Node(apex_ui_spacer_ex(ctx_, opts.to_c()));
}

// a checkbox with a text label
Node Ui::checkbox(const char* label) {
    return Node(apex_ui_checkbox(ctx_, label));
}

// a checkbox with a text label and options
Node Ui::checkbox(const char* label, Opts opts) {
    return Node(apex_ui_checkbox_ex(ctx_, label, opts.to_c()));
}

// a single-line text input
Node Ui::input(const char* text) {
    return Node(apex_ui_input(ctx_, text));
}

// a single-line text input with options
Node Ui::input(const char* text, Opts opts) {
    return Node(apex_ui_input_ex(ctx_, text, opts.to_c()));
}

// a dropdown showing `selected` with the given items
Node Ui::dropdown(const char* selected, std::initializer_list<const char*> items) {
    return Node(apex_ui_dropdown(ctx_, selected, items.begin(), items.size()));
}

// a dropdown showing `selected` with the given items and options
Node Ui::dropdown(const char* selected, std::initializer_list<const char*> items,
                  Opts opts) {
    return Node(apex_ui_dropdown_ex(ctx_, selected, items.begin(), items.size(),
                                   opts.to_c()));
}

// a vertical stack of children
Node Ui::column(std::initializer_list<Node> kids) {
    std::vector<ApexUINode*> handles = to_handles(kids);
    return Node(apex_ui_column(ctx_, handles.data(), handles.size()));
}

// a vertical stack of children with options
Node Ui::column(std::initializer_list<Node> kids, Opts opts) {
    std::vector<ApexUINode*> handles = to_handles(kids);
    return Node(apex_ui_column_ex(ctx_, handles.data(), handles.size(), opts.to_c()));
}

// a horizontal row of children
Node Ui::row(std::initializer_list<Node> kids) {
    std::vector<ApexUINode*> handles = to_handles(kids);
    return Node(apex_ui_row(ctx_, handles.data(), handles.size()));
}

// a horizontal row of children with options
Node Ui::row(std::initializer_list<Node> kids, Opts opts) {
    std::vector<ApexUINode*> handles = to_handles(kids);
    return Node(apex_ui_row_ex(ctx_, handles.data(), handles.size(), opts.to_c()));
}

// a row-major grid laid out into `cols` equal-width columns
Node Ui::grid(int cols, std::initializer_list<Node> kids) {
    std::vector<ApexUINode*> handles = to_handles(kids);
    return Node(apex_ui_grid(ctx_, cols, handles.data(), handles.size()));
}

// a row-major grid laid out into `cols` equal-width columns with options
Node Ui::grid(int cols, std::initializer_list<Node> kids, Opts opts) {
    std::vector<ApexUINode*> handles = to_handles(kids);
    return Node(apex_ui_grid_ex(ctx_, cols, handles.data(), handles.size(),
                               opts.to_c()));
}

// atomically commits a new tree from the given roots
void Ui::render(std::initializer_list<Node> roots) {
    std::vector<ApexUINode*> handles = to_handles(roots);
    apex_ui_render(ctx_, handles.data(), handles.size());
}

// commits a single-root tree
void Ui::render(Node root) {
    apex_ui_render_one(ctx_, root.c_node());
}

// releases the staged nodes and the committed tree without rendering
void Ui::clear() {
    apex_ui_clear(ctx_);
}

} // namespace apex