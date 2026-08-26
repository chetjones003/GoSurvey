---
name: github-issue-implementation
description: Fully implement a GitHub issue from its requirements and acceptance criteria. Use when asked to implement, complete, or finish a GitHub issue. Treat the issue as the specification, inspect the existing codebase before making changes, implement all required functionality, add appropriate tests, run verification, and explicitly validate every acceptance criterion before declaring the issue complete.
---

# GitHub Issue Implementation

## Purpose

This skill is used to **fully implement a GitHub issue from start to finish**.

The GitHub issue is the specification.

The goal is not to produce a partial implementation or a rough first pass.

The goal is:

> **Implement every requirement, satisfy every acceptance criterion, integrate the functionality into the existing application correctly, verify the implementation, and leave the repository in a working state.**

Do not declare the issue complete until the implementation has been verified against every acceptance criterion.

---

# Core Principles

## 1. The GitHub issue is the source of truth

Before writing code, read the entire issue.

Extract:

- Issue title
- Description
- Requirements
- Acceptance criteria
- Expected behavior
- Examples
- Constraints
- Edge cases
- UI requirements
- API requirements
- Performance requirements
- Compatibility requirements
- Testing requirements
- Documentation requirements
- Referenced issues
- Referenced files
- Screenshots or diagrams

Do not rely solely on the issue title.

Important requirements may be buried inside the description.

---

## 2. Never start coding before understanding the existing implementation

Before modifying code:

- Inspect the repository structure
- Locate the relevant subsystem
- Find related classes/functions/modules
- Read surrounding implementation
- Find existing patterns
- Find existing tests
- Understand how the feature integrates with the rest of the application

Do not create a parallel implementation when an existing architecture should be extended.

Prefer integrating with established project patterns.

---

## 3. Acceptance criteria are a checklist, not suggestions

Every acceptance criterion must be explicitly tracked.

Create an internal implementation checklist such as:

```text
AC-1: Add X
AC-2: Support Y
AC-3: Handle Z
AC-4: Add UI control
AC-5: Add tests
```

Do not move on from an acceptance criterion until you understand how it will be satisfied.

---

## 4. Implement the complete feature

Do not stop after implementing the core logic.

Consider the entire feature lifecycle:

```text
Input
  ↓
Validation
  ↓
Command / Controller
  ↓
Business Logic
  ↓
State / Model
  ↓
Persistence
  ↓
Rendering / UI
  ↓
User Feedback
  ↓
Testing
```

The exact layers depend on the project.

A feature is not complete if the underlying function exists but the user cannot access it.

Likewise, a UI button is not complete if it does not correctly invoke the underlying functionality.

---

## 5. Do not work around the architecture

Avoid:

- Duplicating existing functionality
- Hard-coded special cases
- Temporary hacks
- Bypassing validation
- Bypassing the command system
- Bypassing state management
- Introducing unnecessary global state
- Copy/pasting large sections of existing code

If the existing architecture needs to change, make the architectural change deliberately and keep it focused on the issue.

---

# Phase 1 — Establish Repository Context

Start by determining the current repository and project state.

Run:

```bash
git status
git branch --show-current
git remote -v
```

Inspect the repository:

```bash
ls
```

On Windows PowerShell, equivalent commands may be used.

Look for project instructions:

```text
CLAUDE.md
AGENTS.md
CONTRIBUTING.md
README.md
```

Read applicable instructions before modifying code.

Also identify:

- Programming languages
- Build system
- Test framework
- Package manager
- Application architecture
- Source directories
- Test directories
- Build directories
- Scripts

---

# Phase 2 — Load the GitHub Issue

Use GitHub CLI when available:

```bash
gh issue view <issue-number>
```

If comments may contain important requirements:

```bash
gh issue view <issue-number> --comments
```

Read the entire issue.

Do not begin implementation until the issue has been understood.

---

# Phase 3 — Normalize Requirements

Convert the issue into an explicit implementation plan.

For example:

```text
Requirement 1:
Add DIMANGULAR command.

Requirement 2:
Support selecting two lines.

Requirement 3:
Calculate the included angle.

Requirement 4:
Allow placement of the dimension.

Requirement 5:
Add the command to the dimension toolbar.

Requirement 6:
Add DIMSTY command.

Requirement 7:
Allow editing text size, font, color, alignment, etc.

Requirement 8:
Add tests for angle calculation.
```

