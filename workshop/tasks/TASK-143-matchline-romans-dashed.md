# TASK-143 — Default Standard font romans.shx; import DXF linetype group 6

- Type:    bug
- Status:  done
- Opened:  2026-08-29
- Owner:   workshop

## 1. Authority
- Goal:         GOAL related to CAD drawing fidelity (REQ-044 text styles; DXF entity appearance)
- Requirements: REQ-044 (accepted); DXF LINE appearance (group 6 linetype) as already specified for export
- Constraints:  CON-06 smallest change; no new linetype table
- Acceptance:   Standard style uses romans.shx; empty font draws as that default; imported DASHED lines keep DASHED through block INSERT
- Owning subsystem: Domain/IO (DxfIo EntityBase) + UI overlay (CadUi text draw) + TextStyles defaults

## 2. Scope
- In scope: Default Standard font; overlay empty font → romans.shx; DXF group 6 on EntityBase and LWPOLYLINE
- Out of scope: Full DXF LTYPE table import; changing viewport dashPatScale heuristic
- Smallest change: parse group 6; EnsureStandard font; EffectiveFontFamily in draw helpers

## 3. Architectural boundary check
- [x] No — proceed.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | (none — user specified romans.shx app-wide and dashed matchline) | | |

## 5. Assumptions
ASSUMPTION-1: Changing the empty-font draw fallback from ImGui TTF to romans.shx is the intended new app default (user request). Older `.gs` labels with empty fontFamily will change appearance.
- Because: REQ-044 "" = app default
- Risk if wrong: legacy drawings look different
- Validate by: user visual check

ASSUMPTION-2: Matchline bar is DXF LINE with group 6 DASHED; missing parse is why INSERT looks Continuous.
- Validate by: import test on `_matchline_NORTHING.dxf`

## 7. Completion
- Requirements: REQ-044 default Standard font; DXF group 6 on LINE/LWPOLYLINE
- Tests: TextStyle EnsureStandard/EffectiveFontFamily; CadBlockImport `[blockimport]` 43 assertions
- Build: `build\GoSurvey.exe` (release) and `build\debug\GoSurvey.exe` (F5)
