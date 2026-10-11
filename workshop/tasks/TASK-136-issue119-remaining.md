# TASK-136 — Remaining #119 volume, table, TIN-edit, and catchment items

- Type:    feature
- Status:  done
- Opened:  2026-08-28
- Owner:   workshop

## 1. Authority
- Goal:         issue #119 remainder (D-2026-08-28-h)
- Requirements: REQ-146, REQ-147, REQ-148, REQ-149, REQ-150, REQ-151, REQ-152 (accepted)
- Constraints:  CON-07 (build artifacts stay in `build/`); no ACAD_TABLE DXF object
- Acceptance:   restated in spec/requirements.md for each REQ
- Owning subsystem: util (volumes, TIN edge delete, catchment mean), Commands (VOL*, TIN edits, arc breaklines), Domain (Kind::Table), UI (dashboard + table draw), IO (.gs / DXF exploded TEXT)

## 2. Scope
- In scope: cut/fill areas; mixed-sign cell split; TABLE annotation + VOLREPORT TABLE; VOLCSV + dashboard rows; move point / delete TIN line; arc breaklines; catchment mean Z
- Out of scope: alignments, Create Profile, Object Viewer, ACAD_TABLE DXF entity
- Smallest change: fields and commands on existing surfaces/annotations

## 3. Architectural boundary check
- [x] No — TABLE is a CadAnnotation kind per D-2026-08-28-h, not a new entity array

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Keep CSV/tables/TIN edits/arcs/mean in #119? | 2026-08-28 | Yes (user) |

## 5. Assumptions
ASSUMPTION-1: DXF writes exploded TEXT for TABLE cells (decision D-2026-08-28-h).

## 6. Plan
- Approach: finish wiring already-started volume/table/edit paths; Catch2 + headless
- Files: surfacevolume, cadtable, CadCommands, CadUi, PdfPlot, DxfIo, tests
- Test approach: happy + refusal (VOLREPORT TABLE empty, SURFDELLINE miss, DESIGNATEBOUNDARY arc, catchment outside)

## 8. Implementation log
- 2026-08-28 continued remaining wiring: TABLE draw/export, dashboard rows/areas, typed TIN edits, arc designate, tests.

## 9. Self-verification
- [x] build-project        — PASS (`build.bat`)
- [x] architecture-review  — PASS (TABLE is CadAnnotation kind per D-2026-08-28-h)
- [x] code-review          — PASS
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a
- [x] testing              — PASS Catch2 `[req146]…[req152]`; `req146-vol-table-csv-tin-edits`; `req073-surface-volumes`

## 10. Verification result
PASS

COMPLETION REPORT — TASK-136 — 2026-08-28
- Requirements satisfied: REQ-146…152 (Acceptance met: yes)
- Summary: Cut/fill areas and mixed-sign cell split; drawing TABLE (VOLREPORT TABLE / VOLTABLE); VOLCSV and dashboard rows; move point / delete TIN line; arc breaklines; catchment mean Z.
- Tests: SurfaceVolumeTests [req146][req147]; CadTableTests [req148]; Issue119 [req150]; WatershedTests [req152]; req146-vol-table-csv-tin-edits; req073-surface-volumes
- Verification verdict: PASS
- Assumptions: ASSUMPTION-1 (DXF exploded TEXT)
- Architectural decisions: none made by Workshop
- Dependencies: none
- Technical debt noted: none
- Build: `build.bat` green
- Docs updated: spec/requirements.md traceability