Separate:

### Explicit acceptance criteria

Things the issue explicitly says must be true.

### Implicit requirements

Things necessary for the requested feature to actually function correctly.

For example:

> "Add a DIMANGULAR button."

Implicit requirements include:

- Button exists
- Button is visible in the appropriate UI
- Button invokes the correct command
- Command is registered
- Command state initializes correctly
- Command completes correctly
- Command can be cancelled

Do not invent unrelated requirements.

---

# Phase 4 — Inspect Existing Architecture

Search for existing related functionality.

Use repository search:

```bash
rg "related-term"
```

Examples:

```bash
rg "DIMLINEAR"
rg "DIMENSION"
rg "Command"
rg "Snap"
rg "Selection"
```

Identify:

- Related commands
- Similar UI controls
- Existing geometry functions
- Existing state management
- Existing rendering code
- Existing data structures
- Existing tests
- Existing error handling

Read the relevant implementations completely enough to understand their behavior.

---

# Phase 5 — Create an Implementation Plan

Before making substantial changes, formulate a concise implementation plan.

The plan should identify:

1. Files/components that need modification
2. New files/components that need creation
3. Existing functionality that can be reused
4. Architectural changes required
5. Tests that need to be added
6. Verification commands

Example:

```text
Implementation Plan

1. Extend DimensionCommand to support angular dimensions.
2. Add angle calculation helper to geometry module.
3. Add DIMANGULAR command registration.
4. Add toolbar button.
5. Add placement state to the command.
6. Add unit tests for angle calculations.
7. Add command integration tests.
8. Run full test suite.
9. Verify each acceptance criterion.
```

Keep the plan focused.

Do not over-engineer.

---

# Phase 6 — Implement Incrementally

Implement the issue in logical pieces.

After each meaningful change:

- Re-read the affected code
- Check for compile errors
- Check interfaces
- Check callers
- Check state transitions

Avoid making enormous changes before compiling/testing.

Prefer small, verifiable steps.

---

# Phase 7 — Follow Existing Patterns

When implementing something similar to an existing feature, use the existing implementation as a pattern.

For example, if adding a new command:

Inspect an existing command and determine:

- How it is registered
- How input is collected
- How selection works
- How cancellation works
- How command state resets
- How errors are reported
- How undo/redo works
- How rendering is triggered

Then implement the new command consistently.

Do not invent a completely different command architecture unless the issue requires it.

---

# Phase 8 — Handle Edge Cases

For every significant feature, consider:

### Input

- Empty input
- Missing input
- Invalid input
- Duplicate input
- Unexpected input

### Numeric values

- Zero
- Negative values
- Very small values
- Very large values
- Floating-point precision
- Division by zero
- NaN
- Infinity

### Collections

- Empty collection
- One item
- Two items
- Duplicate items
- Large collections

### State

- Initial state
- Repeated execution
- Cancellation
- Reset
- Undo
- Redo
- Existing selection
- Missing selection

### UI

- Feature accessibility
- Disabled state
- Error feedback
- Focus
- Mouse interaction
- Keyboard interaction

Only implement edge-case behavior that makes sense for the application and issue.

---

# Phase 9 — CAD / GoSurvey-Specific Verification

When implementing CAD or surveying functionality, be especially careful with geometry.

Verify:

## Coordinates

Check:

- Northing
- Easting
- Elevation
- X/Y/Z
- World coordinates
- Local coordinates
- Screen coordinates

Do not accidentally swap axes.

---

## Angles

Explicitly verify:

- Degrees vs radians
- Bearing vs azimuth
- Clockwise vs counterclockwise
- Angle normalization
- Quadrants
- 0° / 360° boundaries
- Negative angles
- Full-circle behavior

Never assume an angle convention.

Find and follow the convention already used by the project.

---

## Distances

Verify:

- Units
- Precision
- Zero distance
- Very small distances
- Large distances
- Floating-point tolerance

---

## Snapping

For snapping functionality verify:

- Endpoint snapping
- Midpoint snapping
- Object snapping
- Closest snap point
- Overlapping snap candidates
- Snap priority
- Snap tolerance
- Cursor position
- Geometry proximity

