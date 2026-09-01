---

name: github-issue-review
description: Rigorously review a GitHub issue and its associated PR(s). Treat the issue as the specification and independently verify correctness, completeness, tests, bugs, and regressions. Use when reviewing an issue/PR or determining whether an issue is ready to close.
--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

# GitHub Issue & PR Review

Act as a skeptical senior engineer performing a pre-merge verification.

**The issue is the specification. The PR is the implementation.**

Determine:

> Does the implementation correctly and completely solve the issue without introducing bugs or regressions?

Do not trust PR descriptions, commit messages, author claims, or passing tests without independently verifying them. Do not invent requirements. Prefer executable verification and concrete evidence.

---

## 1. Repository Context

Start by inspecting:

```bash
git status
git branch --show-current
git remote -v
```

Determine the project's:

* Language(s)
* Build system
* Test framework
* Repository structure
* Architecture relevant to the issue
* Normal build/test commands

Read applicable project instructions, especially:

```text
CLAUDE.md
AGENTS.md
CONTRIBUTING.md
README.md
```

Follow those instructions throughout the review.

---

## 2. Load the Issue

Use GitHub CLI:

```bash
gh issue view <issue-number>
gh issue view <issue-number> --comments
```

Treat the issue as the source of truth.

Extract and normalize:

* Requirements
* Acceptance criteria
* Expected behavior
* Constraints
* Examples
* Edge cases
* UI/API requirements
* Performance/compatibility requirements
* Documentation/testing requirements
* Referenced documentation

Convert them into an explicit checklist:

```text
AC-1: ...
AC-2: ...
AC-3: ...
```

Include clearly implied requirements, but do not invent requirements.

If the issue contains contradictions, identify them explicitly.

---

## 3. Find Associated PRs

Search for all PRs related to the issue:

```bash
gh pr list --state all
gh pr list --search "<issue-number>"
```

Check likely PRs with:

```bash
gh pr view <pr-number>
```

Look for:

* `Fixes #123`
* `Closes #123`
* `Resolves #123`
* `Refs #123`
* Issue number in title, branch, or commits

Do not assume the first matching PR is the only relevant PR.

For multiple PRs, review each individually and then verify their combined state.

---

## 4. Inspect Each PR

For every relevant PR:

```bash
gh pr view <pr-number>
gh pr diff <pr-number>
gh pr view <pr-number> --json commits
```

Determine:

* Changed files
* Added/removed/modified behavior
* Tests added/changed
* Important assumptions
* Dependencies on other PRs

Read relevant files and surrounding code. Do not limit the review to the diff.

Trace affected:

* Callers
* State/data structures
* Interfaces
* Event handling
* Parsing/serialization
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

For every requirement, determine:

1. Where is it implemented?
2. What code path executes it?
3. What inputs are accepted?
4. What result is produced?
5. What happens with invalid input?
6. What happens at boundaries?
7. What happens with missing state?
8. What happens when repeated?
9. Does it integrate correctly with existing behavior?

---

## 5. Acceptance Criteria Verification

Every acceptance criterion must receive exactly one status:

* **PASS** — Correctly implemented and verified.
* **PARTIAL** — Incomplete or incorrect in some cases.
* **FAIL** — Not satisfied.
* **NOT VERIFIABLE** — Cannot reasonably establish correctness.

Use `NOT VERIFIABLE` sparingly.

Produce:

| ID   | Requirement | Status  | Evidence    | Problem |
| ---- | ----------- | ------- | ----------- | ------- |
| AC-1 | ...         | PASS    | `file:line` | None    |
| AC-2 | ...         | PARTIAL | `file:line` | ...     |
| AC-3 | ...         | FAIL    | `file:line` | ...     |

Never combine multiple criteria into one assessment.

---

## 6. Hunt for Bugs

Actively try to break the implementation.

Check:

### Logic

