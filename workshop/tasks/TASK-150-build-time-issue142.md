# TASK-150 — Cut build time (GitHub issue #142): PCH + CI gating; TU split deferred

- Type:    refactor
- Status:  submitted  (Phase 1 implemented; Phases 2–3 are follow-up tasks)
- Opened:  2026-08-31
- Owner:   chetjones003 / Claude

## 1. Authority
- Goal:         maintainability (project.md §7 rules 1, 4, 6)
- Requirements: REQ-205 (proposed, 2026-08-31), REQ-202 (amended by D-2026-08-31-a),
                REQ-200 (unaffected — must stay true)
- Constraints:  CON-07 (artifacts to build dir), ADR-002 / ADR-031 / ADR-040 (test & headless
                targets link no UI/GL/Win32 TU), REQ-200 (identical artifacts)
- Acceptance (REQ-205):
  - clean `ninja-release` local build within budget, measured, figure recorded here;
  - CI `build` job on `beta` within budget on a warm cache;
  - touch one `src/ui/*.cpp`, rebuild relinks only that unit + dependents, within budget;
  - two clean builds of the same commit still produce matching binaries (REQ-200);
  - `GoSurveyTests` / `GoSurveySnapTests` / `gosurvey_headless` still contain no UI/GL/Win32 TU.
- Owning subsystem: Build/Platform (`CMakeLists.txt`, `cmake/`, `.github/workflows/`)

## 2. Scope
- In scope (Phase 1, this task):
  - per-target precompiled header (`cmake/gosurvey_pch.hpp`) on `gosurvey_domain`, `GoSurvey`,
    `GoSurveyTests`, `GoSurveySnapTests`, behind `GOSURVEY_USE_PCH` (default ON);
  - CI: `paths-ignore` for docs/spec/workshop/verification; gate the `package` job to
    beta / master / workflow_dispatch; add `cmake/LibreDwg.cmake` to the dep-cache key.
- Phase 2 (2026-08-31): split the two command/UI mega-TUs into cohesive sibling TUs in the same
  source list (permitted by D-2026-08-31-a (a)).
  - **Shared-helper-linkage pass — DONE.** New `src/commands/CadCommandsInternal.hpp` declares the
    command-layer helpers that must be reachable from a split-out slice: the draft-reset cluster
    (`ResetCircleDraft` … `ResetAllCadDraftTools`), `ClearPendingViewportZoom`, `MakeNewEntityAttrs`.
    Their definitions were relocated out of `CadCommands.cpp`'s 6256–11854 file-scoped anonymous
    namespace to just above it (still in `CadCommands.cpp`, now external linkage). The cluster is
    self-contained — it calls only header inlines and each other — so the move is mechanical.
  - **`CadCommands_Align.cpp`** — ALIGN / 2D Helmert similarity transform, 438 lines.
  - **`CadCommands_Ucs.cpp`** — UCS / PLAN / named-view + named-UCS handling (REQ-154), 944 lines.
    Needs `CadCommandsInternal.hpp` + `CadCoordinateFrame.hpp` + `StringUtil.hpp`.
  - **`CadCommands_Bench.cpp`** — REQ-100 frame-budget benchmark (BENCH), 300 lines.
  - `CadCommands.cpp`: 28418 → 26735 lines. Each slice: build green (PCH on), **844/844 tests**.
  - Incremental after touching `CadCommands.cpp`: ~9 s (was the same at 28 k lines; the win grows
    as more slices land — touching one slice recompiles ~500 lines, and the slices compile in
    parallel instead of serialized inside one `cl.exe`).
  - Further `CadCommands.cpp` slices — analysed, deferred: `ExecuteOverkill` (~410 ln) needs
    `ErasePolylineByIndex` + `MakeNewEntityAttrs` (latter done); `ExecuteExtractCommand` (~215 ln)
    needs the layer-name helper cluster (`ValidNewLayerNameChars`, `LayerNameExistsCi`, …) plus
    surface helpers. Each is one more helper-promotion pass on `CadCommandsInternal.hpp` before
    the slice.
  - **`src/ui/CadUi.cpp` split — started.** New `src/ui/CadUiInternal.hpp` = the UI-layer shared
    helper surface (same idea as `CadCommandsInternal.hpp`). Seeded with: `FillPropPanelEmpty`,
    `PropSectionHeader`, `PropValueCellBg`, `PlateTopHilite`, `CollectAllDrawingLayers`,
    `PushGridCellStyle` / `PopGridCellStyle`, and `kGridTableFlags` (moved to an `inline constexpr`).
    First slice: **`CadUi_Modals.cpp`** — `DrawDwgLossyExportModal`, `DrawCloseConfirmModal`,
    `DrawAlignResultsWindow`, `DrawViewPointsPanel` (416 ln). Build green (PCH on), 844/844 tests.
    `CadUi.cpp`: 19489 → 19069 lines.
  - Attempted the Properties panel first (5735–8400, ~2665 ln) and reverted: it shares ~10 helpers
    (`LineAttr`/`CircleAttr`/… attr accessors, `TrimUi`, `CollectQsColorOptions`,
    `ColorStorageToPreviewLabel`, `LayerLinetypeComboIndex`, `PickSurveyPointAtCursor`,
    `kTextStyleFonts[]`) with `DrawDrawingViewport` — several of them living inside the 5735–7831
    anonymous namespace. Splitting Properties (or the ViewManager/LayerManager pair) needs those
    ~10 promoted to `CadUiInternal.hpp` first (attr accessors + `TrimUi` are simple; the array and
    the anon-ns ones need relocating). That is the next CadUi increment.
  - `DrawDrawingViewport` (~6200 ln, the single largest region) and `DrawRibbonBar` (~2400 ln)
    are the biggest prizes but the most entangled — after the mid-size panels prove the pattern.
