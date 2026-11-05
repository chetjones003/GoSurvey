# TASK-998 — Group grading objects and calculate cut/fill volumes

- Type: feature
- Status: done (code verified and UI approved; local commit authorized)
- Opened: 2026-11-02
- Authority: GitHub #998; accepted REQ-404, ADR-070 and D-2026-10-09-b;
  persistent member definitions supplied by REQ-398 / D-2026-10-20-j (#997).
- User direction: implement #998 before scoping optimizer #813. No optimizer implementation
  is authorized by this task's requirements.

## Requirement and acceptance

Create named grading groups containing persistent grading results. Each grading belongs to
at most one group. Build an ordinary TIN covering the union of the ground between member
baselines and daylight lines, including adjoining members without gaps. Reuse REQ-073's
surface volume comparison for cut/fill/net and retain normal surface selection, elevation
queries and Surface Manager participation. Rebuild on membership changes and explicit
member grading rebuilds; ordinary source edits must not change the baked group surface.
Persist membership and baked geometry and load legacy documents unchanged. REQ-404's
historical `.gs` terminology is subject to the project's DWG migration decision
D-2026-10-20-d and ADR-044 payload, as for #997.

Constraints: REQ-076 stable entity ids, REQ-101 numeric tolerance, REQ-201 visible
failures, REQ-300/301 no unnecessary dependencies or abstractions; architecture §8
worker input isolation and §11 immutable triangulation replacement. No GLOSSARY.md exists.

## Implementation plan

1. Represent a grading group as metadata on its ordinary CadSurface, including member
   stable ids and baked baseline/daylight geometry. Keep metadata in surface copies so
   undo and document switching use existing mechanisms; use additive DWG payload fields.
2. Assemble constrained baseline/daylight strips through the existing surface construction
   pipeline. Cull to their union rather than filling the convex hull between disjoint members
   or the interior of a closed baseline. Preserve boundary constraints and shared edges.
3. Add group creation/membership management to the grading command tools. Validate inputs
   before mutation, retain the previous drawing on failure, and make membership moves atomic
   with one undo boundary. Expose the ordinary surface to the existing volume comparison.
4. Refresh affected group geometry only on explicit grading rebuild or membership changes;
   isolate stored geometry from subsequent baseline/target edits.
5. Capture affected Qt surfaces and meaningful states on the hidden desktop, present one
   labeled T3 visualizer, and obtain the required UI approval before committing.

Expected files: CadEntities.hpp, CadCommands.hpp/.cpp, GsIo.cpp, Qt grading entry points
as necessary, QtEditorTests.cpp and affected help documentation. No new dependency,
entity kind, volume engine, or ownership layer.

## Risks and verification plan

- Incorrect footprints can invent earthwork in a closed baseline interior or between separate
  members: test known elevations inside/outside the union and exact hand-computed volumes.
- Source edits can accidentally trigger live recomputation: test baseline/target edits separately
  from explicit member rebuilds and membership changes.
- Invalid/incomplete daylight chains and conflicting elevations must be reported, preserving the
  previous surface rather than committing partial geometry.
- Membership transfer, undo, document switching, and DWG reopen must preserve stable references.
- Agreed TDD boundaries: public grading commands through EditorWindow::Submit, DWG
  SaveDrawingDocument/OpenDrawingDocument, and direct surface-building functions. User confirmed
  all three on 2026-11-02. No internal worker or renderer testing is needed.
- Build with ./build.bat, run focused hidden-desktop Qt cases per slice, then the full CTest
  preset once after implementation. The MSVC builds provide static type checking.
- Run the code-review skill on the implementation diff before final commit.

## Pre-implementation verification

- Existing surface construction and volume-query interfaces inspected; baseline build started.
- Surface assembly uses the existing constrained triangulation and Clip-loop union, with baked
  per-member strips. Group metadata stays on the ordinary CadSurface and additive payload fields;
  this implements REQ-404's persistence, not a new entity or independent group store.
- Risks: preserve baked strips for unchanged members during membership edits; reject incomplete
  chains and conflicting elevations instead of inventing surface. No new dependencies or hot-path
  calculations. Group rebuilds use the existing surface construction path.
- Verdict: APPROVE. Authority, architecture, scope, risks and agreed tests reviewed before code.

## Documentation and completion

Before any PR, review README.md, PRIVACY.md, resources/wiki and the public manual wiki,
update affected sources, and record each outcome. No PR has been requested or created.
Build/test results, review verdict and UI approval will be recorded here as they occur.

## Implementation evidence (in progress)

- Baseline Release build passed. First command/direct-build tracer failed because GRADINGGROUP
  did not exist, then passed after implementation (qt-tests-99860.log).
- Membership transfer/undo/DWG tracer failed on missing persisted group metadata, then passed
  (qt-tests-100484.log). A transient IO compile failure was corrected before verification.
- Adjoining open gradings produce the hand-computed 1500 ft3 fill and no shared-edge gap.
  Explicit member rebuild initially left the group unchanged; the corrected test passes
  (qt-tests-101496.log). The original open-line fixture incorrectly used Enter instead of END;
  the fixture was corrected, and the volume assertion now checks the numeric result independently
  of the display precision.
- Direct geometry tests caught a float narrowing in the reused constraint helper; removed the
  narrowing to retain REQ-101 precision. Conflicting elevations and malformed strips preserve
  the previous TIN. Focused direct test passed (qt-tests-83408.log).
- Ribbon Group command test passed and captured normal tools, design result, volume report,
  empty group, Surface Manager and invalid-member states (qt-tests-81180.log).
- Independent code-review skill reviews ran against initial ecee85c1 and uncommitted worktree
  changes (the mandatory UI gate prevents committing before user review). Standards: three
  blocking correctness findings (payload validation before mutation, station-count bounds,
  dependent-volume parent readiness). Spec: two blocking findings (baked snapshots on repeated
  Add/transfer, same dependent-volume readiness issue). No advisory smells or optimizer scope creep.
  Corrections and regression verification are underway; review is not yet PASS.
- README and PRIVACY inspected: no update needed for this local grading feature. In-app
  Point-Groups-and-Surfaces.md updated with group management, geometry limits and VOLUMES usage.
  Public manual review/update remains required before any future PR; no PR created.

## Review corrections and traceability

Standards review: all three findings corrected. Grading payload validation now runs in
ValidateDocumentJson before state is cleared; grouped regrading checks station counts and closure,
and zero-normal stations count as failures; baked groups advance their readiness revision without
recomputing geometry. Spec review: repeated Add and transfers retain an existing member's baked
snapshot; explicit GRADING rebuild remains the refresh path. The shared parent-readiness finding
is fixed as above. These small findings were fixed and rechecked locally, without another delegate.

| Authority / acceptance | Qt test (public command, DWG or surface-build boundary) |
| --- | --- |
| REQ-404 single group surface, closed baseline interior excluded, baked source edits | gradingGroupBuildsAnOrdinarySurfaceAndDirectRebuildPreservesItsFootprint |
| REQ-404 adjoining members, existing REQ-073 fill report, explicit regrading | gradingGroupAdjoiningMembersHaveKnownVolumesAndExplicitRegradingUpdatesTheGroup |
| REQ-404 membership move, undo, DWG exact geometry/membership persistence | gradingGroupMembershipTransferUndoAndDwgRoundTrip |
| REQ-404 disconnected union, REQ-073 cut total, REQ-101 precision, invalid/conflicting strips | gradingGroupDirectBuildKeepsPrecisionAndRejectsConflictingBakedGeometry |
| REQ-404 source edits, repeated Add/transfer, dependent volume-surface updates | gradingGroupAddsAndTransfersKeepBakedGeometryAndVolumeParentsStayReady |
| REQ-001 / REQ-201 failed DWG open preserves existing drawing | gradingGroupMalformedDwgIsRejectedBeforeReplacingTheDrawing |
| REQ-398/404 degenerate grouped baseline leaves both results intact | gradingGroupDegenerateSourceEditLeavesBothGradingAndGroupIntact |
| REQ-404 grading-tool reachability, empty/error/report/manager UI | gradingGroupsAreReachableFromRibbonToolsAndShowResultEmptyAndErrorStates |

- Final Release build passed, including both application/test executables and all headless helpers.
- All eight focused tests passed together: 10 QtTest passes including init/cleanup, no failures,
  skips or blacklisting; qt-tests-99028.log under build/artifacts/qt.
- Full suite running: ctest --preset ninja-release --parallel 4;
  build/artifacts/issue-998-full-ctest.log.
- Architecture: PASS on inspection; ordinary surfaces, downward dependencies, no new entity kind,
  independent group store, mutable global, volume engine or ownership model.
- Dependencies: no additions. Performance: no renderer work added; baked readiness advances an
  integer in the existing surface tick, with no snapshot resolution or triangulation there.
- Assumptions: none affecting requirements. Workshop architectural decisions: none.
- UI review: screenshots captured under build/artifacts/Release; HTML preview rendered without
  console errors. Final publication and user approval pending. No commit or PR before that gate.

## Final verification — 2026-11-02

- Full Release suite: PASS, 191/191 CTest tests, zero failures, 204.28 seconds. Includes both
  Catch2 shards, all six Qt shards, headless transcripts/fuzz, build.output-layout,
  qt.deployment and qt.parity-ledger. Full output: build/artifacts/issue-998-full-ctest.log.
- Standards: 3 original findings, all corrected and verified; 0 unresolved. Worst remaining: none.
- Spec: 2 original findings, all corrected and verified; 0 unresolved. Worst remaining: none.
- Build, architecture, code quality, dependencies and tests: PASS. No performance improvement
  claimed; no new rendering or per-frame geometry work. No dependencies or technical debt added.
- Requirement acceptance: implementation and automated checks satisfied; required user UI
  approval remains outstanding. Task is not marked done and no commit/PR has been made.
- #813 remains a placeholder. Its objective and adjustable constraints still need a separate
  accepted requirement after this prerequisite is approved/completed.
- UI delivery verified: html_preview reported no console errors; html_render successfully showed
  '#998 — Grading groups UI review' in chat (attachment b98204b3-341f-4789-a2b2-d675318f69f8-724e0524-30b5-4913-916f-ff6d3d7c5cb5-html).
  Review HTML: build/artifacts/issue-998-ui-review.html. Waiting for user approval or requested tweaks.

## UI approval and local completion

- User approved the presented UI on 2026-11-02: "UI looks okay. but what about other grading tools.
  not just groups?" The follow-up asks about the remaining grading workflow; it does not request
  a UI adjustment. Confirmed all four existing grading ribbon controls launch REQ-398's create/edit
  workflow, and #813 optimization remains separately unspecified.
- UI gate satisfied. Commit #998 on the current feat/issue-813 branch per the implement skill.
  Full Release verification remains 191/191 passing; no source changes followed that run.
- No PR opened; external manual review/update remains a prerequisite to a future PR.
