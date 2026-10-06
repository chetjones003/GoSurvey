# Plan: 3D Mass Properties — moments of inertia and principal axes (Issue #460)

## Goal
Implement the deferred half of `#120` Mass Properties: uniform-density **moments / products of inertia** (`Ixx, Iyy, Izz, Ixy, Ixz, Iyz`) and **principal axes / principal moments** for every `brep::Solid`, completing what `REQ-334` (centroid, GitHub #149 acceptance 4) left as GitHub issue #460. Reports are exact analytic integrals, accurate at survey magnitudes, with deterministic degenerate handling and a user-readable surface.

## Success Criteria
- `brep::ComputeMassProperties` (or an articulated companion) returns a **centroidal inertia tensor** `I_c` that matches the closed form for each of the 7 primitives to a stated **relative** tolerance (not REQ-101's ±0.002 ft) — verified on axis-aligned **and tilted** frames.
- `I` about an **arbitrary point** matches the **parallel-axis transfer** `I_p = I_c + m*[(d·d)E - d d^T]` where `d = p - centroid`, `m = density*volume` (unit density), within the same tolerance — checked at the origin and at a survey point.
- **Principal axes** are the eigenvectors of `I_c`, **orthonormal and right-handed**, with eigenvalues matching the analytic principal moments. Jacobi eigendecomposition with no external dependency.
- **Degenerate symmetry** cases return a **deterministic, orthonormal basis** that round-trips through `.gs`: sphere (isotropic, every direction principal) and solids with two equal principal moments (cylinder, cone, regular pyramid per the issue's list) — same solid reports the same axes on two runs and after save/reload.
- **Boolean result** inertia equals the **composite** of its parts (e.g. `UNION` sum, `SUBTRACT` difference, `INTERSECT` direct) within tolerance.
- Every figure holds at **survey magnitudes** with an **asymmetric fixture** at easting ≈2.2e6 / northing ≈1.4e6 (the rule the centroid already enforces, and the place a world-origin-referenced integral fails by thousands of feet).
- A **self-intersecting** solid (tube > major, `SelfIntersects()`) is **refused** — no tensor, no axes — on the same gate `ComputeMassProperties` already uses for volume/centroid. `valid == false` path remains unchanged.
- A face shape the second-moment integrator does not cover **withholds only the inertia/axes**, leaving `volume`, `surfaceArea`, and `centroid` untouched (parallel to `centroidValid`).
- The numbers are **reported where a user can read them** (see Key Decisions).

## Context And Current Facts
- **Spec split:** `spec/requirements.md:6166` (REQ-313 scope boundary) states centroid/moments/principal axes belong to `#120` Phase 6, but Phase 6 was amended to carry only **per-face area** and **centroid**; `spec/requirements.md:6171` and `spec/requirements.md:8476` (REQ-334) both record that *moments of inertia and principal axes are carried by GitHub issue #460, split out of #149 because #149's acceptance lists only centroid*. `spec/architecture.md:2131` and `2358` list moments as a still-open item.
- **Issue #460** (`https://github.com/chetjones003/GoSurvey/issues/460`): 8 acceptance bullets — analytic tensor per primitive, parallel-axis, orthonormal right-handed principal axes + eigenvalues, deterministic degenerate cases with save/reload stability, Boolean composite, survey-magnitude asymmetric fixture, self-intersection refusal, and a reporting surface. The **Work** section names three deliverables: second moments / centroidal tensor, parallel-axis transfer, principal axes (Jacobi).
- **Existing foundation:**
  - `src/util/brep.hpp:1011` `struct MassProperties { bool valid; double volume, surfaceArea; Vec3 centroid; bool centroidValid; }` and `src/util/brep.hpp:1047` `ComputeMassProperties`. `valid` gates `SelfIntersects` (torus tube > major); `centroidValid` is a *second* flag so a face this increment does not cover withholds only the centroid (`spec/architecture.md:4038` ADR-055).
  - Volume is `V = 1/3 ∮(p-q)·n dA` with `q = ReferencePoint(s)` (mean of vertices), i.e. a **solid-local reference** (`src/util/brep.hpp:1033`, `src/util/brep.cpp:15975`). Centroid was added under ADR-055 (`spec/architecture.md:4038`) as `1/2 ∮ r_k^2 n_k dA` **in world axes** (not per-face local) and **about the same `q`** — with `Vec3 M` accumulated in world and `q` kept at model scale. ADR-055 was accepted to avoid two measured failures: per-face-frame accumulation is exact for axis-aligned solids and off by feet for tilted ones (3.2 ft for a tilted box, 2.3 ft for a pyramid), and world-origin referencing is off by 46–6,978 ft at survey magnitudes (`spec/architecture.md:4055`, issue table). The same two notes apply *worse* to second moments, which square coordinates.
  - Per-surface integrand lives in `FaceIntegrals { area, volTerm }` (`src/util/brep.cpp:363`). Volume has 10 paths in `IntegrateFace` (5 closed forms + 5 numeric). Centroid deliberately avoided adding 5 new closed forms and instead uses **quadrature over the exact analytic surface** (16-point Gauss-Legendre, `Gauss16()`, `PlanarFaceMoment`, `CurvedFaceMoment`, `IntegrateFaceMoment`, `src/util/brep.cpp:15623–15908`) — not the display mesh — with analytic edge tangents and Green's theorem for planar faces. Covered set in this increment: plane (single loop, line+arc), cylinder/cone/sphere/torus patch over its rectangle (`src/util/brep.cpp:15908`). Uncovered (`Nurbs`, general trim `paramLoops`, holes, `Ellipse`/`Intersection` edges) → centroid unavailable (`src/util/brep.hpp:15917`, `src/util/brep.cpp:15909`).
  - `FaceArea` (`src/util/brep.hpp:1073`) deliberately **does not** refuse a self-intersecting solid — per-face area stays well defined — while `ComputeMassProperties` does; the plan must preserve that asymmetry (`src/util/brep.cpp:15945`).
  - Reporting today: `src/commands/CadCommands.cpp:30121` `CadReportSolids` (`SOLIDLIST`) prints `volume, area, V/E/F counts, kind, layer` per solid. `SOLIDCHECK` (`src/commands/CadCommands.cpp:30080`) reports validity. One path formats those numbers; every solid already reaches the mass-property call site.
  - Tests: `tests/BrepTests.cpp:5840–6297` is the centroid suite — 7 primitives vs closed form, tilted frame, survey-magnitude at 2.2e6, translation invariance, `Validate` / `SelfIntersects` refusal, Boolean composite, uncovered-shape `centroidValid==false` but `valid==true`, plus `BrepJsonTests.cpp:55` round-trip and `AcisSatParserTests` volume checks.

## Constraints And Non-goals
- **Constraints in force:** REQ-101 fidelity (but a **relative** tolerance for inertia — an inertia is not a length); REQ-201 (named refusal, not silent repair); REQ-300 (no new heavy dependency — Jacobi in-tree); REQ-301 (no unearned abstraction); REQ-312/311 plane model; ADR-045 boundary-representation invariants; ADR-002 header-only testability.
- **Solid-local reference is mandatory** — see Context. Every second-moment term must be taken about the same `q = ReferencePoint(s)` that volume/centroid use.
- **No mesh approximation.** Like the centroid, sample the *analytic* surface, not the tessellation (`src/util/brep.cpp:15655`).
- **Non-goals (stay out of this plan):** general trim loops / holes / `Nurbs` / `Ellipse`/`Intersection` edges for the second moments in increment 1 — withhold, don't approximate; capping the section clip; DXF/DWG export of solids (already excluded with a counted message); view-dependent silhouettes; changing `kGsFormatVersion` (inertia is derived, not persisted).

## Key Decisions
### 1. Extend `MassProperties` with a second validity flag and centroidal + principal fields
**Choice:** add `bool inertiaValid`, `Mat3 inertiaCentroidal` (or 6 doubles `Ixx…Iyz`), `Vec3 principalMoments` (eigenvalues), `Mat3 principalAxes` (columns are eigenvectors, world axes), and optionally a transfer helper `InertiaAboutPoint(p)` or a free function `ParallelAxisTransfer(I_c, m, d)`. Keep `volume`/`area`/`centroid` untouched when `inertiaValid==false`.
**Why:** mirrors the `centroidValid` decision ADR-055 made (`spec/architecture.md:4084`) — two flags avoid suppressing good figures. Keeps the glossary the issue uses: report the **centroidal** tensor as primary (principal axes are defined there) and derive the arbitrary-point tensor.
**Rejected:** folding inertia into `valid` (would hide volume/area/centroid), or a separate `InertiaProperties` return that forces every caller to juggle two calls to report one solid.

### 2. Second moments by quadrature — same instrument the centroid used, not new closed forms
**Choice:** replicate the centroid's quadrature strategy for the second-moment integrand `∫ r^2 n dA` family, over the exact analytic surface about `q`, in **world axes**. Planar faces via Green's/boundary quadrature (extend `PlanarFaceMoment`'s six scalars to the ten needed for `x^2, y^2, z^2, xy, xz, yz`), curved faces via 16×16 Gauss over `(u,v)` (extend `CurvedFaceMoment`). `Vec3` intermediates become a symmetric 3×3 accumulator.
**Why:** five new closed forms × two integrands = ten fresh analytic derivations, each a wrong-number risk; ADR-055 deliberately chose quadrature for the first moments and the measured residual on primitives is 1e-12 (`src/util/brep.cpp:15662`) — far inside any plausible relative inertia tolerance. The volume cross-check pattern (re-derive volume and compare at 1e-9, `src/util/brep.cpp:16003`) can be extended to the second moments.
**Rejected:** deriving closed forms for plane/cylinder/cone/sphere/torus second moments from scratch (higher upfront correctness burden, same survey-magnitude requirement).

### 3. World axes, solid-local origin
**Choice:** accumulate every face's contribution in **world** (`nW`, `r = pW - q`) — not per-face local — and state the invariant in the header.
**Why:** ADR-055 §(a) proved per-face-frame accumulation is invisibly correct for axis-aligned solids and wrong by feet once rotated; the bug cancels on symmetric solids.

### 4. Eigendecomposition: Jacobi, in-tree, deterministic
**Choice:** real-symmetric 3×3 Jacobi iteration (the issue names it explicitly) in `src/util/` or `src/util/brep.*`, no LAPACK/Eigen. Return eigenvalues sorted descending, eigenvectors as a right-handed orthonormal basis. Determinism rule: for equal (or near-equal, within `ε_rel`) eigenvalues, pick a canonical basis seeded from world axes (e.g. Gram-Schmidt from an axis most aligned with a stable reference), and enforce `det == +1` (flip the third column if needed). Sort and handedness make the basis repeatable across save/reload.
**Rejected:** an external eigensolver (REQ-300), or leaving degenerate cases "arbitrary" without a repeatability contract — the issue marks those cases as the interesting ones.

### 5. Reporting surface
**Choice (recommended):** **extend `SOLIDLIST`** to print the centroidal tensor and principal axes/moments when `inertiaValid`, and add a **dedicated `MASSPROP` / `SOLIDMASSPROP` command** (one solid or selection) that prints the full block — volume, area, centroid, `I_c`, `I` about a point, principal moments + axes with units. Keep `SOLIDCHECK` for validity/self-intersection only.
**Why:** `SOLIDLIST` is the "obvious place" and the issue explicitly lists it; a one-line list entry cannot legibly hold a tensor, so both surfaces are needed. A dedicated verb matches AutoCAD's `MASSPROP` expectation and avoids widening `SOLIDCHECK`'s contract.
**Alternative if review prefers minimal surface:** dedicated command only; `SOLIDLIST` stays as is. Not recommended as the sole surface because the list is where a user already looks per solid.

### 6. Tolerance for inertia
**Choice:** a **relative** tolerance, stated in the new REQ/ADR. Proposed: `|I_actual - I_expected| ≤ 1e-8 * max(1, |I_expected|)` per component for the 7 primitives, and `1e-7` for Boolean composites and survey-magnitude runs (one order looser to absorb quadrature + composition). Document justification: inertia has units `length^5` (with unit density) so REQ-101's ±0.002 ft does not apply; scale is set by `r^5`/`h^5`, and the volume residual at survey magnitude is already 1e-11 relative (`issue #460` table) so 1e-8 leaves 3 orders of margin over the numeric floor while still catching formula errors.
**Rejected:** an absolute `±0.01 ft` style bound (unit mismatch), or reusing REQ-101 directly.

## Recommended Approach
Introduce second-moment integration alongside the first-moment one, behind the same gates, behind the same second flag, with the same reference-point and axis rules. Then diagonalize.

1. **Requirement + ADR** — write `REQ-3xx` (inertia & principal axes, GitHub #460) and `ADR-061` (≈ ADR-055 shape: integral, why quadrature, world axes + local origin, degenerate basis,Reporting). Acceptance mirrors the issue's 8 bullets and names the relative tolerance per decision 6. Status proposed → accepted per the standing arrangement for unaccepted REQs (as `spec/requirements.md:8414` notes for REQ-332/333).
2. **Kernel** (`src/util/brep.{hpp,cpp}`) — extend `MassProperties` per decision 1; add `struct InertiaIntegrals` or extend `FaceMoment` to carry `secondMoment[3][3]` (or six unique `∫ r_i r_j n_k` terms). Implement `PlanarFaceInertia` (Green's boundary quadrature — extend the six scalars to the ~12 needed for second moments) and `CurvedFaceInertia` (16×16 Gauss over the same patches the centroid uses, now accumulating `0.5 r_i r_j n_k` etc. — derive the formula from the divergence form of `I = ∫( |r|^2 E - r r^T ) dV = ∮ ... n dA`). Share `q = ReferencePoint(s)`, world accumulation, and the uncovered-shape refusal list. Extend `ComputeMassProperties` to fill inertia + run the volume + centroid + inertia cross-checks (each re-derived volume must match the reported one at 1e-9) before setting `inertiaValid`. Implement Jacobi `EigenDecomposeSym3` returning sorted eigenvalues + right-handed eigenvectors; add `ParallelAxisTransfer`.
3. **Commands** — extend `CadReportSolids` formatting and add the dedicated `MASSPROP` verb (registered in `CadCommands.*`, help text, undo-neutral). Both read the same `MassProperties` and are no-ops for a refused solid beyond the refusal line.
4. **Persistence** — none beyond the existing `.gs` round-trip guarantee: inertia is derived, so save/reload must reproduce the **same deterministic basis** for degenerate cases (asserted, not persisted).
5. **Tests + verification** — see Validation Plan.

## Work Plan
### Slice A — Requirement, ADR, and public contract
- **Files:** `spec/requirements.md` (new `REQ-3xx`), `spec/architecture.md` (new `ADR-061`), `spec/roadmap.md` if the Phase 6 line references inertia.
- **Content:** analytic expectations per primitive (closed forms for `I` about centroid and principal moments for box, wedge, pyramid, cylinder, cone, sphere, torus — at least the textbook `box: Ixx = m/12 (h²+depth²)` family), parallel-axis law, principal-axes orthonormality/right-handedness, degenerate basis determinism, self-intersection/uncovered-shape refusal, survey-magnitude asymmetric fixture, reporting verb, relative tolerance + justification. Normative: which 6-component ordering, which axis order, units note.
- **Depends on:** nothing. Blocks B and C.

### Slice B — Kernel second moments + eigendecomposition
- **Files:** `src/util/brep.hpp` (struct + `EigenDecomposeSym3`, `InertiaAboutPoint` / `ParallelAxisTransfer`), `src/util/brep.cpp` (quadrature paths, `ReferencePoint` reuse, Jacobi), `src/util/ucs.hpp` if a `Mat3` helper is needed (prefer `std::array<double,9>` or a local `Mat3` rather than widening `ucs`).
- **Key internal functions:** `PlanarFaceInertia`, `CurvedFaceInertia`, `IntegrateFaceSecondMoment` (mirrors `IntegrateFaceMoment`), `ComputeMassProperties` extension, `JacobiEigenSymmetric3x3`. Hit the same refusal list as `IntegrateFaceMoment`; leave `Validate`/`SelfIntersects`/`ReferencePoint`/`ClosestPointOnSurface` unchanged. Preserve `FaceArea` asymmetry.
- **Depends on:** A (tolerance and ordering are normative). Blocks D.

### Slice C — Reporting surface(s)
- **Files:** `src/commands/CadCommands.{hpp,cpp}` (formatting + new verb), `src/ui/` if a Properties-panel line is desired (optional increment — keep the command as the increment-1 surface and record the panel as increment 2 if scope is tight), `docs/` help text.
- **Depends on:** B for the fields. Can be developed in parallel behind a stub `MassProperties` if needed.

### Slice D — Tests, fixtures, and headless/API coverage
- **Files:** `tests/BrepTests.cpp` (new `TEST_CASE`s parallel to the centroid suite at `tests/BrepTests.cpp:5840`, plus a survey-magnitude case at `tests/BrepTests.cpp:6166`, a translation case at `6193`, and the uncovered-shape case at `6277`), `tests/BrepJsonTests.cpp` (deterministic basis survives `toJson`/`fromJson`), `tests/*SectionClip*` not needed, `src/util/brep.cpp` cross-check asserts.
- **Fixtures (must be asymmetric):** frustum / wedge / pyramid — the same lesson the issue repeats from the centroid measurements. One Boolean fixture with an off-axis feature (sphere or cylinder subtracted) per the Boolean composite criterion.
- **Depends on:** B (and C for command-level smoke tests via `gosurvey_headless`).

## Validation Plan
- **Unit (Catch2, no window — ADR-045 isolation):**
  ```bash
  ./dev/build
  ./dev/test            # or ctest --test-dir build --output-on-failure
  ```
  New cases (tag `[brep][req460]`):
  - `Every primitive's centroidal inertia matches its closed form` — 7 solids, exact closed form per kind (box/wedge/pyramid/cylinder/cone/sphere/torus), `REQUIRE(mp.inertiaValid)` and `Approx(...).epsilon(1e-8)` per component, run **axis-aligned + tilted** (use `ucs::FromNormal` tilted frame as `tests/BrepTests.cpp:6144` does), at origin and at **E≈2.2e6 N≈1.4e6** with the same tolerance. This is where a world-origin bug would fail by 10³–10⁴ ft-scale.
  - `Parallel-axis transfer matches direct evaluation` — evaluate `I_about(p)` by the same quadrature about `p` vs `ParallelAxisTransfer(I_c, m, p-centroid)`; check at `p=origin` and at an arbitrary surveyed point.
  - `Principal axes orthonormal, right-handed, eigenvalues match analytic` — `dot(e_i,e_j)≈0`, `|e_i|≈1`, `det([e0 e1 e2])≈+1`, and eigen-recomposition `R diag(λ) R^T ≈ I_c`; compare `λ` to analytic principal moments per primitive.
  - `Degenerate cases deterministic` — sphere with two independent constructions (different seam placement / tessellation not involved) returns the **same** `principalAxes` bit-identical; cylinder/cone/pyramid returns the same basis across 10 repeated calls and after `brep::Solid` JSON round-trip. Also assert that any returned basis is orthonormal and right-handed, even though eigenvalues repeat.
  - `Boolean composite` — box with a cylindrical pocket or two overlapping boxes; `I` of the result vs composite of parts ` (V1*I1 - V2*I2)/(V1-V2)` style transfer through centroid, extended to tensors (Steiner per part), within 1e-7.
  - `Survey-magnitude asymmetric` — frustum at survey coordinates, tilted; reuse the 46–6,978 ft table from the issue as the anti-pattern to beat.
  - `Self-intersecting refused` — torus tube > major `REQUIRE_FALSE(mp.inertiaValid)` and `REQUIRE_FALSE(mp.valid)` path; also `REQUIRE_FALSE(mp.centroidValid)` already holds.
  - `Uncovered face withholds only inertia` — a solid with one `Nurbs` / `Ellipse` / `Intersection` / `GeneralLoop` face: `REQUIRE(mp.valid)`, `REQUIRE(mp.centroidValid == <depends>)`, `REQUIRE_FALSE(mp.inertiaValid)` (and `volume`/`area`/`centroid` unchanged).
- **Headless / command smoke:** drive `SOLIDLIST`/`MASSPROP` through `gosurvey_headless` (as `SECTION` and `SECTIONCLIP` do), asserting that a refused solid prints a named reason and that a valid solid's log contains `Ixx`/`Iyy`/`Izz`/`principal` lines. Drive `.gs` save/reload and re-assert determinism.
- **Performance gate:** not a REQ-100 profile (d) change; quadrature is per-solid on demand, not per frame. Assert that the solid tessellation cache key (`src/util/brep.cpp:1164` `solid pointer, chord tolerance, isoline count`) is untouched and that a repeated `ComputeMassProperties` on the same `shared_ptr<const Solid>` is not treated as a cache — it is a pure function, called where needed. No render-path rebuild should appear in the section-clip pan test.
- **Pre-existing suite:** full `ctest` must stay green — especially `tests/BrepTests.cpp` volume/centroid/boolean/section cases and `tests/BrepJsonTests.cpp` round-trip.

## Risks / Rollback
- **Survey-magnitude cancellation (HIGH).** Squaring makes the world-origin error worse than the centroid's. *Mitigation:* reuse `ReferencePoint(s)` verbatim, acc in double, write one asymmetric survey test that fails when `q` is replaced by the origin — the same mutation that would have caught the centroid.
- **Frame-covariance bug (HIGH).** Per-face-frame accumulation is correct for every axis-aligned solid and wrong for tilted ones, so a tilted test is *load-bearing*. *Mitigation:* every primitive checked twice — axis-aligned and tilted.
- **Closed-form divergence / missing term (MEDIUM).** Second-moment divergence form has more terms than the first moment's; a dropped product term passes symmetric tests. *Mitigation:* asymmetric fixtures (frustum, wedge, Boolean off-axis pocket) per the issue's note; cross-check volume re-derived by the second-moment path against the reported volume.
- **Degenerate basis non-determinism (MEDIUM).** Equal eigenvalues give an arbitrary basis; a basis that depends on iteration order or uninitialized memory will be flaky and will break save/reload stability. *Mitigation:* sort eigenvalues, define a canonical tie-break (world-axis-seeded Gram-Schmidt), enforce `det=+1`, and assert bit-identical results across construction order and `.gs` round-trip.
- **Scope creep into general trim / Nurbs (LOW).** Temptation to cover `Ellipse`/`Intersection` edges or `paramLoops` in increment 1. *Mitigation:* withhold with `inertiaValid==false`; record increment 2 explicitly in the ADR.
- **Rollback:** all changes are additive — new fields behind a flag, new formatting behind that flag, new tests behind the flag. Revert to not reporting inertia and return `inertiaValid==false` for every solid; no `.gs` migration is needed because nothing is persisted.

## Open Questions
- **Reporting verb name.** `MASSPROP` (AutoCAD) vs `SOLIDMASSPROP` vs extending `SOLIDLIST` alone. *Recommendation:* do both — extend `SOLIDLIST` and add `MASSPROP` — and name the dedicated verb `MASSPROP` with `SOLIDMASSPROP` as an alias, matching AutoCAD's user muscle memory without renaming the existing list.
- **Units in the report.** Inertia has units `length^5` with unit density (implicit `mass = volume`). Should the log state the assumed density convention explicitly ("unit density, so I has units ft⁵") or follow AutoCAD's `MASSPROP` header? *Assumption for the plan:* state it once in the log header and in the ADR; no user-selectable density in increment 1.
- **Whether to expose the tensor about an arbitrary point interactively.** Parallel-axis is a pure function, so the report can show both `I_c` and `I_about(world origin)` as a demonstration without adding a prompt. *Assumption:* increment 1 reports `I_c` + principal axes; `I_about(p)` is exercised by tests and by the `MASSPROP` log line "about world origin" rather than a new input path.

## Sources
- Issue #460: `https://github.com/chetjones003/GoSurvey/issues/460`
- Existing mass-properties contract: `spec/requirements.md:6166`, `spec/requirements.md:6171`, `spec/requirements.md:8431` (REQ-334), `spec/architecture.md:4038` (ADR-055), `src/util/brep.hpp:1011`, `src/util/brep.hpp:1047`, `src/util/brep.cpp:15623`, `src/util/brep.cpp:15908`, `src/util/brep.cpp:15945`, `src/util/brep.cpp:15964`
- Solid-local reference rule and survey error table: `src/util/brep.hpp:1033`, `spec/architecture.md:4055`, issue #460 constraint table
- Reporting surface today: `src/commands/CadCommands.cpp:30121` (`CadReportSolids` / `SOLIDLIST`)
- Validation + self-intersection + per-face handling: `src/util/brep.hpp:995`, `src/util/brep.cpp:15945`, `src/util/brep.cpp:15909`
- Phase 6 / #149 context: `spec/requirements.md:6170`, `spec/requirements.md:8480`
- Current tests as template: `tests/BrepTests.cpp:5840`, `tests/BrepTests.cpp:6144`, `tests/BrepTests.cpp:6166`, `tests/BrepTests.cpp:6193`, `tests/BrepTests.cpp:6225`, `tests/BrepTests.cpp:6243`, `tests/BrepTests.cpp:6277`

---
*Plan saved to `.agents/plans/2026-09-18-issue460-inertia.md` — reply `Approve`, `Request changes`, or `Cancel` to proceed.*
