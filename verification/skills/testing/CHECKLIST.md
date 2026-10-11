# Checklist: testing

> Mirrors `../../testing-checklist.md`. Missing `must`-coverage or a weakened
> test is blocking.

## Coverage
- [ ] Each `must` REQ has a happy-path test.
- [ ] Each `must` REQ has a failure-mode test asserting the *specified* behavior.
- [ ] Traceability matrix updated; no untethered tests.

## Quality
- [ ] Tests assert the Acceptance condition, not implementation detail.
- [ ] Numeric results checked against tolerance, never exact float equality.
- [ ] Deterministic (no time/order/network/global-state reliance) and independent.
- [ ] Edge cases: empty, boundary, malformed, max size.

## Honesty (anti-gaming)
- [ ] No test deleted/skipped/loosened to pass; any skip has a removal condition.
- [ ] Assertions are meaningful.
- [ ] New test fails against the unpatched code (actually exercises the fix).

## Execution evidence
- [ ] Full suite run and observed green; summary recorded.
- [ ] CI green (if present); flaky tests fixed or quarantined with a task.

## Performance (if a perf REQ applies)
- [ ] Benchmark run on reference hardware, within budget.
- [ ] Before/after numbers recorded for any perf-improvement claim.

## Verdict
```
TEST GATE — <change id> — <date>
- Coverage: pass/FAIL (missing: REQ-…)   Failure-mode: pass/FAIL
- Honesty: pass/FAIL   Suite executed green: yes/NO
- Perf benchmark: within budget/REGRESSION/n-a
- Outcome: PASS | FAIL
```
