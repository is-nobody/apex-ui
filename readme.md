<div align="center">
  <picture>
    <img alt="Apex" src="resources/logo.png" width="75%">
  </picture>
</div>

---

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Apex Version](https://img.shields.io/badge/Apex_UI-26.11-blue)](https://github.com/is-nobody/apex-lang)
![Available](https://img.shields.io/badge/Available-Windows%20%7C%20macOS%20%7C%20Linux%20%7C%20Android-red)

This is the official repository for [Apex UI](https://github.com/is-nobody/apex-ui).

## Why Apex UI?
- **One API, four platforms:** Win32, Cocoa, X11 and Android are fully abstracted behind `ui_window.h`; the widget layer above only sees a framebuffer.

- **Damage tracking:** A per-frame widget snapshot diffs against the previous frame; repaints only touch the rects that actually changed.

- **SIMD painting:** AVX2, SSE2 and NEON fast paths for the row blenders and rectangle fillers, with a scalar fallback.

- **No external runtime:** Freetype, harfbuzz, ICU, GTK, Qt, SDL — all absent from the dependency graph.

## Quick Start
### Build
```bash
# static library
cmake -S . -B build/ -DCMAKE_BUILD_TYPE=Release
cmake --build build/ --parallel

# shared library
cmake -S . -B build/ \
    -DCMAKE_BUILD_TYPE=Release \
    -DAPEXUI_BUILD_SHARED=ON
cmake --build build/ --parallel
```

On Linux the build needs `libx11-dev`, `libxext-dev` and
`libfontconfig-dev`. macOS uses the system frameworks, Windows uses the
standard SDK — nothing to install. Android builds through the NDK.

### Install
```bash
cmake --install build/ --prefix /usr/local
```

Installs `apex_ui.h/hpp` into `include/` and the library into `lib/`.

### Minimal example
```cpp
#include "apex_ui.hpp"
#include <cstring>

int main() {
    ApexUIContext* ctx = apex_ui_context_create();
    if (!ctx) return 1;

    apex::Ui ui(ctx);
    if (!ui.open("Apex UI", 320, 200)) {
        apex_ui_context_destroy(ctx);
        return 1;
    }

    ui.render({ ui.button("Click me") });

    bool running = true;
    while (running)
        apex::poll_events(ctx, [&](const apex::Event& ev) {
            if (ev.type() && std::strcmp(ev.type(), "close") == 0)
                running = false;
        });

    apex_ui_shutdown(ctx);
    apex_ui_context_destroy(ctx);
    return 0;
}
```

The same `main()` runs on Android as-is: `ui.open()` adopts the screen
size instead of asking for one, and the backend feeds touch input and the
system clipboard through the same event queue.

For a batched message loop with a host callback, use `apex_ui_run` — the
callback receives each event table and returns the widget tree to render
for the next frame.

## Documentation
Each of the documents walks through the public C API, the widget descriptor
format, the event model, and the platform backends.

- **[Apex UI: Getting Started](resources/ui_Getting_Started.md)** — Best for newcomers, with a from-scratch walkthrough of `apex_ui.h`, the value runtime, and a first window.
- **[Apex UI: Widget Reference](resources/ui_Widgets.md)** — Best as a lookup, covering every widget descriptor, its options, and the events it emits.

## Getting Help
See [Issues](https://github.com/is-nobody/apex-ui/issues) for bug reports and feature requests.

## Contributing
Apex UI is created and maintained by one person, but contributions are welcome!

Please see [contributing](contributing.md) and remember about [Code of Conduct](code_of_conduct.md)

## License
Apex UI is distributed under the terms of the **MIT license**.

See [license](license) for details.