---

name: github-issue-review
description: Review a GitHub issue and its associated pull request(s) for correctness, bugs, completeness, test coverage, and satisfaction of every acceptance criterion. Treat the GitHub issue as the specification and verify the PR implementation against it. Use when asked to review an issue, review the PR(s) for an issue, validate an implementation, or determine whether an issue is ready to close.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

# GitHub Issue & PR Review

## Purpose

This skill performs a rigorous implementation review by treating the **GitHub issue as the specification** and the associated **pull request(s) as the implementation**.

The goal is not merely to review code quality.

The goal is to answer:

> **Does the implementation actually and completely satisfy what the issue asked for, without introducing bugs or regressions?**

You must independently verify the implementation. Do not rely solely on the PR description, PR author claims, commit messages, or passing tests.

---

# Core Principles

Follow these principles throughout the review.

## 1. The issue is the specification

The GitHub issue defines what should be implemented.

Use the issue's:

* Description
* Acceptance criteria
* Requirements
* Examples
* Technical notes
* Expected behavior
* Referenced documentation
* Screenshots
* Related issues

as the primary source of truth.

The PR description explains the author's intended implementation, but it does **not** override the issue.

---

## 2. Verify claims in the actual code

Never conclude that a requirement is satisfied simply because:

* The PR says it is implemented
* A commit message says it is fixed
* A test exists
* A test passes
* The code appears reasonable at first glance

Trace the implementation and verify the actual runtime behavior.

---

## 3. Every acceptance criterion must be evaluated

Every acceptance criterion must receive one of:

* **PASS**
* **PARTIAL**
* **FAIL**
* **NOT VERIFIABLE**

Do not combine multiple acceptance criteria into a single vague assessment.

If an acceptance criterion is not explicitly written but is clearly implied by the issue, identify it as an **implicit requirement**.

---

## 4. Passing tests do not equal completeness

Tests are evidence, not proof of completeness.

A PR can have 100% passing tests and still:

* Miss an acceptance criterion
* Implement the wrong behavior
* Fail in an edge case
* Break existing functionality
* Leave functionality inaccessible to users
* Contain incomplete code
* Have incorrect error handling

---

## 5. Review behavior, not just changed lines

You must inspect relevant surrounding code.

A bug may exist in:

* Existing code modified indirectly
* Callers
* State management
* Data structures
* Interfaces
* Event handling
* Serialization
* Parsing
* Rendering
* Error handling
* Configuration
* Tests

Do not limit the review to the PR diff when understanding the implementation requires broader context.

---

# Review Workflow

Follow this workflow in order.

---

## Phase 1 — Establish Repository Context

First determine the repository and project structure.

Inspect:

```bash
git status
git branch --show-current
git remote -v
```

Determine:

* Project language(s)
* Build system
* Test framework
* Directory structure
* Relevant architecture
* Existing development conventions
* Available scripts/commands

Look for files such as:

```text
README.md
CLAUDE.md
AGENTS.md
CONTRIBUTING.md
Makefile
CMakeLists.txt
package.json
go.mod
Cargo.toml
pyproject.toml
*.sln
*.csproj
```

Read relevant project instructions before reviewing code.

If `CLAUDE.md` exists, follow it.

---

# Phase 2 — Load the GitHub Issue

Use the GitHub CLI when available.

Example:

```bash
gh issue view <issue-number>
```

If necessary, retrieve additional information:

```bash
gh issue view <issue-number> --comments
```

Extract and record:

### Issue metadata

* Issue number
* Title
* Author
* Labels
* Milestone
* Status

### Requirements

Identify:

* Explicit requirements
* Acceptance criteria
* Expected behavior
* Constraints
* Examples
* Edge cases
* UI requirements
* API requirements
* Performance requirements
* Compatibility requirements
* Documentation requirements
* Testing requirements

### Specification normalization

Translate the issue into a concrete checklist.

For example:

```text
AC-1: RECTANGULAR array creates the requested number of rows.
AC-2: RECTANGULAR array creates the requested number of columns.
AC-3: Row spacing is respected.
AC-4: Column spacing is respected.
AC-5: Original geometry remains unchanged.
AC-6: Invalid input produces an appropriate error.
AC-7: Existing array functionality continues to work.
```