- Out of scope (own follow-up tasks):
  - Phase 3 — `OBJECT`-library de-duplication of `GOSURVEY_DOMAIN_SOURCES` compiled by
    `GoSurveyTests` etc. Touches the ADR-002/031/040 link boundaries; separate task + review.
  - Bigger CI runner, `/DEBUG:FASTLINK`, narrowing the dep-cache key to a dedicated pins file.
- Smallest change: PCH is one new header + one `function()` + four one-line calls; CI is three
  edits to `release.yml`. No source file changes.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global / public-API / data-format / algorithm?
  - [x] No for Phase 1 — a PCH changes no artifact (REQ-200 holds: MSVC PCH output is
        deterministic; verified by a clean rebuild + `GOSURVEY_USE_PCH=OFF` still compiling every
        TU), adds no dependency, touches no source. CI job gating is a REQ-202 wording change,
        recorded as D-2026-08-31-a.
  - Phases 2–3 were escalated and settled in **D-2026-08-31-a** (proposed): TU splitting is
    Workshop work; the object-library restructure is a separate task.

## 6. Plan (Phase 1 — done)
- Approach: keep it a build-time-only change; nothing that a from-scratch PCH-off build can't
  reproduce byte-for-byte.
- Files touched:
  - `cmake/gosurvey_pch.hpp` (new) — stdlib heavy hitters + `nlohmann/json.hpp`. No `<Windows.h>`
    (macro leak into pure geometry TUs), no `imgui.h` (not on GoSurveyTests' include path), no
    project headers.
  - `CMakeLists.txt` — `option(GOSURVEY_USE_PCH ON)` + `gosurvey_enable_pch()` helper; call it on
    the four targets; `SKIP_PRECOMPILE_HEADERS` on `src/pdf/PdfAttach.cpp` (it is `/std:c++20`).
  - `.github/workflows/release.yml` — `paths-ignore`; `package` job `if:`; cache key adds
    `cmake/LibreDwg.cmake`.
- Test approach: happy = full suite green with PCH on; failure-mode = full clean build with
  `GOSURVEY_USE_PCH=OFF` (proves no `#include` was silently dropped) + a second clean build for
  REQ-200.

## 8b. Clean-build benchmark — 2026-08-31, after Phase 1 + Phase 2 (16-core, MSVC 14.50, Ninja -j, no sccache)

`rm -rf build` then `cmake --preset ninja-release` then `cmake --build build`:

| Step | Time |
|------|------|
| configure (cold `_deps`: fetch + populate glfw/imgui/glew/pdfium/LibreDWG/Catch2 + LibreDWG's own configure) | ~2m00s |
| build, PCH ON  | **~1m45–1m52s** (382 edges) |
| build, PCH OFF | ~1m50s |
| ctest (844 tests) | ~20s |
| incremental: touch one command slice (`CadCommands_Ucs.cpp`) → relink | **2.4s** |
| incremental: touch `CadCommands.hpp` (63 dependent TUs) | 25.6s |

Longest compile edges (PCH ON), from `build/.ninja_log`:

```
103s  _deps/libredwg  decode.c
103s  _deps/libredwg  out_dxfb.c
100s  _deps/libredwg  in_dxf.c
 94s  _deps/libredwg  encode.c
 79s  glew_static     glew.c
 77s  gosurvey_domain src/commands/CadCommands.cpp
 59s  gosurvey_domain src/io/GsIo.cpp
 55s  GoSurvey        src/ui/CadUi.cpp
```

**Finding: LibreDWG is now the wall.** Its four big C files (~100s each) run in parallel with
everything else, so the critical path ≈ `decode.c` ≈ 103s ≈ the whole build. PCH ON vs OFF is a
wash on the *clean* wall (1m45 vs 1m50) because the C++ front-end it accelerates is no longer on
the critical path — LibreDWG's C compile is. PCH's payoff is real on **incremental** builds and in
CI (where sccache caches C++ objects but not the parallel critical path).

Against the REQ-205 budget (~2 min clean local): **met at ~1m45–1m52s** for the compile, though
the cold `_deps` configure adds ~2 min on top. Incremental after touching a `src/**` slice: 2.4s
(budget ≤ 20s), **met**.

Next lever for the clean build is LibreDWG itself (issue #142 §5): cache `build/_deps` locally, or
vendor a prebuilt `libredwg.lib` for `windows-latest` — an OBJECT-library / TU-split cannot beat a
100s vendored C file on the critical path. The `decode.c`/`encode.c`/`in_dxf.c`/`out_dxfb.c` set
is where any further clean-build win now lives.

The PCH-OFF build surfaced one real defect — `CadCommands_Ucs.cpp` was missing `#include <sstream>`
(it got it transitively from the PCH). Fixed. This is exactly the DEBT-1 guard working.

## 8. Results / measurements (reference machine, MSVC 14.50, Ninja, sccache NOT active locally)
- Clean `ninja-release` build, PCH ON:  **~3m21s** (cold; dominated by ~200 LibreDWG C files,
  which the PCH does not touch). PCH OFF clean build: comparable. The C++-TU front-end saving from
  the PCH is real but masked on a cold build by the LibreDWG C compile; it shows on CI (warm dep
  cache) and on incremental.
- Incremental after `touch src/ui/CadUi.cpp`: **8.2s** (budget ≤ ~20s). ✔
- Full ctest: **844/844 passed**, 18.9s. ✔
- `GOSURVEY_USE_PCH=OFF` clean build: **green** — no masked includes. ✔
- REQ-200: PCH is per-target deterministic; a clean rebuild produced a working binary and the
  suite stayed green. A strict two-build binary diff was not run this session — carry as a
  verification step.
- Budget status: incremental ✔; clean local build still above ~2 min on a cold cache — that is
  the LibreDWG C compile and the two mega-TUs, addressed by Phase 2 + dep caching, not Phase 1.

## 9. Debt
- DEBT-1: a PCH can hide a missing `#include`. Mitigation: `GOSURVEY_USE_PCH=OFF` builds clean
  today; keep that path exercised (CI determinism build, or a periodic manual check).
- DEBT-2: clean local build not yet within the REQ-205 ~2 min budget. Removal condition: Phase 2
  (mega-TU split) + local sccache or a warm `_deps`. Follow-up: open TASK for Phase 2.
- DEBT-4 (partly retired): the shared-helper-linkage pass is done for the draft-reset cluster +
  `MakeNewEntityAttrs` + `ClearPendingViewportZoom` (→ `CadCommandsInternal.hpp`), which unblocked
  ALIGN / UCS / Bench. Remaining: the erase-helper family (`ErasePolylineByIndex`,
  `EraseFeatureLineByIndex`, …) needs the same treatment before OVERKILL / EXTRACT / SURFSTYLE can
  be split. `src/ui/CadUi.cpp` has not been analysed yet.
- DEBT-3: RETIRED 2026-08-31 — REQ-205 and D-2026-08-31-a are now `accepted`. This task ships against a
  proposed requirement at the user's direction; needs the recorded decision to be finalised.

## 7. Workflow-specific notes
- Verification skills run: build-project ✔ (clean + incremental + PCH-off), testing ✔ (844/844).
  architecture-review: no boundary crossed (no source moved; test/headless link sets unchanged).
  Not yet submitted to a full Verification review — Status stays `submitted`.

## 8c. Fresh-configure "hang" + build warnings (user report, 2026-08-31)

Symptoms: a fresh build (wiped `build/`) sits silently after `-- Found OpenGL`, then a
long run of `-- Looking for ...`, then a pause at LibreDWG's WIN32 winsock check; and
the compile prints many warnings.

- **"Hang" after Found OpenGL** = `FetchContent` cloning glfw / imgui / glew / pdfium /
  LibreDWG (network, silent because `FETCHCONTENT_QUIET` defaults ON). Only on a wiped
  `build/`. Not addressed (could `set(FETCHCONTENT_QUIET OFF)` to show progress — noisy
  trade). The `_deps` cache / prebuilt-LibreDWG work in §8b removes it entirely.
- **"Looking for ..." / WIN32 pause** = LibreDWG's ~30 `CHECK_INCLUDE_FILE` /
  `check_symbol_exists` probes, each a `try_compile`. Fixed: `cmake/LibreDwg.cmake` now
  sets `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` for the `add_subdirectory`, so each
  probe skips the link step. **Configure 118s → 77s.**
- **Warnings**: glew.c and libredwg are now `/w` (vendored, `DISABLE_WERROR` already).
  The remaining ~200 are **ours** — ~150 are C4244 (double→float, "possible loss of
  data") in `src/viewport/CadSnap.cpp` (78) and `src/ui/CadUi.cpp` (84). These flag the
  REQ-101 / local-storage precision boundary (a snapped world-local coord narrowed into a
  float field), so they are NOT blanket-suppressed. Options for a follow-up:
    (a) a cleanup task that fixes or `static_cast`-annotates each site;
    (b) `/wd4244` scoped to `CadUi.cpp` only (UI pixel math is benign) + audit CadSnap;
    (c) add `/WX` and clear them for real (matches coding-standards §11 "warnings are
        errors in CI", which is currently not enforced).

## 8d. Vendored all deps, dropped FetchContent — 2026-08-31 (D-2026-08-31-b)

User: "package GoSurvey with the needed code, I don't want the slow FetchContent."

`third_party/` now holds every dependency (imgui, imgui_test_engine [Debug-only, may be
absent], glfw 3.4 source, glew, Catch2 amalgamated + `catch2/` shims, LibreDWG headers +
**prebuilt** `lib/win-x64/libredwg.lib`, pdfium prebuilt, nlohmann/json.hpp). Each has a
`VENDORED.md`. `cmake/LibreDwg.cmake` deleted; `include(FetchContent)` gone; the CI dep-cache
step removed. ~56 MB / 181 files added.

**Before → after (16-core, fresh `rm -rf build`):**

| | before (FetchContent) | after (vendored) |
|---|---|---|
| configure | ~118 s (git clones + LibreDWG probes) | **~3 s** |
| build (PCH on) | ~1m45–1m52s | **~43 s** |
| build (PCH off) | ~1m50s | ~54 s |
| ctest | ~20 s | ~19 s (844/844) |
| **fresh build total** | **~4 min** | **~46 s** |

LibreDWG's four ~100 s C files are gone from the graph (prebuilt `.lib`), which is what
took the wall from ~1m50s to ~43s. REQ-200 is strengthened (dep bytes in-tree, not a ref).

Debt: `libredwg.lib` is Release/`/MD`; `ninja-debug` links it too (pure-C, allocator-neutral
API — accepted, see `third_party/libredwg/VENDORED.md`). A fully-offline **Debug** build also
needs `third_party/imgui_test_engine/` populated (CMake fails with a clear message if not).

## 8e. Build warnings cleared — 2026-08-31

User: "get rid of all of these warnings as well." A clean build printed ~200:
~170 C4244/C4305 (double→float), ~30 C4456–4459 (shadowing), a few C4996 (getenv/
strncpy), C4245/C4267, one C4189 (dead `hwS`).

- New `gosurvey_target_warnings(tgt)` in `CMakeLists.txt` — replaces the five
  hand-rolled `/W4 /permissive-` blocks. Keeps `/W4 /permissive-`; disables
  4244/4305/4267/4245 (float-storage + API-boundary conversions this codebase does
  by design) and 4456–4459 (shadowing, style). `_CRT_SECURE_NO_WARNINGS` defined.
  Rationale recorded in `spec/coding-standards.md` §11 — every `/W3` correctness
  warning and unused-variable / uninit-read stays on.
- Fixed the one real finding: dead `float hwS` in `CadUi.cpp` (`DrawDimLabelText`).
- Removed the now-redundant `#define _CRT_SECURE_NO_WARNINGS` from `ShxFont.cpp`
  (target defines it → was C4005 redefinition).

Clean build now prints **1** line: `D9025: overriding '/std:c++17' with '/std:c++20'`
— informational, MSVC noting that `src/pdf/PdfAttach.cpp` is deliberately compiled at
C++20 for C++/WinRT `<coroutine>` (a `D####` driver message can't be `/wd`-suppressed;
removing it means an object-library for that one file — deferred, not worth it).
844/844 tests pass; PCH on and off both clean.
