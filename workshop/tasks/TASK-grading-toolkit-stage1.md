# Grading toolkit — Stage 1

- Type: feature
- Status: implement (pre-implementation Verification APPROVE below)
- Authority: accepted REQ-456–458, D-2026-11-02-a, ADR-074 and spec/grading-toolkit.md.
- User decision: Full toolkit in stages, keeping explicit rebuilding. This task is the first
  increment; REQ-459/460/461 remain sequenced follow-ups and #813 is separately scoped.
- Goal: create/edit practical pad, shoulder and elevation gradings through a native palette.
- Constraints: REQ-101 precision, REQ-076 stable ids, REQ-201 visible errors, REQ-300/301,
  ADR-044 DWG payload, ADR-070 ordinary/baked surfaces, existing ownership/thread boundaries.

## Plan and required tests

1. Extend grading metadata/state and optional DWG fields with target mode/value/distance grade.
   Test the command boundary first: signed width/grade, absolute and relative targets without a
   surface; invalid values preserve geometry; save/reopen/edit keeps definitions and identity.
2. Reuse the existing outward normals and commit transaction for each target, keeping surface
   projection untouched. Reuse shared group rebuilds when a grouped result is explicitly rebuilt.
3. Add GRADINGINFILL for selected simple closed feature lines. Persist an infill member marker
   and allow arbitrary bounded polygons in the existing surface-input assembly. Validate geometry
   and build candidates before committing. Tests: known rectangular area/fill, disjoint gap,
   malformed/open boundaries, conflicts, explicit refresh, removal/transfer, undo, DWG persistence.
4. Make ERASE remove selected member contributions in the same transaction as deletion. Validate
   candidate groups first, account for simultaneous surface deletion by stable id, and preserve
   missing-baseline reporting for grading results that remain in the drawing. Tests: grading and
   infill deletion removes earthwork, undo restores both, failed rebuild preserves everything.
5. Add a reusable nonmodal Qt palette through the four existing ribbon controls. Keep typed
   GRADING commands and existing integration routes working. Qt builds/submits commands and
   displays status; it does not calculate grading geometry. Capture surface/non-surface modes,
   edit, no-source/empty, group/infill and invalid-input states on the hidden desktop.
6. Update in-app help; build all Release targets, focused Qt cases each slice, full suite once
   after implementation; perform the code-review skill's two-axis review; fix findings; publish
   one labeled HTML screenshot visualizer and stop for mandatory approval before committing.

## Pre-implementation verification

- Architecture: existing feature-line definitions, surface metadata, Commands/IO/Qt ownership
  retained; representation explicitly defined by ADR-074. No new dependency, store, global,
  entity kind, ownership layer, volume engine or automatic dependency graph.
- Risks: wrong slope sign; inadvertently requiring terrain for algebraic targets; silently filling
  pad interiors; self-crossing/overlapping infill; partial load/delete mutations; stale palette
  indices across undo/document switches; failure/cancel changing geometry. Address with finite
  validation, stable IDs, candidate builds and existing undo/payload machinery.
- Numeric examples and expected failure behavior are in spec/grading-toolkit.md. They are
  observable at the already agreed public command, direct surface-build, DWG and Qt boundaries.
- Verdict: **APPROVE**. Scope, requirement authority, boundaries, numerical semantics,
  representation, risks and tests reviewed before implementation. No additional user gate is
  needed until the Qt screenshot review, unless a new specification gap is discovered.

## Progress / verification evidence

- Initial implementation base: cd8372ab. #998 baseline: Release build and 191/191 CTest green.
- User-approved test seams from #998 continue: public commands, direct surface-building
  functions, DWG save/reopen, Qt integration/screenshots. No internal worker mocks.
- Implemented target-kind metadata for Surface, Distance, Absolute elevation, and Relative
  elevation. Distance accepts positive width plus signed percent grade without terrain; elevation
  targets use cut slope when rising and fill slope when descending. New fields are additive in the
  DWG payload and validated before replacing the open drawing. Legacy surface grading retains its
  old prompt spelling and behavior.
- Implemented explicit `GRADINGINFILL` on selected closed lines, with finite/simple polygon checks,
  constrained TIN clipping, arbitrary polygon persistence, transfer/refresh behavior, and an
  atomic candidate rebuild. `ERASE` now removes selected member snapshots and rebuilds affected
  groups in the delete undo transaction; undo restores both.
- Added a reusable nonmodal Grading Tools palette and connected the existing GRADING ribbon routes.
  It exposes target inputs, open-side choice, group Add/Remove/List/Infill/Delete, and command
  result status. Qt remains an adapter around shared command and surface-building services.
- Focused Qt verification: 6 passed, 0 failed, including direct surface build, known 1,000 ft³
  infill, surface clipping, self-intersection refusal without mutation, Distance/Absolute/Relative
  geometry, DWG save/reopen, ERASE/undo, and palette states/screenshots.
- First full-suite attempt found the existing REQ-371 prompt wording contract. The prompt now keeps
  `[Cut/Fill/Surface]` intact and presents Target separately; the transcript passes.
- Code review found stale palette selection, incomplete restoration of saved target values, ignored
  open-side edits, missing no-surface feedback, insufficient polygon-touch checks, and missing
  successful-flow screenshots. Fixed these with stable-ID selection, saved-input restoration,
  open-side editing, visible validation, shared simple-polygon validation, and added Qt coverage.
- Final Release build succeeded. Full CTest: **191/191 passed**, including all Qt shards and
  `build.output-layout` (197.30 seconds).
- Focused coverage includes direct surface build and clipping, polygon rejection without mutation,
  target geometry, persistence, group commands, deletion/undo, palette create/edit/infill/open-side
  flows, and drawing-switch invalidation.
- Screenshot review and commit remain pending. Stages 2–4 remain outstanding until Stage 1 is
  approved and committed.

## Palette clarity revision

- User feedback asked us to use the blank top area for context and add icons and clearer wording.
- Added a short workflow introduction, target-specific explanations and value labels, H:V slope
  labels, descriptive group-tool guidance, icons/tooltips/accessibility names, and a top-aligned
  layout. Group controls are arranged in two rows to keep the action names readable.
- Release build succeeded. All six Qt editor shards passed; shard 1 was rerun alone after an
  overlapping interrupted CTest invocation caused a log lock. The three focused palette tests
  passed (5 Qt test functions including init/cleanup), generating the final screenshots.
- Awaiting user approval of the refreshed Qt screenshots before commit.
- Replaced the platform glyphs after user feedback with a coherent, palette-colored line-icon set
  drawn for the grading targets and actions. Release build and focused palette Qt tests passed;
  refreshed screenshots show the final icon set.

- User approved the final Qt screenshot review; Stage 1 is cleared to commit.
