# TASK-734 — Import Plant 3D / AcDs ASM `3DSOLID` bodies (REQ-320 increment 2)

- Type:    feature
- Status:  plan
- Opened:  2026-10-07
- Owner:   workshop

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         GOAL-NN (DWG interoperability / 3D reference geometry)
- Requirements: REQ-320 increment 2 (D-2026-10-07-a), REQ-300, REQ-201, ADR-051, ADR-026 (boundary)
- Constraints:  CON-NN as applicable; no commercial ACIS kernel
- Acceptance:
  - Opening `samples/example-piping-system.dwg` imports at least one `brep::Solid`, ZOOM EXTENTS
    frames solid geometry, log not solely `3DSOLID(empty)` × N for that file.
  - Supported ASM/SAB bodies use same analytic import rules as SAT; unsupported bodies refused by name.
  - Existing SAT / `.sat` tests stay green.
  - No third-party ACIS/geometry-kernel dependency.
- Owning subsystem: IO (`LibreDwgCad.cpp`, vendored LibreDWG), Domain (`AcisSatParser`, `brep`)

## 2. Scope
- In scope:
  - AcDs `_data_` segment decode and handle→blob association (ASM + ACIS binary headers).
  - SAB/ASM → SAT conversion feeding `AcisSatParser`.
  - `ImportAcisSolid` path when `acis_empty` but post-decode `acis_data` is populated.
  - Regression test using `samples/example-piping-system.dwg`.
- Out of scope:
  - `ACPP*` custom-object parametric decode (ADR-026).
  - Mesh fallback for unparseable solids.
  - Full sphere/torus / partial revolve / spline surfaces (#300, ADR-051 b-1).
- Smallest change: fix LibreDWG attach for `ASM BinaryFile`, convert to SAT, reuse parser.

## 3. Architectural boundary check
- [x] Yes → recorded as D-2026-10-07-a + ADR-051 addendum (2026-10-07) before implementation.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | V1 outcome: B-rep vs mesh? | 2026-10-07 | B-rep where parseable; named refuse otherwise |
| Q2 | Fixture name / location? | 2026-10-07 | `samples/example-piping-system.dwg` |

## 5. Assumptions
```
ASSUMPTION-1: Most visible geometry in the fixture is reachable via AcDs ASM on 3DSOLID, not only ACPP proxies.
- Because: 89 has_ds_data solids; 37 ASM BinaryFile markers in file; 0 model-space ACPP entities decoded.
- Risk if wrong: import still empty until ACPP reverse-engineering track opens.
- Validate by: Phase A spike attaches ≥1 blob and produces ≥1 solid on fixture import.
```

## 6. Plan
- Approach:
  - **Phase A (LibreDWG):** implement `_data_` in `acds.spec`; extend DECODER attach for `ASM BinaryFile`;
    map blobs via datidx/search to `acis_sab_hdl` targets; update `VENDORED.md`.
  - **Phase B (convert):** extend `dwg_convert_SAB_to_SAT1` ASM header path; unit-test one extracted blob.
  - **Phase C (GoSurvey):** wire `ImportAcisSolid`; add `LibreDwgCadTests` case for fixture; verify extents.
- Files/functions to touch:
  - `third_party/libredwg/src/acds.spec`, possibly `out_dxf.c` (`dwg_convert_SAB_to_SAT1`)
  - `src/io/LibreDwgCad.cpp` — `ImportAcisSolid`
  - `tests/LibreDwgCadTests.cpp`
  - `samples/example-piping-system.dwg` (fixture, committed)
- Test approach:
  - Happy: fixture import → `st.cadSolids.size() >= 1`, extents non-degenerate.
  - Failure: hand-authored corrupt SAB still refused by name, no crash.
- Steps:
  - [ ] Verification APPROVE on this plan (post-SPEC merge)
  - [ ] Phase A LibreDWG vendor patch + VENDORED.md entry
  - [ ] Phase B ASM→SAT conversion
  - [ ] Phase C Import wiring + fixture test
  - [ ] Full ctest `[dwg][libredwg]` + `[acis]`

## 7. Workflow-specific notes
- Feature: SPEC decision landed 2026-10-07; implementation waits Verification APPROVE on branch PR.

## 8. Implementation log
- 2026-10-07 — Task opened; fixture added; D-2026-10-07-a recorded; user confirmed feature request.
- 2026-10-07 — GitHub issue #786 opened.

## 9. Self-verification
- [ ] build-project
- [ ] architecture-review
- [ ] code-review
- [ ] dependency-audit
- [ ] performance-review (n/a unless import regresses)
- [ ] testing

## 10. Verification result
- (pending)