Do not lose requirements merely because they are buried inside prose.

---

# Phase 3 — Find Associated Pull Request(s)

Search for all PRs associated with the issue.

Start with:

```bash
gh pr list --state all
```

Search for the issue number:

```bash
gh pr list --search "<issue-number>"
```

Also inspect likely PRs using:

```bash
gh pr view <pr-number>
```

Look for:

* `Fixes #123`
* `Closes #123`
* `Resolves #123`
* `Refs #123`
* `Related to #123`
* Issue number in the PR title
* Issue number in commits
* Issue number in branch names

If multiple PRs contribute to the issue, review them collectively.

Do **not** assume the first PR found is the only relevant PR.

---

# Phase 4 — Understand the PR

For each associated PR, inspect:

```bash
gh pr view <pr-number>
gh pr diff <pr-number>
```

Also inspect commits:

```bash
gh pr view <pr-number> --json commits
```

Determine:

* What files changed
* What functionality was added
* What functionality was modified
* What functionality was removed
* What tests were added
* What tests were changed
* What assumptions the implementation makes

Compare the PR's stated intent against the issue requirements.

---

# Phase 5 — Inspect the Implementation

Read the changed files completely when necessary.

Do not review only isolated diff hunks.

Trace the implementation through the application.

For each important requirement, answer:

1. Where is this requirement implemented?
2. What code path executes it?
3. What inputs does it accept?
4. What outputs does it produce?
5. What happens on invalid input?
6. What happens at boundary conditions?
7. What happens when related state is missing?
8. What happens when the operation is repeated?
9. Does it interact correctly with existing functionality?

Use repository search aggressively.

For example:

```bash
rg "FunctionName"
rg "ClassName"
rg "CommandName"
```

or the project's equivalent tooling.

---

# Phase 6 — Acceptance Criteria Verification

Create an explicit acceptance-criteria matrix.

Use this structure:

| ID   | Requirement | Status  | Evidence        | Problems           |
| ---- | ----------- | ------- | --------------- | ------------------ |
| AC-1 | Requirement | PASS    | `file.go:123`   | None               |
| AC-2 | Requirement | PARTIAL | `array.go:45`   | Polar case missing |
| AC-3 | Requirement | FAIL    | `command.go:91` | Validation absent  |

Status meanings:

### PASS

The requirement is implemented correctly and evidence supports it.

### PARTIAL

Some portion is implemented, but the requirement is incomplete or incorrect in one or more cases.

### FAIL

The implementation does not satisfy the requirement.

### NOT VERIFIABLE

There is insufficient evidence to determine whether the requirement works.

Use `NOT VERIFIABLE` sparingly.

If you can inspect the code or execute the behavior, do so rather than declaring it unverifiable.

---

# Phase 7 — Bug Hunting

Actively search for defects.

Do not merely summarize the code.

Check for:

## Logic errors

* Incorrect conditions
* Incorrect calculations
* Off-by-one errors
* Incorrect indexing
* Wrong units
* Incorrect coordinate systems
* Incorrect state transitions
* Incorrect assumptions
* Incorrect defaults

## Boundary conditions

Consider:

* Zero
* One
* Minimum valid value
* Maximum valid value
* Negative values
* Empty input
* Very large input
* Duplicate input
* Missing input
* Null/nil values
* Invalid input

## Error handling

Check:

* Errors are detected
* Errors are propagated
* Errors are not silently ignored
* User-facing errors are understandable
* Partial operations don't leave corrupted state
* Invalid operations don't modify existing data

## State and lifecycle

Check:

* Initialization
* Reset behavior
* Repeated execution
* Undo/redo where applicable
* Cancellation
* Selection state
* Resource cleanup
* Object ownership
* Persistent state

## Concurrency

Where applicable check:

* Race conditions
* Shared mutable state
* Locking
* Deadlocks
* Goroutine/thread lifecycle
* Async operations
* Event ordering

