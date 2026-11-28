# ADR-0011: Entity-kind operations apply similarity transforms in Document

- **Status:** Accepted (amended 2026-11-27: AutoCAD parity)
- **Date:** 2026-11-21

## Context

MOVE, COPY, ROTATE, SCALE, MIRROR, STRETCH, ERASE, extents and box collection are each a
hand-written loop over every entity kind, mostly in `src/commands/CadCommands.cpp`. COPY has its own
three duplicate engines (`DuplicateCadSelection{Translated,Rotated,Reflected}`), each building about
24 temporary vectors. The supported-kind sets drift between commands. For example, plain ROTATE and
SCALE skip filled regions, COPY and MIRROR skip PDF underlays, and STRETCH skips block references,
pipe runs, splines and CAD points. Refusals are hand-written per command
(`DropMirrorUnsupportedFromSelection`, `DropArrayUnsupportedFromSelection`,
`DropSolidsFromSelectionForTransform`, `DropSurfacesFromSelectionForTransform`).

3D is layered on top. Plan ROTATE and axis rotation are separate code paths, and uniform SCALE under
a tilted UCS runs a 2D scale followed by a separate Z pass (`ScaleSelectionZAboutBase`). The
transform preview (`src/viewport/TransformPreview.cpp`) keeps its own copies of the rotate, scale
and mirror maths, so it can show a result different from the commit. Paper space has a further
set of twin functions. Snap walks every entity array separately.

ADR-0010 put the entity store in Document. `EntityKind` and `EntityRef` already exist as the
stable-id handle (REQ-076), but they are declared in `CadCommands.hpp`.

## Decision

- **Document owns per-kind operations** in `GoSurvey/Document/EntityKindOps.hpp`. They cover lines,
  circles, arcs, polylines, ellipses, annotations, tables, block references, splines, CAD points,
  filled regions, multileaders, feature lines, solids and pipe runs. They are plain free functions
  per kind, not a virtual class hierarchy.
- **Commands adds the kinds other systems own** by calling those systems' own functions: survey
  points and alignments (Survey), PDF underlays (Pdf), and surfaces (Terrain, refused). Document
  cannot call them, because those systems depend on Document.
- **The transform is one 3D similarity transform in `double`:** rotation about any axis, uniform
  scale, translation and reflection across a plane. A general 4×4 matrix is rejected, so a shear or
  non-uniform scale cannot be expressed. Plan ROTATE is the Z-axis case. The tilted-UCS Z pass goes
  away.
- **Storage stays `float`.** Operations compute in `double` and round once per coordinate on write.
- **Each kind declares what it supports**: transformable, mirror-able, stretchable, or refused with
  a stated reason (as Surface already is under REQ-201). One check reads those declarations and logs
  the reason. A kind is never skipped silently. This replaces the per-command `Drop*` filters.
- **`EntityKind` and `EntityRef` move into Document** and become the handle that operations take.
  Commands keeps one mapping from `SelectedEntity::Type`. The two enums stay separate, because
  selection also covers kinds that other systems own.
- **COPY is "duplicate, then transform".** `DuplicateEntities(store, refs)` appends copies and returns
  their handles, and COPY transforms those handles with the same operation as MOVE. ARRAY uses the same
  pair. The selection stays on the originals.
- **The preview applies the same transform to a scratch copy** of the selected entities, then draws
  it as a wireframe. Solids, pipe runs and meshes keep transforming their cached display
  tessellation for speed, using the same transform value.
- **Paper space reuses the same per-kind operations** in paper coordinates, once model space has
  settled the interface.
- **Migration is behaviour-preserving first.** A table-driven characterisation test records what
  every operation does to every kind today. The pure move lands before any change to kind coverage.
  Coverage gaps follow AutoCAD, checked with `accoreconsole.exe`. Where AutoCAD has no equivalent,
  they follow how the same kind behaves under MOVE.

## Alternatives rejected

- **A general affine matrix:** circles, arcs and text cannot represent a shear or non-uniform
  scale, and AutoCAD's SCALE is uniform.
- **A virtual entity class hierarchy:** it changes how the store holds entities, which ADR-0010
  settled, and adds indirection without a second implementation of the interface.
