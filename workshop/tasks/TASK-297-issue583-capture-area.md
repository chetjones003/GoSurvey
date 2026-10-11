# TASK-297 — Capture Area: keep a piece of the online map inside the drawing

- Type:    feat
- Status:  review
- Opened:  2026-09-30
- Owner:   Workshop
- GitHub:  #583 (increment 2 of 2; builds on TASK-296 / PR #593)

## Requirement authority

REQ-364 (accepted 2026-09-30, D-2026-09-30-b: stored inside the drawing), ADR-064 (e); REQ-363
(placement, the tile pipeline), ADR-044 (trailer), REQ-201.

## Plan / architectural-boundary check

- **Commands (data)**: `DrawingSettings::capturedAreas` — `CapturedArea {map, level, tiles}`,
  `CapturedTile {x, y, image}`; the image is a `shared_ptr<const std::string>` (immutable: undo
  snapshots copy a pointer, ADR-064 (e) / §8). Rides the existing undo / per-tab / trailer paths.
- **Commands (verbs)**: `StartCaptureMapArea(pick)` — a new `Kind::GeoCaptureArea` in the geo
  command family: Pick Area takes two corners (`geoCmdPhase` WaitFirst / WaitSecond), then
  `Capturing`; Capture Area starts in `Capturing` with the visible area. `CommitMapCapture` (one
  undo step) / `FailMapCapture` end it; `RemoveCapturedMapAreas` is one undo step. Wired where
  every geo command is: click route (`SnappedPointPick`), dynamic input (corners only), prompts,
  hint line, cancel message.
- **The controller gathers the capture** (it has the displayed level, the placement and the tiles):
  plans the level's tiles over the area (refused over 256), takes them from loaded tiles, a
  captured area, or the service (a capture does not wait out a retry delay); the first failed tile
  ends it keeping nothing; a 404 tile is simply absent. Progress is written to
  `mapCapture.prompt`.
- **Drawing**: captured tiles are requested with their image attached (`MapTileRequest::image`) —
  the worker only decodes, never the disk cache or network — and drawn after the live map, with the
  map on or off. Map Off releases every texture except the captured ones.
- **UI**: split button — icon half = Capture Area; label half (or the whole button with Map Off) =
  menu: Capture Area, Pick Area, Remove Captured Areas; disabled with Map Off and nothing captured.
- **IO**: trailer JSON `drawingSettings.capturedAreas` (map by name, level, tiles with base64
  images; a private encoder/decoder in `GsIo.cpp`, its only user). A damaged tile is dropped and said.

## Tests

- `tests/OnlineMapTests.cpp` `[req364]` (5 cases): visible capture keeps the view's tiles as served
  → trailer → reopen with Map Off and no network → draws in the same place (the placed mesh equals
  `PlaceMapTile`'s), 0 fetches; Pick Area keeps only the picked rectangle's tiles, a degenerate
  corner is refused; > 256 refused; a failed fetch keeps nothing and says why; Esc keeps nothing;
  Map Off refuses; UNDO per capture; Remove is one undo step; per tab; a damaged base64 tile is
  dropped and said, the rest round-trips binary-safe.
- `tests/LibreDwgCadTests.cpp`: captured areas survive DWG save → reopen with every byte value.
- Dev Shell `req364-capture-area` (live USGS): the split button's icon half captures; Map Off
  still draws it (screenshot); Pick Area from the menu with typed corners; Remove Captured Areas;
  UNDO.

## Status log

- 2026-09-30: implemented on `feat/issue583-capture-area` (stacked on PR #593).

## Completion report

- Build: `./dev/build` clean; `dev/build-devshell.bat` clean.
- Tests: `[req363],[req364]` 18 cases pass, 6 repeated runs clean. `ctest` 2009/2016 — the same 7
  headless failures already failing on `beta`. Dev Shell `req364-capture-area`, `req363-online-map`
  and `req359-geolocation-tab` pass.
- Acceptance (REQ-364):
  - Capture Area stores the visible area's tiles; after save → reopen with the network unavailable
    and Map Off, the captured area draws in the same place ✔
  - Pick Area stores only the tiles covering the picked rectangle ✔
  - more than 256 tiles refused, a failed fetch stores nothing, each with a message ✔
  - UNDO removes a capture; Remove Captured Areas removes all in one undo step ✔
  - Capture Area and Pick Area disabled with Map Off ✔ (menu items disabled; the command refuses)
- Not verified by me: how the split button and its menu LOOK — handed to the user.
- Assumptions:
  - A 404 tile inside the area is left out, not a failure (there is nothing there to keep); an area
    with no tiles at all keeps nothing and says so.
  - Pick Area uses the level the view is showing, even when the rectangle is outside the view.
  - Captured areas need the drawing's location to be placed: a drawing whose location is removed
    keeps them but does not draw them.
- Debt: none new.
- Docs: REQ-364 status; this task.
