# Grading toolkit expansion — D-2026-11-02-a

Status: accepted scope, 2026-11-02. The user selected **Full toolkit in stages**, including
the recommended initial explicit-rebuild policy, after reviewing
`docs/research/civil3d-grading-tools-expansion.md` and its Autodesk-source comparison.
This extends REQ-398/404; it does not authorize automatic source tracking or optimizer #813.

## Stage 1 — usable creation and editing palette (REQ-456–458)

### Target definitions

- Surface: existing REQ-398 daylight behavior, separate positive cut/fill run:rise and no
  extrapolation. The target surface is required only for this mode.
- Distance: a strictly positive horizontal width and a finite signed grade in percent.
  At each baseline station, offset along the existing outward normal by the width;
  output elevation is baseline elevation + width × grade / 100. Zero grade is valid.
- Absolute elevation: a finite target elevation. A station below the target rises at the cut
  slope; a station above it descends at the fill slope. Horizontal offset is elevation
  difference magnitude × the applicable positive run:rise. A station already at target
  has zero offset. No existing-ground surface is needed.
- Relative elevation: a finite signed elevation change from each baseline station, with
  the same rising/cut and falling/fill slope convention. Zero change has zero offset.
- Every mode retains REQ-398's open-line side choice and closed-line outward convention,
  double-precision geometry, stable result identity on edit, cancel/failure preservation,
  explicit rebuilding, undo and document switching. All inputs must be finite; slopes
  must be positive. Reject unusable inputs before changing geometry. Group construction
  can refuse zero-area, intersecting or conflicting faces while preserving the old group.

Worked acceptance: an open level edge at 105 ft, width 10 ft and grade -2% produces an
edge at 104.8 ft exactly 10 ft to the chosen side. At fill 3:1, an absolute target of
100 ft produces a 15 ft offset. A relative target of -2 ft produces a 6 ft offset.
At cut 2:1, a relative target of +2 ft produces a 4 ft offset. Check REQ-101 tolerance,
including survey coordinate magnitudes. Invalid width/slope/nonfinite input changes nothing.

### Explicit infill

`GRADINGINFILL <group>` takes selected simple closed feature lines as interior boundaries
and adds their bounded faces to an ordinary grading-group TIN. It creates no projection or
new feature-line geometry. Boundary vertex elevations are retained; nonplanar interiors use
the existing constrained TIN triangulation. Empty/open/self-intersecting/degenerate boundaries
and conflicting elevations are reported, with no partial membership or surface mutation.
Each boundary belongs to at most one group, sharing REQ-404's transfer rule.

Infill is explicit: ordinary grading still contributes only side strips. Do not fill a
convex hull between disjoint members, or silently fill genuine holes. Each chosen boundary
adds its whole interior; nested void definitions and automatic enclosed-region discovery
are deferred to a subsequent reviewed task. Infill snapshots remain baked when boundaries
are edited; rerunning GRADINGINFILL refreshes the selected infill boundaries. Repeated ordinary
GRADINGGROUP Add or a transfer preserves an existing snapshot. Remove and ERASE remove the
boundary's contribution. ERASE does not delete other members that merely reference a deleted
baseline; their missing source continues to be reported on explicit rebuild.

Acceptance: a 20 × 10 ft rectangular boundary at 105 ft adds 200 ft² of design at 105 ft;
comparison with ground at 100 ft reports 1000 ft³ of fill using REQ-073. A pad's infill and
side strips form a gap-free design. Disjoint boundaries retain the gap. Transfer/removal,
explicit refresh, ERASE, undo, document switching and DWG reopen preserve their meanings.
Failed infill and failed deletion rebuild leave all affected geometry/groups unchanged.

### Qt palette and group-aware deletion

The four grading ribbon routes open a reusable nonmodal Grading Tools palette, retaining
the command workflow for typed/headless use. Provide baseline/result selection, four target
choices with relevant fields, surface selection, separate cut/fill slopes, width/grade or
elevation input, open-line side, Create/Rebuild, and visible operation status. Edits to fields
are drafts until applied; closing/cancelling never commits geometry. Group operations expose
the existing Add/Remove/List plus Infill and Delete, and route to shared command services.
Group selection is explicitly for group operations; Create/Rebuild does not silently assign
an ungrouped result. Provide clear selection requirements and errors without blocking dialogs.

Both palette Delete and generic ERASE remove selected member contributions and rebuild affected
groups in the same undo transaction as entity deletion, preventing invisible earthwork left
behind. Existing commands keep working. Screenshots cover surface/non-surface targets, create,
edit, empty/no-source, infill, group actions and errors. User review is required before commit.

### Representation and boundaries

