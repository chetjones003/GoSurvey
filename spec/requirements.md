# Requirements Specification

> **Template.** Requirements are the concrete, testable expression of the project
> purpose in `project.md`. If a behavior is not written here, it is an
> assumption — not a requirement. Every requirement must be specific enough that
> a reviewer can render a clear **pass/fail** judgment by pointing at an
> artifact (output, log, test, profile, or code structure).

---

## How to write a requirement

Each requirement is a numbered block with a stable ID (`REQ-NNN`). The ID never
changes or is reused, so tasks, tests, and reviews can cite it forever.

```
### REQ-NNN — Short imperative title
- Purpose:     which project goal / user this serves
- Priority:    must | should | may          (RFC-2119 sense)
- Type:        functional | performance | quality | constraint
- Statement:   what must be true, phrased testably
- Acceptance:  the observable condition that proves it (the pass/fail test)
- Owner-layer: which architecture layer is responsible
- Status:      proposed | accepted | implemented | verified
- Revisions:   date — note
```

**Testable vs. not:**

- ✅ "Importing a malformed record logs an error and writes no value to the
  model." (observable)
- ❌ "The importer should be robust." (unmeasurable)
- ✅ "A 100k-vertex scene renders within a 16 ms frame budget on the reference
  GPU." (measurable)
- ❌ "Rendering should be fast." (opinion)

Prefer **must** sparingly; everything cannot be a must. A flood of `must`
requirements is a planning failure, not a sign of rigor.

---

## Functional requirements

### REQ-001 — Reject malformed input, never absorb it
- Purpose: data integrity (interoperability goal)
- Priority: must
- Type: functional
- Statement: When the importer encounters a record it cannot interpret, the
  import fails for that record with a logged error; no partial or guessed value
  is written into the model.
- Acceptance: feeding a known-malformed fixture yields a logged error and the
  record is absent from the result.
- Owner-layer: IO
- Status: accepted
- Revisions: `<date>` — initial.

### REQ-002 — `<Round-trip fidelity>`
- Purpose: `<interoperability>`
- Priority: must
- Type: functional
- Statement: `<A file imported and re-exported reproduces source geometry within tolerance.>`
- Acceptance: `<round-trip of the reference dataset matches within CON tolerance.>`
- Owner-layer: `<IO>`
- Status: proposed
- Revisions: `<date>` — initial.

> Add functional requirements until the in-scope list in `project.md` is fully
> covered — no more, no less.

---

## Traverse measurement & adjustment requirements

> These cover the Traverse Editor's raw-measurement display and the least-squares
> closure analysis (extends FEAT-002). Numeric acceptance asserts against
> tolerance per REQ-101, never exact float equality.

### REQ-010 — Display every raw observation
- Purpose: surveyor review of field data (FEAT-002)
- Priority: must
- Type: functional
- Statement: After importing a raw-data file, the editor displays every
  individual F1/F2 observation retained per leg (horizontal circle, slope
  distance, vertical/zenith angle) — not only the reduced per-leg values.
- Acceptance: importing the sample FBK shows a detail row for each F1/F2
  observation; the visible observation count equals the count in the file.
- Owner-layer: UI (data from Domain)
- Status: accepted
- Revisions: 2026-06-10 — initial.

### REQ-011 — Per-leg observation statistics
- Purpose: blunder/quality review
- Priority: must
- Type: functional
- Statement: For each leg the editor computes and displays the mean, sum, and
  standard deviation from the mean of the repeated observations (horizontal
  angle, distance, vertical angle).
- Acceptance: computed mean/sum/std-dev match an independent hand calculation
  within tolerance (REQ-101).
- Owner-layer: Domain (compute), UI (display)
- Status: accepted
- Revisions: 2026-06-10 — initial.

### REQ-012 — Complementary distance reduction
- Purpose: show both slope and horizontal distance regardless of which was given
- Priority: must
- Type: functional
- Statement: When slope distance is provided the editor computes and shows the
  horizontal distance, and when horizontal distance is provided it shows the
  slope distance, using the leg's vertical/zenith angle.
- Acceptance: complementary distance matches a hand calculation within ±0.01 ft.
- Owner-layer: Domain (compute), UI (display)
- Status: accepted
- Revisions: 2026-06-10 — initial.

### REQ-013 — Imported measurements are view-only
- Purpose: protect raw field data from accidental edits
- Priority: must
- Type: functional
- Statement: In the main editor window, imported F1/F2 measurements are
  presented read-only; no imported measurement field is editable.
- Acceptance: code/UI review confirms no editable control is bound to an
  imported observation value.
- Owner-layer: UI
- Status: accepted
- Revisions: 2026-06-10 — initial.

### REQ-014 — Closure window: unadjusted vs least-squares, side by side
- Purpose: let the surveyor compare and accept an adjustment
- Priority: must
- Type: functional
- Statement: A "Calculate Closure" action opens a window presenting the existing
  unadjusted closure and the least-squares result side by side, across two tabs
  (closure summary; per-observation residuals), and lets the user accept the
  least-squares result.
- Acceptance: both columns populate for a closed loop; an Accept action records
  the least-squares result as the chosen adjustment.
- Owner-layer: UI (data from Domain)
- Status: accepted
- Revisions: 2026-06-10 — initial.

### REQ-015 — Least-squares adjustment of a closed-loop traverse
- Purpose: rigorous adjustment (FEAT-002)
- Priority: must
- Type: functional
- Statement: The editor adjusts a closed-loop traverse by weighted least squares,
  using configurable a-priori standard errors (defaults: σ_angle = 5″,
  σ_dist = 0.02 ft + 2 ppm) to weight observations. Only closed loops are
  adjusted in this increment. A loop closes on the start monument, which may be
  re-observed under a suffixed name (e.g. start "KCP2" closing as "KCP2.1");
  import detects this and the closing foresight is held as the start.
