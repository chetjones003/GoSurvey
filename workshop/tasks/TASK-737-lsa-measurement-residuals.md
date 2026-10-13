# TASK-737 — Per-measurement least-squares residual listing

- Type:    feature
- Status:  done
- Opened:  2026-10-13
- Owner:   workshop

## 1. Authority
- Goal:         surveyor can investigate an adjustment the way a StarNet listing does
- Requirements: REQ-418 (accepted, D-2026-10-13-b)
- Constraints:  CON from coding standards; no new dependency (REQ-300, ADR-001)
- Acceptance:   the five `[req418]` conditions in `spec/requirements.md`
- Owning subsystem: Domain traverse least squares; UI displays the same rows

## 2. Scope
- In scope:        one listing row per measurement, standardized residual, mark over 3, kind summary, chi-square
- Out of scope:    network command language (next increment); direction sets; leveling; grid factors
- Smallest change: extend `LsaResult` and both existing residual views; do not change the solver's coordinates

## 3. Architectural boundary check
- [x] No — the listing is more fields on the existing result. Chi-square critical values stay in the traverse module.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Confirm the residual-listing feature request before specifying it | 2026-10-13 | User: looks good |

## 5. Assumptions
```
ASSUMPTION-1: a GPS component is standardized by the square root of its own variance
- Because:       the confirmed rule is residual divided by that measurement's standard error
- Risk if wrong: a correlated vector's mark can differ from a fully multivariate standardized residual
- Validate by:   the next increment, if a StarNet listing of the same vector disagrees
```

## 6. Plan
- Approach:     fill `measResiduals` from the residuals the solver already computes
- Files:        TraverseLeastSquares.hpp/.cpp, CadUi_TraverseEditor.cpp, TraverseTests.cpp, spec
- Test approach: published chi-square table; square-loop rows; a 1 ft blunder with a tight distance sigma
- Steps:
  - [x] REQ-418 recorded
  - [x] implement and run `[req418]` tests

## 8. Implementation log
- 2026-10-13 listing rows, kind summary, and chi-square added beside the existing per-course residuals. Adjusted coordinates unchanged.

## 9. Self-verification
- [x] build-project        — GoSurvey.exe and GoSurveyTests.exe linked
- [x] architecture-review  — listing stays in the traverse module; UI only displays it
- [x] code-review          — signed residuals and closure unchanged; new rows derived from them
- [x] dependency-audit     — n/a (no new dependency; chi-square is in-tree)
- [x] testing              — `[lsa]` 14 cases, 165 assertions, all passed

## 10. Verification result
- Verdict: PASS against REQ-418. The closure window was compiled; it was not clicked through in the running app.
