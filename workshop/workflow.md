# Workshop Layer — Workflow

> The Workshop Layer **implements**. It turns accepted requirements into code,
> tests, refactors, and documentation. It is the only layer that touches the
> implementation — and it does so under two hard constraints:
>
> 1. It **may not make architectural decisions.** Structure is defined by
>    `spec/architecture.md`; the Workshop builds *within* that shape and escalates
>    anything that would change it.
> 2. It **builds to the Specification and submits to Verification.** It never
>    bends the spec to fit the code.

Authority above it: `spec/` (what to build, under what rules) and `verification/`
(the gates the work must pass). This document is the Workshop's operating manual.

---

## 1. Prime directives

| # | Rule | Why |
|---|------|-----|
| 1 | **No architectural decisions.** Layering, ownership model, subsystem boundaries, new abstractions — all belong to the spec. If the work seems to need one, **stop and escalate** (§4). | Prevents drift; keeps `architecture-review` enforceable. |
| 2 | **Follow the spec.** Every task cites an `accepted` `REQ-NNN`. No requirement → no work. | The spec is the source of truth. |
| 3 | **Satisfy Verification up front.** Build so the work will pass `verification/` *before* submitting it — self-run the relevant checklists. | Verification rejects; pre-passing it saves cycles. |
| 4 | **Ask when incomplete.** Ambiguous or missing requirement → ask, don't guess (§5). | Guesses become drift and rework. |
| 5 | **Plan before code.** Produce an implementation plan and get it sane before writing (§6). | Cheapest place to catch mistakes. |
| 6 | **Log and document.** Maintain a task log; record every assumption (§7–§8). | Traceability and debuggability. |

## 2. Where work lives

```
workshop/
  workflow.md             ← this file
  task-template.md        ← copy per unit of work
  implementation-rules.md ← the rules code must obey while being written
  tasks/                  ← one file per task (the task log lives here)
    TASK-001-*.md
```

A unit of work = one task file, opened from `task-template.md`, living in
`workshop/tasks/`. The task file *is* the task log.

## 3. Task lifecycle

```
open ──▶ plan ──▶ (plan ok?) ──▶ implement ──▶ self-verify ──▶ submit
  │                   │                                          │
  │              ask/escalate                          ┌─────────┴─────────┐
  │                                                    ▼                   ▼
  └────────────────────────────────────────────── PASS → done        FAIL → implement
                                                                       SPEC GAP → blocked
```

- **open** — copy the template; fill **Authority** (goal, REQ, constraints).
  Incomplete Authority = not ready to start.
- **plan** — write the implementation plan (§6). No code yet.
- **implement** — build to `implementation-rules.md`, smallest sufficient change.
- **self-verify** — run the relevant `verification/` checklists yourself.
- **submit** — hand to Verification with the task log and the REQ IDs.
- **done / FAIL / blocked** — record the verdict; loop or escalate.

## 4. The architectural boundary (what the Workshop may NOT decide)

The Workshop **implements** decisions; it does not **make** them. These are
off-limits and must be escalated to the Specification Layer as a SPEC GAP:

- Adding or changing a layer, or a cross-layer dependency direction.
- Introducing a new interface / trait / template / generic / abstraction.
- Changing the ownership model (e.g. reaching for `shared_ptr`/`Rc`/`Arc`).
- Adding global mutable state or a singleton.
- Adding a third-party dependency.
- Changing a public API or an on-disk data format.
- Picking a different algorithm *when the spec specified one*.

> Inside these lines the Workshop has full freedom: local variable choices,
> function decomposition within a subsystem, control flow, test design, naming
> per the standards. The rule is: **decisions visible in the architecture or the
> spec are not yours to make; decisions local to one function are.**

## 5. When to ask for clarification

Stop and ask — do not proceed on a guess — when:

1. The requirement has no clear **Acceptance** condition, or two readings give
   different results.
2. The behavior you're building has **no `REQ-NNN`** behind it.
3. Two requirements/constraints **conflict** and priority is unclear.
4. The task appears to need an **architectural decision** (§4).
5. An input's failure mode (empty/malformed/overflow) is **unspecified**.
6. The change is **irreversible or outward-facing** (release, public API, data
   format) and intent is uncertain.

Ask one specific question with options and a recommendation. Record the question
and answer in the task log. (This mirrors `verification.md` §7 from the other
side: Verification asks at review time; the Workshop asks at build time.)

## 6. Plan before code

Before writing implementation, the task file's **Plan** section must contain:

- The `REQ-NNN`(s) and their Acceptance conditions, restated.
- The owning subsystem (from `spec/architecture.md`) — confirming the work stays
  inside it.
- The smallest-change approach: the specific files/functions to touch.
- The test approach: what proves correctness (happy path + failure mode).
- Assumptions made (§8) and questions raised (§5).
- A checklist of steps.

A plan that requires an architectural decision is not a plan — it's a SPEC GAP.
Get the plan reviewed (by Verification or a human) before coding when the change
is non-trivial.

## 7. Task logs

The task file is the log. Keep it current as you work:

- Status transitions with timestamps.
- Decisions made *within* the allowed boundary, and why.
- Questions asked and answers received.
- Assumptions (linked to §8).
- Verification verdicts and how findings were resolved.

A task is not done until its log reflects reality — including tests run and the
final verdict.

