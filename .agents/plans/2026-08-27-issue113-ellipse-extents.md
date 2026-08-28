## Goal

Fix GitHub issue #113: a drawing containing an `ELLIPSE` whose `ratio` or major-axis vector is not exact at six decimals is not a fixed point on the first DXF export→import→export cycle. Three header lines move (`$EXTMIN` y, `$EXTMAX` y, `$VPORT` group 40) then converge (`B == C`). Make the first cycle byte-identical, reusing the same header-vs-body fix applied for polylines (#64 / TASK-083) and arcs (#111 / TASK-122, `DxfArcToWrite`).

## Success Criteria

- **AC-1**: A decision is recorded in `spec/project.md` — code fix (direction 1), not a REQ-204 amendment.
- **AC-2**: The reproducer from the issue is added as `tests/headless/transcripts/regression-113-dxf-ellipse-extents.txt` asserting `EXPECT SAMEFILE a b` (first cycle) and `b c` (convergence), and it passes.
- **AC-3**: Existing regressions still pass: `regression-111-dxf-arc-angle-roundtrip`, `regression-98-dxf-view-precision`, `regression-94`/`94a-dxf-origin-no-drift`, `regression-63-dxf-arc-ellipse-identity`, `dxf-export-stable` (remains DISABLED as before, no new DISABLED).
- **AC-4**: Full `ctest` green with nothing newly disabled. No format change, no `.gs` migration, `CadEllipse` struct unchanged.

## Context And Current Facts

- **Issue**: #113 filed by `nrjohnson2604` 2026-08-26, found while fixing #111 (TASK-122 §12 DEBT-1). Title: “DXF export -> import -> export still differs once on a drawing containing an ELLIPSE (header extents, not groups 41/42, converges after one cycle)”.
- **Root cause (measured)**: `CadEllipse` holds `majVx`, `majVy`, `ratio` as `float` (`src/commands/CadEntities.hpp`). Writer emits them via `std::to_string(double(v))` at six decimals (`src/io/DxfIo.cpp:3384-3390`). Header extents are swept from in-memory values (`src/io/DxfIo.cpp:2508` loop, `n=48` samples), while entity record describes post-round-trip values — one file, two drawings. Groups 41/42 are literal constants `0.0` / `6.283185` and cannot drift (issue § “It is NOT the angles”).
- **Siblings**: #64 (polylines absent from sweep), #111 (arc angles unrepresentable as `float` radians, fixed by `DxfArcToWrite`), #98 (derived `$VPORT` view, fixed by snapping extents). All declined REQ-204 amendment per `D-2026-08-26-h` and `D-2026-08-26-i`; precedent is to fix the writer.
- **Existing fix in working tree**: Uncommitted `DxfQuantizeFloat` / `DxfEllipseAsWritten` / `DxfEllipseToWrite` and header sweep using `ew = DxfEllipseToWrite(el)` (`src/io/DxfIo.cpp:115-136`, `2508-2522`, `3380-3390`) plus untracked `tests/headless/transcripts/regression-113-dxf-ellipse-extents.txt` (correct reproducer). No decision log entry, no commit, massive CRLF→LF churn in 38 other files.
- **Requirements**: `REQ-204` (DXF export→import→export stable, `spec/requirements.md:4546`), `REQ-201` (refuse/log), `REQ-101` (tolerances). Owning subsystem `io` (`src/io/DxfIo.cpp`, `src/io/DxfIo.hpp`).
- **Test harness**: `gosurvey_headless` (REQ-203 / ADR-031), auto-discovers `tests/headless/transcripts/*.txt` via `CMakeLists.txt:743` glob at configure time. Prior regressions listed in `CMakeLists.txt:768`.

## Constraints And Non-goals

- **Constraints**: Must not change on-disk DXF groups/order/precision; must not change `CadEllipse` / `.gs` format; must not cross architecture layers (`io` only); must preserve `ComputeWorldExtents` agreement (`src/io/DxfIo.cpp:2472` “MUST MATCH … EXACTLY”); must run on Windows MSVC `ninja-release` preset.
- **Non-goals**:
  - Generalising to “every extents sweep reads as-written” (issue direction 2) — architectural, needs separate decision.
  - Amend REQ-204 with idempotence carve-out (direction 3) — declined for #98/#111, same reasoning here.
  - HATCH boundary reader’s own angle normalization (TASK-122 DEBT-2) — different path, tessellates to segments.
  - Paper-space ellipse export (paper ellipses not exported today) — out of scope.
  - Binary DXF (`Binary DXF is not supported` guard).

## Key Decisions

