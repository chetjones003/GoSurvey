# TASK — Qt loads the CS-MAP dictionary at startup (GitHub issue #1083)

## Requirement authority

- **REQ-363** (accepted) — Online base map. A geolocated drawing shows USGS tiles under model space
  (Statement 1-3). Statement 7 lists the failures it may report; "dictionary not loaded" is not one
  of them while the dictionary ships beside the executable.
- **REQ-358** (accepted) — Coordinate-system zone. Statement 1 ships the dictionaries as installer
  payload. Statement 6 is the only case where they may be missing: they cannot be loaded.
- **ADR-063 (c)** — `src/geo/` is the only CS-MAP wrapper and reports a failed load instead of
  crashing.
- Constraints: REQ-201 (no silent failure), REQ-100 (no frame stall).

## Problem

In the Qt editor, `geo::LoadDictionaries` runs only when Drawing Settings or Project Settings is
opened. Until then every consumer of the zone fails with "The coordinate-system dictionary has not
been loaded." That includes the online map (`OnlineMapController`), `ResolveDrawingTransform` and
grid readouts. The online map's failure is also sticky: `geo_.Open` re-runs only on a location
change, so the map stays blank for that location for the rest of the session.

## Files / subsystems affected

- `src/qt/EditorWindow.hpp/.cpp` — one private `LoadCoordinateSystemDictionaries()` helper, called
  from the constructor.
- `src/qt/EditorDialogs.cpp`, `src/qt/EditorProjectSettings.cpp` — the two duplicated lazy loaders
  call the helper instead.
- `tests/QtEditorTests.cpp` — regression test.

## Implementation approach

Move the existing lazy-load block (resolve `resources/csmap`, `geo::LoadDictionaries`) into one
`EditorWindow` member, used by its three callers. The constructor loads it once, before any drawing
can open. A load that fails still leaves `geo::DictionaryError()` set, so REQ-358 item 6's disabled
Zone group and REQ-363 item 7's one message are unchanged. `LoadDictionaries` only sets CS-MAP's
directory and reads the category list, so startup cost is negligible.

With the dictionary loaded before any drawing opens, the map's first `geo_.Open` succeeds. The
sticky-failure path now only applies when the dictionary truly cannot be loaded, where retrying
cannot help either, so the controller is not changed.

## Test approach

- New Qt test: unload the dictionary (`LoadDictionaries` on a missing folder), construct an
  `EditorWindow`, and check that `geo::DictionariesLoaded()` is true and that a Texas Central drawing
  resolves its transform (`ResolveDrawingTransform(...).ok`).
- Existing Drawing Settings / Geolocation / online-map Qt tests still pass.
- Manual: a fresh session opens a geolocated DWG and USGS Imagery draws without opening Drawing
  Settings first.

## Architectural-boundary check

The resource-path lookup stays in the Qt layer (as today). `src/geo/` and `src/commands/` are
unchanged. No new dependency or abstraction: one helper replaces two copies.

## Status

Complete, pending review.

## Completion report

- **Build:** `build.bat` (Release) succeeded.
- **Tests:** full `GoSurveyQtTests` run on the hidden desktop: 233 passed, 0 failed, including the
  new `issue1083CoordinateSystemDictionaryLoadsWithTheWindow`.
- **Manual check:** fresh session → open a geolocated DWG → Geolocation ▸ Online Map ▸ USGS Imagery.
  The old "dictionary has not been loaded" message is gone. With a correct zone (`LA83-NF`), imagery
  draws aligned under the linework without Drawing Settings being opened first.
- **Assumptions:** loading at window construction is cheap (`CS_altdr` plus the category list).
- **Technical debt / follow-ups:** the online map's sticky `geoOk_` is left as is (see approach).
  Issue #1085 (zone details show the wrong system) was found here and filed separately. The viewport
  attribution text looked doubled in one capture; not investigated.
