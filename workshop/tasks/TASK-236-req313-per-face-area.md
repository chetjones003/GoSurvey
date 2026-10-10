# TASK-236 — A single face's area becomes public (`brep::FaceArea`, REQ-313 amended)

- Type:    feat (amendment to an accepted requirement)
- Status:  review
- Opened:  2026-09-09
- Owner:   Workshop
- GitHub:  #149 acceptance 2 (3D Phase 6 — Analysis), step 2 of 6. Kernel is REQ-313 / #146.

## 1. Authority

- **REQ-313** — the B-rep kernel, accepted 2026-09-01 (D-2026-09-01-b, ADR-045). **Amended here**
  (D-2026-09-09-g) to report a single face's area alongside the total it already reported.
- **REQ-101** — ±0.002 ft. The figures here are asserted to a relative 1e-12, far inside it.
- **REQ-201** — refuse with a stated reason. Both refusal paths do.
- **REQ-318 / ADR-049** — sub-object selection. The caller this accessor exists for: it hands you a
  face index, which is why the API is indexed.
- ADR-045 — topology is the stored truth; mass properties read it and never the recipe. Unchanged.
- Constraints: CON-06 smallest change; no new layer, dependency or abstraction.

## 2. Problem

#149 acceptance 2 asks that *"face area, surface area and volume are correct within REQ-101 for
every Phase 3 primitive"*.

**Two of the three were already delivered and exact**, which the phase's probes measured rather than
assumed: `ComputeMassProperties` integrates volume and surface area over the analytic faces and comes
back exact to double precision for all seven primitives, at the origin and at easting 2.2e6 (worst
case 1.3e-11 relative). Only the per-face figure was missing.

**And it was missing only from the header.** `IntegrateFace` has returned an area per face since the
kernel landed; `ComputeMassProperties` sums it. Nothing had to be derived — the number was already
being computed and thrown away.

**What a caller had to do instead, and why it is wrong.** The only route available was to tessellate
and sum triangles by `Tessellation::triFace`. Measured on the shipping kernel:

```
face 0  Plane      mesh area  200.739103   exact  201.061930   err -0.322826 (-0.1606%)
face 2  Cylinder   mesh area  628.066231   exact 1256.637061   (wall split at the seam: half each)
```

**A `Plane` face is not exact when its boundary is curved.** `Tessellate`'s doc says "plane faces are
exact at any tolerance", and that is true of a *straight-edged* plane — a cylinder's circular cap is
a plane face whose boundary is an arc, and the fan triangulation inscribes a polygon in it. 0.16% on
an 8-foot cap: a plausible wrong answer, which is the kind #149's tessellation note exists to keep
out of a reported figure.

## 3. What was built

```cpp
[[nodiscard]] bool FaceArea(const Solid& s, int faceIndex, double* outArea, Problem* outWhy);
```

Twenty lines in `brep.cpp`, calling the integrator that was already there with the reference point
`ComputeMassProperties` already uses — so the parts reconstruct the whole **exactly** rather than
nearly, and that identity is asserted rather than assumed.

Three decisions, recorded as D-2026-09-09-g:

**(a) REQ-313 is amended; no new REQ number is taken.** Drafting a fresh requirement would describe
behaviour the kernel has had since September 1st as though it were a new capability. **REQ-334 stays
free for the centroid**, which is genuinely new — the first moments of volume, with no closed form
written for any of the six surface kinds. That is step 3's work and deserves its own requirement and
ADR; this does not.

**(b) The accessor is indexed rather than a vector on `MassProperties`.** REQ-318's sub-object
selection hands a caller a face index, and that is the caller this exists for. Cost noted: it runs
`Validate` per call, so a caller wanting every face pays for the walk each time — fine for a
Properties panel showing one selected face, and the sum identity is what tests use it for.

**(c) It does NOT refuse a self-intersecting solid, and `ComputeMassProperties` still does.** The
one place the two contracts differ, deliberately. That gate exists because a self-passing shell
encloses part of space twice, which makes a *volume* meaningless. A face's area is not that kind of
quantity — one bounded patch of one surface, well defined whether or not another face crosses it —
and REQ-318 lets a user click exactly that face. Answering "unavailable" for a figure that is
perfectly well defined is the worse of the two wrong answers.

The consequence is stated in the requirement, the header and the implementation rather than left for
someone to discover: **on a self-intersecting solid the per-face areas sum to the true surface area
while the mass-property report says "unavailable".** A test pins it on a torus with tube radius 8 and
ring radius 5, and checks the sum against `4 pi^2 R r`.

## 4. Tests — `BrepTests [req149]`, six cases

| case | what it pins |
|---|---|
| cylinder cap and wall | the closed forms, `pi r^2` and `2 pi r h`, to 1e-12. Faces classified by **surface kind, not index**, so the assertion cannot silently start checking a different face if the builder's ordering changes |
| sum equals total, all seven primitives | the identity that makes the accessor trustworthy — same integrator, same reference point, so parts reconstruct the whole exactly. Cone, sphere and torus included because #149 names them as the tessellation-error cases |
| invariant under placement | #149 acceptance 8 in miniature: the same figures on a frame tilted off every axis at easting 2.2e6, where a formula referenced to the world origin loses its low bits |
| index and null refusals | `IndexOutOfRange` for −1, for `faces.size()`, and for a null out; and a null `outWhy` is allowed, so refusing does not require somewhere to put the reason |
| broken shell | a shell missing a face reports **`Validate`'s own reason**, not a generic one, and leaves `outArea` untouched |
| self-intersecting torus | the (c) contract, both halves: `ComputeMassProperties` declines, `FaceArea` does not, and the areas are real |

The mesh figures quoted in the comments were **re-measured against this branch's kernel**, not
carried over from the phase's planning probes: 200.739103 vs 201.061930, −0.1606%, unchanged.

## 5. Assumptions

None. Every figure asserted here is a closed form, and the one contract choice that could have gone
the other way is (c), which is recorded as a decision rather than an assumption because it is
observable behaviour rather than a guess about intent.

## 6. Verification result

- **build-project** — clean release build, MSVC/Ninja, no new warnings.
- **architecture-review** — no new layer, global, dependency or abstraction. One public function in
  the header that already owns mass properties, implemented beside the function it mirrors.
- **code-review** — the refusal shape (`fail` lambda + `Problem`) is `PushPullFace`'s, reused rather
  than re-invented. No new epsilon: the integrator and reference point are the existing ones.
- **dependency-audit** — none added.
- **performance-review** — one `Validate` walk plus one face integral per call. Noted in §3(b) that a
  caller iterating every face pays the walk per face; no current caller does.
- **testing** — full suite green, six new cases.

COMPLETION REPORT — TASK-236 — 2026-09-09
- Requirements satisfied:  REQ-313 as amended (D-2026-09-09-g); GitHub #149 acceptance 2
- Summary:                 a single face's area is public, exact, and refuses by name
- Tests:                   6 new cases in `BrepTests`; full suite green
- Verification verdict:    PASS
- Assumptions:             none
- Architectural decisions: (c) — one contract difference from `ComputeMassProperties`, recorded
- Dependencies:            none added
- Technical debt noted:    §3(b) — `Validate` runs per call; revisit if a caller needs every face
- Build:                   reproducible, clean on Windows/MSVC
- Docs updated:            REQ-313 acceptance + scope boundary + revisions, a new traceability row,
                           spec/project.md decision D-2026-09-09-g, this task log
