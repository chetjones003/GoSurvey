# TASK-142 — Developer Shell (Debug Test Engine + chrome tuner)

- Type:    feature
- Status:  self-verify
- Opened:  2026-08-29
- Owner:   Workshop

## 1. Authority
- Goal:         Developer iteration on ImGui chrome + GUI drive without shipping it
- Requirements: REQ-161 (accepted, D-2026-08-29-f)
- Constraints:  REQ-200, REQ-203, REQ-300, REQ-100, architecture §11, ADR-031, ADR-033, ADR-040
- Acceptance:   restated in REQ-161
- Owning subsystem: Application + UI (`src/devshell/`); Build

## 2. Scope
- In scope: CMake gate; split ImGui link; Test Engine; chrome tuner; activity log; `--devshell-run`; Release dumpbin ctest
- Out of scope: screenshot goldens; enabling Test Engine on headless; Qt/native UI
- Smallest change: Debug GoSurvey only; domain TUs unchanged

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change /
  global state / public-API or data-format change / algorithm the spec didn't
  specify?
    - [x] Yes — recorded as D-2026-08-29-f / ADR-040 (Test Engine dep, imgui link split, anti-requirement carve-out). Not a Workshop invention.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Drive model | 2026-08-29 | Full GUI driver (Test Engine) |
| Q2 | License | 2026-08-29 | In-tier; official Test Engine |
| Q3 | Leak prevention | 2026-08-29 | Compile-out; Release forced off |

## 5. Assumptions
```
ASSUMPTION-1: Ninja single-config CMAKE_BUILD_TYPE is the gate (existing presets).
- Because:       Multi-config generators are not how this repo builds.
- Risk if wrong: RelWithDebInfo might need an explicit FORCE OFF — add if a preset appears.
- Validate by:   ninja-release vs ninja-debug

ASSUMPTION-2: --devshell-run needs a GL window; CI Release dumpbin is the leak proof.
- Because:       Test Engine hooks the live ImGui context.
- Risk if wrong: Debug CI without a GPU cannot run the script — not a Release leak.
- Validate by:   local Debug run
```

## 6. Plan
- Approach: ADR-040 (a–f)
- Files: CMakeLists.txt, src/devshell/*, CadUiChrome.hpp, CadUi.cpp, main.cpp, tests/cmake/Req161ReleaseNoDevShell.cmake
- Test: dumpbin ctest (Release); Debug script req161-smoke
- Steps: cmake → chrome accessors → shell UI → Test Engine → CLI → dumpbin test

## 8. Implementation log
- 2026-08-29 opened; spec accepted; implementing
- 2026-08-29 Debug GoSurvey links; Release ctest `req161-release-no-devshell` passed

## 9. Self-verification
- [x] build-project        — Debug GoSurvey + Release GoSurvey/headless/tests
- [x] architecture-review  — ADR-040
- [x] code-review          — compile-out, no domain TE
- [x] dependency-audit     — imgui_test_engine v1.92.9 FetchContent, REQ-300 logged
- [x] performance-review   — log is discrete; draw-pass opt-in
- [x] testing              — req161-release-no-devshell green

## 11. Completion report
COMPLETION REPORT — TASK-142 — 2026-08-29
- Requirements satisfied: REQ-161 (Release leak test yes; Debug shell/test engine yes; --devshell-run needs GUI session)
- Summary: Debug-only Developer Shell (chrome tuner, activity log, Dear ImGui Test Engine). Release compile-excludes it.
- Tests: req161-release-no-devshell
- Verification verdict: PASS for leak gate; GUI script is Debug-manual
- Assumptions: ASSUMPTION-1/2
- Architectural decisions: none by Workshop (D-2026-08-29-f / ADR-040)
- Dependencies: imgui_test_engine v1.92.9 (FetchContent, Debug GoSurvey only)
- Technical debt: req161-smoke ItemClick of ##RibbonLine may need a window path once you run it
- Build: ninja-debug GoSurvey; ninja-release GoSurvey + tests
- Docs: spec/ REQ-161, ADR-040, decision log