1. **Choose direction 1: sweep header extents from the ellipse the file states** (quantize `majV`/`ratio` through write round-trip before tessellating). Rejected: direction 2 (generalise once) as larger/architectural, direction 3 (amend REQ-204) as precedent-declined and here entity text is already stable so the fix is local. Evidence: #111 measurement 0/2M unstable after `DxfArcToWrite` vs 1.48M before; same header-vs-body shape as #64 and #111.
2. **Single owner `DxfEllipseToWrite` + `DxfQuantizeFloat` in `src/io/DxfIo.cpp` anonymous namespace** — precedent `DxfArcToWrite`. Rejected: widening precision or storing `double` in `CadEllipse` — format change + migration for a writer defect. Two callers at start: header sweep and entity emit; reader already reconstructs via `ParseDouble` → `float`.
3. **Quantize only `majVx`/`majVy`/`ratio` via `float → to_string → stod → float`** — matches writer’s `std::to_string(double(v))` and reader’s `ParseDouble`. Centre `cx`/`cy`/`z` left unquantized and explicitly documented as scope decision (drift <1e-6, below extents-pad significance; arc fix likewise left centres alone). Rejected: quantizing centre as `worldX` round-trip — would require `worldDocumentOrigin` coupling and circular reasoning.
4. **Record decision as `D-2026-08-27-*` in `spec/project.md`** with cost (angle-equivalent move ≤0.5 ULP, within REQ-101) and rejection of directions 2/3.
5. **Add transcript `regression-113-dxf-ellipse-extents.txt`** asserting both `a vs b` (first cycle — the defect) and `b vs c` (convergence — already passing), with `CHECK ALL` / `EXPECT ELLIPSES 1` per spec harness.

## Recommended Approach

Add one file-local conversion in `src/io/DxfIo.cpp` and point two call sites at it:

```cpp
inline float DxfQuantizeFloat(float v) {
  return static_cast<float>(std::stod(std::to_string(static_cast<double>(v))));
}
struct DxfEllipseAsWritten { float majVx, majVy, ratio; };
DxfEllipseAsWritten DxfEllipseToWrite(const CadEllipse& el);
```

- Header sweep (`ExportDxfFile_Impl` extents loop, ~`src/io/DxfIo.cpp:2508`): `const auto ew = DxfEllipseToWrite(el); ma = hypot(ew.majVx, ew.majVy); … mb = ma * ew.ratio;`
- Entity emit (`ExportDxfFile_Impl` ELLIPSE branch, ~`3380`): `const auto ewEmit = DxfEllipseToWrite(el); emitPair(11, to_string(double(ewEmit.majVx)));` etc. for 21/40.

Both replace the three direct `el.majVx`/`el.majVy`/`el.ratio` reads. No header file change, no signature change outside TU, no new dependency (`<string>`, `<cmath>` already included). The working-tree diff already implements this — plan is to commit it cleanly, add the decision log, and track the transcript.

## Work Plan

**Unit 1 — Source fix (io) — must be first**

- File: `src/io/DxfIo.cpp`
- Steps:
  1. Add docblock citing #113, #111, #64 and “one file, two drawings” shape; add `DxfQuantizeFloat`, `DxfEllipseAsWritten`, `DxfEllipseToWrite` after `DxfArcToWrite` ()`src/io/DxfIo.cpp:110`).
  2. Replace header extents ellipse loop to use `DxfEllipseToWrite` (`src/io/DxfIo.cpp:2508-2522`), keep `n=48`, `kTwoPi`, `ma < 1e-12` guard, add comment referencing #113.
  3. Replace ELLIPSE emit to use `DxfEllipseToWrite` for groups 11/21/40 (`src/io/DxfIo.cpp:3380-3390`), keep 41/42 literals, add comment “same answer as header sweep”.
- Out of scope files must not be staged (revert CRLF churn in `CLAUDE.md`, `spec/*` other than project.md decision, `resources/*`, etc. or normalize `core.autocrlf` before `git add -p`).

**Unit 2 — Decision log (spec)**

- File: `spec/project.md` decision table (append after `D-2026-08-26-i`).
- Entry `D-2026-08-27-* — Resolve issue #113 by direction 1`: choose fix, decline directions 2/3 with measurement-backed rationale (entity text stable, `b == c` even before fix, 6-decimal precision genuinely lost), state cost, note REQ-204 unamended. Cross-ref #111, #98, #64.

**Unit 3 — Regression transcript (tests)**