* Incorrect conditions/calculations
* Off-by-one errors
* Wrong units
* Incorrect defaults
* Invalid state transitions
* Incorrect coordinate systems

### Boundaries

* Zero
* One
* Minimum/maximum values
* Negative values
* Empty input
* Very large input
* Duplicate input
* Missing/null/nil input
* Invalid input

### Error Handling

* Errors detected and propagated
* No silently ignored errors
* Useful user-facing errors
* Failed operations leave state intact
* Invalid operations do not corrupt existing data

### State/Lifecycle

* Initialization/reset
* Repeated execution
* Cancellation
* Undo/redo
* Selection state
* Cleanup
* Persistence

### Concurrency/Resources

Where applicable inspect:

* Race conditions
* Shared mutable state
* Locking/deadlocks
* Thread/goroutine lifecycle
* Async ordering
* Resource leaks
* Invalid references
* Use-after-free
* Double cleanup
* Unclosed resources
* Unbounded allocations

### API/Interface

* Contracts
* Validation
* Return values
* Error semantics
* Backward compatibility
* Existing callers

### UI/Application

Verify:

* Feature is accessible
* Commands are registered
* Controls are connected
* Input works
* Feedback is provided
* State updates correctly
* Visual output matches underlying data

---

## 7. CAD / GoSurvey Checks

For CAD functionality, explicitly verify:

### Geometry

* Coordinates
* Transformations
* Rotation/translation/scaling
* Angles/distances
* Intersections
* Tolerances
* Floating-point behavior

### Coordinate Systems

Check transformations between:

* World
* Local
* Screen
* Model
* Paper space
* Viewport

Explicitly inspect degrees/radians conversions.

### Arrays

For rectangular/polar/path arrays verify:

* Item count
* Rows/columns
* Original preservation
* Spacing
* Rotation
* Base point
* Direction/orientation
* Angular spacing
* Full/partial circle behavior
* Single/zero-item behavior
* Invalid input
* Duplicate geometry
* Floating-point accumulation

### Zoom/Camera

Check:

* Empty drawings
* Single objects
* Very large/small coordinates
* Entire drawing visibility
* Camera state
* Mouse/navigation behavior

### Commands

Verify:

* Registration/invocation
* Aliases where required
* Argument parsing
* Invalid arguments
* State reset
* Undo/redo
* No invalid leftover command state

---

## 8. Regression Analysis

Inspect callers and dependencies of modified code.

Check whether the PR could:

* Change existing APIs
* Alter existing command behavior
* Break existing functionality
* Break serialization/file compatibility
* Corrupt existing drawings/data
* Break UI state
* Cause performance regressions

Run existing tests, not only new tests.

---

## 9. Test Review

Inspect every added or modified test.

Verify:

* Meaningful assertions
* Boundary coverage
* Error coverage
* Regression coverage
* Integration coverage
* End-to-end coverage where appropriate
* No false-positive tests

Ask:

> If the implementation were subtly wrong, would this test fail?

If not, identify the weakness.

Passing tests are evidence, not proof.

---

## 10. Run Verification

Determine the project's actual verification commands from its documentation/configuration. Do not invent commands.

Examples:

```bash
go test ./...
go vet ./...
go build ./...
```

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Record every command and result.

Distinguish:

* PR-related failures
* Pre-existing failures
* Environment/tooling failures
* Tests that could not be executed

---

## 11. Completeness Check

Perform a second pass after acceptance-criteria verification.

Look for:

* Requirements buried in issue prose
* Missing validation
* Missing tests
* Missing UI wiring
* Missing command registration
* Missing documentation
* Missing error handling
* TODOs/placeholders
* Dead/commented-out code
* Unreachable paths
* Disabled feature flags
* Unimplemented paths

Useful search:

```bash
rg "TODO|FIXME|XXX|HACK|not implemented|NotImplemented"
```

Use judgment; not every match is a defect.

Also review PR scope for unnecessary refactoring, generated files, debugging code, unrelated changes, or scope creep.

