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
| `<…>` | `<…>` | `<…>` | `<…>` |

---

## Anti-requirements

> Optional but valuable: things the project deliberately will **not** require.
> Documenting them stops well-meaning contributors from "fixing" non-problems.

- "We do **not** require pluggable rendering backends — OpenGL only until a
  second backend is a real requirement (avoids speculative abstraction)."
- `<…>`
