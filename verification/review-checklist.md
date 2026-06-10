# Review Checklist

> The core of the Verification Layer: the four-domain pass over a change. Work the
> domains **in order** (architecture → code quality → dependencies →
> performance), but treat **correctness** as the highest-severity item
> throughout. Check a box only with evidence; record every unchecked item as a
> finding (`verification.md` §5).

Authority: `spec/architecture.md`, `spec/coding-standards.md`,
`spec/requirements.md`, `spec/project.md`.

---

## Pre-review gate

- [ ] The change cites at least one `accepted` `REQ-NNN`. *(If none → escalate,
      `verification.md` §7.2; do not review further.)*
- [ ] `build-checklist.md` is **PASS**. *(If not → stop; build gate first.)*

---

## Domain 1 — Architecture *(any failure is blocking)*

Run against `spec/architecture.md` §11 invariants.

- [ ] **Layering:** no new dependency points upward across layers.
- [ ] **Boundary:** the change lives in the subsystem that owns the concern; no
      subsystem doing another's job.
- [ ] **Ownership:** every new resource has exactly one visible owner; no new
      `shared_ptr`/`Rc`/`Arc` without a recorded justification.
- [ ] **Global state:** no new global mutable state or hidden singleton access.
- [ ] **Abstraction:** any new interface/trait/template/generic names ≥2
      present-day concrete call sites (REQ-301). Otherwise reject.
- [ ] **Backend boundary:** no `gl*`/backend call outside Renderer/Platform.
- [ ] **Reversibility:** no previously-rejected approach reintroduced without a
      new decision-log entry (CON-03).

## Domain 2 — Code quality

### 2a. Correctness *(highest severity — always blocking)*
- [ ] Satisfies every `REQ-NNN` it claims; behavior matches each **Acceptance**
      condition.
- [ ] Edge cases handled: empty, boundary, malformed, maximum size.
- [ ] Failures surfaced — every error path logs, returns, or asserts; **none is
      empty or swallows the error**.
- [ ] No `CON`/constraint requirement violated.
- [ ] Concurrency (if any) is data-race-free per `spec/architecture.md` §8.

### 2b. Simplicity
- [ ] Smallest change that satisfies the requirement; no speculative generality.
- [ ] No premature pattern, framework, or "system" for a one-off.
- [ ] Control flow is followable without a debugger; guard clauses over deep
      nesting.

### 2c. Ownership & const
- [ ] Ownership explicit; borrowed pointers/refs never free.
- [ ] `const`/immutability by default; large objects passed by reference/borrow.

### 2d. Readability
- [ ] Names follow `spec/coding-standards.md` §5 for the language in use.
- [ ] Comments explain *why*, not *what*; no dead or commented-out code.
- [ ] Functions do one job.

> Severity rule: 2a is always blocking. 2b–2d are blocking when they break a
> *written* standard; otherwise record as **advisory**.

## Domain 3 — Dependencies *(blocking)*

Trigger this domain whenever a dependency manifest/lockfile changed. For each
added or upgraded dependency:

- [ ] **Necessity:** cannot be reasonably implemented in-tree.
- [ ] **Recorded:** has a `project.md` decision-log entry with rationale.
- [ ] **Health:** actively maintained, not abandoned.
- [ ] **Cost:** acceptable build-time / binary-size / transitive-graph impact.
- [ ] **License:** compatible with `project.md` §1.
- [ ] **No duplication:** does not re-solve a problem an existing dependency
      already covers.
- [ ] **Pinned:** version locked and lockfile committed.

## Domain 4 — Performance

Run against the performance requirements in `spec/requirements.md`.

- [ ] No allocation / logging / virtual dispatch / large-copy added to a path the
      spec marks hot — or a profile justifies it.
- [ ] Containers of known size are reserved/pre-sized.
- [ ] Any "faster"/"fine" performance claim is backed by a number from the
      reference benchmark/hardware.
- [ ] Relevant benchmark still meets its budget (frame time / startup / import).
- [ ] No optimization added without a profile proving the need (no complexity
      for unmeasured gain).

---

## Verdict

```
REVIEW VERDICT — <change id> — <date> — <reviewer>
- Architecture:  ✓ / ✗     (findings: …)
- Code quality:  ✓ / ✗     (correctness must be ✓)
- Dependencies:  ✓ / ✗ / n-a
- Performance:   ✓ / ✗ / n-a
- Blocking findings: N      Advisory: M
- Outcome:       PASS | FAIL | SPEC GAP
```

- **PASS** → proceed to `testing-checklist.md` (and `release-checklist.md` if
  shipping).
- **FAIL** → return to Workshop with the numbered findings.
- **SPEC GAP** → block and escalate per `verification.md` §6.1.

> Reminder: a finding with no cited spec item is not valid. Either tie it to the
> spec, downgrade it to advisory phrased as a suggestion, or escalate it as a
> spec gap.
