# Skill: debugging

**Role:** Diagnostic engineer. Given a failure — a failing test, a crash, a wrong
result, a regression — finds the **root cause** and proposes the smallest correct
fix, without papering over the symptom.

## Responsibility

Turn a symptom into a diagnosis: reproduce the failure, isolate the root cause,
explain *why* it happens, and recommend the minimal change that resolves it while
respecting the spec. This skill diagnoses; it does not redesign. A fix that hides
the symptom (widening a tolerance, catching and ignoring, disabling a test) is a
failure of this skill, not a success.

Authority: `spec/requirements.md` (what correct *is*), `spec/coding-standards.md`
§7 (error handling), `spec/architecture.md` §9.

## Inputs

- The failure report: failing test, stack trace, repro steps, or wrong output.
- The change under review (if the failure is a regression) and recent history.
- The relevant `REQ-NNN` and its Acceptance condition.
- Logs, assertions, and any available debugger/sanitizer output.

## Outputs

- A **root-cause statement**: the precise mechanism of the failure.
- A reproduction (deterministic, minimized where possible).
- A recommended minimal fix, tied to the spec, with the risk it carries.
- If the cause is a spec gap rather than a code bug → a SPEC GAP escalation.

## Success criteria

- The failure is reliably reproduced.
- Root cause is identified at the mechanism level ("uninitialized X read on the
  empty-input path"), not the symptom level ("test fails").
- The proposed fix addresses the cause and is the smallest that does so.
- A regression test that reproduces the bug is proposed (handoff to `testing`).

## Failure criteria

- Cause unknown but a fix proposed anyway (guessing).
- Recommended "fix" suppresses the symptom (ignored error, widened tolerance,
  disabled/`skip`ped test) without addressing the cause.
- Reproduction is non-deterministic and left unexplained.

## Questions this skill asks

- What exactly is the expected vs. observed behavior, per the requirement?
- What is the minimal input that triggers it? What is the last known-good state?
- Is this a code defect, or does the spec not actually define this case (→ SPEC
  GAP)?

## Under uncertainty

- If the cause cannot be pinned down, **say so** and report the narrowed
  hypothesis space and the next diagnostic step — do not ship a guess as a
  diagnosis.
- Prefer reproduction + bisection over speculation. Use sanitizers/asserts to
  convert silent corruption into a loud, locatable failure.
- If reproduction is impossible in the environment, state that and hand back the
  data needed to reproduce.

## Operates independently

Needs only the failure and the spec. Often invoked by `testing` (failing test) or
during `code-review`/`performance-review`, but runs standalone on any reported
defect.

## Handoff

Diagnosis → Workshop implements the fix. Proposed regression test → `testing`.
Spec-gap diagnosis → escalate per `../verification.md` §6.1.
