# TASK-032 — RECT command, DXF polyline export, and ORTHO for direct-distance + grips

- Type:    feature + bug
- Status:  done
- Opened:  2026-08-11
- Owner:   chetjones003

## 1. Authority
- Goal:         GOAL-01 (a CAD product usable for real survey deliverables)
- Requirements: REQ-053 (RECT + polylines survive a DXF/DWG save) — accepted 2026-08-11;
                REQ-047 (ORTHO), revised 2026-08-11 to cover direct-distance entry and grip editing;
                REQ-052 (DWG save, which inherits the DXF polyline fix);
                REQ-201 (no silent failures)
- Constraints:  CON-07 (build reproducibility); REQ-300 (no new dependency);
                REQ-301 (no new abstraction without two present-day uses)
- Acceptance:   restated verbatim in `spec/requirements.md` under REQ-053 and REQ-047
- Owning subsystem: Commands (command + commit), IO (`DxfIo` + `DxfEntityEmit`),
                Viewport (`CadSnap`, `CadRubberPreview`, `TransformPreview`), UI (`CadUi` grips, menu)

## 2. Scope
Seven user-reported items, addressed as one unit because five of them meet at the same
two places — the ORTHO constraint and the polyline representation.

- In scope:
  1. Typed distance under ORTHO drew only to the right (bug, REQ-047).
  2. ROTATE preview appeared to scale the objects (bug — see §7, partially unresolved).
  3. Grips ignored ORTHO (bug, REQ-047).
  4. Select similar unreachable from the right-click menu.
  5. RECT command, writeable to DXF/DWG (REQ-053).
  6. Geometric-centre snap on rectangles and joined polylines.
  7. Typed distance while a grip is armed (REQ-047).
- Out of scope: a distinct `CadRect` entity type; rotated (non-axis-aligned) rectangles;
  RECT's AutoCAD sub-options (Chamfer/Fillet/Width/Elevation/Thickness/Area/Dimensions/Rotation);
  DXF export of arcs and ellipses, which are also missing (see DEBT-1).
- Smallest change: represent a rectangle as the closed polyline the system already has, and fix the
  ORTHO paths in place rather than adding a constraint layer.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership change / global / data-format change?
    - [x] No — proceed.
- Reasoning:
  - RECT introduces **no entity type**: a rectangle is a 4-vertex closed polyline, so storage, `.gs`,
    render, selection, grips, snaps and modify commands are untouched. This was the user's explicit
    choice over a `CadRect` type, which *would* have been architectural.
  - `DxfAppendLwPolylineRecord` follows the existing `DxfAppendTextRecord` precedent in the same pure
    header — an addition to an established pattern, not a new abstraction.
  - `ApplyEntityGripPoint` **removes** duplication rather than adding a layer: the grip-geometry switch
    existed once in `CadUi.cpp`; it now exists once in `CadCommands.cpp` with two callers (the mouse
    drag and the typed distance), satisfying REQ-301's two-concrete-uses rule.
  - The DXF **output** gains LWPOLYLINE records. This is not a data-format change to GoSurvey's own
    storage; it is the exporter finally writing an entity it always held.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Issue 2 (rotate preview scaling) — which object types? | 2026-08-11 | "Lines / polylines / arcs only" — ruled out the leading MTEXT hypothesis. See §7. |
| Q2 | How should a rectangle be stored — closed polyline or a new `CadRect` entity? | 2026-08-11 | Closed polyline (the recommended option). |
| Q3 | How far should typed grip input go — ortho distance only, or full point entry? | 2026-08-11 | Distance along the ortho direction. |

## 5. Assumptions
```
ASSUMPTION-1: A typed grip distance should APPLY AND END the stretch, not pin the grip
              and wait for a confirming click.
- Because:       Q3 fixed the input vocabulary but not the commit moment.
- Risk if wrong: an extra click is needed to finish; trivially reversible.
- Validate by:   AutoCAD grip behaviour (type a distance, press Enter, the stretch completes).
                 Confirm on first user run.

ASSUMPTION-2: ORTHO must NOT constrain the RECT rubber band.
- Because:       REQ-047 says ORTHO constrains draft points; RECT's second corner is a draft point.
- Risk if wrong: none observed — applying it would collapse the rectangle to a line, and the shape
                 is already axis-aligned, which is the whole purpose of ORTHO.
- Validate by:   user run.

ASSUMPTION-3: The coincident-endpoint tolerance for "joined" polylines is 1e-6 of the polyline's own
              bounding-box diagonal (floor 1e-9).
- Because:       REQ-053 does not fix a tolerance for "drawn back onto its start point".
- Risk if wrong: a polyline closed by hand within a hair of its start offers no geometric centre, or
                 a near-closed one offers a slightly-off centre.
- Validate by:   relative to the polyline's extent, so it holds at survey scale and at millimetre
                 scale alike. Revisit if a user reports a missing centre snap.
```