- **Keeping the preview's maths separate:** the copies exist only to avoid shared state, and a
  scratch copy has no shared state. Keeping them leaves the preview free to disagree with the commit.
- **Fixing kind coverage while moving the code:** a failing test could not tell the refactor from
  the behaviour change.

## Consequences

- A new entity kind implements one set of operations and works in every transform command, the
  preview, paper space and snap.
- The per-command transform loops, the three duplicate engines and the preview's maths are deleted.
- Kind coverage becomes a declared property that the characterisation test checks, rather than an
  accident of which loops a command author remembered.
- Document gains a dependency on Geometry's math for transforms. The system map already allows it.

## Amendment (2026-11-27): AutoCAD parity decides kind coverage

The owner set the goal for issue #1577 as one-to-one parity with AutoCAD. That changes three things
above.

- **Behaviour follows AutoCAD, not today's command loops.** Phase 0's table records the drift, and
  most of the drift turns out to be GoSurvey refusing or skipping something AutoCAD does. AutoCAD
  2025 (`accoreconsole.exe`, `tools/autocad-transform-oracle/`) shows:
  - SCALE scales Z about the base point, in plan view too.
  - Hatches rotate, rotate-copy, scale (pattern scale too) and mirror. Under MIRROR the pattern
    keeps its angle (MIRRHATCH 0).
  - Multileaders rotate, scale and mirror.
  - Ellipses, text, dimensions, tables and inserts rotate about a tilted axis.
  - MIRROR keeps text, MTEXT, dimensions and tables readable (MIRRTEXT 0). Their boxes land on the
    mirror image of the old box.
  - An insert mirrors to a negative X scale.

  This replaces "behaviour-preserving first" and "follow how the kind behaves under MOVE" as the
  rule for kinds AutoCAD has. Phases 2a and 2b ship as two PRs: PR 1 adds the per-kind operations
  for every Document kind, with no command change. PR 2 moves MOVE, COPY, ROTATE, SCALE, MIRROR
  and ARRAY onto them and changes the phase 0 rows. Each changed row cites the oracle output.
- **The similarity transform is uniform in all three axes.** The plan-view "elevations untouched"
  carve-out for SCALE (REQ-329) goes, because AutoCAD has none.
- **A kind refuses only what its stored data cannot represent, and says why.** Text, MTEXT,
  dimensions, tables and multileaders store no plane normal, so they refuse rotation about a tilted
  axis until they gain one. AutoCAD can do it, so that is a known parity gap, tracked as a follow-up.
  A Position Marker stays move, copy and erase only (REQ-359). Surfaces and point clouds stay
  refused (REQ-201). Survey points and feature lines follow the same uniform transform in X, Y and Z
  as the CAD linework they are drawn on, so a breakline stays on its polyline through SCALE.
  Alignments are horizontal and take only transforms that keep plan view.

## Amendment (2026-11-28): where the preview lives (issue #1700 phase 3)

- **Commands builds the preview's scratch copy.** `CadActiveTransformPreviewPlan` returns the
  transform(s) a pick at the cursor would commit, built by the same code as the commit (for example
  `CadRotateAxisUnit` and `CadMirrorTransform`). `BuildCadTransformPreviewScratch` copies the
  selection into a scratch `DrawingContent` with Document's `CopyEntityInto`, applies the commit's
  refusal check, and transforms the copies with `TransformEntity`. The gizmo ghost, the text ghost
  and the PDF ghost use the same plan.
- **Interaction draws it.** The per-kind wireframe stays in `src/viewport/TransformPreview.cpp`,
  because it is the selection highlight's drawing: one function now draws both, from the live
  drawing or from the scratch copy. Renderer was rejected: it would need Commands' selection types
  and the display caches, and draws only what Interaction hands it.
- **Solids, pipe runs and meshes are not copied.** Their existing display wireframe (or a mesh's
  own triangles, or its box above 20,000 triangles) is moved by the same transform. A solid the
  kernel would reject at commit is therefore still previewed.
- **The preview has a budget.** About a million segments across all copies; a larger ARRAY ghost
  stops adding copies there.
