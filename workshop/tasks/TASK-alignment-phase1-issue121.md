# TASK-alignment-phase1-issue121 — Civil-style alignments, Phases 1 and 2 with the full spec

- Type:    feature
- Status:  in progress (PR #1152)
- Opened:  2026-11-02
- Owner:   chetjones003 (decision), Claude (implementation)
- GitHub:  #121 "Implement Civil 3D-Style Alignments"
- Branch:  feat/implement-issue-121 (from origin/beta)

## 1. Authority
- Goal: Civil/survey design objects (spec/project.md); decision D-2026-11-02-b; ADR-075.
- Requirements: REQ-480..503 (accepted 2026-11-02 by the user). Phase 1 implements REQ-480, 481 (tangent + circular
  curve), 483, 484, 485, 490 (Endpoint/Midpoint/Center/PI/PC/PT/Nearest), 495 (viewport render), 496 (DWG-embedded
  persistence + ordinary LINE/ARC export), 499, 501 (Properties, LIST; cursor readout is not yet done — see §6), 503.
- Acceptance: each REQ's Acceptance list; Phase 1 evidence is in §5.
- Owning subsystem: Domain (`src/alignment`), Commands (`CadCommands_Alignment.cpp`), IO (`GsIo.cpp`, `LibreDwgCad.cpp`),
  Renderer (`ViewportRenderer.cpp`), Qt (`EditorProperties.cpp`, `EditorWindow.cpp`).

## 2. Scope
- In: pure domain module; per-drawing store with undo; typed-argument commands; create from selected objects;
  station formatting/parsing and station equations; station/offset queries; selection, highlight, erase; snaps;
  Properties; Prospector folder; persistence; in-app wiki.
- Phase 2 (REQ-486..489, delivered): constraint solver (Fixed / FloatOnPrev / FloatOnNext / Free), parameter locks,
  insert/delete, ALIGNMENTEDIT/LOCK/INSERT/REMOVE, EDITALIGNMENT layout table (Qt), grips (start, tangent end, curve
  radius), MOVE/COPY/ROTATE/SCALE/MIRROR/ERASE, REVERSEALIGNMENT, ALIGNMENTSTATION, OFFSETALIGNMENT/ALIGNMENTDETACH
  with linked refresh, ALIGNMENTINTERSECT.
- Out (later phases, REQ-492..502): labels/tables/reports, surface and
  feature-line integration, COGO commands, design criteria, interactive on-screen layout toolbar.
- REQ-491 (partial, command-line + viewport): drawing-owned `AlignmentStyle` table (tangent/curve/spiral
  components), `ALIGNMENTSTYLE` command, JSON persistence, per-entity viewport styling via
  `SampleEntityForDisplay`, tests `[req491]`. Not yet: station ticks, geometry markers, label visibility,
  direction arrows, design tab (REQ-502), Qt style editor dialog.
- Phase 3 (in progress on `feat/implement-issue-121`): REQ-481 clothoid spirals (`Clothoid.cpp`,
  `AppendSpiral`, station/offset queries, persistence `k:S`), REQ-482 C2 continuity + requirement level,
  offset refusal for spiral parents, LIST/EDITALIGNMENT spiral rows, **ALIGNMENTADD/INSERT/EDIT** spiral
  syntax, DWG export samples spirals to LINE segments, command tests (`AlignmentSpiralCommandTests.cpp`).
  Spiral grips (end = length, mid = end radius), `ALIGNMENTCONTINUITY`, Properties continuity fields,
  in-repo wiki (`Command-Reference`, `alignmentadd/edit/insert/continuity`).
  Verification: `[alignment]` 70/70; full suite 194/194 (`build/artifacts/ctest-phase3.log`).
  REQ-490 follow-up: spiral junction geometry points (`77050294`); alignment Quadrant on cardinal
  tangents/spirals, not curves (PR #1152, `b1664acf`).
  REQ-490 Station snap: typed `STATION 12+45.32` at point prompts + OSNAP Station with latched value.
  Verification: `[alignment]` 76/76 (includes `[req491]`).

## 3. Architectural boundary check
- [x] Matches ADR-075: dedicated domain object, exact parameters, per-drawing store, trailer authoritative, no Z.
- New dependency: none. New global state: none.

## 4. Questions / decisions
- Reverse clears station equations (their mirrored meaning is an open question, REQ-503 item 1).
- Offset beyond an end = signed distance to that end point (documented in the header).

## 5. Verification evidence
- `GoSurveyTests [alignment]`: tangent, curve closed forms, left/right, compound/reverse, station<->XY round trips,
  state-plane precision, tiny/huge radii, equations (increasing/decreasing/multiple/boundaries), formatting,
  continuity, reverse twice = identity, create-from-objects (lines, arcs, bulge polylines, gaps, fillets, pick/R),
  commands, persistence (bit-exact doubles), undo, pick/box/list/erase, snaps.
- Phase 2: `GoSurveyTests [alignment-edit]` (solver, locks, insert/remove, offset refresh, intersect); Qt editor tests
  cover EDITALIGNMENT dialog wiring; `ctest --preset ninja-release` — 194/194 passed (2026-11-02,
  `build/artifacts/ctest-full2.log`).
- Build: `.\build.bat` (release) from this worktree.

## 7. Pre-PR product documentation (REQ-453)
- `README.md`: no update needed (already mentions alignments at a high level).
- `PRIVACY.md`: no update needed (no privacy impact).
- `resources/wiki/`: updated `Alignments.md`, `Command-Reference.md`, `alignmentadd/edit/insert/continuity` command pages (Phase 3 spirals and continuity).
- Public GitHub wiki: not updated in this session (same content as in-repo wiki pages; update before external publish if required).

## 6. Known gaps (honest)
- No Qt screenshot has been captured/approved yet for the Prospector folder, Properties section and viewport display.
- The cursor readout is a status-bar readout (REQ-501.2 asks for the dynamic cursor itself).
- The Alignment Layout table has no insert/delete buttons; ALIGNMENTINSERT / ALIGNMENTREMOVE do that.
- Station-equation reversal is cleared, not mirrored (open question, REQ-503).
- `Properties` is read-only for alignments (editable fields are REQ-501.1; they arrive with the layout editor).
- AutoCAD-opens-without-Recover has not been verified with accoreconsole in this task.
