# Checklist: performance-review

> Domain 4 of `../../review-checklist.md`. Judged against the perf requirements in
> `spec/requirements.md`. Every claim needs a number.

## Hot-path discipline
- [ ] No allocation/logging/virtual-dispatch/large-copy added to a spec-marked hot
      path without a profile justifying it.
- [ ] Known-size containers pre-sized/reserved.
- [ ] Hot data laid out for its dominant access pattern (`architecture.md` §5).

## Measurement
- [ ] Every "faster"/"fine" claim backed by a number from the reference benchmark.
- [ ] Before/after numbers recorded for any perf-relevant change.
- [ ] Benchmark run on the reference hardware named in the spec.

## Budgets
- [ ] Relevant benchmark meets its budget/threshold (frame/startup/import/tolerance).
- [ ] No regression beyond the stated threshold.

## No premature optimization
- [ ] No optimization complexity added without a profile proving the need.

## Verdict
```
PERFORMANCE VERDICT — <change id> — <date>
- Requirement: REQ-…  Budget: <value>
- Before: <n>  After: <n>  Within budget: yes/NO
- Hot-path regressions: <ids or none>
- Outcome: PASS | FAIL | HOLD (unmeasured)
```
