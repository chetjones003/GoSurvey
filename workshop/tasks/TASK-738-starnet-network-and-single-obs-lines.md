# TASK-738 — Network observations, StarNet single-observation lines, field codes

- Type:    feature
- Status:  done
- Opened:  2026-10-13
- Owner:   workshop

## 1. Authority
- Goal:         a StarNet-style `.dat` file (network observations, not just a chain) imports and adjusts
- Requirements: REQ-419, REQ-420, REQ-421 (accepted, D-2026-10-13-c/d/e/f)
- Constraints:  CON from coding standards; no new dependency (REQ-300, ADR-001)
- Acceptance:   the `[req419]`/`[req421]` tests in `tests/TraverseTests.cpp`
- Owning subsystem: Domain traverse (`TraverseCommandLang`, `TraverseLeastSquares`, `StarNetImport`)

## 2. Scope
- In scope: `CTRL`/`ANGLE`/`DIST`/`AZIMUTH`/`EDIF`/`ZENITH` network commands; StarNet `A`/`D`/`V`/`B`/`M`/`BM`/`DV`/`L`
  single-observation lines and `C` fixity; field-code (`'code`) and comment/blank-line round-trip on import.
- Out of scope: direction sets, leveling networks, `SS` lines (still reported as unsupported), GPS-vector
  network integration (#857).
- Smallest change: extend the existing command-language parser/writer and the least-squares solver's
  equation builder; `StarNetImport` translates StarNet's own syntax into these, it does not grow its own solver.

## 3. Architectural boundary check
- [x] No — network observations are more equation types in the same solver; the StarNet lines are translated
  to the existing command language at import time (D-2026-10-13-e: StarNet's dotted options are not GoSurvey syntax).

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Proceed toward a StarNet-style network adjustment? | 2026-10-13 | User: yes |

## 5. Plan
- Approach: add network observation types to `TraverseCommandLang`/`TraverseLeastSquares`, then have
  `StarNetImport` emit them (plus field codes/comments) while reading a `.dat` file line by line.
- Files: `TraverseCommandLang.hpp/.cpp`, `TraverseLeastSquares.hpp/.cpp`, `TraverseCalc.hpp`,
  `StarNetImport.hpp/.cpp`, `CadUi_TraverseEditor.cpp`, `TraverseTests.cpp`, `spec/requirements.md`.
- Steps:
  - [x] REQ-419/420/421 recorded and accepted
  - [x] `CTRL`/`ANGLE`/`DIST`/`AZIMUTH`/`EDIF`/`ZENITH` parse, round-trip, and solve as a network
  - [x] StarNet `A`/`D`/`V`/`B`/`M`/`BM`/`DV`/`L` lines + `C` fixity translate into the above
  - [x] field codes (`'code`) and `//` comments/blank lines keep their position through import
  - [x] run `[req419]`/`[req421]`/`[starnet]` tests

## 8. Implementation log
- 2026-10-13: network observation commands, StarNet single-observation line translation, and field
  code/comment round-trip implemented and tested.

## 9. Self-verification
- [x] build-project — GoSurvey.exe and GoSurveyTests.exe linked (release)
- [x] architecture-review — StarNetImport stays a translator into the existing command language/solver
- [x] code-review — not yet run as a separate pass; reviewed inline during implementation
- [x] dependency-audit — n/a (no new dependency)
- [x] testing — `[req419]` 6 cases, `[req421]` covered within `[starnet]`; full `[starnet]` suite 154
  assertions/26 cases pass; full test suite 1560/1563 cases pass (3 pre-existing, unrelated failures:
  FBK re-observed-loop detection, 2 BrepTests cases)

## 10. Verification result
- Verdict: PASS against REQ-419/420/421. Not yet exercised against a real third-party StarNet export
  file containing a true network (only hand-built fixtures so far) — flagged as residual risk for #846/#847.
