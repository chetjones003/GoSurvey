# TASK-009 — Native paper-space geometry (REQ-037, ADR-009)

- Type:    feature
- Status:  implement (Phase 5a: data model + persistence)
- Opened:  2026-06-16
- Owner:   Workshop

## 1. Authority
- Goal:         Paper Space milestone — editable sheet geometry (title blocks / annotations)
- Requirements: REQ-037 (accepted 2026-06-16)
- ADRs:         ADR-009 (PaperLayout owns a paper entity store; route draw/edit/snap by active space)
- Constraints:  no broken functionality; no new dependency; coding standards; .gs reproducibility (CON-07)
- Acceptance (REQ-037): in a paper layout the user can draw lines + place text on the sheet;
                each can be moved/copied/rotated/deleted and snapped to; they do NOT appear in model
                space or other layouts; saved to .gs then reloaded restores them per layout.
- Owning subsystem: Domain (PaperLayout store) / UI / Commands / Renderer / IO.

## 2. Scope (incremental)
- 5a (this slice): data model — PaperLayout owns paper-space lines + text (paper inches);
  GsIo save/load per layout; round-trip test. No UI/command yet.
- 5b: render paper entities in the ImGui overlay (paper inches via w2s); LINE + TEXT create in paper space.
- 5c: MOVE/COPY/ROTATE/DELETE + object snapping on paper entities (snap = paper-only).
- Out of scope: circles/polylines/arcs in paper space (later); snapping to model-in-viewport; DXF persistence.

## 3. Architectural boundary check
- [x] No NEW architectural decision in the Workshop — ADR-009 fixes ownership (PaperLayout) +
      routing (active space) + reuse of existing value types. The CadEntities.hpp extraction below
      is a mechanical relocation of existing structs to break an include cycle, not a new abstraction.

## 4. Questions  (answered by user 2026-06-16)
| # | Question | Answer |
|---|----------|--------|
| Q1 | Floating MSPACE (REQ-036) status today | Partly works → audit + fix gaps (separate task TASK-010) |
| Q2 | Paper entity scope, first version | Lines + text first |
| Q3 | Which edit commands on paper geometry | Full set (move/copy/rotate/delete/snap) minus survey tools |
| Q4 | Snapping target in paper space | Paper geometry only |

## 5. Assumptions
```
ASSUMPTION-1: EntityAttributes + CadAnnotation are dependency-free value types and can be moved
  to a new CadEntities.hpp included by both PaperSpace.hpp and CadCommands.hpp with no behavior change.
- Because:       both are POD/string-only; their free functions stay in CadCommands.hpp.
- Risk if wrong: include-order / redefinition breakage.
- Validate by:   full build green after the move.

ASSUMPTION-2: Paper lines reuse the model 6-float layout (x0,y0,z0,x1,y1,z1) with z=0, in paper inches.
- Because:       lets render/edit/snap reuse the same per-line math the model path uses.
- Risk if wrong: minor; z unused on the sheet.
- Validate by:   round-trip + render slice (5b).
```

## 6. Plan (5a)
- Create `src/commands/CadEntities.hpp` (no deps): move `EntityAttributes` + `CadAnnotation` here.
- `CadCommands.hpp`: remove those two struct defs; `#include "CadEntities.hpp"`.
- `PaperSpace.hpp`: `#include "CadEntities.hpp"`; add to `PaperLayout`:
  `std::vector<float> paperLines;` (x0,y0,z0,x1,y1,z1, paper inches),
  `std::vector<EntityAttributes> paperLineAttrs;`,
  `std::vector<CadAnnotation> paperTexts;` (insX/insY in paper inches),
  `std::vector<EntityAttributes> paperTextAttrs;`.
- `GsIo`: serialize/deserialize the four vectors nested under each layout; malformed → empty (no crash).
- Test: round-trip a layout with ≥1 paper line + ≥1 paper text; assert restored.

## 8. Implementation log
- 2026-06-16 plan written; boundary check = No (covered by ADR-009).
- 2026-06-16 [5a] Extracted EntityAttributes + CadAnnotation into dependency-free CadEntities.hpp;
  CadCommands.hpp + PaperSpace.hpp include it (breaks the include cycle). Build green; 159 tests pass.
- 2026-06-16 [5a] PaperLayout gains paperLines/paperLineAttrs/paperTexts/paperTextAttrs (paper inches).
- 2026-06-16 [5a] GsIo save/load of the four vectors per layout, reusing EntityAttributes/CadAnnotation
  To/FromJson; malformed → empty, attrs resized parallel to geometry. Build green.

## TECH DEBT (5a)
- TD-1: No automated .gs round-trip test for paper geometry yet. The unit-test target (GoSurveyTests)
  does not link GsIo.cpp/CadCommands.cpp, and CadCommands.cpp pulls in <imgui.h>, so wiring GsIo into
  the domain test target would drag the GUI into it — disproportionate and an architectural smell.
  Persistence reuses helpers already exercised by model-annotation .gs persistence.
  Removal condition: add a headless GsIo seam (or split AppCommandState IO from the ImGui-coupled
  command TU) so a .gs round-trip can be tested without GUI deps, then add the round-trip test.
  Until then: verified manually via the app once 5b (create) lands.