## 6. Plan
- Approach: fix the two ORTHO defects at their real cause (a coordinate-frame mismatch and a missing
  constraint), make Select similar reachable, then add RECT on top of the existing closed polyline and
  close the export gap that discovery exposed.
- Files/functions touched:
  - `commands/OrthoConstrain.hpp` — `OrthoUnitTowardPoint` moved in (header-only ⇒ testable), with the
    same-frame requirement documented at the definition.
  - `commands/CadCommands.cpp/.hpp` — `OrthoUnitTowardUiCursorFromAnchor` (converts world crosshair →
    local); `ApplyEntityGripPoint`; `StartRectCommand` / `CommitRectangle` / `ResetRectDraft`;
    `Kind::Rect` + `RectPhase`; grip typed-distance branch in `ProcessCommandLineSubmit`;
    grip anchor in `TryBeginEntityGripAtLocal`; registry + dispatch entries.
  - `ui/CadUi.cpp` — ORTHO on the grip drag; grip anchor + pre-drag undo snapshot on arm;
    Select similar closes its popup.
  - `io/UserPrefs.cpp` — `prefsSchemaVersion` migration for the right-click Edit Mode default.
  - `io/DxfEntityEmit.hpp` — `DxfLwPolylineRecord` + `DxfAppendLwPolylineRecord`.
  - `io/DxfIo.cpp` — the polyline export branch and its count in the summary log.
  - `viewport/CadSnap.cpp` — `PolylineBoundsArea` (closed flag **or** coincident endpoints).
  - `viewport/CadRubberPreview.cpp` — RECT rubber band.
  - `viewport/TransformPreview.cpp/.hpp` — view-adaptive tessellation for previewed arcs/ellipses.
- Test approach:
  - happy path: `OrthoConstrainTests` — the ortho axis resolves +X/−X/+Y/−Y from the crosshair;
    `DxfEntityEmitTests` — an LWPOLYLINE carries group 90 = true count before group 70, the closed flag,
    and every vertex in order.
  - failure mode: `OrthoConstrainTests` — a crosshair sitting on the anchor yields no direction, and a
    world-frame crosshair is pinned to +X (the defect, pinned so it cannot return);
    `DxfEntityEmitTests` — a vertex-less record emits nothing rather than a 90-of-zero, and group 440
    is omitted when the entity is opaque.

## 7. Findings, and what is NOT resolved

**Item 1 — typed ORTHO distance always drew right. Root cause found and fixed.**
`OrthoUnitTowardPoint(st.anchorX, st.anchorY, st.uiCursorWorldX, st.uiCursorWorldY, …)` compared a
**world**-space crosshair with a **local**-space anchor. Storage is local (world = local +
`worldDocumentOrigin`), so on any drawing with a real document origin — every Civil 3D import — the
origin was added to `dx` alone. `|dx|` then always dominated `|dy|` and was always positive, so the
axis collapsed to +X. It worked on a fresh drawing at the origin, which is why it survived. Both call
sites (LINE and POLYLINE) now convert the crosshair to local first.

**Item 2 — ROTATE preview appears to scale. PARTIALLY resolved; the reported symptom is unconfirmed.**
The preview's rotation is provably rigid: `rotatePreviewPt` is a plain rotation about the base applied
to source vertices each frame, and the commit path (`RotateAroundBase`) applies the identical transform
with the identical angle convention. Reading it end to end found no way for straight lines or polylines
to change size. One genuine preview-vs-object size discrepancy **was** found and fixed: previewed arcs
and ellipses were tessellated at a **fixed** 48 / 56 segments while the renderer tessellates committed
curves adaptively for the zoom, so a previewed arc's chord polygon cut visibly inside the true curve —
the preview reads as a shrunken copy, and worsens as you zoom in. The preview now uses the renderer's
segment count. **This may not be the reported effect.** A leading hypothesis (MTEXT preview inflating
its box to the axis-aligned bounds of the rotated corners — up to 1.41x at 45°) was ruled out by Q1.
Needs a user repro; tracked as DEBT-3. A real defect found in that hypothesis is recorded as DEBT-2.

**Item 4 — Select similar already existed.** `SelectSimilarToCurrentSelection` was wired into the
viewport shortcut menu. It was unreachable because `rightClickEditMode` shipped as `RepeatLastCommand`,
so right-clicking with a selection repeated the last command instead of opening the menu. The user's
saved profile (`%APPDATA%\GoSurvey\gosurvey-user.json`) held `rightClickEditMode: 0`, so changing the
compiled default alone would not have reached them — hence the `prefsSchemaVersion` migration.

