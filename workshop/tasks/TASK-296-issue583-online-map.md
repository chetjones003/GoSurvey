# TASK-296 — Online base map: the Geolocation tab's Map dropdown (USGS tiles under model space)

- Type:    feat
- Status:  review
- Opened:  2026-09-30
- Owner:   Workshop
- GitHub:  #583 (increment 1 of 2; REQ-364 Capture Area is increment 2)

## Requirement authority

REQ-363 (accepted 2026-09-30, D-2026-09-30-b), ADR-064; REQ-358 (zone), REQ-359 item 5 (the
disabled placeholder this replaces), REQ-360 (transformation), REQ-100 (frame budget), REQ-101
(local-storage invariant), REQ-201 (no silent failure), REQ-300 (no new dependency),
`spec/architecture.md` §8 (one-shot workers).

## Decisions taken before planning

Issue #583 left three questions open (SPEC GAP). **D-2026-09-30-b** (user, 2026-09-30): USGS The
National Map only (Esri priced and set aside, Bing dropped); the key question is moot; a captured map
is stored inside the drawing. Split into REQ-363 (this task) and REQ-364.

## Plan / architectural-boundary check

- **`src/geo/`** (pure): `WebMercator` — lon/lat ↔ mercator, tile boxes, level choice, the 64-tile
  cap. `CoordinateSystems`: `Wgs84GridConverter`, a zone + WGS 84 + both datum paths opened ONCE
  (the one-shot functions search the dictionary on every call).
- **Commands**: `DrawingSettings::onlineMap` (+ the `kOnlineMaps` table, storage names);
  `SetOnlineMap` (one undo step); Remove Location turns the map off in its own undo step;
  `DrawingWgs84Frame` (WGS 84 ↔ LOCAL, zone + REQ-360 transform resolved once, in
  `CadCommands_Geo.cpp` beside `TransformOf`); `CadOnlineMap` — `PlaceMapTile` (4 × 4 cell grid
  per tile), `OnlineMapMessageLatch` (item 7's single messages), `OnlineMapController` (plan the
  view's tiles, ask the service, collect, upload ≤ 4 / place ≤ 4 per frame, a loaded ancestor
  stands in for a missing tile, 256-texture LRU). GL-free: texture upload is injected.
- **Platform**: `MapTileService` — disk cache (LOCALAPPDATA\GoSurvey\MapTiles, pruned to 500 MB
  at start), injected fetch, `stb_image` decode; every tile a §8 one-shot worker, ≤ 6 alive.
  `HttpGetString` gains an optional HTTP-status out-parameter (404 ≠ offline). `stb_image`'s one
  compiled copy moves from `AppIcon.cpp` to `StbImage.cpp` in the domain library (the tests need it).
- **Render**: `RenderTuning::mapTiles`; drawn first with the PDF underlay's textured program,
  depth off; `CreateMapTileTexture` / `DeleteMapTileTexture` (GL stays in the renderer).
- **App**: one controller for the application (tiles shared across tabs), updated every frame,
  model space only; `cmd.onlineMapDrawing` drives the attribution.
- **UI**: the Map button (thumbnail + name + chevron) and its menu with thumbnails, Map Off last;
  the credit line in the viewport's lower-right corner. Capture Area stays disabled (REQ-364).
- **IO**: trailer JSON `drawingSettings.onlineMap` by name (absent / unknown → Map Off).
- **Icons**: `resources/icons/map_usgs_*.png` are real USGS tiles (public domain) of the UT campus,
  `map_off.png` is drawn.

## Tests

- `tests/OnlineMapTests.cpp` (`[req363]`, 11 cases): tile maths; cached converter vs the one-shot
  conversions and the NGS grid; AG9976's level-16 pixel placed within one pixel (in fact < 0.05 ft)
  with the transform off and with k = 0.9999, θ = 1°; the service's disk cache, 404, failure,
  not-an-image and prune; the message latch; Map Off / no location / paper space fetch nothing;
  fetch-place-draw and the 64-tile cap; offline = one message, no stall, news again after a
  success; 404 said once and not re-asked; an unknown zone said once; undo / trailer / per tab /
  Remove Location.
- `tests/LibreDwgCadTests.cpp`: the choice survives DWG save → reopen.
- Dev Shell `req363-online-map` (real network): Map menu → USGS Imagery → capture; zoomed-out
  capture; 600 frames of pan + orbit over new ground with p95 recorded; Map Off from the menu.

## Status log

- 2026-09-30: SPEC GAP raised and decided (D-2026-09-30-b); REQ-363 / REQ-364 / ADR-064 accepted
  (spec PR #592).
- 2026-09-30: implemented. Found while testing: (1) a tile whose fetch failed was never asked for
  again — the "ask only when the wanted list changes" check compared against a list that still held
  it; an answered tile now leaves the sent list. (2) The first tile service was a thread pool with a
  mutex queue, which §8 reserves for an architectural decision; rewritten as §8 one-shot workers, and
  ADR-064 (c)'s wording ("through a queue") corrected on the spec PR. (3) A Dev Shell capture showed
  holes that were the Test Engine's fast-forwarded sleep, not the app: its waits now use the wall clock.

## Completion report

- Build: `./dev/build` clean; `dev/build-devshell.bat` clean.
- Tests: `[req363]` 12 cases pass, 8 repeated runs clean. `ctest` 2003/2010 — the 7 failures are
  the headless ones already failing on `beta` (re-checked two: OFFSET line count, solid edge count).
  Dev Shell `req363-online-map` passes with the live USGS service; `req359-geolocation-tab` still
  passes.
- Acceptance (REQ-363):
  - dropdown with four thumbnailed items, button shows the choice; DWG save → reopen, per tab, UNDO ✔
  - Map Off / not geolocated: no request (fetch count 0) ✔
  - AG9976 within one level-16 pixel, transform off and on ✔ (miss < 0.05 ft, pixel ≈ 6.8 ft)
  - failing fetcher: no stall (every Update < 250 ms while failing), cached tiles still draw, one
    message until a success ✔
  - a cached tile draws with the network unavailable ✔ (service test)
  - pan + orbit with tiles streaming: p95 6.72 ms, max 7.45 ms (570 frames, reference machine) ✔
  - attribution exactly while tiles draw ✔ (`onlineMapDrawing` true with USGS Imagery, false after
    Map Off; drawn only in model space)
  - Visual: the 40 ft circle at AG9976's surveyed coordinate sits on the UT Tower in the imagery.
- Not verified by me: how the menu and credit line LOOK (hover, spacing) — handed to the user.
- Assumptions:
  - The disk cache lives in LOCALAPPDATA, not the roaming APPDATA `UserDataDirectory()`, so 500 MB
    of tiles never roams.
  - A failed tile is retried after 30 s; a 404 is remembered for the session.
  - Six fetches at once (a browser's per-host limit); a fetch times out at 10 s, which also bounds
    how long closing the app can wait for one in flight.
  - Datum: WGS 84 ↔ the zone's datum along CS-MAP's path (for NAD 83 zones CS-MAP's own WGS 84 ↔
    NAD 83 relation), well inside the ~2 m tile resolution.
- Debt: each fetch opens its own WinHTTP session (a TLS handshake per tile); reusing one would be
  faster but changes `HttpFetch`'s one-call shape — not needed for the budget.
- Docs: D-2026-09-30-b, REQ-363, REQ-364, ADR-064, REQ-359 item 5 note; this task.
