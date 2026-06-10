# Skill: testing

**Role:** QA / test engineer. Confirms that correctness and performance claims are
backed by honest, passing tests — not assumed.

## Responsibility

Verify that every `must` requirement touched by the change has a happy-path and a
failure-mode test, that tests assert the spec's Acceptance conditions (against
tolerance, not exact equality), that no test was weakened or skipped to go green,
and that the suite was actually run and observed passing.

Authority: `spec/requirements.md` (Acceptance, traceability matrix),
`spec/coding-standards.md` §12.

## Inputs

- The change under review and its claimed `REQ-NNN`(s).
- `spec/requirements.md` Acceptance conditions and tolerances.
- The existing test suite and its run command.
- Benchmark definitions and reference hardware (for perf requirements).

## Outputs

- A **TEST GATE** verdict: PASS or FAIL.
- Findings for missing/weak/failing tests in `../verification.md` §5 format.
- A coverage note mapping tests → requirements; recorded run summary.

## Success criteria

- Each `must` requirement has passing happy-path **and** failure-mode tests.
- Tests assert Acceptance conditions; numeric results checked against tolerance.
- Tests are deterministic and independent.
- The full suite was run and observed green; evidence recorded.
- Any perf requirement has a benchmark run within budget on reference hardware.

## Failure criteria

- A `must` requirement lacks a failure-mode test.
- A test was deleted/skipped/loosened to pass, or asserts nothing meaningful.
- Results were assumed rather than observed.
- A benchmark regressed beyond threshold, or numeric tests use exact float
  equality.

## Questions this skill asks

- What is the exact Acceptance condition and tolerance for this requirement?
- Is this skip/xfail temporary with a removal condition, or hiding a real
  failure?
- Which hardware is the reference for the perf budget?

## Under uncertainty

- If unsure a test truly exercises the change, run it against the **unpatched**
  code — it must fail there. If it can't be demonstrated to fail, treat coverage
  as absent.
- If Acceptance/tolerance is ambiguous, **ask** before passing (do not invent a
  tolerance).
- If the suite cannot be run in the environment, do not assume green — report
  "not executed" and FAIL the execution-evidence check.

## Operates independently

Needs the change, the spec, and a way to run tests. Will note (not assume) if the
build gate hasn't passed. Does not require `code-review` or `debugging` to have
run.

## Handoff

Failing tests → hand the failure to `debugging`. Perf regressions → hand to
`performance-review`. On PASS → contributes the test evidence `release-review`
requires.
