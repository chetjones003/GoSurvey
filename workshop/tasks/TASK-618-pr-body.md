## Summary
- Accept **REQ-369** and **D-2026-10-02-b**: dynamic blocks move off roadmap Someday; delivery is phased in TASK-618.
- **Increment 1 (this PR):** DWG open/save treats AutoCAD dynamic-block anonymous `*U…` blocks as real block definitions and INSERT references (evaluated geometry), instead of exploding them into loose model-space lines.
- Hide imported anonymous defs from the block library browser; persist flags on `.gs`.

## Test plan
- [x] `./dev/build`
- [x] `GoSurveySnapTests.exe` with tag `issue618` (2 cases, 24 assertions)
- [ ] Manual: open a Civil 3D/AutoCAD drawing with dynamic inserts and confirm blocks stay grouped

## Acceptance criteria (REQ-369 increment 1)
- [x] `LibreDwgCadTests` issue #618 cases pass
- [ ] Increments 2–5 remain open — **issue #618 stays open**

## Notes
Full dynamic-block parameters, evaluation graph export, grips, and AutoCAD round trip are deferred to later increments per REQ-369.
