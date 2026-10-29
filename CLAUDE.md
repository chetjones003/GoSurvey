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

### Pre-PR product documentation review (REQ-453)

Before creating any pull request, inspect all four product documentation sources:

1. `README.md`
2. `PRIVACY.md`
3. The in-app help wiki in `resources/wiki/`
4. The public user manual wiki linked from `gosurveycad.com` (currently the
   [GoSurvey GitHub Wiki](https://github.com/chetjones003/GoSurvey/wiki))

Update every source affected by the change before opening the pull request. In
the task log or pull request description, record each source as reviewed and
either identify its update or state that no update was needed. If an affected
external wiki cannot be updated due to missing access, keep the pull request
unopened until access is available and the update is made.
### Emergency repository-admin bypass

When the repository admin explicitly directs an emergency action on `beta` or `master`, the admin
may use GitHub's existing branch-protection bypass to push or merge directly, including bypassing
required reviews and checks. Follow [the emergency admin bypass procedure](docs/emergency-admin-bypass.md)
to record the action. This is an explicit exception to the normal branch workflow; agents must not
infer emergency authorization from urgency alone.

### Issues and pull request labels

All agents creating a GitHub issue or pull request must apply the appropriate repository labels as part of creation. Choose labels that match the work's type and status from the repository's existing label set. Add the labels during creation when the tool supports it; otherwise, apply them immediately afterward and confirm they are present. If no existing label fits, use the repository's established label conventions when creating a suitable label.

Keep status labels current on the issue and its linked pull request as work progresses: apply `in progress` while implementation is underway, `needs review` when the work is ready for review, and `in review` while a review is underway. Remove the previous status label when moving to the next stage so each item reflects its current state. When work returns to implementation after review, change the status back to `in progress`.

**Branch cleanup (after every merge into `beta`):**

* Only `master` and `beta` should exist, locally and on `origin`.
* Once a branch is merged into `beta`, delete it on `origin`, delete the local branch, and remove its worktree (`git worktree remove`, then `git remote prune origin`).
* Before deleting, check `git cherry origin/beta <branch>`. If it has commits that were never merged, tag them `archive/<name>` instead of losing them.
* Leave the `fork` remote alone.

---

## 1. Environment

This is a **Windows-native repository** using MSVC (`cl`), CMake, and Ninja.

The authoritative build system is:

* `build.bat`
* `CMakePresets.json`

### Build and test Qt

Run these commands from the repository root in PowerShell. Install MSVC C++ build tools,
CMake and Ninja; `build.bat` loads the MSVC environment. The Qt application requires an installed
MSVC x64 Qt 6.8+ kit. Set `$env:GOSURVEY_QT_ROOT` to its root when it differs from
`C:\Qt\6.8.3\msvc2022_64`.

| Task | Command |
| --- | --- |
| Build Qt and all test/helper targets (Release) | `.\build.bat` |
| Run Qt tests on the hidden desktop | `ctest --preset ninja-release -R "^qt[.]editor$"` |
| Run the complete suite, including headless transcripts | `ctest --preset ninja-release` |
| Check the Qt output layout | `ctest --preset ninja-release -R "^build[.]output-layout$"` |
| Clean Release rebuild | `.\build.bat release --clean-first` |

`build.bat` configures and builds the Qt application, domain test suite and headless helpers.
Report failures explicitly; do not describe a partially passing suite as green.

For Debug, use `.\build.bat debug`, then run
`ctest --preset ninja-debug -R "^qt[.]editor$"`. Build all Debug targets before
`ctest --preset ninja-debug`.

### Agent builds in T3 worktrees

Build and launch from the root of the worktree assigned to the current agent:
run `.\build.bat` (or `.\build.bat debug`) there, then `.\run.bat` (or
`.\run.bat debug`). The scripts resolve their source and output paths from that
checkout, so each worktree uses its own standard `build/` directory and its
ordinary `GoSurvey.exe` name. Do not redirect output into another worktree or
the main checkout. Do not close a GoSurvey window running from another worktree
to unblock a build; that process uses a different executable path. If a build
is blocked, first confirm the process path belongs to the current worktree.

`build-qt.bat` forwards to `build.bat`. For direct CMake builds, use
`cmake --build --preset ninja-release` (or `ninja-debug`) from an MSVC developer shell;
the actual CMake trees are under `build/.cmake/`. Tests run with the matching CTest preset.
In WSL/Git Bash, invoke the Windows batch scripts through `cmd.exe /c` and use the Windows
CMake/CTest and `gh` executables. Keep the Windows-native toolchain.

### Agent worktree isolation

Build and run only from the root of the worktree assigned to the current agent, using that
checkout's `./build.bat` (or `./build.bat debug`) and `./run.bat` (or `./run.bat debug`).
Each worktree has its own `build/` directory and application path. Leave GoSurvey instances
started from other worktrees untouched; do not close them or interfere with their builds or
runtime files.

### Keep build output clean

Release application output consists of the Qt application `GoSurvey.exe` and `GoSurveyQtTests.exe` under `build/`.
Debug applications use `build/debug/`; Visual Studio builds use `build/<Configuration>/`.

- Keep CMake caches, generated headers, static libraries, symbols and helper executables under
  `build/.cmake/<preset>/`.
- Write screenshots, PNG captures, logs and test reports under `build/artifacts/`, including
  files generated by new tests and ad hoc checks. Keep generated evidence out of commits.
- Retain required runtime DLLs, `qt.conf`, `qt-plugins/`, `resources/` and runtime `.ini` files
  beside the applications. Images inside `resources/` are application assets.
- Every successful `build.bat` run invokes `tools/tidy-build-output.ps1`, archiving legacy
  clutter under `build/artifacts/legacy-output/<timestamp>/` for recovery. For cleanup without
  compiling, run `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\tidy-build-output.ps1 -RuntimeDirectory .\build`;
  use `.\build\debug` for the Debug runtime directory. Close applications/tests before cleanup.
  Keep cleanup confined to this checkout's build directory; preserve archives until their
  contents are no longer needed.

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

If the required specification does not exist or is unclear, treat it as a **SPEC GAP** and follow
[§5](#5-spec-gap). Resolve it with evidence first; ask the user only when evidence cannot safely
settle the gap.

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

When a requirement, acceptance criterion, or REQ is missing, ambiguous, or contradictory:

1. **Try evidence first** using the [SPEC GAP evidence pass](#5-spec-gap) (repository sources, then
   Autodesk product behavior). Record what you checked and what it showed.
2. If evidence settles the behavior and scope, propose or apply the matching **recorded decision**
   and SPEC update per §5. Do not implement on guesswork.
3. **Stop and ask the user** only when the gap remains after that pass, or when the choice is
   architecturally significant, irreversible, or outward-facing in a way evidence cannot authorize
   alone.

Ask one clear question at a time, provide options grounded in the evidence you gathered, and
recommend one. **Never guess.**

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

**Qt UI changes: compare screenshots with Civil 3D.**
When work adds or changes visible Qt UI (a new window, dialog, ribbon or Settings page, a layout
tweak, or a restyle), screenshots and a completed comparison panel are required before calling the
work done:

1. Capture every affected GoSurvey surface and meaningful state (such as normal, busy, error, empty,
   or disabled). Use the hidden-desktop Qt test run (`tools/run-qt-tests-hidden.ps1`, used by
   `ctest --preset ninja-release -R qt.editor`) with `widget->grab()` or, for the 3D viewport,
   `grabFramebuffer()`. Save PNGs under `build/artifacts/`; never commit them.
2. In Civil 3D, reproduce the same task, drawing content, visible state, and view as closely as the
   products allow. Capture the corresponding Civil 3D surface. Match window size, display scale,
   theme, viewport, camera, zoom, and other relevant view settings; record any setting that cannot
   be matched. Keep both products' captures together under `build/artifacts/`.
3. Have three agents review the paired captures independently using the same task description and
   rubric, without seeing one another's scores first. Each agent records a 0–2 score for each item:
   - **Structure and geometry:** major regions, drawing geometry, spacing, and alignment.
   - **Hierarchy and typography:** control placement, labels, text size, and readability.
   - **Color and visual weight:** background, contrast, line weights, and emphasis.
   - **State and feedback:** visible state, selection, hover, prompts, and other task feedback.
   - **Overall composition:** viewport-to-chrome balance and the arrangement of the whole surface.

   Score each item as `0` for a material mismatch, `1` for a noticeable but usable difference, or
   `2` for a close match. For each score, cite the visible evidence and location in the captures.
4. A blocking mismatch is a difference that hides or misplaces a primary work area, makes a required
   control or label unreadable, or changes how the task or visible state is understood. Count a
   blocker only when at least two reviewers independently cite the same mismatch and point to its
   location in the captures. The panel passes only when the median score for **every** rubric item
   is at least `1` and there are no corroborated blockers. Otherwise it fails; fix the cited
   mismatches, recapture, and run a fresh independent panel. The recorded scores and this fixed rule
   determine the pass/fail result.
5. Save the task description, capture settings, paired screenshots, three score sheets, median
   results, and pass/fail decision under `build/artifacts/`. Report the result and evidence location
   in the task completion record. The panel decision is the completion gate; user approval is not
   required.

If Civil 3D cannot be launched or the task/surface cannot be meaningfully matched, record that
limitation and mark the comparison **inconclusive**. Do not claim a pass or substitute an unpaired
GoSurvey screenshot; report the blocker in the completion record. Continue other verification that
does not depend on the comparison.

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

### Evidence pass (required before escalating)

Do not open a SPEC GAP with the user until you have tried to close it from evidence. Work in this
order; cite sources in the task log:

1. **Repository:** applicable `REQ-NNN`, acceptance criteria, `spec/project.md` decisions, GitHub
   issues and comments, `resources/wiki/`, and existing product behavior in code and tests.
2. **Autodesk help (web):** official AutoCAD and Civil 3D documentation for the command, dialog,
   workflow, or file behavior in question.
3. **Civil 3D (this machine):** reproduce the task in the installed app when the gap is about UI,
   interaction, or visible behavior; capture notes or screenshots under `build/artifacts/`.
4. **AutoCAD console (`accoreconsole.exe`, this machine):** run scripted checks when the gap is about
   geometry, units, DWG/DXF content, or other headless oracle behavior the repo already uses in
   tests (see `resources/wiki/Import-and-Export.md`).

If evidence agrees and only needs authority to enter the SPEC, draft the decision, update the SPEC,
and continue. If evidence contradicts the SPEC, stop implementing and reconcile through a recorded
decision—do not silently pick a side.

When evidence does not settle the gap, or the remaining choice needs user authority (see step 3 in
[§3.4](#4-resolve-uncertainty)), escalate:

1. Stop.
2. Explain the problem to the user in plain English ([§6](#6-explaining-req--spec--ac-problems)).
3. Explain why it matters.
4. Present the available choices, including what the evidence pass showed for each.
5. Recommend the best choice.
6. Record the decision.
7. Update the appropriate SPEC.
8. Resume only after the SPEC is resolved.

**Never solve a SPEC GAP by guessing.** Evidence-backed resolution is required; guessing is not a
substitute for the pass above.

---

## 6. Explaining REQ / SPEC / AC Problems

**When explaining a problem involving a REQ, SPEC, Acceptance Criteria, architecture, or SPEC GAP, assume the user knows absolutely nothing about programming.**

Explain it in plain English before using technical terminology. Assume the user knows nothing about software engineering.

Always explain:

1. **What the SPEC currently says**
2. **What is missing, unclear, or contradictory**
3. **What you already checked** (repository, Autodesk help, Civil 3D, or `accoreconsole`) and what
   that evidence showed
4. **Why the gap still needs a human decision** (if it does)
5. **What choices the user has**
6. **Claude's recommendation**

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
If the SPEC is unclear, resolve it from evidence per §5; ask only when that pass cannot settle it.
If the implementation conflicts with the SPEC, fix the implementation.
If the SPEC must change, get a recorded decision first.**

