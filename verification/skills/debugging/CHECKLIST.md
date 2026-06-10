# Checklist: debugging

> A diagnosis is complete only when every box is checked. An incomplete diagnosis
> is reported as such — never dressed up as a fix.

## Reproduce
- [ ] Expected vs. observed stated against the requirement's Acceptance condition.
- [ ] Deterministic reproduction achieved.
- [ ] Minimal triggering input identified.
- [ ] For a regression: introducing change located (bisect).

## Root cause
- [ ] Failure isolated to the smallest code region.
- [ ] Mechanism explained (what/where/why), not just the symptom.
- [ ] Distinguished code defect vs. spec gap.

## Fix
- [ ] Proposed fix addresses the **cause**, not the symptom.
- [ ] It is the smallest change that resolves the cause.
- [ ] Its risk/side effects noted.
- [ ] Fix does not suppress (no ignored error / widened tolerance / disabled test).

## Handoff
- [ ] Regression test proposed for `testing` (fails before, passes after).
- [ ] Spec-gap diagnosis escalated per `verification.md` §6.1 (if applicable).

## Verdict
```
DIAGNOSIS — <failure id> — <date>
- Reproduced: yes/NO    Root cause: <mechanism> / UNKNOWN
- Classification: CODE FIX | SPEC GAP | UNDIAGNOSED
- Recommended fix: <one line> (risk: …)
- Regression test: proposed/NO
```