- File: `tests/headless/transcripts/regression-113-dxf-ellipse-extents.txt` (already present in working tree, currently `??` untracked).
- Content: header comment (defect, 41/42 innocent, same shape as #111), then:
  ```
  NEW / CMD ELLIPSE / PICK 500.3333333 100.7777777 / PICK 540.1234567 100.9999999 / CMD 0.3333333
  CHECK ALL / EXPECT ELLIPSES 1
  EXPORT DXF %OUT%/r113-a.dxf / NEW / IMPORT DXF %OUT%/r113-a.dxf / CHECK ALL / EXPECT ELLIPSES 1 / EXPORT DXF %OUT%/r113-b.dxf
  EXPECT SAMEFILE %OUT%/r113-a.dxf %OUT%/r113-b.dxf
  NEW / IMPORT DXF %OUT%/r113-b.dxf / CHECK ALL / EXPECT ELLIPSES 1 / EXPORT DXF %OUT%/r113-c.dxf
  EXPECT SAMEFILE %OUT%/r113-b.dxf %OUT%/r113-c.dxf
  ```
- Ensure file is `git add`’ed; `CMakeLists.txt` needs no edit (glob discovery), but re-configure is required.

- Dependencies: Unit 1 → Unit 3 (transcript fails without fix, passes with it); Unit 2 independent but should land in same PR per prior `D-*` practice (spec + `TRACKER.md` update).

**Unit 4 — Tracker/docs (optional, same PR)**

- File: `TRACKER.md` — add “ELLIPSE header extents — 2026-08-27” section under `## CHANGES`, mirroring #111 entry: measurement (single reproducer byte 264, `82.535389→82.535403`), cause, fix, 0/→ stable after.

## Validation Plan

- **Static**: `git diff --stat` shows only 3 files changed (`src/io/DxfIo.cpp`, `spec/project.md`, `tests/headless/transcripts/regression-113-dxf-ellipse-extents.txt` + optional `TRACKER.md`). No `CMakeLists.txt` DISABLED change.
- **Build** (Windows): `cmake --preset ninja-release` then `cmake --build --preset ninja-release` — clean MSVC build, no new warnings in `DxfIo.cpp`.
- **Transcript — red before / green after** (Windows shell, not WSL):
  - Stash fix: `git stash push -m pre-113 --keep-index`, `cmake --build --preset ninja-release`, then `build\gosurvey_headless.exe run tests\headless\transcripts\regression-113-dxf-ellipse-extents.txt --out %TEMP%\r113-out` — expect `SAMEFILE: files differ at byte 264`.
  - Restore: `git stash pop`, rebuild, same run — expect `PASS`, and `b vs c` also `PASS`.
- **Guard regressions** (Windows): `ctest --preset ninja-release -R "regression-111|regression-98|regression-94|regression-63"` — all green. `regression-94a-dxf-origin-large-coord` included.
- **Full suite**: `ctest --preset ninja-release` — expect 638/638 (or current head count) green, `headless.dxf-export-stable` remains DISABLED (no change), nothing newly disabled. Highest-risk step is the full suite — if any `ComputeWorldExtents` agreement breaks, extents-related transcripts will fail first.
- **Manual inspection**: Open `r113-a.dxf` excerpt — verify groups 11/21/40 equal `to_string(quantized)` and `$EXTMIN` y / `$EXTMAX` y / `$VPORT` 40 identical in `r113-a` vs `r113-b`.

## Risks / Rollback

- **Quantizer precision coupling** (`std::to_string` is 6 decimals on MSVC/libstdc++ today). If writer formatting changes (e.g. `std::format`), `DxfQuantizeFloat` would diverge. Mitigation: comment cites MSVC guarantee; if formatting changes, extract `DxfFormatSixDecimals` helper used by both quantizer and emitter.
- **Centre not quantized** — residual `a vs b` drift of ~5e-7 possible if centre straddles rounding boundary. Accepted scope; documented in decision. If later found, extend `DxfEllipseToWrite` to include `cx`/`cy` via `worldX` round-trip.
- **Build-dir path mismatch** (Windows `C:/` cache vs WSL run) has blocked ctest in review. Mitigation: run all validation from `cmd.exe`/`powershell.exe` on Windows host, not WSL bash.
- **Rollback**: Revert single commit — `CadEllipse` and `.gs` untouched so no migration rollback needed; old DXF files remain importable.

## Open Questions

None — workspace facts, issue body, and precedents answer all scope questions. Proceed with direction 1. If reviewer prefers direction 2 (generalise to all entities) it is a deliberate architectural decision and should be filed as a follow-up, not folded into this bug fix per TASK-122’s DEBT-1 precedent.
