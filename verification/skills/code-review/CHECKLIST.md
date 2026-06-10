# Checklist: code-review

> Domain 2 of `../../review-checklist.md`. Correctness items are always blocking;
> simplicity/readability are blocking only when a *written* standard is violated,
> else advisory.

## Correctness (blocking)
- [ ] Satisfies every claimed `REQ-NNN`; Acceptance conditions met.
- [ ] Edge cases handled: empty, boundary, malformed, maximum size.
- [ ] Every error path logs, returns, or asserts — none empty or swallowing.
- [ ] No `CON`/constraint violated.
- [ ] Concurrency (if any) is data-race-free (`spec/architecture.md` §8).

## Simplicity
- [ ] Smallest change that satisfies the requirement.
- [ ] No speculative generality / premature pattern / one-off "system".
- [ ] Followable control flow; guard clauses over deep nesting.

## Ownership & const
- [ ] Single visible owner per resource; borrowed pointers/refs never free.
- [ ] `const`/immutability by default; large objects passed by ref/borrow.

## Readability
- [ ] Names follow `coding-standards.md` §5 for the language.
- [ ] Comments explain *why*; no dead/commented-out code.
- [ ] Functions do one job.

## Verdict
```
CODE-QUALITY VERDICT — <change id> — <date>
- Correctness: ✓/✗   Simplicity: ✓/✗   Ownership: ✓/✗   Readability: ✓/✗
- Blocking: N   Advisory: M
- Outcome: PASS | FAIL | SPEC GAP
```
