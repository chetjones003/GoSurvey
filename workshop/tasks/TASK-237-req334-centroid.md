# TASK-237 — The centroid of a solid (REQ-334, ADR-055)

- Type:    feat (new requirement + new ADR)
- Status:  review
- Opened:  2026-09-09
- Owner:   Workshop
- GitHub:  #149 acceptance 4 (3D Phase 6 — Analysis), step 3 of 6 — the phase's weight.

## 1. Authority

- **REQ-334** — the centroid. Drafted and accepted here (D-2026-09-09-h).
- **ADR-055** — quadrature over the exact surfaces, world axes, solid-local origin, own validity
  flag. Drafted and accepted here.
- **REQ-313 / ADR-045** — the kernel this extends; its volume and reference-point conventions are
  reused unchanged.
- **ADR-048** — the existing authority for adaptive numerical quadrature over an analytic surface's
  mass properties. ADR-055 (b) leans on it rather than inventing a new kind of answer.
- **REQ-101** — ±0.002 ft. The primitives land at 1e-12, nine orders inside it.
- **REQ-201** — refuse with a stated reason. Three refusal paths do.
- Constraints: CON-06 smallest change; no new dependency; no new layer.

## 2. Problem

#149 acceptance 4: *"Centroid is correct for a primitive and for a Boolean result."*

This is the one part of Phase 6 that was genuinely unbuilt. Volume and surface area were already
exact (measured, not assumed), and per-face area turned out to be computed internally already
(D-2026-09-09-g). A centroid needs the **first moments of volume**, an integrand with no closed form
written for any of the six surface kinds.

**And the obvious route is expensive.** `IntegrateFace` reaches the volume through **ten** paths:
five closed forms (plane, conical, cylinder-plane-cut, spherical, toroidal) and five numeric (the
cylinder, cone and sphere carve-outs for procedural edges, the `Nurbs` patch, the general trim loop).
Carrying a moment term through all ten is ten new opportunities for a plausible wrong centroid — the
failure mode this kernel's whole test strategy is built to catch, because a centroid that is 3 ft out
looks exactly like one that is right.

## 3. What was built

`MassProperties` gains `centroid` and `centroidValid`. A new integrator sits beside `IntegrateFace`
rather than inside it, so none of the ten existing paths were touched.

The design and its two traps came out of **P4**
(`Desktop\Notes for claude\issue149-analysis\probes\p4_centroid_quadrature.cpp`), written and run
before the implementation. Both traps were found by the probe failing, not by review:

**Trap 1 — the integrand is not frame-covariant.** `1/2 r_k^2 n_k` in one Cartesian frame is not the
k-th component of any vector that transforms into it in a rotated one. So the natural
implementation — accumulate each face's moment in that face's own frame and rotate into world at the
end, exactly as a normal or a tangent legitimately is — is **wrong**. It is also wrong invisibly:
under it an axis-aligned box, a sphere and a torus all came out **exact**, and only a tilted frame
showed it, at **3.2 ft** on a box and 2.3 ft on a pyramid. Every face now contributes in world axes.

**Trap 2 — a double-counted origin.** The probe's first planar path measured its boundary polygon
from `q`'s projection *and* added `c = O − q`. That left the box and sphere right, because their face
frames are axis-aligned and the cross terms vanish, and made a tilted box 3.2 ft wrong and a hexagonal
pyramid 0.63 ft wrong **at the origin**.

Both are now assertions rather than lessons: the tilted cases exist precisely because an
implementation carrying either bug passes every axis-aligned test.

The rest follows ADR-055: 16-point Gauss-Legendre over each curved face's parameter rectangle;
Green's theorem along the boundary for a planar face, with quadrature **per edge** so one path covers
a straight-edged face and an arc-bounded cylinder cap alike; **analytic** edge tangents, because a
difference quotient lost seven digits to cancellation at easting 2.2e6 (2.8e-5 ft of centroid error —
still inside REQ-101, and needlessly); and a volume cross-check before anything is reported.

## 4. Scope — increment 1 of 2

| covered | refused by name (increment 2) |
|---|---|
| plane bounded by one loop of line/arc edges | `Nurbs` face (REQ-315 lofts) |
| cylinder, cone, sphere, torus over the parameter rectangle | general trim loop (`paramLoops`, ADR-052) |
| every one of REQ-313's seven primitives | a face with holes |
| a Boolean result whose faces stay within the above | an `Ellipse` or `Intersection` boundary edge |

