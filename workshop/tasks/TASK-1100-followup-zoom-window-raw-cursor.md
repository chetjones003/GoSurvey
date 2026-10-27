# TASK-1100-followup — ZOOM WINDOW corners use raw cursor coordinates

## Authority
- Requirement: accepted REQ-121 (ZOOM is excluded from object selection because ZOOM WINDOW corners are unsnapped) and REQ-122 (ZOOM WINDOW uses shared world-rectangle framing).
- Acceptance: with nearby geometry snapping enabled, both clicked corners remain at the raw viewport cursor positions and the rectangle is framed from them; ordinary snapped point-entry behavior is unchanged.
- Constraints: CON-06 minimal solution; follow the viewport/command boundary in `spec/architecture.md` and `spec/coding-standards.md`.

## Diagnosis
The existing Qt ZOOM WINDOW click route runs after `mousePressEvent` replaces `m_Cursor` with the OSNAP result. The first corner passes that snapped position to `BeginSelectionBoxCorner`; the second passes it to `SubmitViewportPick`, whose Zoom branch stores it as the opposite corner. The shared command branch already accepts coordinates, so the Qt viewport is the source of the mismatch.

## Files / subsystems
- `src/qt/EditorViewport.cpp`: preserve the pre-snap cursor coordinate for both ZOOM WINDOW click routes.
- `tests/QtEditorTests.cpp`: extend #1100 coverage with nearby geometry and enabled OSNAP, checking each corner against raw world coordinates.

## Implementation approach
Keep the fix local to the Qt viewport: retain the original cursor coordinate before snap adjustment, and use it only for the active Zoom selection-box corner clicks. Leave other command picks on their existing snapped path. No new abstraction or SPEC change.

## Test approach
Extend the existing Qt regression case: enable OSNAP, place clicks just off known line endpoints so `viewportSnapPickValid` confirms a nearby snap exists, calculate the raw click positions, and assert the stored first and queued second corners match those raw positions. Run the affected Qt test shard and the repository Release build/full test suite as required.

## Architectural boundary check
This remains a UI input-routing correction using the existing `BeginSelectionBoxCorner` and `SubmitViewportPick` APIs; no command ownership, state model, or framing algorithm changes.

## Verification plan
- Demonstrate the new assertions fail against current code before the fix.
- Build Release, run the focused Qt editor test and full CTest suite.
- Review the diff for scope and ensure no product documentation updates are needed; record review outcomes here before opening a PR.

## Implementation and verification results
- Fix: `EditorViewport::mousePressEvent` saves the raw cursor before OSNAP adjusts `m_Cursor`. The first Zoom corner passes raw X/Y and elevation to `BeginSelectionBoxCorner`; the second Zoom click passes raw X/Y to `SubmitViewportPick`. Other active commands continue using the snapped cursor.
- Regression proof: the focused Qt case failed before the fix at the first raw-corner assertion; after the fix it passed (3 QtTest results, 0 failed).
- Build: `build.bat` — PASS (Release, all frontend/test/helper targets).
- Full suite: `ctest --preset ninja-release` — PASS, 191/191, including six Qt editor shards, headless transcripts, `build.output-layout`, `qt.deployment`, and `qt.parity-ledger`.
- Architecture review: PASS — stays in the Qt viewport input-routing layer, no new dependency, state model, or abstraction.
- Code review: PASS — both ZOOM WINDOW corners use the pre-snap cursor; other command picks preserve existing behavior; test proves nearby geometry triggers OSNAP while each corner remains raw.
- Dependency audit: PASS — no dependency changes.
- Performance review: N/A — no hot-path work added beyond a local value copy on click; no algorithm or per-frame work changed.
- Product documentation review (REQ-453): `README.md` reviewed — no update needed; `PRIVACY.md` reviewed — no update needed; `resources/wiki/` reviewed — existing `Commands/zoomwindow.md`, `Known-Limitations.md`, and navigation text already say window corners ignore snaps; public GitHub manual wiki reviewed — `Views-and-Navigation.md` already states both corners use the unsnapped cursor, no update needed.
- UI screenshot review: N/A — the change corrects click coordinate routing and does not change a visible Qt surface.
- Outcome: PASS; ready for review.
