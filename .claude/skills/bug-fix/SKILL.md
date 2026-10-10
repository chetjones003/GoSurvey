---
name: bug-fix
description: Use when the user reports a bug or wants something fixed. Collects a structured BUG REPORT, then drives it through the three-layer workflow — identify expected behavior and violated requirements, find the root cause with evidence, and implement the smallest correct fix validated by Verification. No speculative fixes; root cause must be identified first.
---

# bug-fix

Drive a bug through the project's three-layer governance system (`CLAUDE.md`,
`spec/`, `verification/`, `workshop/`). **The root cause must be identified with
evidence before any fix is written. No speculative fixes.**

---

## Step 0 — Collect the bug report

Build the BUG REPORT by **interviewing the user one question at a time**. Ask a
single question, wait for the answer, then ask the next. **Never dump the whole
template at once.** Anything the user already supplied pre-fills that field — skip
questions already answered, and confirm what you inferred rather than re-asking.

Ask in this order, one at a time:

1. **Title** — "What's a short description of the bug?"
2. **Observed Behavior** — "What actually happens?"
3. **Expected Behavior** — "What should happen instead?"
4. **Steps to Reproduce** — "How do you trigger it? List the steps in order."
   (Collect until the user signals they're done.)
5. **Relevant Files** — "Any files or areas you suspect? (optional — skip if
   unsure.)"

*Constraints* default to the standard set below; you don't ask for them. *Relevant
Files* is optional; everything else is needed before proceeding.

Do not proceed until **Title, Observed Behavior, Expected Behavior, and Steps to
Reproduce** are populated. Then assemble the answers into the structure below and
**echo the completed report back to the user for confirmation** before starting.

```
BUG REPORT

Title:              <short bug description>
Observed Behavior:  <what happens?>
Expected Behavior:  <what should happen?>
Steps to Reproduce: 1. <step>
Relevant Files:     <optional>
Constraints:        - Root cause must be identified.
                    - Do not implement speculative fixes.
                    - Verification must validate the fix.
                    - Existing functionality must continue working.
```

---

## Step 1 — SPECIFICATION (define correct)

- Determine the **expected behavior** precisely, grounded in the spec — not just
  the reporter's wording.
- Identify the **violated requirement(s)**: which `REQ-NNN` and Acceptance
  condition does the observed behavior break?
- If the behavior was **never specified**, this is a SPEC GAP, not a silent fix —
  escalate to define the requirement first, then continue.

Output: expected vs. observed stated against the governing requirement.

## Step 2 — VERIFICATION (diagnose with evidence)

Act as the Verification Layer using the `debugging` skill's discipline
(`verification/skills/debugging/`). Produce, in order:

1. **Reproduce.** Establish a deterministic reproduction; minimize the triggering
   input. If it is a regression, identify the introducing change (bisect).
2. **Root cause.** Identify the mechanism — *what* is wrong, on *which* path, and
   *why* it produces the symptom. State it in one or two sentences.
3. **Evidence.** Provide concrete evidence supporting the diagnosis (the failing
   path, a log/assert/sanitizer result, the minimal repro). A cause without
   evidence is a hypothesis — label it as such and keep investigating.
4. **Regression risks.** What could the fix break? Which existing behavior must be
   protected?
5. **Validation tests.** Define a regression test that **fails against the current
   code** and will pass once fixed, plus any tests guarding the regression risks.

**Do not implement a fix until the root cause is identified with evidence.** If
the cause cannot be pinned down, report the narrowed hypothesis space and the next
diagnostic step rather than guessing.

## Step 3 — WORKSHOP (smallest correct fix)

Follow `workshop/implementation-rules.md`, bug-fix workflow.

- Write the **regression test first** (it should fail now).
- Implement the **smallest correct fix** that addresses the *root cause*, not the
  symptom. No widened tolerance, no swallowed error, no disabled/skipped test, no
  speculative change.
- **Explain why the fix works** — connect the change to the root cause and show
  why it resolves it without altering unrelated behavior.
- Make no architectural decision; if the proper fix requires one, escalate as a
  SPEC GAP.

## Step 4 — Run all required validation checks

- Confirm the regression test now passes and the full suite stays green (run it;
  record results — never assume).
- Self-run the relevant `verification/skills/` checklists: `build-project`,
  `code-review`, `testing`, and `architecture-review` (confirm no structural
  change crept in). Resolve every blocking finding.

---

## Output

Report these four sections explicitly:

1. **Root cause** — the mechanism, with supporting evidence.
2. **Fix plan** — the smallest correct change and why it addresses the cause.
3. **Implementation** — the modifications made, file by file, with rationale.
4. **Verification results** — regression test (fails-before / passes-after),
   suite status, and the checklist verdicts.

---

## Hard rules

- ❌ No fix before a root cause is identified with evidence.
- ❌ No speculative fixes; no suppressing the symptom (ignored error, widened
  tolerance, disabled test).
- ❌ Do not break existing functionality — protect it with tests.
- ✅ Smallest correct fix; explain why it works; Verification validates it.