## Memory/resource safety

Check for:

* Leaks
* Invalid references
* Use-after-free
* Double cleanup
* Unbounded allocations
* File/resource handles not being closed

## API/interface correctness

Check:

* Function contracts
* Parameter validation
* Return values
* Error semantics
* Backward compatibility
* Callers that may be affected

## User-facing behavior

For UI/application changes verify:

* The feature is actually accessible
* Commands are registered
* UI controls are connected
* Keyboard/mouse input works
* Feedback is provided
* State updates correctly
* Visual results correspond to the underlying data

For CAD functionality specifically consider:

* Coordinate accuracy
* Units
* Angles
* Radians vs degrees
* World/local coordinates
* Floating-point precision
* Snapping
* Selection
* Undo/redo
* Object persistence
* Rendering vs model state

---

# Phase 8 — Regression Analysis

Determine what existing functionality could be affected.

Search for callers and dependencies of modified code.

Ask:

* Did an existing API change?
* Did behavior change for existing callers?
* Could old commands behave differently?
* Could serialization change?
* Could file compatibility change?
* Could existing drawings/data become invalid?
* Could UI state break?
* Could performance regress?

Run existing tests, not just newly added tests.

---

# Phase 9 — Test Review

Inspect every test added or modified by the PR.

Determine whether the tests actually test the behavior they claim to test.

Check for:

* Meaningful assertions
* Boundary cases
* Error cases
* Regression tests
* Integration coverage
* End-to-end behavior where appropriate
* False-positive tests
* Tests that merely execute code without validating results

A test that only verifies "no exception occurred" is generally insufficient for correctness-critical behavior.

Ask:

> If this implementation were subtly wrong, would this test fail?

If the answer is no, identify the weakness.

---

# Phase 10 — Run Verification

Determine the project's normal verification commands.

Examples:

### Go

```bash
go test ./...
go vet ./...
go build ./...
```

### C++

Use the project's documented build/test commands, for example:

```bash
cmake --build build
ctest --test-dir build
```

### JavaScript/TypeScript

```bash
npm test
npm run build
```

### Python

```bash
pytest
```

Do not invent commands that don't match the project.

Use the project's documentation and configuration to determine the correct commands.

Record:

* Command executed
* Result
* Relevant failures
* Whether failures appear related to the PR

If tests fail for unrelated reasons, distinguish that clearly.

---

# Phase 11 — Check Completeness

After verifying explicit acceptance criteria, perform a second pass for omissions.

Look for:

* Requirements mentioned in issue prose but missing from acceptance criteria
* Related functionality implied by the issue
* Missing validation
* Missing tests
* Missing UI wiring
* Missing command registration
* Missing documentation
* Missing error handling
* Unimplemented TODOs
* Placeholder implementations
* Dead code
* Commented-out implementation
* Feature flags that prevent functionality from working
* Code paths that are never reachable

Search for suspicious markers:

```bash
rg "TODO|FIXME|XXX|HACK|not implemented|NotImplemented"
```

Use judgment; not every match is a problem.

---

# Phase 12 — Review PR Scope

Determine whether the PR:

* Contains unrelated changes
* Modifies unnecessarily large portions of the codebase
* Includes generated files unnecessarily
* Includes debugging code
* Introduces unrelated refactoring
* Makes review difficult
* Changes behavior beyond the issue's scope

Unrelated changes should be called out when they increase risk.

Do not reject a PR merely because it contains harmless cleanup.

---

# Severity Classification

Classify discovered problems.

## CRITICAL

A problem that can:

* Corrupt user data
* Cause severe security vulnerabilities
* Crash the application in normal use
* Make the feature fundamentally unusable
* Cause serious irreversible consequences

## HIGH

A significant defect that:

* Violates an important acceptance criterion
* Causes incorrect results
* Breaks existing functionality
* Causes frequent crashes
* Makes a major part of the feature unusable

## MEDIUM

A meaningful problem that:

* Breaks an edge case
* Causes incorrect behavior under certain conditions
* Leaves a requirement partially implemented
* Creates a meaningful regression risk

