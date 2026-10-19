# TASK-887 — Match the approved visual mockups in the Qt frontend

- Type:    feature (appearance)
- Status:  approved for PR; integrating current beta
- Opened:  2026-10-18
- Owner:   Workshop

## 1. Authority
- Goal:         n/a — existing shell and Start tab presentation
- Requirements: **REQ-081** (accepted, revision 11), **REQ-308** (accepted, revision 5),
  **REQ-374** (accepted, Start-page placement amended by D-2026-10-18-a)
- Constraints:  Light appearance and ImGui rendering unchanged; Qt changes stay presentation-only; existing project,
  drawing, account, import-format, settings and help actions remain functional; no new dependencies.
- Acceptance:   REQ-081 — Dark UI follows the supplied screenshots' navy/blue-charcoal surfaces,
  aqua interaction accents, restrained amber details, mountain-and-valley contour lines and deep
  navy drawing canvas with fine blue grid; Light remains unchanged. REQ-308 — Start retains the
  ribbon/tabs/status bar and provides four drawing/project actions, a structured recent-drawings
  table, and compact right-column cards with unclipped project rows.
- Owning subsystem: UI (`src/qt/QtAppearance.cpp`, `src/qt/EditorChrome.cpp`, `src/qt/EditorWindow.cpp`)

## 2. Scope
- In scope: Qt Dark appearance, Start and drawing workspace layout/styling; use existing application
  data and actions to populate and operate the mockup components.
- Out of scope: ImGui frontend, Light appearance, new import or learning features, changes to
  project/drawing persistence or map rendering behavior.
- Smallest change: adjust the existing Qt palette/stylesheet, restructure Qt's Start page and docks,
  and pass Qt-specific navy background/grid colors through existing renderer tuning.

## 3. Architectural boundary check
- [x] No — existing UI subsystem and Qt frontend; no new dependency, abstraction, data format,
  global state, or backend API.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Revise the UI requirements to match the approved mockups? | 2026-10-18 | Yes; user explicitly approved the revisions. |
| Q2 | Which frontend and branch should own the implementation? | 2026-10-18 | Qt frontend on `beta`; user corrected the initial ImGui assumption. |

## 5. Assumptions
None. Mockup sample names, dates, paths and counts are illustrative; real Qt models remain authoritative.

## 6. Plan
- Approach: use `ApplyQtAppearance` for the global Qt shell palette and stylesheet; update Qt's
  mode-dependent chrome, Start page, side docks, and viewport tuning to the supplied references.
- Files/functions to touch: `src/qt/QtAppearance.cpp`, `src/qt/EditorWindow.cpp`, their relevant
  declarations only if required, `spec/requirements.md`, `spec/project.md`, this task log.
- Test approach: build the Qt application and inspect the Start page and a drawing workspace
  visually; exercise visible actions through existing routes. No automated tests are added or run
  for this visual request.
- Steps:
  - [x] Record user-approved requirement revisions and decision.
  - [x] Review Qt ownership boundaries, current Start page behavior, and implementation plan.
  - [x] Initial palette and Start-page implementation.
  - [x] Refine requirements to match the user's exact reference screenshots.
  - [x] Keep the approved ribbon, tab strip, and status bar visible on Start and fit the landing content.
  - [x] Restyle drawing docks, ribbon, title bar, and Qt Dark canvas.
  - [x] Build and relaunch the Qt release app for user review.

## 7. Workflow-specific notes
- Feature pre-flight complete. Existing Qt start page and palette provide the implementation seam;
  user authorized the two requirement revisions before code.

## 8. Implementation log
- 2026-10-18 — user chose the modern/abstract blend and approved the Start page mockup.
- 2026-10-18 — user directed work to the Qt frontend and `beta`; confirmed existing Qt worktree is
  clean and already on `beta`.
