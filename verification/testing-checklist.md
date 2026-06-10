# Testing Checklist

> Tests are the primary evidence the Verification Layer uses to confirm
> correctness and performance. This gate confirms the evidence exists, is
> honest, and actually passed. Missing tests for a `must` requirement is a
> **blocking** finding.

Authority: `spec/requirements.md` (traceability matrix, Acceptance conditions),
`spec/coding-standards.md` §12.

---

## Pass/fail rule

> **PASS** only if every `must` requirement touched by the change has a passing
> happy-path **and** failure-mode test, results were actually observed (not
> assumed), and no test was weakened to make it pass.

---

## 1. Coverage of requirements

- [ ] Every `must` `REQ-NNN` implemented or modified by the change has at least
      one **happy-path** test.
- [ ] Every `must` requirement also has a **failure-mode** test asserting the
      *specified* behavior (e.g. "malformed record → logged error, record
      absent"), not merely "does not crash."
- [ ] Each test maps to its requirement in the `spec/requirements.md`
      traceability matrix (the matrix is updated).
- [ ] No test exists without a requirement behind it (untethered tests are
      flagged — either add the requirement or remove the test).

## 2. Test quality

- [ ] Tests assert the requirement's **Acceptance** condition, not an
      implementation detail.
- [ ] Numeric/domain results assert against the spec **tolerance**, never exact
      float equality (e.g. REQ-101 coordinate tolerance).
- [ ] Tests are deterministic: no reliance on time, ordering, network, or shared
      mutable global state.
- [ ] Tests are independent: each can run alone and in any order.
- [ ] Edge cases covered: empty, boundary, malformed, maximum size.

## 3. Honesty checks *(anti-gaming)*

- [ ] No test was deleted, skipped, or loosened to turn red green. Any new
      `skip`/`xfail`/`t.Skip`/`#[ignore]` carries a written removal condition and
      a tracking task.
- [ ] Assertions are meaningful (not `assert(true)` or a tolerance widened to
      hide a regression).
- [ ] A new test, when run against the *unpatched* code, would have **failed**
      (the test actually exercises the fix).

## 4. Execution evidence

- [ ] The full suite was **run** and observed green — results recorded, not
      presumed. Paste/attach the run summary.
- [ ] CI is green on the change (if CI exists).
- [ ] Flaky tests are identified and either fixed or quarantined with a tracking
      task — never ignored silently.

Per-language run command (fill in actual invocation):

| Language | Run tests |
|----------|-----------|
| C++ | `ctest --test-dir build --output-on-failure` |
| Rust | `cargo test --locked` |
| Zig | `zig build test` |
| Go | `go test ./...` |

## 5. Performance / benchmark evidence *(when a perf requirement is in play)*

- [ ] The relevant benchmark was run on the **reference hardware** named in the
      spec.
- [ ] Result is within budget (frame time / startup / import time / tolerance).
- [ ] Before/after numbers recorded for any change claiming a perf improvement.
- [ ] A regression beyond the stated threshold is reported as a blocking
      performance finding (`review-checklist.md` Domain 4).

## 6. Manual verification *(when automated coverage is impractical)*

- [ ] For UI/interaction behavior not unit-testable, a documented manual test
      script was followed and its outcome recorded.
- [ ] Steps are reproducible by another engineer from the written script alone.

---

## Verdict

```
TEST GATE — <change id> — <date>
- Requirement coverage:  pass / FAIL (missing: REQ-…)
- Failure-mode tests:    pass / FAIL
- Honesty checks:        pass / FAIL
- Suite executed green:  yes / NO
- Perf benchmark:        within budget / REGRESSION / n-a
- Outcome:               PASS | FAIL
```

A FAIL returns to Workshop. Missing failure-mode coverage for a `must`
requirement, or a weakened test, is always blocking — it is not advisory.
