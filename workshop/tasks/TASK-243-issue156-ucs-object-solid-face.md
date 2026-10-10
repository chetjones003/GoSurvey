# TASK-243 — UCS Object aligns to a planar face of a B-rep solid

## Requirement authority

- **REQ-154** (GitHub #126) — the UCS service. Its "Not in this requirement" item 4 deferred
  `Object` alignment to 3D faces/meshes/surfaces/solids, blocked on face-level picking.
- **GitHub issue #156** — the deferred acceptance item, split out of #126 as decision D-2026-08-28-n
  and deferred a second time as D-2026-08-31-d: *"implement #156 as a thin consumer"* of the
  sub-object pick once it lands.
- **REQ-318 / ADR-049** (GitHub #148) — the sub-object pick subsystem. It has landed on `beta`;
  `src/util/solidpick.hpp` names #156 as the consumer it was built for.
- **REQ-201** — a refusal the user can see, never a silent miss or a degenerate frame.
- **D-2026-09-10-a** — the recorded decision to lift item 4 for planar solid faces only.

## Files affected

- `src/commands/CadCommands.hpp` — `ProcessUcsViewportPick` gains an optional `const ray3d::Ray*`.
- `src/commands/CadCommands.cpp` — the one call site forwards `pickRay`.
- `src/commands/CadCommands_Ucs.cpp` — `UcsFromSolidFacePick` (new file-local helper);
  `UcsFromObjectPick` tries it first when a ray is available; the `default:` refusal message no
  longer claims solids are unsupported.
- `tests/headless/transcripts/issue156-ucs-object-solid-face.txt` (new).
- `spec/requirements.md` — REQ-154 amended (item 4, the `Object` paragraph, an acceptance bullet,
  the revision line, the status row).
- `spec/project.md` — decision D-2026-09-10-a.

## Existing code reused

- `PickSubObjectAcrossSolids` + `solidpick::PickSubObject` — the same pick the hover pre-highlight
  and PRESSPULL use. Called with a zeroed `solidpick::Tolerance` so only a **face** can be reported.
- `ucs::FromNormal` — the Arbitrary Axis Algorithm, already in place and unit-tested.
- `CadCoord::WorldFromLocal` — storage→world for the origin (a normal needs no rebase).
- The camera ray already threaded to `SubmitViewportPickImpl` as `pickRay` for FILLET (issue #373
  follow-up) and routed to `UCS Object` via `ViewportClickRoute::RawEntityPick`.

## Implementation approach

The smallest change: `UCS Object` already receives the pick. When the click carries a valid camera
ray (orbited / non-plan model view), try a solid sub-object face pick *before* the 2D entity pick:

- **planar face** → origin = picked point projected onto the face plane; +Z = outward normal
  (honouring `Surface::inward` exactly as the kernel does); X/Y from `ucs::FromNormal`.
- **curved face** (cylinder / cone / sphere / torus wall) → refuse with a reason; stay in the pick
  phase. A `handled` flag tells the caller a solid *was* clicked, so it does not fall through and
  quietly select a line behind the solid.
- **no solid under the ray** → fall through to the existing 2D entity pick, unchanged.

Meshes and triangulated surfaces are still refused — they resolve as one object with no planar-face
identity, and grouping coplanar triangles is a separate subsystem this issue never scoped.

## Test approach

`headless.issue156-ucs-object-solid-face` — every success assertion is on the resulting **geometry**
(REQ-154's own discipline), not just the log:

1. Align to a box's +X side face; draw at UCS (3,2,0); prove the line lands on the plane world-X=10.
2. Align to the box's top face (axis-aligned normal still works); same geometry check.
3. The frame change moves nothing (the box and the line stay put).
4. A cylinder **wall** is refused with "that face is curved"; the command stays in its pick phase;
   a follow-up click on the flat top cap then succeeds.
5. A miss falls through to the 2D path's own "no object found at that point".

## Verification

- **build-project** — PASS (Release). No new warnings from the changed files.
- **testing** — PASS. `ctest` **1441/1441** (1440 + the new transcript). `req154-ucs-plan`,
  `req318-subobject-selection`, `req313-*` all unchanged and green.
- **architecture-review** — PASS. No new module; a file-local helper in the command layer that
  calls the existing pick and the existing frame math. A UCS still never touches a stored
  coordinate.
- **code-review** — self-run. The `handled` flag is the one non-obvious point and is commented at
  its definition and both use sites.

## Assumptions

- The camera ray (`Camera::ScreenRay`) is in solid **storage** space (X/Y local to the document
  origin, Z absolute) — the same space `PickSubObjectAcrossSolids` and every other sub-object
  consumer already feed it. Verified against the hover pre-highlight, which builds its ray the same
  way.

## Technical debt

- **DEBT-1 — plan view and paper space cannot align to a solid face.** There is no camera ray to
  cast there, so `pickRay` is null and the 2D entity pick is all that applies. This matches how the
  sub-object hover and click already behave (both require an orbited model view) and is the correct
  boundary, not a shortcut — a face pick with no depth is not well posed.
- **DEBT-2 — meshes and triangulated surfaces stay refused.** Stated in the spec and the decision.
  If mesh-face alignment is ever wanted it needs a coplanar-triangle grouping subsystem; it would
  reuse `ucs::FromNormal` and this same command branch.
