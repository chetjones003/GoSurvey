# GoSurvey — source layout

This document describes how C++ sources are organised. All `#include "Header.hpp"` paths are short names only — CMake and the editor add each domain folder to the include path so headers are found without directory prefixes.

## Directory map

| Folder | Role |
|--------|------|
| `src/app/` | Application entry (`main.cpp`) — GLFW/ImGui frame loop, startup workspace loading, high-level wiring. |
| `src/ui/` | ImGui panels, ribbon, menus, command line UI, drawing viewport input, splash screen, rich MTEXT editor. |
| `src/cad/` | CAD model and command orchestration (`AppCommandState`, all drawing/editing commands, object snap, linetype metadata, rubber-band preview, transform preview). |
| `src/util/` | Header-only helpers shared across domains (e.g. `StringUtil.hpp` — ASCII trim / lowercasing). |
| `src/io/` | Workspace `.gs` I/O (JSON via nlohmann), DXF import/export, user preferences, survey CSV import/export, DXF ACI colour tables. |
| `src/survey/` | In-memory survey / COGO points model (`SurveyPoint`, create-points bookkeeping, label styles). |
| `src/render/` | OpenGL viewport renderer — geometry batches, overlays, PDF texture drawing. |
| `src/platform/` | OS-specific helpers: Windows file dialogs, custom frame controls (title bar), application icon loading. |
| `src/pdf/` | PDF underlay subsystem: rasterisation pipeline (`PdfAttach.cpp`), async attach worker, snap geometry extraction, image-based snap-target detection, visibility mask, spatial endpoint grid. |

## Key data structures

### `PdfAttachment` (`src/pdf/PdfAttach.hpp`)

Holds everything needed to render and snap-to one attached PDF page:

- `glTexId` — GPU texture of the rasterised page.
- `snapLinesFlat` — Flat `(x0,y0,x1,y1)` segments (or degenerate point pairs for image-detected targets) in PDF page-point space.
- `snapEndptGrid` — CSR spatial grid built by `BuildSnapEndptGrid`; buckets endpoint pairs by page-space cell for O(cells-near-cursor) lookup.
- `snapVisMask` — 256×256 binary grid; each cell is 1 if the corresponding page tile contains visible foreground pixels. Used by snap to reject candidates in blank areas.
- `snapVisDark` — `true` when this is a dark-background PDF (CAD export style); drives both the snap mask threshold and the shader filter direction.
- `showBackground` — User toggle; `false` (default) renders the page with background made transparent.

### Snap pipeline (`src/cad/CadSnap.cpp`, `src/pdf/PdfAttach.cpp`)

1. **Extraction** (`ExtractSnapFromPage`) — walks pdfium path objects; collinear-run dedup and minimum-length filter produce `snapLinesFlat`.
2. **Image-based override** (`BuildImageSnapPts`) — for dense PDFs (>2 000 path segments, e.g. GIS exports), replaces path-based snap: renders a 512×512 binary mask, analyses 8-connected topology to find endpoints (1 neighbour), corners (2 non-opposite), and junctions (3+), emits degenerate point pairs.
3. **Grid build** (`BuildSnapEndptGrid`) — packs endpoint pairs into the CSR `SnapGrid`.
4. **Query** (`CadSnap::FindBest`) — inverse-transforms cursor to PDF space, enumerates grid cells within tolerance, checks `snapVisMask`, transforms hits back to world space.

### Rendering (`src/render/ViewportRenderer.cpp`)

The PDF texture shader (`kTexFs`) supports two filter modes selected by `uDarkBg`:

- **Light-bg mode** — un-premultiplies the white tint (`bgMix = min(r,g,b)`), fades near-white pixels to transparent, boosts dark/grey content toward white so black lines remain readable on the dark CAD viewport.
- **Dark-bg mode** — un-premultiplies the black tint (`contentA = max(r,g,b)`), fades near-black pixels to transparent, restores full colour saturation to surviving pixels.

Background type is auto-detected by `DetectDarkBackground` using corner-patch sampling (4 corners + 4 edge midpoints, 12×12 px each) so PDFs with large coloured fills are classified correctly.

## Build

- **CMake 3.20+** with the `ninja-release` preset (see `CMakePresets.json`).
- **Languages**: `CXX C RC` — the `RC` language compiles `resources/icons/app.rc` to embed the application icon in the Windows executable.
- `target_include_directories` adds `src` and all subdirectories so includes stay portable.

## Frame order (conceptual)

1. Poll input, `ImGui::NewFrame`.
2. UI (`CadUi`, layout, modals) mutates `AppCommandState`.
3. Rubber lines (`CadRubberPreview`) and transform/selection preview batches (`TransformPreview`).
4. `ViewportRenderer` draws geometry, PDF texture overlays, snap glyphs.
5. Present.

## Adding a new module

Place the `.cpp`/`.hpp` pair in the most fitting folder, add the `.cpp` to `add_executable` in `CMakeLists.txt`. No include changes needed unless you add a new top-level folder (which requires a new `target_include_directories` line in CMake). Large UI entry points may be split across multiple `.cpp` files in the same folder sharing one `.hpp` API (e.g. `CadUi_SurveyReports.cpp`).