---

## 12. Severity

Classify findings accurately:

**CRITICAL**

* Data corruption
* Severe security issue
* Normal-use crash
* Fundamentally unusable feature
* Serious irreversible consequence

**HIGH**

* Important acceptance criterion violated
* Incorrect results
* Existing functionality broken
* Frequent crashes
* Major functionality unusable

**MEDIUM**

* Meaningful edge-case defect
* Partial requirement
* Significant regression risk
* Incorrect behavior in certain conditions

**LOW**

* Limited-impact defect
* Uncommon edge case
* Minor usability problem

**INFO**

* Non-blocking observation or suggestion

Do not inflate severity.

For each substantive finding provide:

```text
### [SEVERITY] Short description

**Location:** `path/to/file.ext:123`

**Problem:** What is wrong.

**Why it matters:** Consequence.

**Expected behavior:** What should happen.

**Recommendation:** Reasonable fix direction.
```

Only report style issues when they violate project conventions, materially hurt maintainability, or create realistic bug risk.

---

## 13. Final Report

Return exactly:

# GitHub Issue Review

## Issue

`#123 — Issue title`

## PRs Reviewed

* `#456 — PR title`

## Overall Verdict

Choose exactly one:

* **APPROVE**
* **APPROVE WITH NOTES**
* **REQUEST CHANGES**
* **CANNOT VERIFY**

Briefly explain.

## Acceptance Criteria

| ID   | Requirement | Status  | Evidence    |
| ---- | ----------- | ------- | ----------- |
| AC-1 | ...         | PASS    | `file:line` |
| AC-2 | ...         | PARTIAL | `file:line` |
| AC-3 | ...         | FAIL    | `file:line` |

> **X/Y acceptance criteria pass.**

Explain failed/partial criteria.

## Findings

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

List actual findings using the required finding format.

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

Explain.

## Completeness Assessment

Answer:

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

Explain.

## Final Recommendation

End with a clear plain English (like the user knows nothing about programming) recommendation. Use analogies and examples if needed.

---

## Verdict Rules

**APPROVE** only if:

* All acceptance criteria pass
* No critical/high/medium correctness issues remain
* Verification succeeds or failures are clearly unrelated
* Tests provide reasonable coverage
* No obvious missing functionality exists
* Regression risk is acceptable

**APPROVE WITH NOTES** if:

* All requirements pass
* No meaningful correctness defects remain
* Only low/info findings exist

**REQUEST CHANGES** if:

* Any criterion fails
* An important criterion is partial
* A critical/high/medium bug exists
* Required functionality is missing
* Relevant tests fail
* Significant regression risk exists

**CANNOT VERIFY** only if required repository/PR information or verification cannot reasonably be obtained.

Do not use it merely because a test is difficult to run.

---

## Multi-PR Issues

When PRs are cumulative:

1. Review each PR individually.
2. Identify dependencies.
3. Determine the combined implementation state.
4. Do not mark functionality missing if another cumulative PR provides it.
5. Identify which PR introduces each defect.
6. Verify the combined result against the issue.

Report:

```text
PR #123
- ...

PR #124
- ...

Combined Issue Status
- ...
```

---

## GitHub Review Comments

Only post to GitHub when explicitly requested.

Before posting:

1. Verify current issue and PR numbers.
2. Re-check current PR state.
3. Base findings on current code.
4. Avoid duplicating existing reviews.
5. Distinguish blocking findings from suggestions.
6. Place comments on specific lines when possible.
7. Use the appropriate GitHub CLI/API mechanism.

---

# Review Mindset

Be skeptical but fair.

Do not search for problems merely to find problems. Do not trust claims until independently verified.

Prefer:

**issue requirements → code inspection → executable verification → evidence → verdict**

The goal is not:

> "Does this PR look good?"

The goal is:

> **"Does this implementation correctly and completely solve the issue?"**

If yes, say so. If no, identify exactly why.

