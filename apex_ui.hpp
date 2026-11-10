// apex_ui.hpp
// Public C++ API of the Apex UI library
// https://github.com/is-nobody/apex-ui
// MIT license

#ifndef APEX_UI_HPP
#define APEX_UI_HPP

#include "apex_ui.h"

#include <initializer_list>
#include <type_traits>

namespace apex {

// widget options, mirroring ApexUIOpts field for field; zero/null = "not set"
class Opts {
public:
    const char* id        = nullptr;  // stable identity, reported back by events
    const char* css_class = nullptr;  // styling tag for hosts
    double      width     = 0.0;      // explicit pixel width, 0 = auto
    double      height    = 0.0;      // explicit pixel height, 0 = auto
    bool        disabled  = false;    // greyed out, input-ignoring
    bool        checked   = false;    // initial checkbox state

    Opts&       with_id(const char* v) { id = v;        return *this; }  // fluent setter
    Opts&    with_class(const char* v) { css_class = v; return *this; }  // fluent setter
    Opts&         with_width(double v) { width = v;     return *this; }  // fluent setter
    Opts&        with_height(double v) { height = v;    return *this; }  // fluent setter
    Opts& with_disabled(bool v = true) { disabled = v;  return *this; }  // fluent setter
    Opts&  with_checked(bool v = true) { checked = v;   return *this; }  // fluent setter

    ApexUIOpts to_c() const {                                           // plain-c view
        ApexUIOpts o;
        o.id = id; o.css_class = css_class;
        o.width = width; o.height = height;
        o.disabled = disabled;
        o.checked = checked;
        return o;
    }
};

// thin, copyable, non-owning handle to a context-owned widget
class Node {
public:
    Node() : node_(nullptr) {}                                    // null handle
    explicit Node(ApexUINode* n) : node_(n) {}                    // wrap raw c handle

    ApexUINode* c_node() const { return node_; }                  // raw handle for c api calls
    explicit operator bool() const { return node_ != nullptr; }   // null check

    friend bool operator==(const Node& a, const Node& b) { return a.node_ == b.node_; }
    friend bool operator!=(const Node& a, const Node& b) { return a.node_ != b.node_; }

private:
    ApexUINode* node_;                                            // non-owning pointer
};

// borrowed view over an event handed to a poll callback; do not store
class Event {
public:
    explicit Event(const ApexUIEvent* ev) : ev_(ev) {}

    const char* type() const { return apex_ui_event_type(ev_); }                    // "click", "change", ...
    const char* id() const   { return apex_ui_event_id(ev_); }                      // source widget's Opts::id
    double num(const char* key) const { return apex_ui_event_num(ev_, key); }       // numeric field
    const char* str(const char* key) const { return apex_ui_event_str(ev_, key); }  // borrowed pointer

private:
    const ApexUIEvent* ev_;
};

// non-copyable facade over an ApexUIContext; the context stays owned by the caller
class Ui {
public:
    explicit Ui(ApexUIContext* ctx) : ctx_(ctx) {}
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    ApexUIContext* ctx() const { return ctx_; }                    // raw context
    bool open(const char* title, int width, int height) {          // opens the native window
        return apex_ui_open(ctx_, title, width, height);
    }

    // leaf widgets
    Node button(const char* label);                                // push button with text
    Node button(const char* label, Opts opts);
    Node label(const char* text);                                  // body text line
    Node label(const char* text, Opts opts);
    Node heading(const char* text);                                // heading text, larger font
    Node heading(const char* text, Opts opts);
    Node separator();                                              // horizontal rule
    Node separator(Opts opts);
    Node spacer();                                                 // flexible gap
    Node spacer(Opts opts);                                        // fixed gap via width/height
    Node checkbox(const char* label);                              // checkbox with text
    Node checkbox(const char* label, Opts opts);                   // opts.checked = initial state
    Node input(const char* text);                                  // single-line text input
    Node input(const char* text, Opts opts);
    Node dropdown(const char* selected, std::initializer_list<const char*> items);  // select
    Node dropdown(const char* selected, std::initializer_list<const char*> items, Opts opts);

    // containers
    Node column(std::initializer_list<Node> kids);                 // vertical stack
    Node column(std::initializer_list<Node> kids, Opts opts);
    Node row(std::initializer_list<Node> kids);                    // horizontal row
    Node row(std::initializer_list<Node> kids, Opts opts);
    Node grid(int cols, std::initializer_list<Node> kids);         // row-major, cols columns
    Node grid(int cols, std::initializer_list<Node> kids, Opts opts);

    // commit
    void render(std::initializer_list<Node> roots);                // atomic: commit + drop staged nodes
    void render(Node root);                                        // single-root convenience
    void clear();                                                  // drop tree without rendering

private:
    ApexUIContext* ctx_;                                           // non-owning context pointer
};

// pumps the context once and invokes fn per queued event, in order
template <typename F>
inline void poll_events(ApexUIContext* ctx, F&& fn) {
    using Fn = std::remove_reference_t<F>;                         // strip the reference
    auto thunk = [](const ApexUIEvent* ev, void* ud) {             // c callback trampoline
        Fn& f = *static_cast<Fn*>(ud);
        f(Event(ev));
    };
    Fn* fn_ptr = &fn;                                              // safe for lvalue lambdas
    apex_ui_poll_events(ctx, static_cast<ApexUIHandler>(thunk), fn_ptr);
}

} // namespace apex

#endif // APEX_UI_HPP