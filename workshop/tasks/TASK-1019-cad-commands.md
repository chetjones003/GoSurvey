# TASK-1019 — Paper-space CAD commands (issue #1019)

- Type:    feature
- Status:  complete (PR pending)
- Opened:  2026-10-28
- Owner:   chetjones003
- Issue:   https://github.com/chetjones003/GoSurvey/issues/1019

## 1. Authority
- Goal:         paper-space object parity (REQ-039) and draw/inquiry completeness
- Requirements: REQ-456 (accepted, D-2026-10-28-c); amends REQ-039, REQ-443, REQ-105, REQ-104
- Constraints:  REQ-201 named refusals; REQ-101 numeric tolerance; REQ-301 minimal abstraction
- Decision:     D-2026-10-28-c — Option C: all eight commands in model space and paper space

## 2. Scope
In scope: LEADER, SPLINE (paper), POINT, PEDIT, MEASURE, DIVIDE, AREA, LIST; register MLEADER;
PDMODE/PDSIZE; CadPoint entity and paper spline/point stores.

Out of scope: LEADER Tolerance/Copy/Block; PEDIT Width/Ltype gen; restoring pre-Fit vertices;
MASSPROP; XLINE/RAY/DONUT/MLINE.

## 3. Architectural boundary check
- New entity kind `CadPoint` / `EntityKind::Point` appended after Spline (id-sweep append-only).
- LEADER reuses `CadMultileader` / `paperMultileaders`.
- Paper draw via `ActivePaperGeometryTarget`.
- No new layer or dependency.

## 4. Test approach
`tests/Issue1019CommandTests.cpp` through `ProcessCommandLineSubmit` in model and paper.

## 5. Pre-PR product documentation (REQ-453)
- README.md — reviewed, no update (commands are not listed there)
- PRIVACY.md — reviewed, no update
- resources/wiki/ — Drawing-Tools, Known-Limitations, Command-Reference, new command pages
- Public GitHub wiki — Command-Reference, Drawing-Tools, Known-Limitations, Inquiry-Commands, Modify-Tools, Annotation, Paper-Space-and-Layouts, Import-and-Export updated (wiki commit dde790d)

## 6. Verification
- `.\build.bat` Release: PASS
- `Issue1019CommandTests` `[req456][issue1019]`: 18 cases / 139 assertions PASS
- `CommandRegistryHandlerTests` `[registry]`: PASS
- DWG POINT import (`LibreDwgCadTests` issue #613): now expects `CadPoint` (REQ-456); PASS
- `ctest --preset ninja-release`: 190/191 passed; shard-0 SEGFAULT was the stale POINT→Position Marker assertion, re-run after the test fix
- AC 1–8 covered by Issue1019CommandTests (AC 3 MOVE added)
- Remaining: TransformPreview has no POINT ghost during MOVE
- Review follow-up: paper polyline bulge store so PEDIT Fit/Decurve work on sheets; Join, Insert, AREA Add/Subtract, MEASURE pick origin, DIVIDE closed N, LIST paper endpoints, LEADER format/annotation, paste log, const ActivePaperGeometryTarget, renamed DrawInquiry TU
- Second review: drop dead `leaderFormat` and `ListPhase`; const MEASURE path; LIST polyline/spline/leader geometry; AREA Object requires closed; LEADER Arrow/None drawn; MEASURE open polyline nearer end; PEDIT Fit closing bulge; drawable-geometry gate tests for spline-only and point-only drawings
- Third review: fold `CadLeaderArrowFromPath` into `CadMultileaderArrow`; share path/polyline/bulge helpers; split Leader/Area/Measure pick+text handlers; MEASURE paper target as `PaperEntityRef`; `AppendScratchToPaperLayout` is void and copies POINT/SPLINE; paper LEADER Mtext box uses paper inches; DXF/DWG paper POINT/SPLINE round-trip
