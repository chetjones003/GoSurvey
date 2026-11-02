# TASK-alignment-phase1-issue121 — Civil-style alignments, Phase 1 (foundation) with the full spec

- Type:    feature
- Status:  in review
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
- Out (later phases, REQ-486..502): layout editor/constraints, grips, REVERSEALIGNMENT command (the domain operation
  exists and is tested), OFFSETALIGNMENT, intersections, spirals, styles/labels/tables/reports, surface and feature-line
  integration, COGO commands, design criteria, dynamic-cursor readout, move/copy/rotate/scale of an alignment,
  interactive on-screen layout.

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

## 6. Known gaps (honest)
- No Qt screenshot has been captured/approved yet for the Prospector folder, Properties section and viewport display.
- Dynamic-cursor station/offset readout (REQ-501.2) is not implemented.
- `Properties` is read-only for alignments (editable fields are REQ-501.1; they arrive with the layout editor).
- AutoCAD-opens-without-Recover has not been verified with accoreconsole in this task.
