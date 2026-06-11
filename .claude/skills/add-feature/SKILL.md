---
name: add-feature
description: Use when the user wants to add, build, or implement a new feature. Collects a structured FEATURE REQUEST, then drives it through the three-layer Specification → Verification → Workshop workflow defined in CLAUDE.md. Verification must approve before any code is written.
---

# add-feature

Drive a new feature through the project's three-layer governance system
(`CLAUDE.md`, `spec/`, `verification/`, `workshop/`). **No implementation begins
until Verification is complete and approves.**

---

## Step 0 — Collect the feature request

Build the FEATURE REQUEST by **interviewing the user one question at a time**. Ask
a single question, wait for the answer, then ask the next. **Never dump the whole
template at once.** Anything the user already supplied (as arguments or in their
message) pre-fills that field — skip questions already answered, and confirm what
you inferred rather than re-asking.

Ask in this order, one at a time:

1. **Title** — "What's a short name for this feature?"
2. **Problem** — "What problem does this solve?"
3. **User Goal** — "What should the user be able to *do* once this ships?"
4. **Requirements** — "What are the specific requirements? List as many as you
   need — one per line." (Collect until the user signals they're done.)
5. **Acceptance Criteria** — "How will we know it's done? Give the observable
   pass/fail conditions." (Collect until done.)

For each question, offer a recommendation or example when it helps the user
answer, but let them decide. *Constraints* default to the standard set below — only
ask if the user implies a special constraint. *Relevant Files* is optional; ask
only if useful.

Do not proceed until **Title, Problem, User Goal, Requirements, and Acceptance
Criteria** are populated. Then assemble the answers into the structure below and
**echo the completed request back to the user for confirmation** before starting
the workflow.

```
FEATURE REQUEST

Title:           <short feature name>
Problem:         <what problem is being solved?>
User Goal:       <what should the user be able to do?>
Requirements:    - <one per line>
Constraints:     - Must not break existing functionality
                 - Must follow project architecture
                 - Must follow coding standards
                 - No unnecessary dependencies
Acceptance Criteria:
                 - [ ] <observable pass/fail condition>
```

---

## Step 1 — SPECIFICATION (review truth)

Read the relevant specification before anything else.

- Review `spec/project.md`, `spec/requirements.md`, `spec/architecture.md`, and
  `spec/coding-standards.md` for what already exists and what binds this work.
- Determine whether the requirements are **complete and testable**: does each
  Acceptance Criterion map to an observable pass/fail condition?
- Identify whether this feature corresponds to an existing `REQ-NNN` or needs a
  new one. A feature with no requirement behind it needs one drafted (proposed)
  and accepted before Workshop may build it.
- **Ask the user clarifying questions if anything is ambiguous, conflicting, or
  underspecified** — one specific question at a time, with options and a
  recommendation. Do not guess.

Output of this step: a short statement of the governing/added requirement(s) and
their Acceptance conditions, plus any answered clarifications.

## Step 2 — VERIFICATION (review before building)

Act as the Verification Layer (`verification/verification.md`). Use the review
skills under `verification/skills/` as your lenses. Produce, in order:

1. **Architectural impact analysis** — which subsystem owns this; does it stay
   within `spec/architecture.md` layering/ownership; does it require any *new*
   abstraction, dependency, layer, global state, public-API, or data-format
   change? Any of those is an architectural decision that must be a recorded
   spec decision, not a Workshop choice.
2. **Risk identification** — correctness, regression, performance, and dependency
   risks.
3. **Required tests** — the happy-path and failure-mode tests that will prove
   each Acceptance Criterion (assert against tolerance where numeric).
4. **Implementation plan** — the smallest sufficient change: files/functions to
   touch, steps, and where tests go. Use `workshop/task-template.md` as the shape.
5. **Verdict** — **APPROVE**, or **REJECT** with specific, spec-cited findings if
   the request violates a specification or constraint, or **SPEC GAP** if the
   spec is missing/ambiguous (escalate; do not invent the rule).

**Do not begin implementation until this verification is complete and the verdict
is APPROVE.** If REJECT or SPEC GAP, report the findings and stop for user/spec
input.

## Step 3 — WORKSHOP (implement approved work)

Only after an APPROVE verdict. Follow `workshop/implementation-rules.md`.

- Implement the approved plan — the smallest correct change, inside the owning
  subsystem, to `spec/coding-standards.md`.
- **Make no architectural decision.** If one surfaces mid-implementation, stop and
  escalate as a SPEC GAP.
- **Explain every modification** — for each changed file, state what changed and
  why, tied to a requirement.
- **Generate tests** — the happy-path and failure-mode tests identified in Step 2;
  run them and record results (never assume green).
- Document any assumptions in the task log
  (`ASSUMPTION-N`: what / because / risk-if-wrong / validate-by).

## Step 4 — Self-verify and complete

- Self-run the relevant `verification/skills/` checklists: `build-project`,
  `architecture-review`, `code-review`, `testing` (and `dependency-audit` /
  `performance-review` if applicable). Resolve every blocking finding.
- Produce a **completion report** (the format in `CLAUDE.md` step 7): requirements
  satisfied, summary, tests, verification verdict, assumptions, architectural
  decisions (should be "none made by Workshop"), dependencies, technical debt,
  build status, docs.

---

## Hard rules

- ❌ Do not write code before the Step 2 verdict is APPROVE.
- ❌ Do not add a dependency, abstraction, layer, or global to make it convenient
  — escalate it as a spec decision.
- ❌ Do not edit `spec/` to make the feature pass; spec changes are recorded
  decisions.
- ✅ Prefer the simplest maintainable solution; avoid unnecessary abstraction;
  minimize dependencies; preserve build reproducibility; document assumptions.
