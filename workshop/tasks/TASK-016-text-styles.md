# TASK-016 — Named text styles (REQ-044)

## Authority
- Requirement: **REQ-044** (accepted 2026-06-21) — named text styles, live reference with per-text
  overrides, "Standard" default, STYLE dialog + active dropdown, `.gs` persistence, old files unchanged.
- ADR: **ADR-020** — document-owned `TextStyle` table; bake-on-write resolution; additive `.gs` (no
  version bump); SHX oblique (faux for TTF).
- Constraints: REQ-200 (reproducible build), REQ-201 (no silent failures), REQ-300/301 (no new
  dependency/abstraction).

## Architectural-boundary check
- New Domain value type `TextStyle` (concrete, reused by model + paper text) — not an abstraction (§11.4).
- Table owned by the document (`DrawingDocument`/snapshot/`AppCommandState`), threaded like
  `drawingLayerTable`; active-style name on `AppCommandState` (settings pattern) — no new global.
- `.gs` change is additive with no `kGsFormatVersion` bump → old files still load (strict version check).
- No new dependency, no GL outside the renderer, layering preserved.

## Plan (incremental)
- **Phase 1 (this task):** data model + "Standard" + active-style dropdown + create path + `.gs`
  persistence + unit tests.
- Phase 2: STYLE management dialog (create/rename/delete/edit + `RebakeAllForStyle`).
- Phase 3: Properties per-text overrides + oblique rendering (SHX shear; faux TTF).

## Phase 1 — files touched
- `src/commands/CadEntities.hpp` — `TextStyle` struct; `CadAnnotation` gains `styleName`, `obliqueDeg`,
  `ovFont/ovHeight/ovOblique/ovBold/ovItalic`.
- `src/commands/TextStyle.hpp` (new) — pure helpers: `Find`, `EnsureStandard`, `IsStyleableText`,
  `RebakeAnnotation`, `Assign`, `RebakeAllForStyle`.
- `src/commands/CadCommands.hpp` — `textStyles` (+ `activeTextStyleName`) on `AppCommandState`,
  `DrawingDocument`, `DrawingGeometrySnapshot`; declare `ActiveTextStyle`, `SetActiveTextStyle`.
- `src/commands/CadCommands.cpp` — thread the table through Save/Restore document + undo snapshot;
  `EnsureStandard` in `SyncDrawingLayerTableWithGeometry`; `ActiveTextStyle`/`SetActiveTextStyle`;
  stamp the active style onto new TEXT (10748) and MTEXT (6935).
- `src/io/GsIo.cpp` — serialize/read `textStyles` + `activeTextStyleName` + the new annotation fields
  (tolerant, no version bump; missing table → synthesize "Standard").
- `resources/default-template.gs` — seed a "Standard" style.
- `src/ui/CadUi.cpp` — "Current text style" dropdown in the text settings (calls `SetActiveTextStyle`).
- `tests/TextStyleTests.cpp` (+ `CMakeLists.txt`) — pure resolution tests.

## Test approach
- `TextStyleTests` (Catch2): EnsureStandard idempotent; Find case-sensitivity; Assign bakes + clears
  overrides; edit re-bakes non-overridden, keeps overridden; other-style + legacy text untouched;
  dimensions ignored. Manual: dropdown selects active; new text adopts style font/height; old `.gs`
  loads unchanged; `.gs` round-trip preserves table + per-text style.

## Assumptions
- ASSUMPTION-1: new-text height follows the active style via the `SetActiveTextStyle` →
  `defaultPlottedTextHeightInches` sync (existing height plumbing reused), so no churn at the ~12
  render/measure sites. Risk-if-wrong: a custom typed height isn't marked an override until Phase 3, so
  a Phase-2 style edit could re-bake it; acceptable until overrides ship. Validate-by: Phase 3.

