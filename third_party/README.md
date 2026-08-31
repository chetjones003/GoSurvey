# third_party/ — vendored build dependencies

Every build dependency lives here as source (or a prebuilt lib where building from
source is not worth the cost). No FetchContent, no network at configure time —
D-2026-08-31-b, GitHub issue #142. REQ-200 / REQ-300.

Each subtree has a `VENDORED.md` with its upstream URL, pinned tag/commit, what was
trimmed, and how to refresh it. To update a dependency: replace its files and bump
its `VENDORED.md` in the same commit.

| Dir | Upstream | Pin | Form |
|-----|----------|-----|------|
| `imgui/` | github.com/ocornut/imgui | `81cfcb8c1` (v1.92.9 docking) | source (core + backends) |
| `imgui_test_engine/` | github.com/ocornut/imgui_test_engine | `v1.92.9` | source, Debug-only (may be absent) |
| `glfw/` | github.com/glfw/glfw | `3.4` | source (no docs/tests/examples) |
| `glew/` | github.com/nigels-com/glew | `2.2.0` | source (`glew.c` + headers) |
| `catch2/` | github.com/catchorg/Catch2 | `v3.5.2` | amalgamated `.hpp`/`.cpp` + `catch2/` shims |
| `libredwg/` | github.com/LibreDWG/libredwg | `0.13.3` | headers + prebuilt `lib/win-x64/libredwg.lib` |
| `pdfium/` | github.com/bblanchon/pdfium-binaries | `chromium/7857` | prebuilt binaries |
| `nlohmann/` | github.com/nlohmann/json | `v3.11.3` | `json.hpp` single header |
| `stb_image.h` | github.com/nothings/stb | (see file header) | single header |
