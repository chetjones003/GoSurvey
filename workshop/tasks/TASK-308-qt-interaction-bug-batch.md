# TASK-308 — Qt viewport and command interaction bugs

- Type: bug batch
- Status: implementation in progress; verification incomplete
- Opened: 2026-11-02
- Owner: Codex

## 1. Authority

- Requirements: REQ-047 (ORTHO and grip editing), REQ-056 (TRIM), REQ-121 (object selection),
  and REQ-439 (Qt selection and grips).
- User report: (1) grip drags do not snap; (2) grip hover actions are missing, including line-end
  Lengthen/Extend-style actions; (3) clicking a command option such as Azimuth or 2P also clicks
  through to the viewport; (4) the cursor does not snap to grips; and (5) after two LINE 2P picks,
  Enter or a typed `+/-` angle adjustment is ignored while right-click works.
- Constraints: identify each root cause before fixing; preserve domain ownership and REQ-121's
  deliberate unsnapped object-selection behavior; add focused regression coverage; no new dependency.

## 2. Scope

- In scope: Qt viewport press/move/release routing, grip hover actions and grip snapping, command
  option click routing, cursor snapping to selected grips, and LINE 2P keyboard input after its two
  reference picks.
- Out of scope: TRIM precision, undo routing, object isolation, snap glyph sizing, and general
  selection/multiselection behavior; those were included in the broader continuation but are not in
  the user's original request.

## 3. Initial diagnosis evidence

- **Grip drag snapping (confirmed):** `EditorViewport::mouseMoveEvent` computes snaps only when
  `midCmdUsesSnap` is true, but that expression omits entity, dimension, and MTEXT grip drags.
  `mousePressEvent` includes these grip states, so the initial press and subsequent drag use
  different snap gates. This explains no snap during the drag; test the fix against other hidden and
  visible candidates and ORTHO precedence.
- **Grip hover actions (confirmed):** grip drawing in `EditorViewport::paintGL` marks only an
  actively dragged grip as hot. `mouseMoveEvent` has no entity-grip hover resolution or action
  label state. Compare the existing beta behavior before adding Qt state or labels.
- **Click/selection symptoms (confirmed, likely related):** ordinary idle selection appends a new
  hit without clearing on a plain click, and the generic branch does not add a new hit on Shift
  click. Several selection-box branches also retain separate armed-box state. Trace exact click
  routes and protect REQ-439 box accumulation and dismiss behavior.
- **LINE 2P input (diagnosis incomplete):** `ApplySegmentAnglePickToViewportPick` correctly consumes
  its third click and asks for Enter or `+/-`; command submission behavior and Qt command-field focus
  need tracing to identify why keyboard input is not accepted in the reported state.
- **TRIM gap (diagnosis incomplete):** model line trimming computes an intersection and assigns that
  point to one endpoint. Reproduce with two independently trimmed intersecting lines and inspect the
  intersection solver, storage precision, and DIST pick tolerance before changing geometry math.
- **Undo, isolation snap, indicator size (diagnosis incomplete):** trace the Qt shortcut dispatch,
  REQ-084 hidden-entity snap filters, and the renderer's fixed `6.f` snap glyph argument against
  existing requirements and baseline behavior.

## 4. Plan

1. Finish deterministic reproductions and root-cause evidence for the remaining reports.
2. Record exact affected files, focused regression coverage, risks, and architectural boundary.
3. Implement the smallest fixes and add regression cases before production changes.
4. Build and run the applicable Qt/domain verification, inspect the diff, and record limitations.
5. Capture affected UI states and compare with Civil 3D where available; report an inconclusive
   comparison if Civil 3D or the required independent review panel is unavailable.

## 5. Architectural boundary

Qt owns event routing and presentation; shared Commands/Viewport services own picking, snapping,
isolation, trimming, and undo. Keep geometry corrections in the shared command layer only if a
repro proves the shared algorithm is at fault. No new abstractions or dependencies are planned.

## 6. Implementation and verification update (2026-11-02)

- Confirmed source-path changes: grip drags participate in snap resolution; selected grips are
  available as snap targets; snap glyphs use the configured drawing-state size; isolated
  line/circle/polyline/arc/ellipse/spline candidates are filtered; Shift-click toggles selection
  while a plain click replaces it; dynamic input yields Enter and `+/-` during LINE/POLYLINE bearing
  adjustment; and command hint labels stop mouse propagation into the viewport.
- Selected line-end grips now open a delayed Lengthen/Stretch action menu through the existing grip
  editing route.
- TRIM line and polyline target coordinates and intersection calculations now retain `double`
  precision rather than narrowing stored coordinates to `float`. The reported drawing was not
  available to reproduce, so acceptance of the gap correction remains unconfirmed.
- Ctrl+Z with an empty focused command field now routes to drawing undo. The broad undo report still
  lacks a deterministic reproduction; this narrow routing change does not demonstrate that every
  undo/redo case is fixed.
- `build.bat` completed successfully for Release and compiled both frontends, domain tests, and
  helpers. No test suite was run. Hidden-desktop Qt interaction tests, product repros, and screenshots
  remain outstanding. The required three-reviewer Civil 3D comparison is unavailable in this run,
  so visible UI review is inconclusive.
- Still unproven: full overlap Multi Selection behavior, all snap-isolation candidate families, the
  general swallowed-click symptom, and all undo/redo flows. Deterministic reproductions and focused
  regression coverage are still needed before marking the batch complete.

## 7. Scope clarification (2026-11-02)

- The user supplied the original prompt and narrowed acceptance to the five interaction reports in
  section 1. The other bug reports and changes listed in section 6 are not acceptance criteria for
  this task. Do not report TASK-308 complete until each of the five original behaviors has been
  verified; the current commit has only a successful Release build recorded.

