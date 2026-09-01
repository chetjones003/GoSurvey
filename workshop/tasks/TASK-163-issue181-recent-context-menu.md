# TASK-163 — Start screen: right-click context menu for Recent Drawings (GitHub issue #181)

## Requirement authority
- REQ-308 (Start screen tab) — accepted. Statement already provides for a missing recent
  entry being "offered for removal from the list". This task adds the management surface for
  that (and adjacent conveniences) as a right-click context menu. Priority "should".
- No architectural decision: the Explorer shim lives in `CadUi_StartScreen.cpp` beside the
  existing `%APPDATA%` / wall-clock / `ShellExecuteA` glue, exactly as REQ-308's Owner-layer
  note prescribes. The pure `RecentDrawings` module gains only a `Clear()` peer of `Remove()`.

## Files / subsystems affected
- `src/io/RecentDrawings.{hpp,cpp}` — add `recent::Clear()`.
- `src/ui/CadUi_StartScreen.cpp` — context menu in both grid and list loops; `RevealInFileManager`
  shim; `InvalidateAllThumbTex`; `ClearRecentDrawings()`; confirm modal.
- `src/ui/CadUi.hpp` — declare `ClearRecentDrawings()`.
- `tests/RecentDrawingsTests.cpp` — cover `Clear()`.

## Implementation approach
- `ImGui::BeginPopupContextItem("##ctx")` under the existing `PushID(e.path)` in each loop,
  attached to the entry's `InvisibleButton`. Actions: Open, Open Containing Folder, Copy Full
  Path, Remove From List, Clear All Recent….
- Open + Open Containing Folder disabled when the file is missing (`std::filesystem::exists`).
- Store-mutating actions are deferred to after `EndChild()` (capture `path` by value), like the
  existing `clickedPath` handling — avoids iterator invalidation of `entries`.
- Copy/Reveal happen inline (they do not touch the store).
- Clear All opens a confirm modal (`BeginPopupModal`) rendered after `EndChild()`.

## Test approach
- Unit: `recent::Clear()` empties a populated store and is a no-op-safe on a missing one;
  `Note()` still works afterwards.
- Manual (GUI, not automatable): right-click in grid and list view, exercise each item, confirm
  no crash when Remove/Clear mutate the list. Hand visual check to the user.

## Architectural-boundary check
- Renderer/Platform boundary intact: `ShellExecuteW` stays in the UI glue file. No new dependency.