- Acceptance: on a synthetic closed loop with a known injected misclosure, the
  adjusted coordinates reduce the misclosure to ~0 within tolerance (REQ-101).
- Owner-layer: Domain (compute)
- Status: accepted
- Revisions: 2026-06-10 — initial.

### REQ-016 — Per-observation residuals
- Purpose: blunder detection
- Priority: must
- Type: functional
- Statement: The closure window's residuals tab shows each observation's angular
  residual and distance residual from the least-squares adjustment.
- Acceptance: residuals match an independently worked least-squares example
  within tolerance (REQ-101).
- Owner-layer: Domain (compute), UI (display)
- Status: accepted
- Revisions: 2026-06-10 — initial.

### REQ-017 — Insufficient redundancy is surfaced, not absorbed
- Purpose: no silent failure (REQ-201)
- Priority: must
- Type: functional
- Statement: When a traverse has insufficient redundancy for adjustment (e.g. an
  open traverse, redundancy ≤ 0) or the normal-equation system is singular, the
  closure window reports "least squares unavailable" with a reason and shows no
  adjusted values; it does not crash or emit NaN/silent results.
- Acceptance: an open traverse yields the message and no adjusted output; a
  singular system logs an error and produces no value.
- Owner-layer: Domain (compute), UI (display)
- Status: accepted
- Revisions: 2026-06-10 — initial.

---

## Performance requirements

> Performance is a requirement, not an afterthought — but always paired with a
> *measurement method*. A performance requirement with no defined benchmark is
> not verifiable.

### REQ-100 — Frame budget
- Purpose: interactive responsiveness (desktop/OpenGL)
- Priority: should
- Type: performance
- Statement: The viewport sustains `<60 FPS / 16 ms>` while displaying a scene
  of `<N>` primitives on the reference hardware.
- Acceptance: the benchmark scene profiled on the reference machine stays within
  budget at the 95th-percentile frame.
- Owner-layer: Renderer
- Status: proposed
- Revisions: `<date>` — initial.

### REQ-101 — Numerical tolerance
- Purpose: domain correctness (CAD/survey)
- Priority: must
- Type: performance/quality
- Statement: Computed `<coordinates>` match the reference dataset within
  `<±0.01 ft>`.
- Acceptance: the regression dataset passes at the stated tolerance (assert
  against tolerance, never exact float equality).
- Owner-layer: `<domain/compute>`
- Status: proposed
- Revisions: `<date>` — initial.

---

## Quality requirements

### REQ-200 — Deterministic, reproducible build
- Purpose: maintainability
- Priority: must
- Type: quality
- Statement: A clean build from a fixed commit produces identical artifacts and
  emits them to the build directory, never the source tree.
- Acceptance: two clean builds of the same commit yield matching binaries
  (modulo timestamps).
- Owner-layer: Build/Platform
- Status: accepted
- Revisions: `<date>` — initial.

### REQ-201 — No silent failures
- Purpose: debuggability
- Priority: must
- Type: quality
- Statement: Runtime failures are surfaced (returned status or logged error);
  programmer errors trip an assertion. No failure path is empty.
- Acceptance: code review confirms every error branch logs/returns; assertions
  guard invariants.
- Owner-layer: all
- Status: accepted
- Revisions: `<date>` — initial.

---

## Constraint requirements

> Restate the hard limits from `project.md` §7 as verifiable requirements so the
> review can fail a change that crosses one.

### REQ-300 — Dependency discipline
- Priority: must
- Statement: A new third-party dependency enters the build only after the
  three-question policy in `project.md` is answered affirmatively and recorded
  in the decision log.
- Acceptance: each dependency maps to a decision-log entry.
- Status: accepted

### REQ-301 — Minimal abstraction
- Priority: must
- Statement: A new interface/trait/template/generic is introduced only with two
  or more present-day concrete uses.
- Acceptance: review names the two call sites, or the abstraction is removed.
- Status: accepted

---

## Traceability matrix

> Keep this table current. It is the at-a-glance health check: a requirement with
> no test is unverified; a test with no requirement is untethered work.

| Requirement | Layer | Test(s) | Status |
|-------------|-------|---------|--------|
| REQ-001 | IO | `<TEST-001>` | accepted |
| REQ-100 | Renderer | `<bench-frame>` | proposed |
| REQ-101 | compute | `<regression set>` | proposed |
| REQ-010 | UI | manual (FBK import shows raw rows) | implemented |
| REQ-011 | compute | `TraverseTests` "ComputeStats" | implemented |
| REQ-012 | compute | `TraverseTests` "Complementary distance" | implemented |
| REQ-013 | UI | review (raw rows read-only) | implemented |
| REQ-014 | UI | manual (closure window, side-by-side) | implemented |
| REQ-015 | compute | `TraverseTests` "adjustment drives misclosure to zero" | implemented |
| REQ-016 | compute | `TraverseTests` "perfect loop yields zero residuals" | implemented |
| REQ-017 | compute | `TraverseTests` "insufficient/invalid input is surfaced" | implemented |

---

## Anti-requirements

> Optional but valuable: things the project deliberately will **not** require.
> Documenting them stops well-meaning contributors from "fixing" non-problems.

- "We do **not** require pluggable rendering backends — OpenGL only until a
  second backend is a real requirement (avoids speculative abstraction)."
- `<…>`
