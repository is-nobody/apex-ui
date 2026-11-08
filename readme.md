<div align="center">
  <picture>
    <img alt="Apex" src="resources/logo.png" width="75%">
  </picture>
</div>

---

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Apex Version](https://img.shields.io/badge/Apex UI-26.11-blue)](https://github.com/is-nobody/apex-lang)
![Available](https://img.shields.io/badge/Available-Windows%20%7C%20macOS%20%7C%20Linux-red)

This is the official repository for [Apex UI](https://github.com/is-nobody/apex-ui).

## Why Apex UI?
- **One API, three platforms:** Win32, Cocoa and X11 are fully abstracted behind `ui_window.h`; the widget layer above only sees a framebuffer.

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
standard SDK — nothing to install.

### Install
```bash
cmake --install build/ --prefix /usr/local
```

Installs `apex_ui.h` into `include/` and the library into `lib/`.

### Minimal example
```c
#include "apex_ui.h"

int main(void) {
    ApexUiContext* ctx = apexui_context_create();
    apexui_init(ctx, apexui_make_none());

    apexui_value root = apexui_make_table(1);
    apexui_array_push(root, apexui_button("Click me", apexui_make_none()));
    apexui_render(ctx, root);

    apexui_value ev;
    while (apexui_is_table(ev = apexui_next_event(ctx)))
        apexui_value_release(ev);

    apexui_context_destroy(ctx);
    return 0;
}
```

For a batched message loop with a host callback, use `apexui_run` — the
callback receives each event table and returns the widget tree to render
for the next frame.

## Documentation
Each of the documents walks through the public C API, the widget descriptor
format, the event model, and the platform backends.

- **[Apex UI: Getting Started](resources/ui_Getting_Started.md)** — Best for newcomers, with a from-scratch walkthrough of `apex_ui.h`, the value runtime, and a first window.
- **[Apex UI: Widget Reference](resources/ui_Widgets.md)** — Best as a lookup, covering every widget descriptor, its options, and the events it emits.
- **[Apex UI: Platform Backends](resources/ui_Backends.md)** — Best for porters, covering the Win32, Cocoa and X11 window layers, the clipboard, and the file dialogs.

## Getting Help
See [Issues](https://github.com/is-nobody/apex-ui/issues) for bug reports and feature requests.

## Contributing
Apex UI is created and maintained by one person, but contributions are welcome!

Please see [contributing](contributing.md) and remember about [Code of Conduct](code_of_conduct.md)

## License
Apex UI is distributed under the terms of the **MIT license**.

See [license](license) for details.