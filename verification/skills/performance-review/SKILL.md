# Skill: performance-review

**Role:** Performance engineer. Protects the project's performance budgets and
enforces the discipline of *measure before claiming* — in both directions:
no unjustified regression, and no complexity added for unmeasured gain.

## Responsibility

Confirm the change does not regress a performance requirement and that any
performance claim (faster *or* "fine") is backed by a number from the reference
benchmark on reference hardware. Owns Domain 4 of `../review-checklist.md`. Guards
hot paths against added allocation, logging, virtual dispatch, and large copies;
equally guards readability against premature optimization.

Authority: `spec/requirements.md` performance requirements (frame budget,
tolerance, import/startup time), `spec/coding-standards.md` §10,
`spec/architecture.md` §5 (data-oriented design).

## Inputs

- The change (diff), focused on code in paths the spec marks hot.
- The performance requirements and their budgets/thresholds.
- Benchmark definitions, reference hardware spec, and before/after numbers.
- Profiles, if the change touches a hot path.

## Outputs

- A **performance verdict**: PASS / FAIL.
- Findings for regressions or unmeasured claims in `../verification.md` §5 format.
- Recorded before/after numbers for any perf-relevant change.

## Success criteria

- Relevant benchmark meets its budget on reference hardware.
- No allocation/logging/virtual-dispatch/large-copy added to a hot path without a
  profile justifying it.
- Every "faster"/"fine" claim carries a measured number.
- Known-size containers pre-sized; hot data laid out for its access pattern.

## Failure criteria

- A benchmark regresses beyond the stated threshold.
- A hot-path change ships with no measurement, where it plausibly affects a perf
  requirement.
- Optimization complexity added with no profile proving the need (readability cost
  for unmeasured benefit).

## Questions this skill asks

- Which path does this touch, and is it marked hot in the spec?
- What are the before/after numbers on reference hardware?
- Is this optimization justified by a profile, or is it speculative?
- What is the budget/threshold for the affected requirement?

## Under uncertainty

- If perf impact is plausible but unmeasured, **require a measurement** before
  PASS — do not pass on a guess, in either direction.
- If reference hardware/benchmark is unavailable, state that and HOLD the perf
  verdict rather than assuming within budget.
- If meeting a budget would force complexity that harms readability (or vice
  versa), **ask the user** which the project prefers here
  (`../verification.md` §7.5).

## Operates independently

Needs the change, the perf requirements, and a benchmark/profiler. Pairs with
`testing` (which runs the benchmarks) and `debugging` (for regression
root-cause), but reaches its own verdict.

## Handoff

Regression → `debugging` for root cause, then Workshop. Benchmark execution →
`testing`. Unmeasured but plausible impact → back to Workshop with "measure
first."