If multiple snap points are available, verify that the correct candidate is selected according to the application's rules.

---

## Commands

For new or modified commands verify:

- Command registration
- Command invocation
- Input collection
- Selection
- Snapping
- Preview
- Completion
- Cancellation
- Reset
- Undo/redo
- Error handling
- Rendering
- Persistence

---

# Phase 10 — UI Integration

If the issue requires UI changes, verify the complete path:

```text
UI Control
    ↓
Event Handler
    ↓
Command / Action
    ↓
Application Logic
    ↓
State Update
    ↓
Renderer
```

Do not consider a UI requirement complete until the control actually works.

For every new button/menu item:

- Verify it appears in the intended location
- Verify it has the correct label/icon
- Verify it invokes the correct functionality
- Verify enabled/disabled state if applicable
- Verify command cancellation/reset behavior
- Verify errors are surfaced appropriately

---

# Phase 11 — Error Handling

Implement appropriate error handling.

Check:

- Invalid input
- Missing objects
- Invalid selections
- Failed calculations
- File errors
- Resource errors
- Invalid state

Errors should not silently disappear.

Avoid:

```text
catch error and ignore it
```

or equivalent behavior unless the ignored error is explicitly safe and intentional.

Do not allow invalid operations to leave the application in a partially modified state.

---

# Phase 12 — Tests

Add or update tests appropriate to the change.

Tests should cover:

### Normal behavior

The expected successful path.

### Boundary behavior

Important limits and edge cases.

### Invalid behavior

Inputs that should be rejected.

### Regression behavior

Existing functionality that could be affected.

---

## Test quality rule

Ask:

> If the implementation were subtly wrong, would this test fail?

If not, the test is probably insufficient.

Avoid tests that only verify that code executes without crashing.

Prefer assertions that verify actual results.

---

# Phase 13 — Run Verification

Determine the project's correct verification commands.

Do not blindly run commands that don't apply to the repository.

Examples:

### Go

```bash
go test ./...
go vet ./...
go build ./...
```

### C++

Use the repository's configured build:

```bash
cmake --build build
ctest --test-dir build
```

### JavaScript / TypeScript

```bash
npm test
npm run build
```

### Python

```bash
pytest
```

Run the appropriate commands for the project.

---

# Phase 14 — Investigate Failures

If verification fails:

Do not immediately assume the test is wrong.

Determine:

1. Is the failure caused by the implementation?
2. Is the failure caused by an existing bug?
3. Is the test outdated?
4. Is the environment misconfigured?
5. Is the failure unrelated?

If the failure is caused by the implementation, fix it.

Do not simply report the failure and stop if the issue can reasonably be corrected.

---

# Phase 15 — Review Your Own Implementation

After implementation and tests pass, perform a second review as though reviewing someone else's PR.

Ask:

### Correctness

- Does the implementation actually do what the issue requested?
- Are calculations correct?
- Are state transitions correct?
- Are errors handled?

### Completeness

- Did I implement every acceptance criterion?
- Did I implement every important requirement?
- Did I forget UI integration?
- Did I forget command registration?
- Did I forget persistence?
- Did I forget tests?

### Regression

- Did I change existing behavior?
- Could existing callers break?
- Could existing commands break?
- Could old data become invalid?

### Maintainability

- Does the implementation follow project patterns?
- Is there unnecessary duplication?
- Did I introduce unnecessary complexity?
- Are names clear?
- Are abstractions appropriate?

---

# Phase 16 — Acceptance Criteria Verification

Now perform an explicit final verification against every acceptance criterion.

Create a table:

| ID | Acceptance Criterion | Status | Evidence |
|---|---|---|---|
| AC-1 | ... | PASS | `file:line` |
| AC-2 | ... | PASS | `file:line` |
| AC-3 | ... | PASS | `test:123` |

Every criterion must be:

**PASS**

before declaring the issue complete.

If any criterion is:

- PARTIAL
- FAIL
- NOT VERIFIABLE

continue working unless there is a legitimate external blocker.

---

# Phase 17 — Check Git Diff

Before finishing:

```bash
git diff
```

Also inspect:

```bash
git status
```