Extend the existing optional CadGradingDefinition with a narrow target-kind enum, target value
and signed distance-grade value. Missing target fields in legacy payloads mean Surface.
Extend ordinary CadSurface group-member metadata with an infill marker; baked faces accept
simple polygons as well as existing quadrilateral strips. Persist additive fields through
the existing DWG payload, validate before loading clears the drawing, and preserve legacy
drawings unchanged. No new entity kind, independent group store, volume engine, mutable global,
dependency, ownership layer, or live dependency graph. Keep numerical work/validation in shared
Domain/Commands, persistence in IO, and Qt widgets as adapters. Domain errors must be observable.

## Stage 2 — reuse and elevation editing (REQ-459)

### Copy Create

Copy Create requires an existing grading result as the source and a different feature line as
its new baseline. It copies the source's target kind, target surface reference (when applicable),
cut/fill runs, open-baseline side, target value and distance grade. It does not copy the source's
baseline id, entity id, name or geometry. The source remains unchanged; the created result gets a
new stable id and is independently editable. A missing target surface is reported and never
substituted.

### Drawing-local named presets

Each drawing owns named grading presets and persists them additively in its DWG trailer. A preset
stores criteria only: target kind, target surface stable id when applicable, cut/fill runs, open
baseline side, target value and distance grade. It stores no baseline or result id. Saving a new
preset, explicitly updating a preset, and deleting a preset each make one undo step. Names must be
nonempty and unique within the drawing; a collision is refused rather than silently replacing a
preset. A preset's criteria are copied into a newly created grading definition. Later preset
changes never propagate into existing results. A preset whose target surface has been deleted is
reported on apply and does not silently select another surface. A drawing with no preset field
loads with an empty preset collection; malformed preset data is rejected before the current drawing
is replaced.

### Qt elevation table and properties

`FLELEVEDIT` opens a Qt table for the selected feature line. It displays the REQ-088 station,
elevation, length ahead, grade back and grade ahead values from the existing derived table builder.
Station and length are read-only. Elevation, grade back and grade ahead cells are editable where
a segment exists; each edit uses the corresponding existing FLELEV command/service. Raising or
lowering all elevations, and inserting or deleting an elevation point also use one existing
FLELEV command/service per action. Every applied action makes one undo step. Invalid and cancelled operations preserve
geometry and redo history. Breakline surfaces continue to rebuild through REQ-088's existing dirty
revision behavior; grading and group outputs remain baked and update only through their explicit
rebuild/member operations.

The grading properties display the source baseline, target kind and target reference/value, cut and
fill runs, and open-baseline side. Group properties display the group surface and its member feature
lines, marking infill members. Explicit Add/Remove/transfer operations update the visible membership
immediately and remain undoable. No property view creates a live link between a preset, grading,
or group member.

Acceptance: Copy Create produces a new stable result on the chosen baseline while preserving the
source definition; edits to either result remain independent. Preset create/update/delete are
undoable and round-trip through tab switching and DWG save/reopen; applying a preset copies its
values, and later preset changes leave earlier results unchanged. The Qt table matches the derived
FLELEV values, each supported edit matches its existing command/service and is one undo step, and
invalid/cancelled edits preserve geometry and redo history. Grading properties show the persisted
criteria; group properties show current grading and infill membership, and transfers/removal survive
undo and reopen. All new payload values are validated before replacing the open document.
Linked criteria, locking propagation and automatic regrading remain outside this stage.
## Stage 3 — regions, transitions and benches (REQ-460)

Provide explicit start/end station intervals, compatible-target transitions, and an explicit
operation using a result edge as the next baseline. Preserve ordinary edit-selected-result behavior.
Rebuilding a chain is explicit, ordered upstream to downstream; detect missing sources/cycles and
preserve previous results on failure. Acceptance includes hand-computed interval endpoints and
intermediate slopes, adjoining surface seams, chained bench geometry, undo and DWG reopen.
Before coding this stage, its task must specify interval ownership, closed-baseline wrap,
sampling, target compatibility, and transaction boundaries. No implicit live graph is authorized.

## Stage 4 — utilities and surface quality (REQ-461)

Add independent detached-surface snapshots, grading/group properties, clear source diagnostics,
group/selection raise-lower tools, and reviewed tessellation/corner/search controls. Snapshots
must remain unchanged after group edits; group raise/lower must rebuild affected gradings and
surfaces atomically and reuse the existing volume engine. Acceptance includes independent
snapshots, known elevation/volume changes, invalid/cancelled changes, undo and persistence.
Before coding quality controls, define their numerical semantics and tolerances in the stage task.
Classic automatic volume balancing and the separate optimization solver (#813) require their
own objective/constraints decision; they are not implicitly implemented by this toolkit scope.

## Verification and sequence

Stage 1 precedes 2, then 3, then 4. Each task has a reviewed plan, meaningful tests, full Release
build/suite, two-axis code review and the mandatory Qt screenshot approval gate. Existing agreed
test boundaries apply: public grading commands, direct surface-building functions, DWG save/reopen,
and Qt integration/screenshots. Later-stage architecture is resolved before its implementation,
without blocking already-defined Stage 1. New public geometry APIs or dependencies require review.
