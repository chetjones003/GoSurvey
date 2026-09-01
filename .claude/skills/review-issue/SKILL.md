---

name: github-issue-review
description: Rigorously review a GitHub issue and its associated PR(s). Treat the issue as the specification and independently verify that the implementation is correct, complete, tested, and free of significant bugs or regressions. Use when reviewing an issue/PR, validating an implementation, or determining whether an issue is ready to close.
---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

# GitHub Issue & PR Review

Act as a skeptical senior engineer performing a **pre-merge verification**.

The GitHub **issue is the specification**. The PR is the implementation.

Your job is to determine:

> Does the implementation correctly and completely solve the issue without introducing bugs or regressions?

Do not optimize for making the PR look good. Do not invent requirements. Prefer executable verification and concrete evidence over assumptions.

---

## 1. Establish Repository Context

Inspect the repository before reviewing:

```bash
git status
git branch --show-current
git remote -v
```

Identify:

* Project language(s)
* Build system
* Test framework
* Repository structure
* Relevant architecture
* Normal build/test commands
* Project conventions

Read applicable instructions such as:

```text
CLAUDE.md
AGENTS.md
CONTRIBUTING.md
README.md
```

Follow `CLAUDE.md` and other project instructions.

---

## 2. Load the Issue

Use GitHub CLI:

```bash
gh issue view <issue-number>
gh issue view <issue-number> --comments
```

Treat the issue as the source of truth.

Extract:

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
* Referenced documentation

Normalize the issue into an explicit checklist.

Example:

```text
AC-1: ...
AC-2: ...
AC-3: ...
```

Also identify **implicit requirements** when they are clearly required by the issue.

Do not lose requirements simply because they are buried in prose.

If the issue contains contradictory requirements, explicitly identify the contradiction instead of silently choosing an interpretation.

---

## 3. Find All Associated PRs

Search for every PR related to the issue.

Use:

```bash
gh pr list --state all
gh pr list --search "<issue-number>"
```

Inspect likely candidates:

```bash
gh pr view <pr-number>
```

Look for:

* `Fixes #123`
* `Closes #123`
* `Resolves #123`
* `Refs #123`
* Issue number in title
* Issue number in branch
* Issue number in commits

Do not assume the first matching PR is the only relevant PR.

If multiple PRs contribute to the issue, review them individually and collectively.

---

## 4. Understand Each PR

For every relevant PR:

```bash
gh pr view <pr-number>
gh pr diff <pr-number>
gh pr view <pr-number> --json commits
```

Determine:

* Files changed
* Functionality added/removed/modified
* Tests added/modified
* Important implementation assumptions
* Dependencies on other PRs

Do not rely on PR descriptions or commit messages as proof that something works.

---

## 5. Inspect the Actual Implementation

Read relevant changed files and surrounding code.

Do not restrict the review to the diff.

Trace important behavior through:

* Callers
* State management
* Data structures
* Interfaces
* Event handling
* Parsing
* Serialization
* Rendering
* Error handling
* Configuration
* Tests
* Related functionality

Use repository search aggressively:

```bash
rg "FunctionName"
rg "ClassName"
rg "CommandName"
```

For each requirement determine:

1. Where is it implemented?
2. What code path executes it?
3. What inputs are accepted?
4. What outputs/results are produced?
5. What happens with invalid input?
6. What happens at boundaries?
7. What happens with missing state?
8. What happens when repeated?
9. Does it integrate correctly with existing functionality?

---

# 6. Verify Every Acceptance Criterion

Every acceptance criterion must receive exactly one status:

* **PASS** — Correctly implemented and verified.
* **PARTIAL** — Some required behavior works, but the requirement is incomplete.
* **FAIL** — Requirement is not satisfied.
* **NOT VERIFIABLE** — Insufficient evidence remains after reasonable investigation.

Prefer PASS/PARTIAL/FAIL. Use NOT VERIFIABLE sparingly.

Create an explicit matrix:

| ID   | Requirement | Status  | Evidence    | Problems |
| ---- | ----------- | ------- | ----------- | -------- |
| AC-1 | ...         | PASS    | `file:line` | None     |
| AC-2 | ...         | PARTIAL | `file:line` | ...      |
| AC-3 | ...         | FAIL    | `file:line` | ...      |

Never combine multiple acceptance criteria into one vague assessment.

---

# 7. Hunt for Bugs

Actively try to break the implementation.

Check:

### Logic

