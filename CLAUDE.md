# CLAUDE.md — Master Governance

This file governs the **entire** system. It is the authoritative entry point for
any work in this repository. Every change passes through a three-layer process,
and this document defines that process and the rules that bind it.

> The detailed C++/Rust/Zig/Go coding standards that previously lived in this
> file now live in **`spec/coding-standards.md`** (preserved and authoritative).
> This file governs *how work happens*; `spec/` governs *what is true*.

---

## The system

Work is organized into three layers with a strict separation of authority.

```
┌──────────────────────────────────────────────────────────────┐
│ 1. SPECIFICATION   (spec/)        — defines truth              │
│      project.md · requirements.md · architecture.md           │
│      coding-standards.md · roadmap.md                         │
├──────────────────────────────────────────────────────────────┤
│ 2. VERIFICATION    (verification/) — reviews and gates work    │
│      verification.md · *-checklist.md · skills/               │
├──────────────────────────────────────────────────────────────┤
│ 3. WORKSHOP        (workshop/)     — implements approved work   │
│      workflow.md · task-template.md · implementation-rules.md │
└──────────────────────────────────────────────────────────────┘

   Authority flows DOWN (spec → workshop).
   Evidence flows UP (workshop → verification → spec).
   The spec changes ONLY by a recorded decision — never to excuse code.
```

---

## Layer behavior rules

These are binding. Each layer does its job and **does not** do another layer's
job.

### 1. SPECIFICATION — defines truth (`spec/`)

- **Defines truth.** It is the single source of truth; all other layers obey it.
- **Defines requirements.** Numbered, testable `REQ-NNN` with Acceptance
  conditions (`spec/requirements.md`).
- **Defines architecture.** Layers, boundaries, ownership, data flow
  (`spec/architecture.md`).
- **Defines standards.** Coding standards and conventions
  (`spec/coding-standards.md`).
- **Does NOT implement code.** It describes outcomes and rules, never function
  bodies.
- Changes only through a deliberate, recorded decision (decision log in
  `spec/project.md`).

### 2. VERIFICATION — reviews everything (`verification/`)

- **Reviews all proposed work** against the Specification — nothing ships
  unreviewed.
- **Uses skills to validate work** (`verification/skills/`): build-project,
  testing, debugging, code-review, architecture-review, performance-review,
  dependency-audit, release-review.
- **Identifies risks** (correctness, architecture, dependency, performance).
- **Identifies missing requirements** — work with no `REQ-NNN` behind it is
  flagged.
- **Rejects work that violates specifications.** It is empowered to FAIL.
  Correctness and safety findings are never waivable.
- **Asks questions when uncertainty exists** (`verification.md` §7).
- **Does NOT implement** and **does NOT invent requirements** — it escalates a
  SPEC GAP instead.

### 3. WORKSHOP — implements (`workshop/`)

- **Implements approved tasks** — only tasks citing an `accepted` `REQ-NNN`.
- **Follows specifications** — builds within `spec/architecture.md` and to
  `spec/coding-standards.md`.
- **Responds to verification findings** — loops until PASS.
- **Produces code and deliverables** — plus tests, task logs, and docs.
- **Does NOT make architectural decisions.** New abstractions, layers,
  dependencies, ownership changes, global state, public-API or data-format
  changes, or algorithm swaps are **architectural** → escalate as SPEC GAP
  (`workshop/workflow.md` §4).
- **Does NOT edit the spec** to make failing code pass.

---

## The mandatory workflow

Every unit of work — feature, bug, refactor, test, or docs — follows these seven
steps in order. Do not skip a step; do not reorder.

### 1. Read specification
Locate the authority. Find the `GOAL-NN`, `REQ-NNN` (must be `accepted`), and
applicable `CON-NN` in `spec/`. Restate each Acceptance condition. **If no
requirement exists, stop** — this is a Specification task; escalate, do not
build.

### 2. Generate implementation plan
Open a task from `workshop/task-template.md` into `workshop/tasks/`. Fill
**Authority**, run the **architectural-boundary check**, and write the **Plan**
(approach, files to touch, test approach, steps). **No code yet.** A plan that
requires an architectural decision is a SPEC GAP, not a plan.

### 3. Run verification review
Submit the plan to Verification. It checks the approach against the spec —
architecture, scope, missing requirements, risks — **before** implementation.
Cheapest place to catch drift.

### 4. Ask user questions if needed
If the requirement is ambiguous, requirements conflict, a failure mode is
unspecified, an architectural decision is implied, or the change is irreversible/
outward-facing → **ask one specific question with options and a recommendation.**
Record the question and answer in the task log. Do not proceed on a guess.

