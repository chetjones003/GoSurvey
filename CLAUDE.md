# CLAUDE.md — Project Governance

This file defines **how Claude works in this repository**.

## Authority

The repository has three layers:

1. **SPEC** (`spec/`) — defines what is true and what must be built.
2. **VERIFICATION** (`verification/`) — checks work against the SPEC.
3. **WORKSHOP** (`workshop/`) — implements approved work.

**Authority flows down:** SPEC → VERIFICATION → WORKSHOP
**Evidence flows up:** WORKSHOP → VERIFICATION → SPEC

`spec/` is the single source of truth. Never invent requirements or change the SPEC merely to make implementation pass.

---

## Git Workflow

* `master` = latest stable release. **Keep it clean and release-ready.**
* `beta` = current development branch. It may contain changes not yet fully tested for release.
* **Never work directly on `master` or `beta`.**
* Create focused branches from `beta`:

  * `feat/<name>`
  * `fix/<name>`
  * `docs/<name>`
  * `refactor/<name>`
  * `test/<name>`
  * `chore/<name>`
* Merge all development branches into `beta` through a PR.
* When `beta` is fully tested and ready for release, merge `beta` → `master` through a release PR.
* Never merge development branches directly into `master`.

**Normal flow:**

`feature/fix/docs branch → PR → beta → release PR → master`

---

## 1. Environment

This is a **Windows-native repository** using MSVC (`cl`), CMake, and Ninja.

The authoritative build system is:

* `build.bat`
* `CMakePresets.json`

Claude may run under WSL/Linux/Git Bash. Use `./dev/` for Windows tooling:

| Task            | Command           |
| --------------- | ----------------- |
| Build           | `./dev/build`     |
| Run             | `./dev/run`       |
| Test            | `./dev/test`      |
| Clean           | `./dev/clean`     |
| Status          | `./dev/status`    |
| GitHub          | `./dev/gh ...`    |
| Issues          | `./dev/issue ...` |
| PRs             | `./dev/pr ...`    |
| Windows command | `./dev/win ...`   |

Do not replace the Windows build system with Linux equivalents or install a separate Linux `gh`.

---

## 2. Specification

Read the relevant files in `spec/` before making changes:

* `project.md`
* `requirements.md`
* `architecture.md`
* `coding-standards.md`
* `roadmap.md`

Every task must have:

* an applicable **accepted `REQ-NNN`**
* the relevant **Acceptance Criteria**
* any applicable constraints (`CON-NN`)

If the required specification does not exist or is unclear, stop and raise a **SPEC GAP**.

Specification changes require a deliberate, recorded decision.

---

## 3. Workflow

Every feature, bug fix, refactor, test, or documentation change follows this order:

### 1. Understand

Read the relevant SPEC and identify the applicable:

* `GOAL-NN`
* accepted `REQ-NNN`
* `CON-NN`
* Acceptance Criteria

### 2. Plan

Create/update the task in `workshop/tasks/`.

Include:

* Requirement authority
* Files/subsystems affected
* Implementation approach
* Test approach
* Architectural-boundary check

**Do not code yet.**

### 3. Verify the plan

Check the plan against the SPEC, architecture, scope, risks, and missing requirements.

### 4. Resolve uncertainty

If requirements are ambiguous, contradictory, incomplete, architecturally significant, irreversible, or outward-facing:

**Stop and ask the user. Never guess.**

Ask one clear question at a time, provide options, and recommend one.

### 5. Implement

Implement the **smallest correct solution** that satisfies the approved requirement.

Follow:

* `spec/architecture.md`
* `spec/coding-standards.md`
* `workshop/implementation-rules.md`

Add appropriate happy-path and failure/edge-case tests.

### 6. Verify

Run the applicable verification skills:

* `build-project`
* `architecture-review`
* `code-review`
* `dependency-audit`
* `performance-review`
* `testing`

Resolve all blocking findings.

The result must be:

**PASS**, **FAIL**, or **SPEC GAP**.

### 7. Complete

A task is complete only when its required completion report, tests, verification, assumptions, technical debt, build status, and documentation updates are recorded.

---

## 4. Layer Responsibilities

### SPEC

Defines requirements, architecture, standards, and project decisions.

**Does not implement code.**

### VERIFICATION

Reviews work against the SPEC and identifies correctness, architecture, testing, dependency, performance, and completeness problems.

**Does not implement fixes or invent requirements.**

### WORKSHOP

Implements approved requirements and responds to verification findings.

**Does not make architectural decisions or modify the SPEC to justify implementation.**

Detailed procedures belong in each layer's own documentation.

---

## 5. SPEC GAP

A **SPEC GAP** exists when the repository does not provide enough information to safely determine what should happen.

Examples:

* Missing requirement
* Missing or incomplete Acceptance Criteria
* Contradictory requirements
* Unclear expected behavior
* Undefined architectural boundary
* Required architectural decision

When a SPEC GAP occurs:

1. Stop.
2. Explain the problem to the user.
3. Explain why it matters.
4. Present the available choices.
5. Recommend the best choice.
6. Record the decision.
7. Update the appropriate SPEC.
8. Resume only after the SPEC is resolved.

**Never solve a SPEC GAP by guessing.**

---

## 6. Explaining REQ / SPEC / AC Problems

**When explaining a problem involving a REQ, SPEC, Acceptance Criteria, architecture, or SPEC GAP, assume the user knows absolutely nothing about programming.**

Explain it in plain English before using technical terminology. Assume the user knows nothing about software engineering.

Always explain:

1. **What the SPEC currently says**
2. **What is missing, unclear, or contradictory**
3. **Why Claude cannot safely decide on its own**
4. **What choices the user has**
5. **Claude's recommendation**

Use simple real-world analogies when they make the problem easier to understand.

**Do not ask the user to make a technical decision without first explaining what the decision means and why it matters.**

---

## 7. Coding Principles

Prefer:

* Simple solutions
* Readable, maintainable code
* Easy debugging
* Existing patterns
* Minimal dependencies
* Small, focused changes

Avoid:

* Clever or unnecessarily complex code
* Unnecessary abstractions
* Unnecessary dependencies
* Duplicate architecture
* Global state
* Unapproved architectural changes

Do not introduce an abstraction without **at least two current concrete uses**.

Document assumptions and technical debt. If an assumption affects the SPEC or architecture, ask the user instead of deciding it yourself.

---

## Guiding Principle

> **Build the simplest correct thing that the specification actually requires.**

**If the SPEC is clear, implement it.
If the SPEC is unclear, ask.
If the implementation conflicts with the SPEC, fix the implementation.
If the SPEC must change, get a recorded decision first.**