* Incorrect conditions
* Incorrect calculations
* Off-by-one errors
* Wrong units
* Incorrect coordinate systems
* Incorrect defaults
* Invalid state transitions

### Boundaries

Test or inspect:

* Zero
* One
* Minimum values
* Maximum values
* Negative values
* Empty input
* Very large input
* Duplicate input
* Missing input
* Null/nil values
* Invalid input

### Error handling

Verify:

* Errors are detected
* Errors are propagated
* Errors are not silently ignored
* User-facing errors are understandable
* Failed operations do not corrupt state
* Invalid operations do not modify existing data

### State/lifecycle

Check:

* Initialization
* Reset
* Repeated execution
* Cancellation
* Undo/redo
* Selection state
* Cleanup
* Persistent state

### Concurrency

Where applicable:

* Race conditions
* Shared mutable state
* Locking
* Deadlocks
* Thread/goroutine lifecycle
* Async ordering

### Resource safety

Check for:

* Leaks
* Invalid references
* Use-after-free
* Double cleanup
* Unbounded allocations
* Unclosed resources

### API/interface

Check:

* Contracts
* Validation
* Return values
* Error semantics
* Backward compatibility
* Existing callers

### User-facing behavior

For application/UI changes verify:

* Feature is accessible
* Commands are registered
* UI controls are connected
* Input works
* Feedback is provided
* State updates correctly
* Visual output matches underlying data

---

# 8. CAD / GoSurvey Verification

For CAD functionality, explicitly check:

### Geometry

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

### Coordinate systems

Verify transformations between:

* World
* Local
* Screen
* Model
* Paper space
* Viewport coordinates

Never assume degrees and radians are interchangeable. Inspect conversions.

### Arrays

For rectangular/polar/path arrays check:

* Item count
* Rows/columns
* Original object preservation
* Spacing
* Rotation
* Base point
* Direction
* Orientation
* Angular spacing
* Full-circle behavior
* Partial-circle behavior
* Single-item behavior
* Zero/invalid input
* Duplicate geometry
* Floating-point accumulation

### Zoom/camera

Check:

* Empty drawings
* Single objects
* Very large/small coordinates
* Entire drawing visibility
* Camera state
* Mouse interaction
* Existing navigation behavior

### Commands

Verify:

* Registration
* Invocation
* Aliases when required
* Argument parsing
* Invalid arguments
* State reset
* Undo/redo integration
* No invalid leftover command state

---

# 9. Regression Analysis

Inspect callers and dependencies of modified code.

Determine whether the PR could:

* Change existing APIs
* Alter existing command behavior
* Break old functionality
* Break serialization
* Break file compatibility
* Corrupt existing drawings/data
* Break UI state
* Cause performance regressions

Run existing tests, not only newly added tests.

---

# 10. Review Tests

Inspect every test added or modified.

Determine whether the tests actually prove correctness.

Check for:

* Meaningful assertions
* Boundary cases
* Error cases
* Regression coverage
* Integration coverage
* End-to-end coverage where appropriate
* False-positive tests
* Tests that merely execute code without checking results

Ask:

> If this implementation were subtly wrong, would this test fail?

If not, identify the weakness.

Passing tests are evidence, not proof of completeness.

---

# 11. Run Verification

Determine the project's actual verification commands from its documentation/configuration.

Examples:

```bash
go test ./...
go vet ./...
go build ./...
```

or:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

or the project's equivalent.

Do not invent commands that do not apply to the repository.

Record:

* Command
* PASS/FAIL
* Relevant output
* Whether failure is PR-related

Distinguish:

* PR-related failures
* Pre-existing failures
* Environment/tooling failures
* Tests that could not be executed

---

# 12. Check for Missing Work

Perform a second completeness pass after acceptance criteria verification.

Look for:

* Requirements buried in issue prose
* Missing validation
* Missing tests
* Missing UI wiring
* Missing command registration
* Missing documentation
* Missing error handling
* TODOs/placeholders
* Dead code
* Commented-out implementation
* Unreachable code
* Disabled feature flags
* Unimplemented paths

Search where useful:

```bash
rg "TODO|FIXME|XXX|HACK|not implemented|NotImplemented"
```

Use judgment; not every match is a defect.

---

# 13. Review PR Scope

Identify:

* Unrelated changes
* Excessive refactoring
* Unnecessary generated files
* Debugging code
* Scope creep
* Changes that increase regression risk

Do not reject harmless cleanup merely because it is unrelated.

---

# Severity

Classify findings accurately.

### CRITICAL

