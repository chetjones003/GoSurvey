---

name: github-issue-implementation
description: Fully review and implement a GitHub issue from start to finish. Use when asked to review, plan, implement, and submit a GitHub issue as a PR.
----------------------------------------------------------------------------------------------------------------------------------------------------------

# GitHub Issue Implementation

Fully take a GitHub issue from **review → implementation → PR → final review**.

The GitHub issue, repository `CLAUDE.md`, and `spec/` are the source of truth. Follow them at all times.

## Workflow

### 1. Review the Issue

Read the **entire issue**, including comments, and understand:

* Requirements
* Acceptance Criteria
* Expected behavior
* Constraints
* Edge cases
* UI/API/testing requirements

Inspect the existing codebase and relevant architecture before planning.

Determine whether the issue is sufficiently defined to implement.

If there is a requirements, specification, or architectural problem:

**Stop and explain the problem to the user in plain English. Do not guess.**

### 2. Create the Plan

Create a concise implementation plan covering:

* Requirements and Acceptance Criteria
* Files/subsystems affected
* Existing code to reuse
* Implementation approach
* Tests
* Verification

Check the plan against the repository architecture and coding standards.

### 3. Create the Branch

Start from the current `beta` branch and create an appropriately named branch:

```text
feat/<name>
fix/<name>
docs/<name>
refactor/<name>
test/<name>
chore/<name>
```

Never work directly on `beta` or `master`.

### 4. Implement

Implement the complete issue, not just its core functionality.

* Follow existing architecture and patterns.
* Make the smallest sufficient change.
* Add/update appropriate tests.
* Integrate all required UI, commands, APIs, persistence, rendering, etc.
* Do not invent unrelated functionality.
* Do not silently expand scope.

### 5. Verify

Before committing:

* Build the project.
* Run relevant tests.
* Run applicable verification skills.
* Review the implementation for correctness, completeness, regressions, and architectural problems.
* Verify **every Acceptance Criterion explicitly**.

Do not proceed if the implementation does not satisfy the issue.

### 6. Commit and Push

Review the diff for:

* Unrelated changes
* Debug code
* Temporary files
* Generated artifacts
* Secrets
* Accidental modifications

Commit with a clear message describing the change.

Push the branch to the remote.

### 7. Create the PR

Create a Pull Request:

```text
<feature/fix/docs branch> → beta
```

The PR should clearly describe:

* What changed
* Why it changed
* Tests performed
* Acceptance Criteria status

Do not target `master` for normal issue work.

### 8. Final Review

After creating the PR, perform a **second independent review** of:

* The original GitHub issue
* Every Acceptance Criterion
* The PR diff
* The implementation
* Tests
* Architecture
* Potential bugs/regressions

Treat this as reviewing someone else's PR.

If problems are found, fix them, push the changes, and review again.

Do not declare the issue complete until the final review passes.

### 9. Report

Give the user a concise final report containing:

The report should be in plain English like the user has no experience in software engineering. Use clear analogies and examples to help the user understand

* Issue number/title
* What was implemented
* Branch name
* Commit
* PR number/link
* Tests/build performed
* Acceptance Criteria results
* Final review result
* Any remaining concerns

## Important Rules

* **Never guess about requirements.**
* **Never modify the SPEC to make implementation pass.**
* **Never work directly on `master` or `beta`.**
* **Never merge development work directly into `master`.**
* **Never declare completion without verifying every Acceptance Criterion.**
* **Never stop at a partial implementation when the issue requires more.**

### Explaining Problems to the User

When explaining a **REQ, SPEC, Acceptance Criteria, architecture, or SPEC GAP problem**, assume the user knows **nothing about programming**.
The report should be in plain English like the user has no experience in software engineering. Use clear analogies and examples to help the user understand

Explain:

1. What the issue currently says.
2. What is missing, unclear, or contradictory.
3. Why it matters.
4. What choices the user has.
5. Which choice you recommend.

Use plain English and simple analogies when helpful.

**Explain the decision before asking the user to make it.**