- 2026-10-18 — amended REQ-081, REQ-308 and REQ-374; recorded D-2026-10-18-a.
- 2026-10-18 — Verification plan review: **APPROVE**. UI-only changes stay within the Qt UI owner;
  no new dependency or architectural decision. Main risks are losing existing entry points while
  rearranging the Start page, poor sizing at smaller windows, and accidental changes to Light or
  viewport appearance. Plan addresses these by reusing existing routes/data, responsive layouts,
  and a dedicated Dark-only palette.
- 2026-10-18 — Qt inventory found no Open Packed Project route and no separate survey-setup route
  in the Qt Start screen. REQ-308 was narrowed to the working Qt actions; the page does not offer
  an unavailable workflow. The stale pre-existing resize hook was replaced with responsive
  two-column stacking.
- 2026-10-18 — user supplied exact Start and drawing-workspace screenshots after reviewing the app.
  D-2026-10-18-b and REQ-081 revision 9 / REQ-308 revision 3 now define those screenshots as the
  target, including hiding all drawing chrome on Start and changing only the Qt Dark canvas ground.
- 2026-10-18 — refinement plan review: **APPROVE**. Work remains inside the existing Qt frontend
  and renderer tuning seam. Map controls reuse existing commands; no map provider/network behavior
  or persistence is changed. Light theme and ImGui renderer defaults remain as before.
- 2026-10-18 — Added the compact GoSurvey brand header, responsive Start-page columns, navy cards,
  aqua/amber actions, and faint peripheral contour paths in the Dark Start background. Recent
  drawings and projects retain their live stores and open/remove actions. Light palette remains
  unchanged and the contour decoration is confined to the Start page.
- 2026-10-18 — Refined the Start page to hide drawing tabs, ribbon, docks and status controls while
  active; added the screenshot's five-row searchable Projects list and matching workspace side
  panels. Drawing mode now places Properties left and Prospector/Settings plus the live USGS Map
  panel right. Existing map actions and online map selection are reused.
- 2026-10-18 — `build-qt.bat` completed successfully (Qt release app and test executable linked;
  no tests were run). `git diff --check` passed. Launched `build/GoSurveyQt.exe` from the beta
  worktree (PID 22572; window title GoSurvey; responsive). Captured the local desktop to inspect
  the Start page; corrected the recent-table spacing and reduced titlebar contour contrast. The
  T3 device screenshot service remains unavailable, so the drawing workspace was not captured.
- 2026-10-18 — User followed up that the whole Start composition should fit like the supplied
  widescreen image. Tightened page margins, action cards, recent rows, and the Projects, Quick links,
  and Learning resources cards. Verified from a local desktop capture that all five visible recent
  drawings, five recent projects, and both right-side cards fit together. Escaped ampersands in
  button labels so “Survey setup” and “Help & support” render correctly instead of as mnemonics.
  Rebuilt successfully and relaunched the responsive Qt window (PID 29340) with the complete Start
  page visible.
- 2026-10-18 — User requested New project/Open project actions beside New drawing/Open drawing,
  a “Welcome to GoSurvey” heading without subtitle, unclipped recent-project rows, and irregular
  mountain/valley contours on Start and drawing panels. Updated REQ-081/REQ-308 and D-2026-10-18-e.
  Reused existing `NewProject()` and `OPENPROJECT` flows, removed the Import data card, increased
  project row height/padding and viewport height, and replaced circular decorative paths with
  asymmetric terrain contours in the Start body, panel overlays, and title strip.
- 2026-10-18 — `build-qt.bat` passed. Captured and visually checked the Start page: all four action
  cards, all five project entries with both text lines visible, the three right-column cards, ribbon,
  tabs, and status bar fit together. `git diff --check` passed; no tests were run per instruction.
  Relaunched the app on Start (PID 5308).

## 9. Self-verification
- [x] build-project — `build-qt.bat` passed on beta after screenshot refinement.
- [x] architecture-review — existing Qt UI subsystem; no new dependency, boundary, or data flow.
- [x] code-review — final diff inspected; stale resize hook and unused project-thumbnail helpers removed.
- [ ] dependency-audit — n/a (no manifests or dependencies)
- [ ] performance-review — n/a (no hot-path change)
- [x] testing — no automated tests added/run for this visual request, per instruction.