* Data corruption
* Severe security issue
* Normal-use crash
* Fundamentally unusable feature
* Serious irreversible consequences

### HIGH

* Important acceptance criterion violated
* Incorrect results
* Existing functionality broken
* Frequent crashes
* Major functionality unusable

### MEDIUM

* Meaningful edge-case defect
* Partial requirement
* Significant regression risk
* Incorrect behavior under certain conditions

### LOW

* Limited-impact defect
* Uncommon edge case
* Minor usability problem

### INFO

* Observation
* Suggestion
* Non-blocking improvement

Do not inflate severity.

---

# Finding Requirements

Every substantive finding should include:

```text
### [SEVERITY] Short description

**Location:** `path/to/file.ext:123`

**Problem:**
What is wrong.

**Why it matters:**
What consequence it causes.

**Expected behavior:**
What should happen.

**Recommendation:**
A reasonable direction for fixing it.
```

Only report style issues when they violate project conventions, significantly hurt maintainability, or create realistic bug risk.

Do not invent requirements.

---

# Final Report

Return exactly this structure:

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

Briefly explain the verdict.

## Acceptance Criteria

| ID   | Requirement | Status  | Evidence    |
| ---- | ----------- | ------- | ----------- |
| AC-1 | ...         | PASS    | `file:line` |
| AC-2 | ...         | PARTIAL | `file:line` |
| AC-3 | ...         | FAIL    | `file:line` |

Then state:

> **X/Y acceptance criteria pass.**

Explain any failed/partial criteria.

## Findings

Order by severity:

### CRITICAL

None.

### HIGH

None.

### MEDIUM

None.

### LOW

None.

### INFO

None.

Use the required finding format for actual findings.

## Verification Results

```text
PASS  <command>
PASS  <command>
FAIL  <command>
```

Explain failures and whether they are related.

## Test Coverage Assessment

Choose:

* **GOOD**
* **ADEQUATE**
* **INSUFFICIENT**
* **NONE**

Explain why.

## Completeness Assessment

Explicitly answer:

* Are all acceptance criteria implemented?
* Are all issue requirements addressed?
* Are important edge cases handled?
* Is error handling adequate?
* Are existing behaviors preserved?
* Are tests sufficient?
* Is anything obviously missing?

## Regression Risk

Choose:

* **LOW**
* **MEDIUM**
* **HIGH**

Explain why.

## Final Recommendation

End with a clear recommendation.

---

# Verdict Rules

### APPROVE

Only when:

* All acceptance criteria pass
* No critical/high/medium correctness issues remain
* Verification succeeds or failures are clearly unrelated
* Tests provide reasonable coverage
* No obvious missing functionality exists
* Regression risk is acceptable

### APPROVE WITH NOTES

When:

* All requirements pass
* No meaningful correctness defects remain
* Only low-severity or informational issues exist

### REQUEST CHANGES

When:

* Any acceptance criterion FAILs
* An important criterion is PARTIAL
* A critical/high/medium bug exists
* Required functionality is missing
* Relevant tests fail
* Significant regression risk exists

### CANNOT VERIFY

Only when:

* Repository/PR information cannot reasonably be obtained
* Implementation cannot be inspected
* Required verification cannot reasonably be performed

Do not use this merely because a test is difficult to run.

---

# Multi-PR Issues

When multiple PRs contribute to one issue:

1. Review each PR individually.
2. Identify dependencies between PRs.
3. Determine the combined implementation state.
4. Do not call functionality missing if another intentionally cumulative PR provides it.
5. Identify which PR introduces each defect.
6. Verify the final combined state against the issue.

Include:

```text
PR #123
- ...

PR #124
- ...

Combined Issue Status
- ...
```

---

# GitHub Review Comments

Only post to GitHub when explicitly requested.

Before posting:

1. Verify the current PR number.
2. Verify the issue number.
3. Re-check the current PR state.
4. Ensure findings are based on current code.
5. Avoid duplicating an existing review.
6. Distinguish blocking findings from suggestions.
7. Place findings on specific lines when possible.

## Use the GitHub CLI/API appropriate to the repository.

## Review Mindset

Be skeptical, but fair.

Do not try to find problems merely for the sake of finding them.

Do not trust:

* PR descriptions
* Commit messages
* Author claims
* Passing tests

until independently verified.

Do not assume something is implemented because the code looks reasonable.

Trace behavior and verify it.

The goal is not:

> "Does this PR look good?"

The goal is:

> **"Does this implementation correctly and completely solve the issue?"**

If yes, say so.

If no, identify exactly why.

