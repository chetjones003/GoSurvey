# TASK — 0.7.8 interaction and PDF extraction bug batch

- Type: bug batch / Qt parity follow-up
- Status: done — accepted REQ-458 and REQ-399 amendment recorded; implementation complete pending Civil 3D comparison for status bar chrome
- Opened: 2026-10-29
- Owner: Workshop

## 1. Authority

- Existing accepted requirements: REQ-027 (layout viewport scale distinction), REQ-039,
  REQ-047, REQ-103, REQ-121, REQ-305, REQ-370, REQ-399, REQ-450, and REQ-455.
- New acceptance needed: the current model viewport zoom status-bar control and the mixed-validity
  PDF table review behavior. User decisions on 2026-10-29: zoom uses ratio presets (such as 1:50),
  with a dialog to add custom ratios; those custom scales are stored with the current drawing and
  restored through DWG/DXF (the user says `.gs` is no longer supported); valid PDF coordinate rows
  remain reviewable and invalid rows are reported separately. The zoom scale list remains separate
  from annotation scales, so adding a zoom preset does not change annotation behavior.
- Constraints: preserve existing drawing coordinates/storage (REQ-101), no new dependencies,
  and follow `CLAUDE.md`'s Civil 3D screenshot comparison gate (see §6 Verify).
- Report: nine 0.7.8 issues confirmed by the user on 2026-10-29. The screenshots supplied with the
  report show status-bar bottom spacing and a coordinate table containing incomplete/note cells.

## 2. Scope

1. Box selection accumulates in modify-command selection steps.
2. MOVE text and any other in-scope modify operation missing its specified live preview show the
   preview through the existing transform preview path.
3. A box gesture must not consume a later object-pick click.
4. LINE honors enabled ORTHO in its preview and committed point.
5. Starting a modify command with no preselection reaches a usable object-selection step and gives
   the prescribed selection prompt.
6. During normal point-entry commands, the visible cursor follows active object snaps; object
   selection retains REQ-121's deliberate no-snap behavior.
7. Add bottom breathing room to the Qt status bar to match the supplied reference.
8. Add a status-bar control for current model-view zoom, using presets and a dialog to add custom
   scales. This is distinct from drawing plot scale and paper viewport model-to-paper scale.
9. PDF extraction keeps valid table rows as candidates and reports incomplete or non-coordinate
   rows separately in review; it does not silently discard them or invalidate good rows.

Out of scope: changing idle selection's documented Shift-to-add behavior; changing ORTHO geometry or
snap priority; changing REQ-121's selection-step snapping rules; OCR; changing paper viewport scale;
any persistent camera/file format unless existing view-state rules require it.

## 3. Plan review

Before implementation:

- Verify each symptom against its accepted requirement and identify a concrete root cause in the Qt
  event/render path or shared command/parser path.
- Resolve the zoom control's product semantics in a **new** accepted requirement before code
  (**not REQ-456** — that ID is already **REQ-456 Paper-space command parity**, issue #1019).
  Proposed placeholder: **REQ-458** (model viewport zoom status-bar control). Preset values, custom-scale
  validation, scope, and DWG/DXF persistence must be stated and recorded in `spec/`.
- Amend REQ-399 to define **per-row** invalid/skipped table reporting in the PDF review UI before code;
  valid coordinate rows remain candidates (including circle outcomes under existing rules). Today
  `TryParseTable` skips incomplete rows silently; REQ-399 clause 4 already requires unparsed regions at
  the extract level, not row-level review for mixed tables.
- Keep the plan within existing ownership: `EditorViewport`/Qt status bar, shared transform preview,
  existing snap/ORTHO functions, and `PdfCoordExtract`/PDF review UI. No new dependency or generic
  service is planned.

### Diagnosis evidence found so far

- **Text preview (2): confirmed cause.** `BuildTransformPreview` handles geometry but no annotations;
  the legacy frontend has separate `CadAnnotationCollectTransformPreviews` and table preview
  functions, but the Qt viewport never calls them. Qt's `DrawQtModelAnnotations` draws only the
  committed drawing annotations.
- **Box gesture consumes a later pick (3):** Qt `SelectionAccumulate` routing plus, when
  `selBoxWaitingSecond`, an entity hit toggles selection and cancels the armed box instead of closing
  the fence at the entity point.
- **PDF invalid rows (9): confirmed cause.** `TryParseTable` adds rows with both valid coordinates,
  but drops all other rows without recording their source or reason. The review UI receives only
  candidates.
- **Snap cursor (6): current Qt code computes `FindBest` while a point-entry command is active and
  intentionally suppresses it during REQ-121 object-selection steps. Confirm whether the reported
  case is a point-entry command with OSNAP enabled before making a snap-path change.
- **LINE ORTHO (4):** shared commit/rubber paths call `ApplyOrthoConstrainFromAnchor`; Qt overlay now
  draws a dashed ortho tracking line (same pattern as polar) when ORTHO is on and a base point exists.
  User-visible LINE behavior still needs Civil 3D comparison if the reported angle bug persists.
- **Additive box selection (1):** the shared rectangle selector merges hits into the current
  selection, but the Qt idle drag path calls `ClearSelection` when a plain (non-Shift) drag starts.
  The intended scope (idle selection versus modify-command selection) must be verified against the
  user's reported workflow before changing it.
- **Empty-selection modify prompt (5):** `StartMoveCommand` correctly enters `PickSelection`, but
  logs a long instruction string. Diagnose the Qt command input route together with (3), then use
  REQ-121's shared prompt if the live prompt is the reported surface.
- **Status-bar bottom spacing (7):** the Qt status bar has no bottom content margin; the screenshot
  supports a local layout adjustment. Validate at the user's Windows taskbar/dpi scale.

## 4. Verification plan

- Reproduce each report against the current code and record before-state evidence.
- Use existing automated coverage where it can assert shared command/parser behavior. UI-only
  behavior is confirmed by driving the Qt app and capturing each affected surface/state.
- Build the Qt application and run applicable Qt verification per `CLAUDE.md` after implementation.
- Capture Civil 3D comparison panels for status bar, selection step, MOVE text preview, ORTHO LINE,
  and snap cursor per post-beta `CLAUDE.md` (replacing the earlier T3 visualizer gate in this task).

## 5. Architectural boundary check

- No new layer, dependency, ownership model, or persisted entity format is planned.
- New zoom-scale persistence and PDF invalid-row representation require explicit **REQ-458** (proposed)
  and **REQ-399** amendment acceptance first; do not implement those semantics until recorded.