## Log
- 2026-06-21 — Phase 1 implemented; building + running tests.
- 2026-06-21 — Follow-ups from user review:
  - Moved the active-text-style dropdown into the Annotate ribbon (next to Text/Mtext).
  - Fixed TEXT hover/pick offset. Root cause (found via a temporary HOVERDBG log): TEXT rotation entry
    used the bearing convention (`MathAngleRadFromBearingCwNorthDeg`), so typing `0` → π/2 (north =
    vertical). `CadAnnotationRoughBounds` + the SHX renderer honor the π/2, but the TTF `AddText` path
    ignores rotation, so the glyph drew horizontal while its hit-box was rotated 90° → hover fired above
    the glyph. Switched TEXT rotation to standard (0 = horizontal, CCW positive) at the TEXT prompt, the
    active-command hint, and the Properties "Rotation °" field. Diagnostic removed.
  - Multi-select Properties still shows a read-only "rel. north" rotation summary for mixed annotation
    kinds (unchanged to avoid touching dimension semantics).
- 2026-06-21 — User clarified the intended convention: rotation is **bearing, clockwise from north**
  (0 = text runs up, 90 = reads left-to-right). So the "0 = horizontal" change above was reverted and
  the real defect fixed instead:
  - Restored the bearing convention consistently (TEXT prompt, active-command hint, Properties
    "Rotation ° CW from N"). Typing 0 and pressing Enter both = bearing 0 (north).
  - Fixed the actual root cause: the single-line text renderer now draws the glyph **unrotated then
    rotates its draw-list vertices about the insertion point** (top-left pivot, screen angle
    −rotationRad) — the same pivot `CadAnnotationRoughBounds` uses — so the visible glyph matches its
    hit-box for any rotation and either font (SHX or TTF). Previously TTF ignored rotation entirely.
  - The single-line TEXT selection/hover highlight is now a **rotated quad** (`annTextCorners`) so the
    box follows rotated text instead of staying axis-aligned.
- 2026-06-21 — TEXT rotation default set to bearing 90 (horizontal), AutoCAD-faithful; typed values keep
  0 = north.

## Phase 2 — STYLE management dialog (done)
- `STYLE` / `ST` command (kRegistry + DispatchByPrimary) and a "Manage styles…" button on the Annotate
  ribbon open a **Text Style Manager** window (`DrawTextStyleManagerWindow`, mirrors the Layer Manager).
- The window lists styles with: Current (radio → `SetActiveTextStyle`), Name (rename, except Standard),
  Font (free-text + common-font quick-pick combo), Height, Oblique°, Bold, Italic, and Delete.
- **Add** seeds the new style from the current style. **Rename** re-points referencing text + the active
  style to the new name (live reference). **Delete** is blocked for Standard and for in-use styles.
- **Editing any property re-bakes referencing, non-overridden text** via `TextStyles::RebakeAllForStyle`
  and re-syncs the new-text default height when the current style is edited (live reference, ADR-020).
- New window flag `showTextStyleManagerWindow`; drawn from `main.cpp`.
- Undo: Add/Rename/Delete push undo snapshots (pre-mutation). In-place property tweaks are not pushed to
  undo (the bound widget mutates in place; consistent with the existing Properties panel) — noted debt.
- Build clean; 52/52 tests green (pure `TextStyleTests` cover the resolve/re-bake logic the dialog uses).
- 2026-06-21 — AutoCAD-faithful UI redesign (user-requested from reference screenshots):
  - **Ribbon dropdown → thumbnail flyout**: a button opens a popup grid of cards, each rendering an
    "AaBb123" sample in the style's actual font (`DrawTextStyleSample`: SHX strokes via `Shx`, else TTF via
    `FontReg`), with the name below and the current style highlighted; a "Manage Text Styles…" item at the
    bottom opens the dialog.
  - **Text Style dialog → AutoCAD layout**: left Styles list + "All styles" (cosmetic) + live preview
    box; middle Font (Font Name combo, Font Style = Regular/Bold/Italic/Bold Italic → bold/italic),
    Size (Height), Effects (Oblique Angle); right Set Current / New… / Delete; footer Apply / Cancel.
    Unsupported AutoCAD effects (Use Big Font, Annotative, Upside down, Backwards, Vertical, Width Factor)
    are shown **disabled** for visual parity — not modeled (would be a separate REQ-044 amendment).
  - Edits remain live (re-bake on change); New… is a modal seeded from the selected style; Cancel closes
    (no transactional revert — noted debt).
