# ISSUE-807 — Surface contextual ribbon controls

- Type: feature
- Status: implementation complete; final review and PR pending
- Opened: 2026-11-06
- GitHub: https://github.com/chetjones003/GoSurvey/issues/807

## 1. Authority

- Issue #807 names eleven controls; the user directed implementation of all and requested Autodesk
  AutoCAD/Civil 3D documentation to define missing behavior.
- **REQ-143** defines the surface contextual tab and disabled-control behavior. It is amended to list
  the new implemented actions and keep unrelated unsupported controls disabled.
- **REQ-086** defines linked surface point files. **REQ-070/071/072** define styles, contour
  extraction, and analyses. **REQ-480–502** define horizontal alignments and surface sampling.
- **REQ-373–383** define the existing project/file infrastructure; **REQ-076/ADR-027** require
  stable IDs for cross-object references; **ADR-044** governs DWG trailer persistence; **REQ-100**
  bounds surface render costs; **REQ-201** requires observable refusals.
- **REQ-145** defines session-only `QUICKPROFILE`. User decision below adds a separate saved surface
  profile/profile view. Full vertical design tangents and curves remain tracked by issue #1163.
- Official source research: [`docs/research/ISSUE-807-autodesk-surface-controls.md`](../../docs/research/ISSUE-807-autodesk-surface-controls.md).

## 2. Scope and documented behavior

The control set is: Add Labels, Add Legend, Point Files, Reduced LOD, High LOD, Crossing BL,
Visibility, Drape Image, Move to Surface, Create Profile, and Data Shortcut.

- Labels cover contour, spot elevation, slope, and watershed results; contour labels may be single or
  repeated along a guide. Legend tables cover the supported current analysis results.
- Point Files reuses REQ-086's linked surface source.
- Reduced/High LOD changes viewport display detail only; the full TIN remains authoritative.
- Crossing BL lists intersections and supports surface-local resolution or trimming without changing
  shared source geometry.
- Visibility offers point-to-point line of sight and Zone of Visual Influence, excluding corridor
  sight-distance checks.
- Drape Image attaches an external raster, maps it onto TIN triangles and clips to surface coverage.
- Move to Surface changes selected block-reference insertion elevations to the queried TIN values.
- Create Profile creates a saved existing-ground profile along a horizontal alignment and a placed
  profile view. **The user approved this scope on 2026-11-06.** REQ-145 is amended; `QUICKPROFILE`
  remains unchanged. The full vertical design profile system stays in #1163.
- Data Shortcut creates a project-owned source reference and read-only consumer surface reference.

## 3. Implementation plan

1. REQ-508…516 and amendments to REQ-143/145 specify the acceptance conditions; ADR-080 records
   ownership, identity, persistence, and rendering decisions. Plan verification is against those
   documents and the current `beta` codebase before implementation.
2. Reuse REQ-086 point-file actions, `src/alignment` and `alignment::SampleGround` (REQ-480…502),
   `CadBlockRef`, `CadTable`, surface-query and project-file modules. Add only the missing stores,
   commands, renderer paths, and UI; keep derived caches separate from authoritative data.
3. Implement in dependency order: (a) labels, legends, LOD, block placement; (b) breakline resolution
   and visibility; (c) raster attachment/draping; (d) saved surface profiles/views and project
   surface references; (e) ribbon actions and UI states. A control remains disabled until its backing
   behavior is complete.
4. Add happy-path, refusal/edge-case, persistence, accuracy, performance and ribbon-wiring tests for
   each requirement. Prove Reduced LOD leaves query/pick/export data unchanged.
5. Run the Windows Release build and applicable CTest suites; review all acceptance criteria and
   architecture/code/testing standards.
6. Review README, PRIVACY, `resources/wiki/`, and the public manual wiki; update affected sources and
   record all outcomes before the PR.
7. Rebase on `beta`, commit, push, open a labeled PR to `beta`, complete final review, and babysit CI
   and reviews through the merge gate.

## 4. Decisions

| Decision | Resolution |
|---|---|
| Create Profile must include the documented alignment-based surface-profile workflow. | User approved on 2026-11-06. REQ-513 adds a persistent sampled surface profile and view; `QUICKPROFILE` stays session-only. Vertical design tangents/curves remain in #1163. |
| Point Files uses linked surface definition data. | Already settled by REQ-086; ribbon entry reuses that behavior. |
| Move to Surface applies to block references and changes insertion Z only. | Directly follows Autodesk's documented target and operation; REQ-512 specifies atomic failure behavior. |
| Reduced/High LOD is render-only. | Preserve full TIN accuracy for queries, picks, plots and exports; persist the display choice per surface (REQ-508/ADR-080). |
| Other controls follow documented surface workflows adapted to GoSurvey. | REQ-515/516 cover supported labels and analysis tables; REQ-509 resolves crossings without changing shared geometry; REQ-510 offers point and area visibility checks; REQ-511 attaches external rasters; REQ-514 creates read-only project surface references. |

## 5. Initial slice verification (superseded)

- Research: PASS; official Autodesk source URLs and limitations are recorded in the source note.
- Plan: PASS (`verification/reviews/ISSUE-807-plan-review.md`).
- First PR slice implementation: Point Files opens the existing linked-source manager; Reduced/High
  LOD persists per-surface render detail and preserves authoritative TIN query data and full-detail
  plot geometry; Move to Surface atomically raises selected block references to sampled elevations.
  Typed command forms are available for LOD and Move to Surface. Labels, legend, crossing breaklines,
  visibility, image draping, saved profiles and project data shortcuts remain for follow-up PR slices.
