# TASK-1007 — Paper viewport controls (issue #1007)

- Type: feature
- Status: ready for issue closure with combined PR
- Opened: 2026-11-03
- GitHub issue: #1007

## 1. Authority

- Requirement: REQ-482 (accepted), plus REQ-027/029/030/031/113/201.
- Acceptance: REQ-482 Acceptance 1–5.
- Owning subsystems: Commands/Domain, Qt Renderer, Plot, DWG/DXF IO.

## 2. Scope

- In scope: enforce on/off, display lock, and hide-plot independently; persist all three through DWG and DXF.
- Out of scope: changing viewport clipping, layer plot settings, or model-space viewport behavior.
- Smallest change: use the existing `Viewport::on` and `Viewport::locked` fields; add one hide-plot value; apply those values at render, view-edit, and plot boundaries; use native VIEWPORT status bits 32768, 16384, and 2048 respectively.

## 3. Architectural boundary check

- A new field is required. No new dependency, layer, global state, or abstraction is planned. The exact persistence representation for hide-plot is subject to DWG/DXF format verification before implementation; if it requires an architectural/data-format choice not covered by REQ-482, stop and record a SPEC GAP.

## 4. Plan

- Approach: guard model-content rendering for off viewports; prevent camera/view changes inside a locked viewport while retaining paper-frame selection/editing; omit hide-plot viewports from PDF and applicable plot output; map persistence and legacy defaults through both codecs.
- Files/functions to inspect and likely touch: `src/commands/CadCommands.hpp/.cpp`, `src/qt/EditorViewport.cpp`, `src/io/PdfPlot.cpp`, `src/io/DxfIo.cpp`, `src/io/LibreDwgCad.cpp`, focused paper-space and codec tests, and command/UI controls as supported by existing patterns.
- Test approach: verify each control independently and in combination; verify locked camera changes are refused while frame edits work; verify onscreen/offscreen/plot differences; verify DWG/DXF save/reopen and default values for old files.
- Steps:
  - [x] Confirm native DWG/DXF hide-plot persistence semantics (VIEWPORT status group 90 bit 2048).
  - [x] Implement model, command/UI controls, rendering/edit/plot enforcement, and codecs.
  - [x] Add focused behavioral and save/reopen coverage.
  - [x] Run focused build/tests and record product compatibility limits.

## 5. Decisions

- 2026-11-03: User selected all three controls and DWG/DXF persistence.
- Repository evidence: `Viewport` already has `on` and `locked`; DXF group 90 and LibreDWG VIEWPORT `status_flag` carry those flags. Status group 90 also has native bit 2048 for hide-plot. GoSurvey does not consistently enforce the existing flags or currently read/write bit 2048.
- Product evidence: AutoCAD 2025 accoreconsole exposes MVIEW ON/OFF and Lock. Civil 3D is unavailable and Autodesk Help exposed only a dynamic shell. The native status bit resolves the DWG/DXF storage representation without relying on those unavailable product surfaces.

## 6. Verification review

- Verdict: APPROVE.
- Architecture: ownership remains in Commands/Domain, Qt Renderer, Plot, and IO. No new abstraction/dependency or file-format extension is needed; all three controls use native VIEWPORT status flags.
- Risks: inconsistent enforcement across rendering and camera controls; PDF visibility diverging from editor; loss or corruption of hide-plot through one codec; external CAD interoperability of nonstandard metadata.
- Required tests: all three independent toggles, combined values, locked pan/zoom/scale refusal, frame edit still allowed, on/off rendering, hide-plot PDF omission, old-file defaults, and DWG/DXF round-trip including combination states.
- Persistence representation verified; Workshop implementation may proceed.

## 7. Implementation log

- 2026-11-03: User confirmed all three controls and DWG/DXF save/reopen.
- 2026-11-03: Added REQ-482 and D-2026-11-03-h; initial plan and verification recorded.
- 2026-11-03: Implemented independent viewport on/off, display lock, and hide-plot controls in the viewport manager; enforced off/lock/hide-plot in paper rendering, floating camera controls, command scale changes, and PDF output; mapped native VIEWPORT status flags and GoSurvey DWG payload values.
- 2026-11-03: Added focused domain/UI/plot and DWG/DXF round-trip checks for the combined control state. Round-trip coverage explicitly verifies off, locked, and hide-plot values in both formats.
- 2026-11-03: Release build passed. Focused Catch2 run `[req489],[req482],[req490]`: 213 assertions in 15 cases passed, including DWG/DXF round-trip with on=false, locked=true, and hide-plot=true.
- 2026-11-03: Qt editor shard 0 passed after correcting its undo assertions. The viewport-controls screenshot is `build/artifacts/qt-editor/manual/shard-0/artifacts/qt-1007-viewport-controls.png`. Civil 3D comparison is inconclusive because Civil 3D is unavailable on this machine.

## 8. Outcome

- Requirements satisfied: REQ-482 implementation and focused coverage complete.
- Tests: Release build passed; Qt editor shard 0 passed; focused Catch2 suite passed (213 assertions in 15 cases).
- Documentation: product documentation review required before the combined PR.
- Done: ready for closure with the combined PR.
