# TASK-1084 — Qt: show the REQ-350 Pipe Fittings palette

- Type:    bug
- Status:  submitted
- Opened:  2026-10-24
- Owner:   Workshop
- GitHub:  #1084 (labels: bug, QT-migration)

## 1. Authority

- Goal:         the Qt editor reaches parity with the retired ImGui frontend (#966 removed it).
- Requirements: REQ-350 (accepted) — Pipe fitting tool palette; ADR-062 (part thumbnails).
- Constraints:  REQ-100 (a part is rendered once and cached), REQ-201 (a refusal names the part),
  architecture invariant §11.6 (no `gl*` call outside Renderer/Platform).
- Acceptance (REQ-350, restated):
  - Starting `PIPERUN` opens the palette by itself, as a floating window that can be moved and
    resized, without blocking the command line.
  - A `2in` run's **Flanges** tab lists `2IN_BLIND_FLANGE` and `2in_WELD_NECK_FLANGE` and not
    `CJ_4in_WELD_NECK_FLANGE`; a `4in` run lists the 4in part and not the 2in ones, re-filtered live.
  - Every listed row shows a shaded preview with ports marked, and the part's name beside it.
  - Picking a row then clicking **on** the run splices (one undo step); **off** any run places a
    block INSERT.
  - The palette stays open after the run is committed or cancelled; its close box closes it; the
    reopen command shows it again with its position kept.
  - An empty category shows a sentence naming the filtered size.
  - (Nozzle round trip, REQ-100 budget: already delivered by the domain layer, unchanged here.)
- Owning subsystem: Qt frontend (`src/qt/`) + Renderer (one new read-back entry point).

## 2. Scope

- In scope: the missing Qt window. `AppCommandState::pipeFittingPaletteOpen` was set by `PIPERUN` /
  `PIPEPALETTE` but nothing in `src/qt` read it (found: `grep -rn pipeFittingPaletteOpen src/qt`).
- Out of scope: the PIPECATALOG "no elbow-90 found" log for a 4in run (existing, intended behaviour:
  PIPERUN draws a smooth bend when the library has no part); a ribbon button for PIPEPALETTE (the
  spec names a *command* as the reopen path); any change to filtering, arming or placement rules
  (all already in `CadBlocks.cpp`, unchanged).
- Smallest change: one Qt palette window in a new `src/qt/EditorPipePalette.cpp`, driven by the
  existing domain functions (`CadPipePaletteCollectRows`, `CadPipePaletteEmptyReason`,
  `CadPipePaletteArmPart`), plus a pixel read-back on the renderer so the palette can show the
  thumbnails it already renders.

## 3. Architectural boundary check

- New abstraction / layer / dependency / global state / data-format change?
  - [x] No — proceed. No new dependency, no persistence-format change, no new global; the window
    position uses `QSettings` like the main window's geometry.

## 4. Questions

None. REQ-350's ImGui-specific wording (ini file, `PaletteTabButton`, `BeditVerticalText`,
`ImGui::Image`) is historical since #966; each maps one-to-one onto a Qt equivalent (below), so this
is not a SPEC GAP.

## 5. Assumptions

```
ASSUMPTION-1: ADR-062 (a) says the palette "passes the texture id to ImGui::Image". A Qt palette is a
  separate top-level window with its own GL context, so it cannot sample the renderer's texture.
- Because:       ADR-062 was written for the single-context ImGui frontend.
- Decision:      the renderer still renders each part once into its own cached FBO; a new
                 `ReadPartThumbnailPixels` copies the 64x64 result to a QImage once, and the palette
                 shows the QImage. The ADR's invariants hold (renderer owns all GL, no gl* in the
                 palette, once-per-part cache, name-only fallback without a GL context).
- Risk if wrong: a reviewer wants the ADR text amended; no behaviour changes.
- Validate by:   code review.

ASSUMPTION-2: "tabs on the right, vertical text" (REQ-350 (b)) = QTabWidget with East tab position.
ASSUMPTION-3: "open state and position persist like every other palette window" — open state is
  session state (as `blockAuthoringPaletteOpen`); position/size persist in QSettings
  (`editor/pipeFittingPaletteGeometry`).
```

## 6. Plan

- `ViewportRenderer::ReadPartThumbnailPixels(key, rgba, px)` — read-back of a cached thumbnail.
- `src/qt/EditorPipePalette.{hpp,cpp}`:
  - `ServicePipeFittingThumbnails` — port of the ImGui thumbnail service (scratch import, tessellate,
    port markers, 4 per paint) that stores QImages;
  - `EditorWindow::ShowPipeFittingPalette` / `RefreshPipeFittingPalette` — the floating dialog.
- `EditorViewport`: owns the `PartThumbnailImages` map; services requests at the end of `paintGL`.
- `EditorDialogs.cpp` `ServiceUiRequests`: shows / closes the dialog from `pipeFittingPaletteOpen`
  (the same pattern as the Block Authoring palette).
- Tests (`tests/QtEditorTests.cpp`): see §11.
- Screenshots of the palette states for user approval before commit (CLAUDE.md §3.6).

## 7. Workflow-specific notes

- Bug: root cause = the ImGui window (`CadUi_PipeFittingPalette.cpp`) was deleted in #971 and never
  re-created in Qt; the flag it read is still set by the commands.
- Regression test fails before the fix: yes — without the window `findChild("pipeFittingPalette")`
  is null.

## 8. Implementation log

- 2026-10-24 branch `fix/qt-pipe-fittings-palette` from `beta`; implemented as planned.
- 2026-10-24 finding: a 4in run listed nothing. The bundled startup sweep imports
  `fittings/CJ_4in_WELD_NECK_FLANGE.sat` as an untyped block (its `.json` sidecar was ignored on
  import), and `CadBlocksCollectLibraryEntries` then skips the sidecar because the name is already in
  the drawing. Fixed in `ImportCadBlocksFromPathImpl`: a part that arrives with no piping metadata
  takes its same-stem sidecar's (one shared `ReadFittingSidecar`, also used by the library scan).
  Needed for REQ-350's "a 4in run lists the 4in part".

## 9. Self-verification

- [x] build-project        — PASS (`.\build.bat`, Release, all targets)
- [x] architecture-review  — PASS (no new dependency/global/format; ASSUMPTION-1 noted)
- [x] code-review          — PASS
- [x] dependency-audit     — n/a (none added)
- [x] performance-review   — PASS (each part rendered once, 4 per paint, only the shown tab; library read cached)
- [x] testing              — PASS: `ctest --preset ninja-release` 191/191

## 10. Verification result

- Verdict: PASS (screenshots shown to the user; PR requested)

## 11. Outcome

- Requirements satisfied: REQ-350 in the Qt editor (Acceptance met: yes, see PR)
- Tests added: `QtEditorTests` `issue1084PipeRunOpensPaletteWithSizeMatchedParts`,
  `issue1084PaletteRowsShowShadedPicturesAndArmThePart`, `qt1084PipeFittingPaletteScreenshots`;
  `CadPipePaletteTests` `[issue1084]` (sidecar kept on the bundled sweep)
- Docs updated: this task file
- Done: 2026-10-24