**Item 5 — the export gap.** `ExportDxfFile_Impl` emitted LINE, CIRCLE, POINT, MTEXT and HATCH only.
Polylines were **never written to DXF**, so they were silently absent from every export and every DWG
save (which converts from the DXF). A RECT would have been undeliverable. Fixed as part of REQ-053.

**Item 6 — geometric centre.** The snap existed but required the explicit closed flag, so a polyline
drawn back onto its own start point offered none. Now either condition qualifies (ASSUMPTION-3).

## 7b. Second user review (2026-08-11) — three follow-ups

**A. Select similar matched object type only.** Now matches **type AND layer AND colour**
(REQ-054, recorded as a SPEC GAP — neither the command nor the right-click menu had a requirement).
Layer/colour compare case-insensitively with `""` meaning layer `0` / colour `ByLayer`, so entities
differing only in spelling still match. Annotations still narrow further by annotation kind. The log
now names the layer and colour it matched on.

**B. "RECT doesn't seem finished" — correct; the first pass wired only the command-line path.**
An audit against what every other draw command touches found five missing integration points, all now
done:
  - `RibbonIconKind::Rect` + icon + ribbon button + autocomplete icon mapping;
  - `CommandExpectsPointEntry` / `CadPointPromptLabel` / `CommandInputHint` — without these the cursor
    dynamic-input box (REQ-024) never appeared for either corner;
  - `st.lastCommand` + the `RepeatLastCommand` case — right-click repeat did nothing;
  - **paper-space routing**: `CommitRectangle` wrote to the model store even on a paper layout, so a
    rectangle drawn on a sheet landed in the model. It now routes through `ActivePaperGeometryTarget`
    into `paperPoly*`, and paper space has its own click handler (pure paper space routes clicks
    per-command, it does not go through `SubmitViewportPick`);
  - rubber bands for pure paper space and for floating model space (the GL rubber path covers model
    space only).

**C. Grip dynamic input.** The command palette is gated on `cmd.active != None`, and a grip drag runs
with no active command — so it never appeared. Added a sibling box at the cursor showing the **live
stretch distance** (`entityGripLiveDistance`, written by the drag each frame), with the text **selected**
so a keystroke replaces it. The selection is held by an `ImGuiInputTextFlags_CallbackAlways` callback:
ImGui applies keystrokes *before* the callback fires, so the callback compares the buffer against the
exact string it last pushed to tell "the user typed" (stop refreshing, keep their text) from "we
refreshed it" (overwrite with the live value and re-select). Esc still cancels the drag with the field
focused — `main.cpp`'s Escape handler is not gated on `WantTextInput`.

## 7c. Third user review (2026-08-11) — three more

**A. RECT hung on "specify first corner".** A **sixth** integration point, missed in §7b's audit: the
model-space click dispatch in `CadUi.cpp` is an explicit `else if` chain naming every point-picking
command, and `K::Rect` was not in it. Clicks were computed, snapped and then dropped on the floor. The
paper-space and floating-viewport paths added in §7b worked, which is why the gap survived that pass.
Added to the list with a comment stating the failure mode, since the chain gives no compile-time signal.

**B. New / Open did not focus the new tab.** Not a missing feature — `pendingDrawingTabSwitch` and
`ImGuiTabItemFlags_SetSelected` were already wired. A frame-ordering defect: tabs are submitted in index
order, and the body of whichever tab ImGui reported selected did
`activeDrawingIdx = i; pendingDrawingTabSwitch = false;`. The still-selected OLD tab has the lower index,
so it was always reached first — it reset `activeDrawingIdx` to itself and ate the pending flag before
the new tab was submitted, so `SetSelected` was never applied. The body now ignores the selection ImGui
reports for any tab other than the pending target.

**C. Pan and zoom are saved (REQ-055).** The `.gs` writer had no view state at all. Added an additive
`document.view` object. The pan is written in **WORLD** coordinates, not the local storage space it lives
in: `MaybeRebaseLargeCoordinates` runs on load and can change `worldDocumentOrigin`, which would leave a
local pan pointing somewhere else in the drawing — precisely the state-plane case this codebase keeps
hitting. Restore runs after that rebase. Zoom is clamped to the range the zoom controls use, so a
hand-edited value cannot strand the drawing on an unrecoverable view (REQ-201). Files without the key
fall back to framing the drawing; an empty drawing keeps the default view rather than logging
"nothing to frame".

