# TASK-173 — Phase 4 solid feature operations: the SPEC GAP and the proposed specification (GitHub issue #147)

## Requirement authority

- **Proposed REQ-314** — Feature operations on the solid kernel: extrude, revolve, slice, and
  analytic Booleans.
- **Proposed REQ-315** — Sweep and loft (split out of #147, blocked on a freeform-surface decision).
- **Proposed ADR-046** — Feature operations on the solid kernel: analytic extrude / revolve / slice,
  and phased analytic Booleans.
- **D-2026-09-02-a** — the recorded decision behind all three.
- Builds on: REQ-313 / ADR-045 (the kernel and its validity invariants), REQ-311 (`ucs::Ucs`),
  REQ-312 (arbitrary-plane curves).
- Constraints in force: REQ-101 (±0.01 ft), REQ-201 (no silent failure), REQ-300 (in-tree kernel,
  no ACIS/OpenCascade), REQ-301 (minimal abstraction), REQ-100 profile (d).
- GitHub issue #147, Phase 4 of #120.

## The SPEC GAP

Issue #147 cannot be implemented as filed. As with issue #146 (Phase 3), there is **no accepted
requirement anywhere in `spec/requirements.md`** for the Phase 4 operations — nothing mentions
extrude, revolve, sweep, loft, Boolean union/subtract/intersect, or slice as accepted work. REQ-313
stops at the kernel and the seven primitives and names "the Phase 4 boolean result" only as a
hypothetical. The roadmap does not list Phase 4. The checklist in the GitHub issue is the issue
author's wish list, not a specification.

Per CLAUDE.md §5 the work stopped there. Two things also became clear on reading the kernel:

1. **The current kernel cannot represent everything #147 asks for.** `SurfaceKind` is
   `{Plane, Cylinder, Cone, Sphere, Torus}` and `CurveKind` is `{Line, Arc}`. Extrude and revolve
   of line-and-arc profiles stay inside that set. **Sweep and loft do not** — a general swept or
   lofted surface is freeform. **General analytic Booleans do not either** — a plane cutting a
   cylinder obliquely produces an elliptical edge, and two non-coaxial cylinders produce a quartic
   edge, neither of which is a line or an arc.
2. **Analytic Booleans are the single hardest thing in all of #120**, by the issue's own words.

## Questions put to the user, and the answers

Each was explained in plain English before being asked (CLAUDE.md §6).

1. **How to proceed on Phase 4.** Chosen: *draft the spec first, then implement one operation per
   PR.* Rejected: extrude-only minimal spec; full Phase 4 in one pass.
2. **How the Booleans are computed.** Chosen: *analytic B-rep* — surface-to-surface intersection in
   closed form, curved faces stay curved, volume stays exact, consistent with ADR-045. Rejected:
   mesh-based Booleans (robust and shippable, but the display mesh becomes part of the model and the
   volume becomes approximate); hybrid mesh-cut-then-refit; deferring Booleans entirely.

The user was told, and it is recorded in ADR-046, that the analytic route needs a general
intersection-curve type added to the kernel and is genuine commercial-CAD-kernel-scale work — hence
the phasing in ADR-046 (c).

## What this task delivered

Specification only — **no code**.

- `spec/requirements.md` — proposed **REQ-314** (extrude, revolve, slice, phased analytic Booleans)
  and proposed **REQ-315** (sweep and loft, parked).
- `spec/architecture.md` — proposed **ADR-046**, including the delivery-order increment plan and the
  unresolved freeform-surface open question that blocks REQ-315.
- `spec/project.md` — decision-log entry **D-2026-09-02-a** (status: proposed).
- This task file.

## Delivery order (each a later task and its own PR)

1. **Extrude** — straight, single-loop profile, no taper.
2. **Revolve** — line and arc profiles, full and partial; plus the extrude taper option.
3. **Slice** — by plane, one side or both (the stepping stone to Booleans).
4. **Booleans Increment B1** — operand pairs whose intersection curves are all lines/arcs; others
   refused by name.
5. **Booleans Increment B2** — general analytic intersection-curve type; refusals lifted pair by
   pair.
6. *(REQ-315, separate ADR revision)* — freeform surfaces, then sweep and loft.

## Status

**Accepted as written by the user, 2026-09-02.** REQ-314 and ADR-046 are accepted; REQ-315 is
accepted as a parked scope holder (its Statement is still blocked on the freeform-surface question).
Issue #147 stays open, now tracked by REQ-314 + REQ-315. The first implementation task is extrude
(increment 1), filed separately.

## Verification

Not applicable — this task changes only `spec/` and adds this task file. No build, no tests.
The verification that matters here is the user confirming the proposed requirements match intent.
