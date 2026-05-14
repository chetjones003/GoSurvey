# GoSurvey — source layout

This document describes how C++ sources are grouped after the Part 1 “tidy structure” refactor. **All existing `#include "Header.hpp"` paths are unchanged**; CMake and the editor add each domain folder to the include path so headers are found by name only.

## Directory map

| Folder | Role |
|--------|------|
| `src/app/` | Application entry (`main.cpp`) — GLFW/ImGui frame loop, high-level wiring. |
| `src/ui/` | ImGui panels, ribbon, menus, command line, drawing viewport input, splash, rich MTEXT UI. |
| `src/cad/` | CAD model orchestration (`AppCommandState`, commands, snap, linetype metadata used by geometry). Includes `TransformPreview.cpp` (move/copy/scale/rotate/offset viewport previews) and `CadRubberPreview.cpp` (draft rubber). |
| `src/util/` | Header-only helpers shared across domains (e.g. `StringUtil.hpp` for ASCII trim / lowercasing used by the command shell and UI). |
| `src/io/` | Workspace `.gs` I/O, DXF, user prefs, survey CSV import/export, DXF color tables. |
| `src/survey/` | In-memory survey / COGO points model. |
| `src/render/` | OpenGL viewport renderer (geometry batches, overlays). |
| `src/platform/` | OS-specific UI helpers (Windows file dialogs, frame controls, icons). |

## Build

- **CMake** lists each translation unit under its folder; `target_include_directories` lists `src` plus every subdirectory above so includes stay portable.
- **IntelliSense**: `.vscode/c_cpp_properties.json` mirrors those include paths when not using `compile_commands.json` alone.

## Frame order (conceptual)

1. Poll input, `ImGui::NewFrame`.
2. UI (`CadUi`, layout, modals) mutates `AppCommandState`.
3. Rubber lines (`CadRubberPreview`) and transform/selection preview batches (`TransformPreview`), then `ViewportRenderer` draw.
4. Present.

## Adding a new module

Place the `.cpp`/`.hpp` pair in the most fitting folder, add the `.cpp` to `add_executable` in `CMakeLists.txt`, and add nothing to includes if the folder is already listed (new top-level folder requires a new include line in CMake and VS Code config). Large UI entry points may be split into additional `.cpp` files in the same folder (same `CadUi.hpp` API) to keep translation units reviewable.
