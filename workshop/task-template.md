# Task Template

> Copy this file into `workshop/tasks/TASK-NNN-<slug>.md` to open a unit of work.
> The copied file **is the task log** — keep it current through the whole
> lifecycle. Do not start implementation until the **Authority** and **Plan**
> sections are complete. See `workshop/workflow.md` for the full process.

---

```
# TASK-NNN — <short imperative title>

- Type:    feature | bug | refactor | testing | docs
- Status:  open | plan | implement | self-verify | submitted | done | blocked
- Opened:  <date>
- Owner:   <who>

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         GOAL-NN (from spec/project.md)
- Requirements: REQ-NNN (, REQ-NNN …)   ← must be `accepted`
- Constraints:  CON-NN that apply
- Acceptance:   restate each requirement's Acceptance condition here, verbatim
- Owning subsystem: <from spec/architecture.md — work must stay inside it>

## 2. Scope
- In scope:        <what this task will do>
- Out of scope:    <what it explicitly will NOT do>
- Smallest change: <the least implementation that satisfies the requirement>

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change /
  global state / public-API or data-format change / algorithm the spec didn't
  specify?
    - [ ] No — proceed.
    - [ ] Yes → STOP. This is an architectural decision. Escalate as SPEC GAP
          and set Status: blocked. Record the escalation below.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | <ambiguity / missing acceptance / conflict> | <date> | <answer> |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: <what you assumed>
- Because:       <what was unspecified>
- Risk if wrong: <impact>
- Validate by:   <how/when confirmed, or the question that resolves it>
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach:     <how, at a high level, within the owning subsystem>
- Files/functions to touch: <list>
- Test approach: happy path = <…>; failure mode = <…>
- Steps:
  - [ ] step 1
  - [ ] step 2
  - [ ] step 3

## 7. Workflow-specific notes
> Fill the block matching Type (see workflow.md A–E).
- Feature:  pre-flight answered? tests-first?
- Bug:      root cause = <mechanism>; regression test fails-before? 
- Refactor: "no behavior change" confirmed? behavior covered by tests?
- Testing:  each test → REQ; tolerance-based? fails against unpatched code?
- Docs:     every statement cross-checked against current code/spec?

## 8. Implementation log  (append as you work)
- <date> <status change / decision within boundary / finding fixed>

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [ ] build-project        — PASS
- [ ] architecture-review  — PASS (no Workshop architectural decision)
- [ ] code-review          — PASS (correctness, simplicity, ownership, readability)
- [ ] dependency-audit     — PASS / n-a
- [ ] performance-review   — PASS / n-a (numbers recorded if perf-relevant)
- [ ] testing              — PASS (happy + failure-mode, run green)

## 10. Verification result
- Submitted:  <date>
- Verdict:    PASS | FAIL | SPEC GAP
- Findings:   <ids + how resolved, or link>

## 11. Outcome
- Requirements satisfied: REQ-NNN (Acceptance met: yes)
- Tests added:            <ids>
- Refactors:              <ids or none>
- Docs updated:           <files or none>
- Done:                   <date>
```

---

## Filling guidance

- **Authority is a gate.** If you cannot name an `accepted` `REQ-NNN`, the task is
  a Specification task, not a Workshop task — escalate (`workflow.md` §5.2).
- **The boundary check (§3) is mandatory.** A "Yes" answer means the Workshop must
  not proceed — it escalates. This is the single most important line in the
  template.
- **Plan before code (§6).** Reviewers (and your future self) should be able to
  predict the diff from the plan.
- **Keep the log honest (§8, §9, §10).** Record what actually happened —
  including failed self-checks and how you fixed them.
