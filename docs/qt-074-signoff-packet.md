# GoSurvey 0.7.4 Qt parity sign-off packet

**Purpose.** This packet records what the Qt migration delivers against the beta 0.7.4
behavior baseline, where the evidence lives, what remains unverified, and which visual
directions were selected. It does not authorize changing the production default or
removing the ImGui executable. Both remain gated on the user's sign-off under tracker
[#878](https://github.com/chetjones003/GoSurvey/issues/878).

**Evidence date:** 2026-10-19. The GitHub issue states below are the live issue states
read for this packet. The parity ledger contains the route-by-route feature audit and
focused evidence; this packet summarizes its coverage and remaining limits.

## Visual review decision

The user approved the final selected-design captures on 2026-10-19. This records visual
approval for the Qt parity packet only. It does not switch the production default, remove
ImGui, or constitute the broader migration decision tracked by #878. The verification
limits below remain visible.

## Verification snapshot

- Release Qt build: passed (`build/artifacts/build-919-qt.log`).
- Combined Release build: passed (`build/artifacts/build-919-all.log`).
- Full Release CTest run: **FAIL, 2683/2685 passed** (`build/artifacts/ctest-919-final.log`).
  - `headless.regression-surfacedelete-selection` failed at step 5 because the
    transcript selected a surface when no surface of that type existed.
  - `qt.editor` crashed in `approvedTopographyStaysInPanelBackgrounds` at
    `QWidget::grab` with Windows access violation `0xC0000005` while running on the
    hidden desktop. Six earlier isolated repetitions recorded four crashes and two
    passes. The hidden-runner's unsigned exit-code conversion was fixed in this task;
    this full run now reports the actual `SEGFAULT` instead of a PowerShell cast
    exception. The intermittent crash remains unresolved.
- `build.output-layout` and `qt.deployment` passed in that full run. The Qt deployment
  check starts the deployed Qt set without an ImGui executable present.
- The full suite is not green and must not be described as such. Focused issue results
  and limits remain recorded in the linked task files and ledger. The two new #919
  regression tests cover read-only project save refusal without losing typed drawing
  work, and a sign-in decline whose preference cannot be saved.

## Parity coverage

The [parity ledger](qt-074-parity-ledger.md) maps the 0.7.4 release workflows to Qt
entry points, status, tests and known gaps. Its meanings are strict: **Verified** has
focused workflow evidence; **Implemented** means the Qt route exists but focused
coverage is incomplete; **Missing** means the workflow is absent or materially
incomplete. The feature table has no `Missing` rows, but several rows remain
`Implemented` and the cross-cutting table still records partial coverage. Those are
not promoted to verified by this packet.

| Area | Current evidence and limitation |
|---|---|
| Drawing, project and core editor workflows | Issues #880–#904 have closed implementation issues. The ledger describes native Qt routes and cites focused tests where recorded. Some entries remain `Implemented`, including areas with untested read-only or cancellation branches. |
| PDF workflows | Issues #905–#910 have closed implementation issues. The ledger links focused search, calibration, extraction, compare/audit, attachment and annotation evidence. Live external PDF service behavior is not implied by those tests. |
| Traverse and adjustment | Issues #911–#916 have closed implementation issues. Focused evidence covers options, editor, merge/import, cancellation, project settings, reporting and point clouds. The full suite failure above remains a release-wide limit. |
| Build separation | Issues #917 and #918 are closed. The shared library is free of ImGui APIs, and the Qt executable's configured link graph and deployed runtime are checked separately. Production still includes both frontends. |
| Dark appearance | Accepted REQ-436 records the user's selected graphite surfaces with aqua interaction states and amber emphasis. The captures below show the selected Start, drawing-workspace and point-cloud attach directions. |
| Startup sign-in and persistence | REQ-091 has focused startup-choice coverage. This task adds coverage that a failed preference write keeps the choice unresolved and reports why. A real OAuth network round trip is not part of these deterministic tests. |

### Remaining verification limits

- The full Release test suite has two failures in the cited run; neither is presented as
  passed. The topography screenshot crash is intermittent and is still open for
  diagnosis. The hidden runner now preserves the Windows crash exit code correctly.
- `headless.regression-surfacedelete-selection` has a stale/invalid fixture selection
  at step 5; this sign-off task does not change the shared surface deletion workflow.
- Network-dependent update, telemetry and crash-report behavior is tested with local
  fakes or local artifacts where possible. A live release manifest/download, actual
  elevated installer launch, real crash relaunch, and real graphics-driver failure
  are not automated here; see their ledger rows.
- The Qt workflow for every row marked `Implemented` needs the additional focused
  coverage listed in that row before the ledger can call it `Verified`.
- GitHub issue #879 remains open even though the ledger document exists in this
  worktree. Issues #914 and #918 are closed in GitHub, while their checkboxes in the
  parent #878 description remain unchecked. The parent description is stale in those
  two places and has not been edited as part of this packet.
- Issue #919 and parent tracker #878 remain open pending review. No issue has been
  closed by this task.

## Selected visual directions

These are final selected captures from existing hidden-desktop Qt workflows, not new
visual alternatives. Their originating issue records contain the option-selection
history. PNGs remain untracked under `build/artifacts/Release/`.

| Surface | Selected evidence |
|---|---|
| Start page, graphite with aqua/amber accents | `build/artifacts/Release/qt-topo-start.png` |
| Drawing workspace and canvas, graphite with aqua/amber accents | `build/artifacts/Release/qt-topo-drawing.png` |
| Point-cloud attachment dialog, selected graphite/amber direction | `build/artifacts/Release/qt-916-attach-final-graphite-amber.png` |

The user approved these final captures on 2026-10-19. The approval is recorded here and
in the #919 task log.

## Work-item reconciliation for tracker #878

Every #878 work item is listed below. GitHub currently reports #879 and #919 as open;
#880–#918 are closed. The tracker description still has unchecked boxes for #914 and
#918, despite those issues being closed. #878 itself remains open.

| # | Work item | GitHub state |
|---:|---|---|
| #879 | Create the GoSurvey 0.7.4 Qt parity ledger | Open — ledger is present; tracker issue remains open |
| #880 | Add the Qt sign-in choice at startup | Closed |
| #881 | Restore recent-file startup and version-based What's New | Closed |
| #882 | Connect Qt software update workflows | Closed |
| #883 | Connect Qt telemetry and crash-report workflows | Closed |
| #884 | Complete Qt drawing import/export and loss disclosures | Closed |
| #885 | Add the Qt drawing template workflow | Closed |
| #886 | Port project settings to Qt | Closed |
| #887 | Port add-drawing and coordinate-conversion project workflows | Closed |
| #888 | Port project drawing copy and link prompts to Qt | Closed |
| #889 | Port project health, pack, and turnover workflows | Closed |
| #890 | Port stale project-lock takeover to Qt | Closed |
| #891 | Complete core Qt geometry properties and context menus | Closed |
| #892 | Port Qt view, UCS, and visual-style dialogs | Closed |
| #893 | Port Qt surface and surface-style management dialogs | Closed |
| #894 | Port Qt group-management dialogs | Closed |
| #895 | Port Qt block-editor workflows | Closed |
| #896 | Wire 0.7.3 Pipe Network and Feature Line ribbon actions | Closed |
| #897 | Complete Qt layout, page setup, and viewport-scale workflows | Closed |
| #898 | Complete paper-space rendering and table parity in Qt | Closed |
| #899 | Port map tiles and geolocation workflows to Qt | Closed |
| #900 | Port 3D gizmo and section-plane overlays to Qt | Closed |
| #901 | Complete Qt annotation scale, object isolation, and perspective controls | Closed |
| #902 | Complete Qt selection gestures and grip editing | Closed |
| #903 | Complete Qt UCS-aware ORTHO/POLAR, shortcuts, and transient tools | Closed |
| #904 | Complete survey-point marker visibility and style controls in Qt | Closed |
| #905 | Add Qt PDF search and thumbnail navigation | Closed |
| #906 | Complete Qt PDF calibration, checks, and snapping | Closed |
| #907 | Port PDF coordinate extraction to Qt | Closed |
| #908 | Port PDF compare and audit workflows to Qt | Closed |
| #909 | Complete Qt PDF underlay attachment | Closed |
| #910 | Complete persisted PDF annotations and rotated/high-DPI workflows | Closed |
| #911 | Port 0.7.4 traverse unit, grid-scale, and curvature controls | Closed |
| #912 | Complete the Qt least-squares adjustment editor | Closed |
| #913 | Port traverse merge import and Trimble JobXML workflows to Qt | Closed |
| #914 | Complete Qt open-traverse adjustment and command-editor workflows | Closed — parent checkbox is stale |
| #915 | Complete Qt adjustment project settings and report plotting | Closed |
| #916 | Complete Qt point-cloud format workflows for 0.7.4 | Closed |
| #917 | Extract remaining ImGui-only APIs from shared code | Closed |
| #918 | Remove ImGui from the Qt link graph and verify deployment | Closed — parent checkbox is stale |
| #919 | Prepare the 0.7.4 Qt parity sign-off packet | Open — awaiting review |

## Scope boundary

This packet records Qt evidence only. It does not switch the production executable,
change the default frontend, remove ImGui, alter drawing formats, or claim production
migration approval. Those changes remain gated on the explicit decision recorded by
tracker #878.
