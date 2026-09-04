# TASK-201 — closed sweep paths: full-circle and multi-segment loops (REQ-315, issue #259)

## Requirement authority

- **REQ-315** — Sweep and loft, accepted/delivered (#241, PRs #244–#251). Its own Revisions line
  named this as deferred: *"a full-turn arc segment"* alongside twist-on-curved-path and mitred
  corners (each its own later increment). This task closes that one item.
- **GitHub issue #259** — the tracking issue for all three deferred items. User chose to start with
  the full-turn arc case (2026-09-04), and — when asked whether to scope it to a single full-circle
  arc segment or also multi-segment paths that close into a loop — chose the broader scope.
- **A SPEC amendment is part of this task.** REQ-315's Statement and Acceptance said nothing about a
  closed path; both gained a bullet (spec/requirements.md, 2026-09-04 revision) before this file was
  written, per CLAUDE.md's authority order.

## Scope, as decided

1. **A full-circle single arc segment** (`SweepPath` with one `SweepSegment{arc=true}` whose sweep is
   a full turn, `points[0] == points[1]`) sweeps to a closed solid — no end caps, matching
   `brep::Revolve`'s existing full-turn treatment.
2. **A multi-segment path that returns to its own start** (`points[0] == points[np-1]`, each segment
   itself `< 2π`, e.g. several bulge-polyline arcs/lines forming a closed loop) — same treatment.
3. **Out of scope, unchanged:** twist / fixed orientation on anything but a single straight segment
   (still refused — a closed path is never a single straight segment, so this is already excluded by
   the existing guard); a mitred (tangent-discontinuous) corner, including at the closing seam, which
   is checked the same way interior joints already are.

## Files / subsystems affected

- `spec/requirements.md` — REQ-315 Statement + Acceptance (done ahead of this file).
- `src/util/brep.hpp` / `src/util/brep.cpp` — `Sweep()`: relax `SegGeom`'s `< 2π` guard for the
  single-full-circle case; detect a closed path (`points[0] ≈ points[np-1]`); check tangent
  continuity at the closing seam the same way interior joints are; skip both end caps when closed;
  wrap the last ring's vertices/edges onto the first ring's instead of duplicating them (mirrors
  `Revolve`'s `full` branch — same file, same pattern).
- `src/commands/CadCommands.cpp` — `SweepPathFromSelection`: accept a full-circle `Arc` entity
  (`|a.sweepRad| ≈ 2π`, already flows through the existing Arc branch once the kernel guard is
  relaxed — no command-layer change needed there); accept a `Circle` entity as a path (new branch,
  same shape as the arc case: one segment, full sweep); accept a **closed** `Polyline` as a path
  (removes today's blanket refusal, replaced with a closed-loop path build — reachable only when it
  is not the first closed shape in the selection, since `GatherSweepInputs` still tries the profile
  role first for every selected entity in order).
- `tests/BrepTests.cpp` — new `[req315][sweep]` cases: full-circle arc-path sweep equals a full-turn
  revolve (Pappus check, mirrors the existing partial-arc-path case at line ~4029); a closed
  multi-segment (bulge polyline) path sweep validates with no cap faces and the expected face/edge
  count; a closed path with a sharp (non-tangent) closing seam is refused by
  `Problem::SweepPathCorner`; a path that only touches its start without a tangent-continuous closure
  is refused, not silently treated as closed.

## Implementation approach

Mirror `brep::Revolve`'s existing `full` flag rather than inventing new topology:
- `SegGeom` accepts `|sweep| ≈ 2π` only when `np == 2` (a lone segment) — a full turn split across
  several segments never individually reaches 2π, so the existing per-segment bound stays correct for
  every segment of a multi-segment loop; only the single-full-circle case needs the bound relaxed.
- Closed-path detection: `ray3d::Length(points[0] - points[np-1]) < tol * (1 + scale)`.
- When closed: after the frame is carried to the last path point, it must (within
  `IsRightHandedOrthonormal`'s existing tolerance) equal the start frame — a rotation-minimizing frame
  around a planar closed path closes by construction; a non-planar closed path that does not close
  consistently is refused (reuse `Problem::SweepPathCorner`, since a frame mismatch at the seam is the
  same kind of discontinuity the interior-joint check already names).
- Topology: build rings `0..np-2` only; ring `np-1`'s vertices/edges are the SAME ids as ring 0's
  (`ringV[np-1] = ringV[0]`, `ringE[np-1] = ringE[0]`) rather than newly created — this is exactly
  `Revolve`'s `idx = V[k][0]` wraparound, applied to sweep's per-point rings instead of per-station
  rings. No cap faces are pushed.

## Test approach

`BrepTests.cpp`, `[req315]` tag, new cases as listed above. Volume checked against Pappus's theorem,
the same way the existing partial-arc-path case already is — **not** against `brep::Revolve` run on
the same shape, which turned out to be the wrong comparison: `Revolve` requires the profile to
*touch* its axis (a single-valued outer curve from the axis), the opposite of what `Sweep` already
requires of an arc segment's axis (`Problem::SweepProfileTouchesAxis`). A closed-path sweep therefore
builds a shape — a hollow ring clear of its own axis — that `Revolve` cannot build at all for
comparison; this was caught by the first test draft actually running it, not by re-reading the code,
and the spec's Acceptance bullet was corrected to say so before this file was closed out. Face/edge
count and Euler characteristic (0 — genus 1, no caps) checked directly. Existing suite must stay
green.

## What was built

| file | change |
|---|---|
| `spec/requirements.md` | REQ-315 Statement + two Acceptance bullets for a closed sweep path; Revisions line |
| `src/util/brep.hpp` | doc comments — `SweepPath`, `Sweep()` — updated for the closed-path case |
| `src/util/brep.cpp` | `Sweep()`: single-full-circle-segment split into two half turns (avoids a literal 2π edge, the same reason `Revolve` splits its own full turn); closed-path detection; closing-seam tangent check; closing-frame check; ring `np-1` aliased to ring `0` when closed; no end caps when closed |
| `src/commands/CadCommands.cpp` | `SweepPathFromSelection`: a `Circle` entity and a **closed** `Polyline` now build a closed path (an open polyline is unchanged); a full-sweep `Arc` entity already flowed through unchanged once the kernel guard was relaxed |
| `tests/BrepTests.cpp` | 4 new `[req315]` cases: full-circle arc path (Pappus, capless, genus 1), multi-segment closed loop (three 120° arcs, Pappus, capless), closing-seam mitred-corner refusal, (the fourth is the multi-segment case's own no-cap assertion folded in) |

## Verification

- **build-project** — PASS. Release MSVC/Ninja, clean, no new warnings.
- **testing** — PASS. `ctest` **1140/1140**. `GoSurveyTests.exe` standalone: 8,158,127 assertions /
  941 cases, all green — no regression from the ring-aliasing / cap-skipping change to the
  already-shipped open-path cases.
- **architecture-review** — PASS by inspection. No new `Problem` value (the closing-seam and
  frame-closure mismatches reuse `SweepPathCorner`, which already names exactly this kind of
  discontinuity). No new `SurfaceKind`; the closed case reuses the same per-band NURBS construction
  with the last ring aliased rather than rebuilt. `solidpick`, the renderer and IO are unaffected — a
  closed-sweep solid is an ordinary `brep::Solid` once built.
- **code-review** — self-run at implementation time; the one finding (comparing against `Revolve`
  instead of Pappus) is recorded above and fixed before this file closed.

## Acceptance criteria status

Against issue #259's "full-turn (360°) arc path segment" item, scoped by user decision to also cover
multi-segment closed loops:

| criterion | status |
|---|---|
| A full-circle single arc segment sweeps to a valid closed solid, no caps | **met** — kernel test, command-layer `Circle`/full-`Arc` entity wiring |
| A multi-segment path that closes into a loop sweeps to a valid closed solid, no caps | **met** — kernel test, command-layer closed-`Polyline` wiring |
| A mitred (non-tangent) closing seam is refused by name, not silently built | **met** |
| Twist / fixed orientation still refused on any closed path | **met** — unchanged existing guard (`singleStraight` excludes every closed path by construction) |

## Not covered by test, stated plainly

- **The command-layer entity wiring's click/selection path itself** — a headless transcript verb for
  selecting a full-circle Arc/Circle/closed-Polyline and running SWEEP was not added; the kernel
  function they feed (`brep::Sweep`) is fully covered, and `SweepPathFromSelection`'s new branches
  mirror the already-tested open-path branches closely enough that the risk is in the kernel, not the
  plumbing — but this is GUI/manual verification, stated plainly rather than assumed.
- **A non-planar closed path's frame-closure refusal** — the safety-net check added (`SweepPathCorner`
  when the carried frame does not close) has no direct test; every closed-path test here is planar,
  where the rotation-minimizing frame closes by construction and the check never fires. Revisit if a
  future increment allows a non-planar closed path.

## Technical debt

- **DEBT-1 — no command-layer / transcript test for the new Circle-as-path and closed-Polyline-as-path
  selection branches.** The kernel is fully covered; the selection-role disambiguation
  (`GatherSweepInputs`'s greedy profile-first scan) is exercised only by code inspection.

## Architectural-boundary check

- No new `Problem` enum value — the closing-seam mismatch reuses `SweepPathCorner`, which is already
  exactly "a tangent discontinuity where two segments meet" and a seam mismatch is that.
- No new `SurfaceKind`; no change to `nurbs::` evaluators — the closed case reuses the same
  `RevolveCurve` / `RuledCurveToCurve` per-band construction, just without a trailing cap and with the
  last ring's ids aliased to the first.
- `solidpick`, the renderer, and IO are unaffected — a closed-sweep solid is a normal `brep::Solid`
  once built (same `.gs` v4 serialization, no new keys).