- First-slice proof: `.\build.bat` PASS after review fixes; two Catch2 shards PASS (including PDF
  parity, typed command routing, persistence and dirty-state coverage); output-layout previously PASS;
  focused surface/ribbon coverage previously PASS (133 assertions across three cases). A selected-surface
  ribbon screenshot is captured at `build/artifacts/qt-editor/manual/shard-5/artifacts/issue807-surface-context.png`.
- Visual comparison: INCONCLUSIVE because Civil 3D is not installed (only its Object Enabler is
  present); evidence is in `build/artifacts/issue807/visual-comparison.md`.
- Product documentation review: README and PRIVACY reviewed; no updates needed. In-app wiki updated
  in `resources/wiki/Point-Groups-and-Surfaces.md`; public manual wiki updated in commit `34d8749`
  and refreshed with typed command forms before this slice's PR.
  The implementation and checks below supersede this first-slice snapshot.

## 6. Final implementation and verification

- All eleven controls are implemented: Add Labels, Add Legend, Point Files, Reduced/High LOD,
  Crossing BL, Visibility, Drape Image, Move to Surface, Create Profile, and Data Shortcut. Saved
  surface profiles and profile views are separate from the existing session-only `QUICKPROFILE`;
  data shortcut consumers are read-only references with source/destination-origin handling.
  Unsupported unrelated controls remain disabled per REQ-143.
- Four change-request review rounds have completed; all findings were fixed. The fourth round found and the
  implementation now validates contour-label associations after terrain changes, clips ZVI cells
  to the requested circular footprint and refuses empty results before taking undo state, and
  excludes attached model images from templates. Regression tests cover single and guide contour
  labels after a surface elevation change, ZVI boundary and uncovered-area behavior, and template
  export with a draped raster. Earlier third-round findings found and the
  implementation now covers the drawing-switch drape cache key, profile selection remapping, legend
  local coordinates, shortcut-dependent refresh, unavailable profile sources, fixed-TIN alignment
  profile refresh, and raster attachment repointing. Earlier findings included terrain texture
  vertex layout and PDF layout restoration, profile gap persistence, project-origin transforms,
  read-only reference rebuild guards, asynchronous label/profile refresh and worker generation
  invalidation, world/local crossing conversions, face-intersection sight analysis, guide-path contour
  labels, unavailable linked labels, and content-sensitive legend refresh.
- Proof: Release build PASS; focused Issue #807 and project-file tests PASS (3,736 assertions in 16
  test cases); pre-rebase full `ctest --preset ninja-release` PASS (194/194). On the rebased head, the
  Release build PASS; full CTest had one Qt shard timeout (193/194 passed), then isolated shard 3
  rerun PASS (57 functions in 153 seconds), covering all 194 tests across those two runs.
  `build.output-layout`, `qt.deployment`, and `qt.parity-ledger` passed on the full run;
  `git diff --check` PASS.
- Hidden-desktop Qt screenshots are in
  `build/artifacts/qt-editor/manual/shard-5/artifacts/`: `issue807-surface-context.png`,
  `issue807-no-surface-selection.png`, and `issue807-surface-visibility-menu.png`. They are grouped
  in the T3 visualizer; test artifacts are not committed.
- Product documentation review (REQ-453): README and PRIVACY inspected; no updates needed. All pages
  under `resources/wiki/` inspected; `Point-Groups-and-Surfaces.md` updated. The public manual wiki
  was inspected and updated in commit `35ce287` (GoSurvey wiki repository); its surface page and
  command forms were checked against this implementation. No other manual pages needed changes.
- Autodesk visual comparison: INCONCLUSIVE because Civil 3D is not installed (only its Object Enabler
  is present); see `build/artifacts/issue807/visual-comparison.md`. No comparison pass is claimed.
- Final implementation verification: PASS. Findings from all four change-request review rounds are fixed and covered by regressions. The fifth and final diff review approved the implementation with no findings. PR checks and merge remain. The recorded Civil 3D comparison is inconclusive; no comparison pass is claimed.
- PR-agent follow-up findings were checked against the code and fixed: shortcut creation now refuses
  dirty source drawings and surfaces without a saved stable ID; profile-source recovery contributes
  to render-cache invalidation; crossing corrections split at the recomputed world-space intersection
  after source edits; profile graph coordinates retain double precision. Regression coverage passes:
  Release build plus 99 assertions in five focused REQ-509/513/514 test cases, including large-coordinate
  short profile segments. The PR full-validation workflow is rerunning on the fixes.

- Round 5 (PR #1168 review on 53242e4, rebased onto beta): visibility now refuses a surface without a built TIN and refuses (rather than skipping) an intersected interval whose elevation cannot be evaluated; linked legend tables show "Analysis / Surface value unavailable / —" after their source surface is deleted; Drape Image converts the entered insertion point from world to drawing-local coordinates before attaching; profile source recovery retries only the recoverable source-missing/unavailable states, so a persistent "alignment has no covered stations" failure is not re-sampled every tick. Regression tests: `[req516]` legend invalidation, `[req508]` LOD geometry, `[req510]` TIN-less refusal, and the Qt `issue807DrapeImageBuildsTerrainTextureGeometry` path. Status: full `ctest --preset ninja-release` PASS on the rebased head (194/194, `build/artifacts/issue807/ctest-full.log`).
- Autodesk visual comparison (CLAUDE.md §3.6): SKIPPED by the user's direction on 2026-11-07. Civil 3D is not installed on this machine, so no comparison pass is claimed; the earlier record stays inconclusive. Panel scoring was not run.
