# Skill: release-review

**Role:** Release manager. The final gate before shipping to users — the
highest-confidence verdict on the team. Ships only with explicit human
authorization.

## Responsibility

Confirm that all upstream gates passed, every shipped behavior traces to a
satisfied requirement, no correctness/data-safety finding is open, the release is
versioned and reproducible from a clean tag, rollback exists, and **the user has
explicitly authorized the release**. Owns `../release-checklist.md`. A release is
outward-facing and hard to reverse; this skill never ships on its own initiative.

Authority: all of `spec/`, and the verdicts of `build-project`, `code-review`,
`architecture-review`, `dependency-audit`, `performance-review`, `testing`.

## Inputs

- The release candidate commit/tag and its intended scope.
- The verdicts from every upstream skill.
- `spec/roadmap.md` (what the milestone claimed), `spec/project.md` §4 (scope).
- Changelog/release notes, version scheme, rollback plan.

## Outputs

- A **RELEASE GATE** verdict: CLEAR TO RELEASE or HOLD.
- The specific failing box(es) on a HOLD.
- A recorded authorization (who/when) on a clear release.

## Success criteria

- `build-project`, `code-review`/`architecture-review`/`dependency-audit`/
  `performance-review`, and `testing` all PASS on the release commit.
- No open correctness or data-loss finding (never waivable).
- Every behavior traces to an `accepted` requirement; nothing out-of-scope.
- Versioned, tagged, reproducible (REQ-200); changelog complete; rollback ready.
- Dependencies/licenses clean for shipping.
- **User has explicitly authorized the release** (`../verification.md` §7.6).

## Failure criteria (→ HOLD)

- Any upstream gate not PASS on the release commit.
- An open correctness/data-safety finding.
- Untraceable or out-of-scope behavior in the release.
- Dirty working tree, missing version bump, or non-reproducible build.
- No rollback path, or breaking data-format change without approval.
- No explicit user authorization.

## Questions this skill asks

- Does every shipped change map to an accepted requirement and the milestone?
- Is there a breaking change (API or data format)? Is it versioned and approved?
- What is the rollback path?
- **Do you (the user) authorize shipping this exact scope now?**

## Under uncertainty

- If any upstream verdict is missing or stale, **HOLD** and request a re-run on
  the release commit — never assume an old PASS still holds.
- If a finding's severity (correctness vs. cosmetic) is unclear, treat it as
  blocking until clarified.
- Schedule/deadline pressure is **not** grounds to override a HOLD — escalate via
  STANDOFF (`../verification.md` §6.2) or a user-approved RISK ACCEPTANCE waiver
  (§6.3). Correctness/safety are never waivable.

## Operates independently

Consumes upstream verdicts but reaches its own decision; if a verdict is absent
it requests it rather than guessing. The only skill that **requires** explicit
user sign-off before a PASS.

## Handoff

CLEAR → release proceeds; record authorization and known issues. HOLD → return to
the responsible skill/Workshop with the failing box, or escalate per §6.