## 8. Technical debt
```
DEBT-1: DXF export still writes no ARC and no ELLIPSE.
- Constraint: found while adding the polyline branch; fixing it is a separate requirement with its own
  record composition and tests, and REQ-053 does not cover it.
- Remove when: an arc/ellipse export requirement is accepted. Same shape of fix as this task's.
- Impact: arcs and ellipses are still silently dropped from DXF and DWG saves.

DEBT-2: Rotating an MTEXT does not rotate it — both the preview and the commit replace its box with
  the axis-aligned bounds of the rotated corners, so the box grows (up to 1.41x at 45°) and the text
  stays horizontal. `CadAnnotation::rotationRad` exists and is honoured for single-line TEXT.
- Constraint: making MTEXT rotate needs the MTEXT renderer to honour rotationRad, which is beyond
  this task's requirements.
- Remove when: an MTEXT-rotation requirement is accepted.

DEBT-3: Item 2's reported symptom is unreproduced. The tessellation fix is real but may not be it.
- Remove when: the user confirms it is resolved, or supplies a repro.

DEBT-4: A new draw command has no checklist, so RECT needed THREE passes to be complete — six
  integration points were missed across the first two (ribbon; the dyn-input prompt trio; lastCommand +
  RepeatLastCommand; paper-space commit routing + click handler; non-model rubber bands; and the
  model-space viewport-click dispatch chain). Several give no compile-time signal: the click dispatch
  and the prompt functions are `if/else` chains over Kind with a silent default.
- Constraint: writing that checklist is a docs change outside this task's requirements.
- Remove when: the integration points are listed in `workshop/implementation-rules.md`. §7b and §7c above
  are the raw material. Worth considering alongside it: making the click dispatch a `switch` over Kind
  with no default, so the compiler names the next omission.
```

## 9. Verification
- `build-project`: clean build of `GoSurvey` and `GoSurveyTests` (ninja, MSVC/clang), no warnings
  introduced in the touched files.
- `testing`: 740 assertions / 115 cases green, including 4 new ORTHO cases and 4 new LWPOLYLINE cases.
- `architecture-review`: no new abstraction, layer, dependency, global or storage-format change;
  `ApplyEntityGripPoint` reduces duplication; each change sits in the subsystem that owns it.
- `dependency-audit`: no dependency added or changed.
- `performance-review`: `PolylineBoundsArea` adds one bounding-box pass per polyline per snap query,
  only when geometric-centre snap is on — same order as the edge loop already running beside it. The
  preview tessellation change moves previewed curves from a fixed count to the renderer's adaptive
  count, which is what the committed geometry already costs.
- Not verified by test: everything requiring the running GUI — the RECT picks, the grip drag, the
  right-click menu, and item 2. These are the manual conditions listed in the REQ-053 / REQ-047
  matrix rows.

COMPLETION REPORT — TASK-032 — 2026-08-11
- Requirements satisfied:  REQ-053 (Acceptance met: yes, pending the manual GUI conditions);
                           REQ-047 as revised (yes, pending the manual GUI conditions);
                           REQ-054 (yes, pending the manual GUI conditions);
                           REQ-055 (yes, pending the manual GUI conditions)
- Summary:                 Added RECT as a fully integrated draw command (ribbon, dynamic input, repeat,
                           model/paper/floating routing) stored as a closed polyline, and gave the DXF
                           exporter the LWPOLYLINE branch it never had; fixed the world/local frame
                           mismatch that pinned typed ORTHO distances to +X; put grips under ORTHO with
                           typed distance entry and a live dynamic-input box; made Select similar
                           reachable and narrowed it to type + layer + colour; extended geometric-centre
                           snap to joined polylines.
- Tests:                   OrthoConstrainTests (4 new cases, incl. the frame-sensitivity guard),
                           DxfEntityEmitTests (4 new cases) — 740 assertions / 115 cases green
- Verification verdict:    PASS (findings resolved: none outstanding)
- Assumptions:             ASSUMPTION-1..3 documented above; 1 and 2 open pending the user's run
- Architectural decisions: none made by Workshop; REQ-053 + the REQ-047 revision were escalated and
                           recorded in the spec/project.md decision log before implementation
- Dependencies:            none added
- Technical debt noted:    DEBT-1 (no ARC/ELLIPSE in DXF export), DEBT-2 (MTEXT does not rotate),
                           DEBT-3 (item 2's symptom unreproduced), DEBT-4 (no new-command checklist)
- Build:                   reproducible, clean on Windows
- Docs updated:            spec/requirements.md (REQ-053, REQ-054, REQ-055, REQ-047 revision,
                           4 matrix rows), spec/project.md (3 decision-log entries)
- File format:             `.gs` gains an additive `document.view` key (pan in world coords + zoom).
                           No version bump — older files load, newer files load in older builds.