### 5. Implement work
Build to `workshop/implementation-rules.md`: the smallest sufficient change,
inside the owning subsystem, with tests (happy path + failure mode). Document
assumptions as you go. Make no architectural decision — escalate if one appears.

### 6. Run verification skills
Self-run the relevant `verification/skills/` checklists, then submit for review:
`build-project` → `architecture-review` → `code-review` → `dependency-audit` →
`performance-review` → `testing`. Resolve every blocking finding. Verdict is
**PASS**, **FAIL** (loop to step 5), or **SPEC GAP** (escalate to `spec/`). For a
release, also run `release-review` — which requires explicit user authorization.

### 7. Produce completion report
On PASS, write the completion report (template below) and record the deliverable.
The task is **done** only when the report is complete and honest.

```
COMPLETION REPORT — TASK-NNN — <date>
- Requirements satisfied:  REQ-NNN (Acceptance met: yes)
- Summary:                 <what was delivered, 1–2 lines>
- Tests:                   <ids> (happy + failure-mode, run green)
- Verification verdict:    PASS  (findings resolved: <ids/none>)
- Assumptions:             <ids documented in task log, validated/open>
- Architectural decisions: none made by Workshop (escalated: <ids/none>)
- Dependencies:            <added/none> (each recorded in decision log)
- Technical debt noted:    <ids/none — see Technical debt rule>
- Build:                   reproducible, clean on target platform
- Docs updated:            <files/none>
```

---

## Additional rules (binding on all layers)

These apply to every plan, review, and implementation. Verification enforces
them; the Workshop obeys them; the Specification embodies them.

1. **Prefer simple solutions.** The simplest version that is fast enough and
   correct wins. When in doubt, choose simpler.
2. **Avoid unnecessary abstractions.** No interface/trait/template/generic
   without **two or more** present-day concrete uses. Duplication is cheaper than
   the wrong abstraction. (Adding an abstraction is an architectural decision —
   Workshop escalates it.)
3. **Minimize dependencies.** Before adding one, answer: can it be done simply
   in-tree? is it maintained and worth the build cost? does it solve a problem we
   have today? Any "no" → don't add it. Every dependency is recorded in the
   decision log.
4. **Prefer maintainable code over clever code.** Code is read far more than
   written. Readability and debuggability outrank cleverness at every review
   tier.
5. **Maintain architectural consistency.** Dependencies flow downward only; each
   change lives in the subsystem that owns it; one visible owner per resource. No
   change crosses a `spec/architecture.md` §11 invariant.
6. **Preserve build reproducibility.** Clean builds of a fixed commit produce
   identical artifacts; lockfiles committed; artifacts go to the build directory,
   never the source tree (REQ-200, CON-07).
7. **Identify technical debt.** When a constraint forces a compromise, name it
   explicitly in the completion report and the task log, with a removal condition
   and a follow-up task. Never let debt hide.
8. **Document assumptions.** Every assumption is written in the task log
   (`ASSUMPTION-N`: what / because / risk-if-wrong / validate-by). Assumptions
   about architecture or the spec are not the Workshop's to settle — convert them
   to a question or SPEC GAP.

---

## Escalation: the SPEC GAP

The mechanism that keeps the system honest. When work reveals the spec is
missing, ambiguous, contradictory, or wrong — or when an architectural decision
is required — **do not** edit code to an unwritten rule and **do not** edit the
spec to excuse code. Instead:

1. Stop. Mark the task **blocked: SPEC GAP**.
2. File a proposed change against the relevant `spec/` file (its change
   protocol).
3. Get a recorded decision (decision log in `spec/project.md`).
4. Resume only after the spec is updated by that decision.

> **The spec changes through a deliberate, visible decision — never as a side
> effect of implementation.** This single rule prevents implementation drift.

---

## Quick reference

| I want to… | Go to |
|------------|-------|
| Know what is true / required | `spec/` (`project.md`, `requirements.md`) |
| Know the architecture / standards | `spec/architecture.md`, `spec/coding-standards.md` |
| Start a task | `workshop/task-template.md` → `workshop/tasks/` |
| Know the implementation rules | `workshop/implementation-rules.md` |
| Review or gate work | `verification/verification.md` + `verification/skills/` |
| Ship a release | `verification/skills/release-review/` (needs user sign-off) |
| Change the spec | File a SPEC GAP; record the decision in `spec/project.md` |

**Guiding principle:** the best code is easy to read, easy to debug, easy to
modify, fast enough, and free of unnecessary abstraction. When in doubt, choose
the simpler solution — and when the spec is unclear, ask.
