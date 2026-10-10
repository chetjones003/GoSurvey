# Workshop Layer — Implementation Rules

> The rules code must obey **while it is being written**. `spec/coding-standards.md`
> defines the standards; this document is the Workshop's working discipline for
> meeting them — and for staying inside the boundary that separates
> *implementation* (the Workshop's job) from *architecture* (not the Workshop's
> job). When this document and the spec disagree, the spec wins; report the
> discrepancy as a SPEC GAP.

---

## 1. The boundary, restated as a working rule

> **Implement decisions; do not make them.**

While coding, you will constantly choose between options. Classify each choice:

| Choice is about… | Whose call | What to do |
|------------------|-----------|------------|
| A local variable, loop, guard clause, helper function *inside one subsystem* | **Workshop** | Decide it; follow the standards. |
| Naming, formatting, error-handling mechanism per the standards | **Workshop** | Decide it; follow the standards. |
| Which existing API to call, given the spec's architecture | **Workshop** | Decide it. |
| A new abstraction, layer, dependency, ownership model, global state, public API, data format, or a different algorithm than specified | **Specification** | **Stop. Escalate as SPEC GAP.** Do not decide. |

If you are unsure which row a choice falls in, treat it as the bottom row and
ask. Drift enters through choices the Workshop *thought* were local.

## 2. Build the smallest sufficient thing

- Implement the least code that satisfies the requirement's Acceptance condition
  (CON-06: solve today's problem).
- No speculative generality, no "while I'm here" features, no framework for one
  call site. Duplication is cheaper than the wrong abstraction — and the
  abstraction isn't yours to add anyway (§1).
- If you feel the urge to generalize, that's a signal you may be crossing into
  architecture. Note it as a possible future requirement on the roadmap; don't
  build it.

## 3. Follow the standards (working checklist)

These are the standards from `spec/coding-standards.md`, phrased as things to do
*as you type*:

- **Simplicity first.** Write the concrete version. Don't add an
  interface/trait/template "to be safe."
- **Ownership explicit.** One visible owner per resource (`unique_ptr` / move /
  `defer` / single-owner struct). Borrowed pointers never free. No
  `shared_ptr`/`Rc`/`Arc` — that's an ownership-model decision (§1).
- **Const / immutable by default.** `const` params and methods, immutable `let`,
  `const` slices. Mutation is visible and deliberate.
- **Pass big objects by reference/borrow**, never by value into a hot path.
- **Name per the language's convention** (the right column of
  `coding-standards.md` §5 for C++/Rust/Zig/Go). Don't mix conventions.
- **Comments say *why*.** Document units, invariants, and non-obvious decisions.
  Delete commented-out code.
- **Every error path is non-empty.** Handle, return, or assert — never swallow.
  Assertions for programmer errors; status/`Result`/error-union/`error` for
  recoverable ones.
- **No logging in hot paths or tight loops.**

## 4. Errors and failure modes

- Identify each input's failure modes (empty, malformed, boundary, overflow)
  *before* coding the happy path. If the spec doesn't define the behavior for one
  → ask (`workflow.md` §5.5).
- Reject malformed input; never absorb or guess it (mirrors the project's data-
  integrity requirements).
- Make failures observable: a logged reason or a returned status. Silent failure
  is a defect `code-review` will block.

## 5. Performance discipline

- **Measure before optimizing.** Do not add complexity for speed without a
  profile. Unmeasured optimization is rejected by `performance-review` just like
  a regression.
- Pre-size known containers (`reserve` / `ensureTotalCapacity` / `make([],0,n)`).
- Don't add allocation/logging/virtual dispatch to a spec-marked hot path without
  a number justifying it.
- Choosing a *different algorithm* than the spec specifies is an architectural
  decision (§1) — escalate, don't substitute.

## 6. Tests are part of implementation, not after it

- A change isn't "implemented" until its tests exist and pass.
- For each `must` requirement touched: a happy-path test **and** a failure-mode
  test asserting the specified behavior.
- Tests assert the Acceptance condition against tolerance (never exact float
  equality), are deterministic, and are independent.
- A new test must fail against the unpatched code — otherwise it isn't testing
  your change.
- Run the suite and record the result. Never write "tests pass" without having
  run them.

## 7. Refactoring discipline

- A refactor **preserves behavior**. Never combine a refactor with a feature/fix
  in the same commit/task — split them.
- Establish a test safety net first; add characterization tests if coverage is
  thin.
- Move in small, independently-verifiable steps; run tests after each.
- A refactor that changes layering, ownership, boundaries, or abstractions is
  **not a refactor** — it's an architectural change. Escalate (§1).

## 8. Documentation discipline

- Docs and doc-comments must match current code and the spec. Accuracy over
  volume.
- Doc-comment public APIs: signature intent in one line; document units,
  ownership, and failure behavior.
- Keep examples runnable and correct; update them when behavior changes.
- Docs never introduce a new requirement or behavior — if a doc would, that's a
  SPEC GAP.

## 9. Assumptions and questions (while coding)

- The moment you assume something the spec didn't state, write it down
  (`workflow.md` §8 format) in the task log.
- An assumption about anything in §1's bottom row is not allowed — convert it to a
  question or SPEC GAP immediately.
- Prefer one well-formed question now over hours of work in the wrong direction.

## 10. Self-verify before submitting

Before handing the task to Verification, run the relevant `verification/skills/`
checklists yourself and fix what they catch:

- `build-project` (clean, warning-free, deterministic)
- `architecture-review` (you made no architectural decision)
- `code-review` (correctness, simplicity, ownership, readability)
- `testing` (happy + failure-mode, run green)
- `dependency-audit` (only if you touched a manifest — and adding a dependency is
  itself a §1 escalation)
- `performance-review` (only if perf-relevant; numbers recorded)

Submitting work that fails its own self-verification wastes a review cycle. The
goal is for Verification to confirm a PASS, not to discover obvious failures.

## 11. Hard stops (never do these)

- ❌ Edit a `spec/` document to make failing code pass. (Spec changes are
  decisions, recorded upstream — not Workshop edits.)
- ❌ Add an abstraction, dependency, layer, or global to make something
  convenient.
- ❌ Suppress a symptom: widen a tolerance, catch-and-ignore, or disable/skip a
  test to go green.
- ❌ Mark a task done without running its tests and recording the verdict.
- ❌ Proceed past an ambiguity by guessing instead of asking.

Each of these is exactly what the Verification Layer exists to catch — and doing
them guarantees a rejection.
