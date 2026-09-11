# TASK-187 — REQ-315 unblocked: the freeform-surface decision and the loft/sweep specification (GitHub issue #147)

## Requirement authority

- **REQ-315** — Sweep and loft on the solid kernel. Was *accepted, blocked* since 2026-09-02;
  this task writes its Statement and Acceptance and moves it to *accepted*.
- **ADR-048** — The kernel's freeform surface: a hand-rolled minimal NURBS patch, numerically
  integrated; REQ-315 delivers loft then sweep.
- **D-2026-09-03-b** — the recorded decision behind ADR-048.
- Amends: **ADR-045 (b)** (the closed-form mass-property rule widens to cover a `SurfaceKind::Nurbs`
  face — the carve-out D-2026-09-02-i first opened); **ADR-046** (open question resolved, delivery
  order item 8 filled in).
- Builds on: REQ-314 / ADR-046 (the feature-operation layer), REQ-313 / ADR-045 (the kernel),
  REQ-311 (`ucs::Ucs`), REQ-312 (arbitrary-plane curves).
- Constraints in force: REQ-101 (±0.01 ft), REQ-201 (no silent failure), REQ-300 (in-tree kernel,
  no third-party geometry library), REQ-301 (minimal abstraction), REQ-100 profile (d).
- GitHub issue #147, Phase 4 of #120.

## The SPEC GAP

Issue #241 (`/implement-issue 241`) asked to implement REQ-315. It could not be implemented: the
requirement's Statement read *"To be written once the freeform-surface decision is made"* and its
status was **blocked** on ADR-046's open question — how the kernel represents a surface that is none
of ADR-045's five analytic kinds (plane, cylinder, cone, sphere, torus). Per CLAUDE.md §5 the work
stopped and the choice was put to the user.

## Questions put to the user, and the answers (2026-09-03)

Each was explained in plain English before being asked (CLAUDE.md §6).

1. **How to represent a freeform surface.** Chosen: **NURBS** — a rational B-spline patch. Rejected
   by the user's earlier direction: a tessellated mesh fallback (drifting volume, model-embedded
   tessellation, the ADR-045 alternative-(2) failure).
2. **How much of NURBS to build.** Chosen: **minimal subset** — only the forms loft and sweep
   generate (degree ≤ 3, rational weights only for arc profiles, untrimmed, seam-split). Rejected:
   a full general NURBS modeller now (speculative, CLAUDE.md §7).
3. **Hand-rolled or vendored.** Chosen: **hand-rolled, in-tree** (`src/util/`, no library). Rejected:
   vendor OpenNURBS / tinynurbs (REQ-300, the standing in-tree-kernel commitment).
4. **Which operation first.** Chosen: **loft first**, sweep second — loft needs no path or
   orientation machinery and exercises the new surface type in isolation.

Mass properties for a NURBS face were **not** a fresh question: D-2026-09-02-i already established
adaptive numerical quadrature for a non-analytic face; ADR-048 widens that clause to name the NURBS
face too.

## What this task delivered

Specification only — **no code**.

- `spec/requirements.md` — **REQ-315** Statement + Acceptance written; status *blocked* → *accepted*.
  Two backward references in REQ-314's scope notes updated.
- `spec/architecture.md` — **ADR-048** added; **ADR-045 (b)** quadrature clause widened; **ADR-046**
  open question marked resolved, delivery-order item 8 filled in, "still not addressed" note updated.
- `spec/project.md` — decision-log entry **D-2026-09-03-b** (status: accepted).
- This task file.

## Delivery order (each a later task and its own PR, mirroring REQ-314)

1. **Loft** — `SurfaceKind::Nurbs` + the evaluator (Cox–de Boor basis, rational patch eval + first
   derivatives, adaptive tessellation), the adaptive Gauss–Legendre area/volume quadrature, the
   patch validator, `.gs` `kGsFormatVersion` 3 → 4, and the `LOFT` command (typed + prompted)
   between two-or-more equal-edge-count planar profiles. Volume checked against extrude / frustum /
   barrel hand-values within REQ-101.
2. **Sweep** — the `SWEEP` command: a closed planar profile along a line / arc / bulge-polyline path
   with a rotation-minimizing frame (double-reflection) plus optional constant twist and a
   normal-to-path vs. fixed-orientation option. Asserted to agree with extrude (straight path) and
   revolve (planar arc path) where the analytic result exists.

## Status

**Accepted as written by the user, 2026-09-03.** REQ-315 is accepted; ADR-048 is accepted; the
ADR-045 (b) and ADR-046 amendments are recorded. Issue #147 stays open, now tracked by REQ-314 +
REQ-315. Issue #241 remains open as the REQ-315 tracking issue; its `blocked` label should be
removed and the two implementation increments filed. The first implementation task is **loft**,
filed separately.

## Verification

Not applicable — this task changes only `spec/` and adds this task file. No build, no tests. The
verification that matters here is the user confirming the four choices match intent, which they did.