## 10. Verification result
- Plan verdict: **APPROVE** — 2026-10-18.
- Final verdict: build passed; Start page visually reviewed from a local desktop capture. Drawing
  panel contours were implemented but not captured in a live drawing workspace during this pass.

## 11. Outcome
- Requirements satisfied: revised Qt Start actions/layout and terrain contour motif implemented.
- Tests added: none.
- Docs updated: `spec/requirements.md`, `spec/project.md`, this task log.
- Done: build succeeded and updated Qt app launched from beta on Start. User can inspect the live app.

## 12. Topographic reference correction — 2026-10-18
- Bug report: opening a drawing or Start shows parallel repeated waves; the approved reference
  instead has individually shaped, nested terrain contours with an amber contour descending
  across the lower-left Properties background, and matching peripheral artwork in the right docks.
- Root cause: DrawContourArtwork translates one three-segment wave for every contour. It cannot
  express the reference's changing spacing, saddle, and nested lower-left landforms. The later
  PropertiesContourWidget also reserves a separate 150–190 px strip outside the tree background.
- Authority: REQ-081 revision 11 and REQ-308 revision 5, existing user approval to match the mockups.
- Plan review: APPROVE. Trace the supplied reference as static Qt vector paths, reuse that artwork
  on Start/chrome, paint within the existing dock backgrounds, and remove the reserved strip.
  Changes remain in Qt presentation; no persistence, renderer, dependency, or architecture change.
- Validation: regression check for no reserved Properties strip and visible amber contour;
  hidden-desktop widget captures of drawing, Properties, Start, compact window, and Light theme.
- Status: correction in progress; prior 'complete' status does not establish visual acceptance.
- Verification evidence: the regression check failed against the previous UI with a 157 px
  reserved Properties strip. After correction, the terrain/background and title/command-bar
  checks pass on the hidden desktop. A separate floating-dock keyboard-focus test fails at the
  final focus handoff; replacing both new tree subclasses with the original QTreeWidget controls
  reproduces the identical failure (baseline-focus.log), so it is not caused by contour painting.
- Implementation: TopographicArtwork.hpp holds individually traced contour paths from the approved
  Properties artwork plus the distinct rising right-dock saddle. Existing tree viewports paint
  the art only in unoccupied regions; there is no overlay or reserved strip. Start's recent-drawings
  area is transparent so it does not cover the artwork. Light mode suppresses the decoration.
- Checks: dependency/architecture review PASS (Qt-only immutable vector data and existing painters);
  code review PASS (no new persistence, commands, rendering backend behavior, or dependencies).
  Screenshots use widget grabs and framebuffer composition under build/artifacts/Release.
- Completion state: awaiting user visual review; no commit or PR until the screenshot review.
- Final validation: Qt Release build PASS; approvedTopographyStaysInPanelBackgrounds and
  customTitleBarAndFloatingCommandLineWork PASS (final-contours.log). git diff --check PASS.
  Baseline floating-dock focus failure is recorded separately and was not altered for this task.
  Final captures: qt-topo-properties-detail.png, qt-topo-drawing.png, qt-topo-start.png,
  qt-topo-toolspace.png, qt-topo-drawing-compact.png, and qt-topo-light.png under
  build/artifacts/Release. Qt app relaunched on Drawing 1 (PID 6384).

- User approved the screenshot result by requesting a PR to beta and admin merge after CI is green.


- Integration preserves beta's functional Properties controls, Map capture progress/actions, Toolspace context menus, and section overlays. Renamed the style refinement decision to D-2026-10-18-e to avoid beta's existing decision ID.

- Current-beta integration: Qt Release build PASS; contour/background, title/command-bar, and revised Start surface checks PASS (5 QtTest results including setup/cleanup). The native editable Properties panel now paints the traced terrain in its body background; no form rows or Map workflows were removed.

- Integration workflow checks PASS: Map settings/task panel plus five Properties editing/validation/undo/empty-state cases (8 QtTest results including setup/cleanup). User authorized PR and admin merge after CI; screenshots were shown again after beta integration.