## 8. Follow-up reports and diagnosis (2026-11-02)

- User testing added four reports: quick click-drag box selection is lost when the press hits an
  object; Lengthen must keep the line bearing and accept signed distance input; the cursor is
  invisible over command history/help overlays; and TRIM leaves a near-zero endpoint gap and shows
  neither the trim line nor removed span.
- **Click-drag selection (superseded):** an earlier fix promoted a press on linework into a drag box;
  the user rejected click-drag for idle selection. Idle boxes now use two clicks per REQ-455 /
  Civil-style window/crossing (first release arms, second click completes).
- **Overlay cursor (confirmed source cause):** the viewport deliberately uses `BlankCursor` because
  it paints its own crosshair. The command history and hint widgets inherited that cursor. They now
  set visible text/arrow cursors explicitly.
- **TRIM preview (confirmed source cause):** shared 2D/3D removed-span preview functions exist, but
  the Qt viewport never called them and passed null removal geometry to the renderer. Qt now adds a
  dashed trim-line preview and routes the removed span through the renderer's red removal overlay.
- **TRIM endpoint gap (resolved in follow-up §9):** line cutter coordinates were narrowed to float`r`n  before intersection; the live drawing supplied the coordinates needed to reproduce the residual.
- Interactive verification remains pending; do not mark this task complete.
- Follow-up implementation now includes **two-click idle box selection** (first click release arms
  the corner at the release point; second click finishes — no press-drag fence) in
  `src/qt/EditorViewport.cpp`, explicit visible cursors for command history/help in
  `src/qt/EditorChrome.cpp`, and Qt wiring for the existing TRIM removed-span preview plus a dashed
  cutting-line preview in `src/qt/EditorViewport.cpp`.
- **2026-11-02 follow-up (items 1–3):** idle object picks defer until release; idle empty picks arm
  the box on release so pointer motion during the first click does not swallow the corner;
  `RefreshCommandCursor` and the floating command bar restore arrow/I-beam over history and hint
  chrome; Lengthen grip drags constrain to the line bearing in `ApplyEntityGripPoint` (typed signed
  distance was already supported). Drag-to-box promotion was removed after user feedback.
- **2026-11-02 UI follow-up (user retest):** selection highlights for trimmed lines now keep endpoint
  coordinates in `double` through `BuildSelectionHighlight` / `ViewportRenderer` (fixes highlight
  offset at survey coordinates). Grip Lengthen/Stretch menu closes after one second with the pointer
  off the menu. Status bar shows one scale control at a time — viewport zoom in model space,
  annotation scale in paper space. Viewport zoom combo no longer injects a live `1:43.82`-style ratio
  after wheel zoom; it keeps the last chosen preset (REQ-458 user-set ratio). Changing the preset in
  the status bar updates the stored ratio only and does not alter \c viewportZoom (pan/zoom stay as-is).
- The updated Release build completed successfully after these edits. The app window already open
  in this worktree predates the rebuild and must be relaunched to load them. No behavior-level run
  has been recorded yet; the TRIM endpoint gap still needs the user's drawing before it can be
  diagnosed.

## 9. TRIM endpoint precision repro and fix (2026-11-02)

- Saved the live pre-trim drawing as `build/artifacts/intersecting-lines-before-trim.dwg`. The two
  reported lines are approximately `(-34.4379, 6.8974)–(15.8321, 7.8792)` and
  `(-12.1502, 23.6868)–(-11.9539, -12.1502)`.
- **Root cause confirmed:** `CollectCutSegments` copied model line XY endpoints from the double-valued
  `userLinesFlat` state through `float` casts. `TrimSegmentIntersectPickSide` then intersected the
  original double target with a cutter shifted by float rounding. Reproducing the reported coordinates
  before the fix left the trimmed target endpoint `9.39e-8` drawing units off the original cutter.
- Added a plan-view drawn-line TRIM regression test using those coordinates. It failed before the fix
  on the endpoint-to-cutter residual, then passed after `CollectCutSegments` retained the stored
  doubles. The fix is limited to line cutter coordinates; the preview shares this collector.
- Verification: `GoSurveyTests.exe "[trim]"` passed (27 cases, 122 assertions). `build.bat` compiled
  the changed domain code and test executable, but the final Qt app relink failed with LNK1104 because
  the live `build/GoSurvey.exe` process holds the output file. The currently running app was left open.

## 10. PR 1154 CI follow-up (2026-11-02)

- The latest full-validation run (41549557552) built successfully, then failed two Qt test functions:
  `qtCrossingSelectionDragAndEscapeCancellation` and
  `task078IdleAndCommandDragBoxesAccumulateWithoutSwallowingClicks`. Both tests still used an idle
  press-drag gesture although the accepted idle behavior in section 8 uses two clicks. The first
  test sent Escape before releasing the first press, so it never armed the box it meant to cancel.
  Update only those idle sequences; retain the command-selection drag assertion.
- The same run's MSVC analysis reported C4189 at `EditorWindow.cpp:2426`: `dpi` remained after
  the status-bar preset stopped changing the camera. Remove that unused local.
- CI repair is confined to the test sequences and dead local. No application behavior or architecture
  changes are intended. The Release build passed; both affected Qt cases passed on the hidden desktop
  (4 QtTest functions including init/cleanup), and the full CTest suite passed 194/194. The refreshed
  CI MSVC analysis check remains to run after pushing. Build, testing, code review, and architecture
  checklist verdicts are PASS for this narrow CI repair.
- The existing PR description records README, PRIVACY, in-app wiki, and public wiki reviews. This
  follow-up only corrects CI coverage and a dead local; none of those product documents is affected.
