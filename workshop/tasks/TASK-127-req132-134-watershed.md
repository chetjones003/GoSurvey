# TASK-127 — Watershed, water-drop, and catchment (REQ-132…134)

- Type:    feature
- Status:  implement
- Opened:  2026-08-27
- Owner:   workshop

## 1. Authority
- Goal:         M-Surfaces-119
- Requirements: REQ-132, REQ-133, REQ-134 (accepted D-2026-08-27-a)
- Constraints:  ADR-039 (j); ADR-028 (b) cache geometry; CON-06; no Watersheds style tab; not stored in `.gs`
- Acceptance:
  - REQ-132: synthetic single-basin yields one basin draining to the designed target; two-basin/ridge yields two basins that do not cross the ridge; an internal depression is classified as such, not silently merged into a neighbour; a null TIN is refused with a specific message.
  - REQ-133: on a constant-grade plane the path is a straight downhill line to the designed boundary; a start in a designed pit terminates at that pit; a start outside the TIN reports outside and draws nothing.
  - REQ-134: an outlet at a designed basin pour-point reports that basin's area within REQ-101 of the synthetic fixture; an outlet on a ridge that drains both ways reports the union of contributing triangles, or a stated split rule documented here — not a silent half; a null TIN / miss is a named refusal.
- Owning subsystem: util (watershed), Commands, Renderer (borrowed cache lines), UI (Surface Manager inspect)

## 2. Scope
- In scope: `util/watershed`; `WATERSHED` / `WATERDROP` / `CATCHMENT`; live-only cache outlines; EXTRACT bake of last drop/catchment; Surface Manager basin list.
- Out of scope: Watersheds style tab; persisting polygons in `.gs`; grid/corridor.

## 3. Architectural boundary check
- [x] No — ADR-039 (j) already names `util/watershed`, cache display, EXTRACT bake.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Ridge split when the outlet sits on a shared edge | 2026-08-27 | Union of both covering triangles' reverse-flow sets (documented below). |

## 5. Assumptions
```
ASSUMPTION-1: Per-triangle successor is the neighbour across the first edge hit by a ray from the
centroid in TriangleDownhillDirection (same plane fall as slope arrows). Flat (grade ≤ 0.1%) and
degenerate triangles are DrainKind::Flat. A boundary exit is DrainKind::Boundary. A successor cycle
(including a 2-cycle at a pit) is DrainKind::Depression — never reassigned to a neighbour's Boundary.
- Because: REQ-132 defers algorithm to this task.
- Risk if wrong: Civil 3D D8-on-raster users may expect vertex-based flow.
- Validate by: Catch2 fixtures listed in §6.

ASSUMPTION-2: CATCHMENT on a shared edge (two covering triangles) is the UNION of both reverse-flow
sets. A pick strictly inside one triangle uses only that triangle's upstream set.
- Because: REQ-134 allows a stated split rule instead of a silent half.
- Risk if wrong: a pick that numerically sits on a ridge reports both basins.
- Validate by: ridge fixture Catch2.

ASSUMPTION-3: WATERDROP / CATCHMENT typed coordinates are storage XY (same as SURFELEV), TIN-local.
```

## 6. Plan
- Approach: GL-free `ComputeWatershed` / `ComputeWaterDrop` / `ComputeCatchment`; commands fill a live `surfaceWatershedCache`; display assemble borrows line buffers; EXTRACT copies last path/ring to an unlinked polyline.
- Files: watershed.hpp/cpp, CMakeLists, CadCommands.*, CadCommands.hpp, CadUi_Surfaces.cpp, PdfPlot.cpp, WatershedTests.cpp, req132 transcript.
- Test: Catch2 fixtures (single basin, ridge, pit, plane drop, outside, null); headless not-built / outside / EXTRACT.

## 8. Implementation log
- 2026-08-27 opened after Phase 2 commit ae56b23. Verification APPROVE: ADR-039 (j) already records the module, cache-only display, and EXTRACT bake.
- 2026-08-27 implemented util/watershed (centroid downhill successor, 2-cycle/n-cycle pits, ridge union via multi-basin seeds). WATERSHED / WATERDROP / CATCHMENT + EXTRACT. Catch2 REQ-132…134 green. Headless req132-watershed green.
