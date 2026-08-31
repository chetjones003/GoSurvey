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
  - Not started: `src/ui/CadUi.cpp` (~19 k) split; further `CadCommands.cpp` slices. Next
    candidates (`ExecuteOverkill`, `ExecuteExtractCommand`, SURFSTYLE) each call the
    `ErasePolylineByIndex` / erase-helper family — promote that family to `CadCommandsInternal.hpp`
    the same way before extracting them.
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
- DEBT-3: REQ-205 / D-2026-08-31-a are **proposed**, not accepted. This task ships against a
  proposed requirement at the user's direction; needs the recorded decision to be finalised.

## 7. Workflow-specific notes
- Verification skills run: build-project ✔ (clean + incremental + PCH-off), testing ✔ (844/844).
  architecture-review: no boundary crossed (no source moved; test/headless link sets unchanged).
  Not yet submitted to a full Verification review — Status stays `submitted`.
