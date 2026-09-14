# TASK-253 — Closing #149's last two acceptance criteria on the record

- Type:    test (acceptance closure; no product change)
- Status:  review
- Opened:  2026-09-11
- Owner:   Workshop
- GitHub:  #149 (3D Phase 6 — Analysis), acceptance **3** and **8**.

## 1. Authority

- **GitHub #149** acceptance 3 and 8 — the two criteria the phase had not closed.
- **REQ-105** — DIST, which is acceptance 1's command and the subject of acceptance 8's gap.
- **REQ-313 / ADR-045** — the B-rep kernel, whose existing tests are acceptance 3's evidence.
- **REQ-101** — ±0.002 ft, the tolerance both criteria are measured against.
- **ADR-054 Phase C** — the `double` widening of the coordinate stores, which is the design this
  ends up documenting rather than changing.
- Constraints: CON-06 smallest change.

## 2. Problem

#149's other six criteria were closed by PRs #464–#469 and #478. Two were not:

**Acceptance 3** — *"A primitive's computed volume matches its analytic formula (sphere, torus and
cone are the ones that expose tessellation error — these must use exact geometry, not the
tessellated approximation)."* Met by the #146 kernel and its tests, but **never claimed by a #149
task log**, so nothing pointed a reviewer at the evidence.

**Acceptance 8** — *"Measurements remain accurate at survey coordinate magnitudes."* Pinned for
solids, faces, the centroid and the section — `BrepTests` builds and measures at E 2,196,000, and
`SectionClipTests` places the clip plane there. **DIST had no survey-magnitude case at all**:
before this, no coordinate anywhere in `DistCommandTests.cpp` exceeded about 83,000. That is
acceptance 1's own command, unmeasured at acceptance 8's magnitudes.

## 3. Acceptance 3 — what the evidence actually is

No new test. `BrepTests.cpp` already asserts each primitive's volume against its closed form, and
the three the criterion names are the three it names because tessellation error shows there:

| primitive | asserted against | line |
|---|---|---|
| cone with apex | `π r² h / 3` | 244 |
| cone frustum | `(π h / 3)(r₀² + r₀r₁ + r₁²)` | 257 |
| sphere | `4/3 π R³` | 267, 327 |
| torus | `2π² R r²` | 279, 337 |

and *"Tessellation agrees with the analytic figures and winds outward"* (line 597) is the case that
holds the two against each other — which is the second half of the criterion, the part that says the
figure must not be the tessellated approximation.

The criterion is therefore **met, and now claimed**. Recording it as a pointer rather than
duplicating the assertions is deliberate: a second copy in a #149-flavoured file would be a second
thing to keep in step with the kernel, and the first one to drift would be the copy.

## 4. Acceptance 8 — what was actually at risk, and what protects it

`AppCommandState::distFromX/Y/Z` are **`float`**. A float's resolution at 2.2e6 is about **0.25 ft**
— 125× REQ-101. If those fields ever held a survey coordinate, DIST would be quietly and badly
wrong, in the same shape as the two defects this phase already found (the centroid's reference point,
and the clip plane's anchor): exact at the origin, wrong at state-plane coordinates.

They do not hold one, and the reason is `MaybeEstablishDocumentOriginFromTypedPoint`: a typed point
whose magnitude passes `kLargeCoordinateRebaseThreshold` **rebases the document origin**, and
everything is then stored local to it. The floats hold small numbers however far out the survey is.

Measured, not assumed. Three configurations, all exact:

| configuration | result |
|---|---|
| survey point typed into a fresh drawing | dX 3, dY 4, slope 5.8310 — exact |
| a **0.010 ft** delta at E 2,196,000 | `dX = 0.0100` — exact, and this is the one a raw float loses entirely |
| small work near the origin first, **then** measuring out at survey magnitude | exact; the origin **re-establishes** and the locals are small again |

That third row is the one worth having: it is the configuration where the storage width would decide
the answer, and the origin turns out to follow the work rather than staying put. I had assumed it
would not, and wrote the case asserting the opposite; measuring corrected it.

## 5. What the four new cases assert

`DistCommandTests` `[req149]` — and each asserts the **outcome and the mechanism together**:

- DIST is exact at survey magnitudes, **and** the origin absorbed the magnitude, **and** the stored
  `float` locals are consequently small;
- a hundredth of a foot resolves at E 2,196,000 — five times REQ-101, so a failure is unambiguous
  rather than marginal;
- horizontal distance and grade hold there too, which is acceptance 1's other two numbers at
  acceptance 8's magnitudes; grade is a **ratio**, so quantization shows as a wrong percentage
  rather than a small absolute error;
- work near the origin followed by a survey-magnitude measurement stays exact, with a **fractional**
  delta — 2,196,000 and 2,196,003 are both under 2²⁴ and therefore exact as floats even unabsorbed,
  so an integer delta would have passed whatever the origin did and proved nothing.

Asserting the mechanism is what makes these tests of the invariant rather than of one arithmetic
path. The outcome alone would keep passing if origin-at-entry were removed and the fields widened to
`double` instead — a different design, but fine — and would go **silently wrong** if origin-at-entry
were removed and the fields left as they are.

**Proven to bite:** disabling the origin rebase fails 3 of the 4 cases and 9 assertions, with
`distFromX` reported as `2196000.0` — the float holding a survey coordinate, which is precisely the
exposure these exist to guard.

## 6. Verification

**Full suite 1484/1484.** No product code changed: this task adds four test cases and the record for
two acceptance criteria.

## 7. Assumptions and debt

- **ASSUMPTION-1.** Acceptance 8's "measurements" are the ones #149 itself enumerates — DIST, area,
  volume, centroid and section. AREA/PERIMETER for 2D entities is outside the phase and is not
  covered here.
- **DEBT-1.** The `float` width of `distFromX/Y/Z` is now *documented* as safe-by-invariant rather
  than safe-by-construction. It depends on the origin rebase continuing to fire; the third case is
  what would notice if it stopped. Widening them to `double` beside the stores ADR-054 Phase C
  already widened would remove the dependency, and is worth doing if that invariant ever gets harder
  to state — but it is not a change to make while nothing is wrong.

## 8. Result

**PASS.** With acceptance 6 delivered by TASK-249 in this same PR, **all eight of #149's acceptance
criteria are now met and claimed**, and the issue is closable.