## LOW

A minor issue that:

* Has limited impact
* Affects uncommon cases
* Creates small usability problems
* Could reasonably be addressed separately

## INFO

A suggestion or observation that is not a correctness problem.

Do not inflate severity.

---

# Important Review Rules

## Do not confuse style with correctness

Only report style issues when they:

* Violate established project conventions
* Make the code difficult to maintain
* Create a realistic bug risk

Do not flood the review with formatting preferences.

---

## Do not invent requirements

If the issue does not require something, do not mark the PR incomplete merely because you personally would have implemented it differently.

You may identify a risk as an observation, but distinguish it from an acceptance-criteria failure.

---

## Do not automatically trust tests

A test can be wrong.

Verify that assertions actually establish the required behavior.

---

## Do not automatically trust the issue either

If the issue contains contradictory requirements, explicitly identify the contradiction.

Do not silently choose an interpretation.

---

## Prefer concrete evidence

Every substantive finding should include:

* Location
* Problem
* Why it matters
* Expected behavior
* Suggested direction for fixing it, when useful

Example:

```text
HIGH — Polar array divides by `count - 1`.

Location:
src/commands/array.cpp:184

Problem:
When count == 1, the angular increment calculation divides by zero.

Why it matters:
A single-item polar array is a valid input according to AC-2.

Expected behavior:
A count of one should produce the original/one requested instance without calculating an angular increment.
```

---

# Final Review Format

At the end of the review, produce the following structure.

# GitHub Issue Review

## Issue

`#123 — Issue title`

## PRs Reviewed

* `#456 — PR title`
* `#457 — PR title`

## Overall Verdict

Choose exactly one:

* **APPROVE**
* **APPROVE WITH NOTES**
* **REQUEST CHANGES**
* **CANNOT VERIFY**

Then provide a concise explanation.

---

## Acceptance Criteria

| ID   | Requirement | Status  | Evidence    |
| ---- | ----------- | ------- | ----------- |
| AC-1 | ...         | PASS    | `file:line` |
| AC-2 | ...         | PARTIAL | `file:line` |
| AC-3 | ...         | FAIL    | `file:line` |

Summarize the overall acceptance-criteria result immediately below the table.

Example:

> **5/6 acceptance criteria pass. AC-4 requires changes before the issue is complete.**

---

## Findings

Order findings by severity.

### CRITICAL

List critical findings.

### HIGH

List high-severity findings.

### MEDIUM

List medium-severity findings.

### LOW

List low-severity findings.

### INFO

List informational observations.

If a severity has no findings, write:

> None.

Each real finding should use:

```text
### [SEVERITY] Short description

**Location:** `path/to/file.ext:123`

**Problem:**  
Describe the defect.

**Why it matters:**  
Explain the consequence.

**Expected behavior:**  
Describe what should happen.

**Recommendation:**  
Describe a reasonable fix.
```

---

## Verification Results

Report the actual commands executed.

Example:

```text
PASS  go test ./...
PASS  go vet ./...
PASS  go build ./...
```

For failures:

```text
FAIL  go test ./...

Failure:
TestArrayPolarSingleItem

Assessment:
Appears related to this PR because...
```

Distinguish:

* PR-related failures
* Pre-existing failures
* Environment/tooling failures
* Tests that could not be executed

---

## Test Coverage Assessment

Evaluate whether the PR has sufficient tests.

Use:

* **GOOD**
* **ADEQUATE**
* **INSUFFICIENT**
* **NONE**

Explain why.

---

## Completeness Assessment

Explicitly answer:

* Are all acceptance criteria implemented?
* Are all issue requirements addressed?
* Are important edge cases handled?
* Is error handling adequate?
* Are existing behaviors preserved?
* Are tests sufficient?
* Is anything obviously missing?

---

## Regression Risk

Rate:

* **LOW**
* **MEDIUM**
* **HIGH**

Explain the reasoning.

---

## Final Recommendation

End with a clear recommendation.

For example:

> **REQUEST CHANGES**
>
> The implementation satisfies 5 of 6 acceptance criteria, but AC-4 is not met. Additionally, the polar-array implementation contains a division-by-zero defect when `count == 1`. The PR should not be merged until these issues are addressed and regression tests are added.

---

# Approval Rules

Use **APPROVE** only when:

* All acceptance criteria are satisfied
* No critical/high/medium correctness issues remain
* Verification succeeds or failures are demonstrably unrelated
* Tests provide reasonable coverage
* No obvious missing functionality exists
* Regression risk is acceptable

Use **APPROVE WITH NOTES** when:

* All requirements are satisfied
* No meaningful correctness defects remain
* Only low-severity or informational issues exist
* Remaining observations do not need to block merging

Use **REQUEST CHANGES** when:

* Any acceptance criterion is FAIL
* A significant acceptance criterion is PARTIAL
* A critical/high/medium bug exists
* Required functionality is missing
* Tests demonstrate a relevant failure
* The implementation creates a significant regression risk

Use **CANNOT VERIFY** only when:

* The required repository/PR information cannot be obtained
* The implementation cannot be inspected
* Required verification cannot reasonably be performed

Do not use `CANNOT VERIFY` simply because a test is difficult to run.

---

# GitHub Review Comments

If the user asks you to post the review to GitHub, do not merely describe what you would post.

Use the GitHub CLI to inspect the current PR state first.

Then post the review using the appropriate GitHub CLI/API mechanism.

Before posting:

1. Verify the PR number.
2. Verify the issue number.
3. Ensure findings are based on current code.
4. Ensure the review is not duplicating an existing review unnecessarily.
5. Clearly distinguish blocking findings from suggestions.

When possible, place comments on the specific lines responsible for a defect.

---

# Multi-PR Issues

If an issue has multiple PRs:

1. Review each PR individually.
2. Determine the combined implementation state.
3. Avoid treating functionality from one PR as missing from another if the PRs are intentionally cumulative.
4. Identify dependencies between PRs.
5. Verify that the final combined state satisfies the issue.
6. Identify which PR introduces each defect.

The final report should include both:

```text
PR #123
- ...
- ...

PR #124
- ...
- ...

Combined Issue Status
- ...
```

---

# Special Handling for CAD / GoSurvey

For CAD functionality, pay particular attention to:

## Geometry

Verify:

* Coordinates
* Transformations
* Rotation
* Translation
* Scaling
* Angles
* Distances
* Intersections
* Tolerances
* Floating-point behavior

## Coordinate systems

Verify:

* World coordinates
* Local coordinates
* Screen coordinates
* Model coordinates
* Coordinate transformations

Never assume degrees and radians are interchangeable.

Explicitly inspect conversions.

---

## Arrays and geometric repetition

For rectangular, polar, or path arrays verify:

* Number of generated objects
* Original object preservation
* Spacing
* Rotation
* Base point
* Direction
* Orientation
* Item count
* Rows/columns
* Angular spacing
* Full-circle behavior
* Partial-circle behavior
* Single-item behavior
* Zero-item behavior
* Negative/invalid input
* Duplicate geometry
* Floating-point accumulation

---

## Zoom / camera behavior

For commands such as zoom extents verify:

* Entire drawing is visible
* Empty drawings behave sensibly
* Single-object drawings work
* Very large/small coordinates work
* Camera state is updated correctly
* Mouse interaction behaves correctly
* Middle-mouse behavior does not interfere with existing navigation

---

## Command system

For new commands verify:

* Command is registered
* Command can be invoked
* Command aliases work if required
* Arguments are parsed correctly
* Invalid arguments are handled
* Command state resets correctly
* Command integrates with undo/redo if applicable
* Command does not leave the application in an invalid state

---

# Review Mindset

Act as a skeptical senior engineer performing a pre-merge verification.

Do not try to make the PR look good.

Do not try to find problems merely for the sake of finding problems.

Your job is to determine the truth:

> **Does this implementation correctly and completely solve the issue?**

If it does, say so.

If it does not, clearly identify exactly why.

Prefer evidence over assumptions, executable verification over speculation, and concrete findings over vague commentary.