The refusals are the point. A cylindrical bore meets a box along `Intersection` curves, and a face
bounded by one has a domain this integrator does not describe — so the centroid is withheld **and the
volume and surface area are not**. That is what the second validity flag buys: one flag would have
forced a choice between suppressing two good figures and reporting a third that was not computed.

**Acceptance 4 is met**: the seven primitives against their closed forms, and a Boolean result
(box minus a rectangular through-cut) against the composite `(V₁c₁ − V₂c₂)/(V₁ − V₂)`. The Boolean
cases increment 1 does *not* reach are the ones with procedural edges, and they refuse rather than
mislead.

## 5. Tests — `BrepTests [req334]`, eight cases

| case | what it pins |
|---|---|
| seven primitives vs closed form | box H/2, wedge (−L/2+L/3, 0, H/3), pyramid h/4, cylinder h/2, cone h/4, frustum's `zbar`, sphere and torus at their centres — to 1e-9 |
| **tilted frame** | trap 1. Passes under both wrong implementations if omitted |
| **survey magnitudes**, tilted included | acceptance 8; the world-origin alternative is 46–6,978 ft out here and exact at (0,0,0) |
| translation covariance | independent of every closed form: moving a wedge moves its centroid by the same vector and nothing else |
| **Boolean result** | acceptance 4's second half, against the composite formula |
| invalid solid | no centroid |
| self-intersecting torus | no centroid — and the volume declines too, which is the contract this follows |
| uncovered faces (cylindrical bore) | no centroid, **and the volume and area still reported** |

The wedge, pyramid and frustum are load-bearing rather than decorative: a **symmetric** solid's
centroid comes out right even from a badly wrong integrand, because the errors cancel across the
symmetry. Every symmetric primitive passed under both bugs above.

## 6. Assumptions

```
ASSUMPTION-1: A 16-point Gauss-Legendre rule is exact for these integrands, not merely close.
- Because: the rule is exact to degree 31, and every integrand is a low-degree polynomial (planar
  boundary) or low-order trigonometric function (the four curved kinds) of its parameter
- Validated, not assumed: the primitives land at 1e-12 ft or better, which is round-off and not
  truncation. A rule that was merely adequate would show a residual that moved with the point count
- Risk if wrong: a systematically biased centroid, small and plausible
- Validate by: the closed-form cases, all of which are exact rather than approximate
```

## 7. Verification result

- **build-project** — clean release build, MSVC/Ninja, no new warnings.
- **architecture-review** — no new layer, dependency or abstraction. The new integrator sits beside
  `IntegrateFace` and modifies none of its ten paths; `MassProperties` gains fields rather than
  acquiring a parallel struct, for the reason ADR-055 (d) gives.
- **code-review** — the reference point, the validity gate and the refusal style are the existing
  ones. The one genuinely new idea, quadrature for a mass property, has ADR-048 as precedent and
  ADR-055 as its own record.
- **dependency-audit** — none added.
- **performance-review** — 256 evaluations per curved face and 16 per boundary edge, on a command
  the user invokes by hand. `ComputeMassProperties` is called after each solid-creating command;
  measured no perceptible change in the transcript suite's runtime.
- **testing** — full suite **1397/1397 green**, eight new cases.

COMPLETION REPORT — TASK-237 — 2026-09-09
- Requirements satisfied:  REQ-334 (new, accepted); GitHub #149 acceptance 4
- Summary:                 the volume centroid, exact for every primitive and for a Boolean result
- Tests:                   8 new cases in `BrepTests`; 1397/1397
- Verification verdict:    PASS for increment 1; the refused face shapes are increment 2
- Assumptions:             ASSUMPTION-1, validated by the closed-form cases
- Architectural decisions: ADR-055 (a)–(f), all recorded before implementation
- Dependencies:            none added
- Technical debt noted:    increment 2 — `Intersection`/`Ellipse` edges, general trim loops, `Nurbs`
- Build:                   reproducible, clean on Windows/MSVC
- Docs updated:            REQ-334, ADR-055, a traceability row, D-2026-09-09-h, this task log