## 8. Document assumptions

Every assumption is written in the task's **Assumptions** section as:

```
ASSUMPTION-N: <what you assumed>
- Because: <why — what was unspecified>
- Risk if wrong: <impact>
- Validate by: <how/when this gets confirmed, or the question that resolves it>
```

Unvalidated assumptions about anything in §4 (architecture/spec) must be raised as
a question or SPEC GAP — they are not the Workshop's to settle silently.

---

# Detailed workflows

Each workflow is a concrete sequence. All of them share the lifecycle in §3 and
end at a Verification submission.

## A. New feature implementation

1. **Authority.** Confirm an `accepted` `REQ-NNN` exists. If not → ask
   (§5.2)/escalate; do not build.
2. **Pre-flight questions.** Answer the validation questions: owning subsystem,
   any new abstraction (→ §4 escalate), failure modes, how correctness is proven.
3. **Plan (§6).** Write the plan; confirm it needs no architectural decision.
4. **Tests first where practical.** Add the happy-path and failure-mode tests for
   the Acceptance condition (they fail initially).
5. **Implement.** Smallest sufficient change, inside the owning subsystem, to
   `implementation-rules.md`.
6. **Self-verify.** Run `build-project`, `code-review`, `architecture-review`,
   `testing` checklists from `verification/skills/`. Fix findings.
7. **Submit** to Verification with the task log + REQ IDs.
8. **Resolve.** PASS → record deliverable, mark done. FAIL → back to step 5 with
   findings. SPEC GAP → blocked, escalate.

## B. Bug fixing

1. **Authority & expected behavior.** Identify the `REQ-NNN` the bug violates (the
   definition of "correct"). If the behavior was never specified → SPEC GAP, not a
   silent fix.
2. **Reproduce first.** Get a deterministic repro. Engage the `debugging` skill's
   discipline: symptom → root cause. **Do not write a fix before the cause is
   known.**
3. **Regression test.** Write a test that reproduces the bug and **fails against
   the current code** (proves it exercises the defect).
4. **Plan the minimal fix (§6).** The smallest change that addresses the *cause*,
   not the symptom. No widened tolerance, no swallowed error, no disabled test.
5. **Implement** the fix; confirm the regression test now passes and nothing else
   broke.
6. **Self-verify** (build, code-review, testing). Confirm no architectural change
   crept in (if it did → §4 escalate).
7. **Submit.** Log the root cause in the task file.

## C. Refactoring

1. **Authority & boundary.** A refactor changes structure *within the spec*, never
   the spec's architecture. If it would change layering/ownership/abstractions →
   that's an architectural decision → **SPEC GAP**, not a refactor.
2. **No behavior change + features in one step.** A refactor preserves behavior.
   If you also need a feature/fix, split them into separate tasks.
3. **Safety net first.** Ensure tests cover the behavior being preserved; add
   characterization tests if coverage is thin.
4. **Plan (§6).** State explicitly: "no behavior change," what moves, and why
   (cite the smell or the constraint motivating it).
5. **Implement** in small, verifiable steps. Run tests after each step.
6. **Self-verify**, with emphasis on `architecture-review` (did structure stay
   within the spec?) and `testing` (identical behavior).
7. **Submit.** Record the refactor and its motivation in the task log. Any
   structural decision encountered → escalate, don't decide.

## D. Testing

> Used both as a phase of A–C and as a standalone task to raise coverage.

1. **Authority.** Tie each test to a `REQ-NNN` and its Acceptance condition. No
   untethered tests.
2. **Coverage plan.** For each `must` requirement in scope: a happy-path **and** a
   failure-mode test asserting the *specified* behavior.
3. **Honest tests.** Assert against tolerance (never exact float equality);
   deterministic and independent; a new test must fail against the unpatched
   code.
4. **Run and record.** Execute the suite; record results — never assume green.
5. **Self-verify** against the `testing` checklist.
6. **Submit.** Update the `spec/requirements.md` traceability matrix.

## E. Documentation updates

1. **Authority.** Docs must match the spec and the implemented behavior. The
   target is accuracy, not invention — docs never define new requirements (that's
   a SPEC GAP).
2. **Scope.** Identify what changed (API surface, usage, build steps, glossary)
   and the audience.
3. **Plan (§6, lightweight).** List the files/sections to update.
4. **Update** to the project's doc-comment and prose standards
   (`spec/coding-standards.md` §8). Keep examples runnable/accurate.
5. **Verify accuracy.** Cross-check every statement against current code and spec.
   Build docs if generated; ensure examples compile/run.
6. **Self-verify** (build gate if docs are part of the build; code-review for
   doc-comments).
7. **Submit.** Note in the log which behavior/spec the docs now reflect.

---

## Definition of done (all workflows)

A task is done only when:

- It satisfies every claimed `REQ-NNN` (Acceptance met) with no `CON` violated.
- Relevant `verification/` gates return **PASS**; all blocking findings resolved.
- Tests exist (happy + failure mode), were run, and pass.
- The task log is complete: assumptions documented, questions/answers recorded,
  verdict logged.
- No architectural decision was made by the Workshop; any encountered was
  escalated.

If any of these is unmet, the task is **not done** — it loops back to implement
(FAIL) or up to the spec (SPEC GAP).