Look for:

- Accidental changes
- Debugging code
- Temporary files
- Generated artifacts
- Unrelated modifications
- Commented-out code
- TODOs
- Secrets
- Credentials
- Large accidental files

Remove anything unrelated to the issue.

Do not revert legitimate existing user changes.

---

# Phase 18 — Final Verification

Run the relevant verification commands one final time after all fixes.

Do not rely on an earlier test run if code changed afterward.

Record the final state.

---

# Completion Criteria

An issue is considered **fully implemented** only when:

- [ ] The issue has been completely read
- [ ] Requirements have been extracted
- [ ] Acceptance criteria have been identified
- [ ] Existing architecture has been inspected
- [ ] Implementation has been completed
- [ ] Required UI integration is complete
- [ ] Required command/API integration is complete
- [ ] Appropriate error handling exists
- [ ] Important edge cases are handled
- [ ] Tests have been added/updated where appropriate
- [ ] Relevant tests pass
- [ ] Build succeeds
- [ ] Relevant static analysis passes
- [ ] Existing functionality has been checked for regressions
- [ ] Every acceptance criterion has been explicitly verified
- [ ] Git diff has been reviewed
- [ ] No unrelated changes remain

Do not claim completion if one of these is clearly outstanding.

---

# Do Not Stop Prematurely

Do not stop after:

- Creating a class
- Creating a function
- Making the project compile
- Making one test pass
- Implementing the "main" part of the feature
- Updating the UI without connecting it
- Updating backend logic without exposing it
- Adding tests without verifying behavior

Continue until the complete feature works.

---

# Handling Ambiguous Requirements

If the issue is ambiguous but a reasonable interpretation can be derived from:

- Existing project behavior
- Existing architecture
- Similar commands/features
- Existing tests
- Common conventions within the application

use the most consistent interpretation and document the assumption.

If ambiguity would materially change the implementation and cannot reasonably be resolved from the repository, stop and ask the user a focused question.

Do not invent major product behavior.

---

# Handling Existing Bugs

If implementation reveals an existing bug:

Determine whether it is required to complete the issue.

### Fix it when:

- The issue depends on the broken behavior
- The bug prevents the acceptance criteria from being satisfied
- The fix is small and directly related
- Leaving it would make the feature incorrect

### Do not expand scope unnecessarily when:

- The bug is unrelated
- Fixing it requires a large architectural change
- The issue can be correctly implemented without touching it

In that case, document the unrelated issue rather than silently expanding scope.

---

# Handling Failed or Broken Existing Tests

If existing tests fail before your implementation:

Determine whether they are pre-existing.

Do not modify unrelated tests merely to make the suite green.

If your implementation changes the intended behavior, update tests only when the issue legitimately changes that behavior.

---

# Coding Standards

Follow existing project conventions.

Prefer:

- Clear names
- Small focused functions
- Existing abstractions
- Existing utilities
- Existing error-handling patterns
- Existing test patterns

Avoid:

- Unnecessary abstractions
- Premature optimization
- Large unrelated refactors
- Duplicate implementations
- Magic numbers
- Temporary hacks
- Dead code
- Debug logging left behind

---

# Final Response

When the implementation is complete, provide a concise final report.

Use:

# Issue Implementation Complete

## Issue

`#123 — Issue title`

## Implementation Summary

Briefly describe what was implemented.

## Acceptance Criteria

| ID | Requirement | Status |
|---|---|---|
| AC-1 | ... | PASS |
| AC-2 | ... | PASS |
| AC-3 | ... | PASS |

Every acceptance criterion should show **PASS**.

## Files Changed

List the important files and briefly explain their purpose.

## Tests

List tests added or modified.

## Verification

Report the commands actually executed.

Example:

```text
PASS  go test ./...
PASS  go vet ./...
PASS  go build ./...
```

## Notes

Mention:

- Important implementation decisions
- Any assumptions
- Any remaining non-blocking concerns

If nothing remains:

> No known outstanding issues.

---

# Final Rule

Before declaring the issue complete, ask yourself:

> **If a skeptical reviewer compared this implementation line-by-line against the GitHub issue, would every acceptance criterion be demonstrably satisfied?**

If the answer is not clearly yes, **keep working**.
