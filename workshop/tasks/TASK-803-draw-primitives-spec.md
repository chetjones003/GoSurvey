# TASK-803 — Specify Spline, Revision Cloud and Wipeout (GitHub #803)

- Type:    docs
- Status:  done
- Opened:  2026-10-20
- Owner:   Claude

## 1. Authority
- Goal:         drawing-command completeness (REQ-104).
- Requirements: REQ-104 (proposed sketch), new REQ-443, REQ-444, REQ-442 (accepted).
- Decision:     D-2026-10-20-e (accepted).
- Owning subsystem: Commands/Domain/IO/Render/UI.

## 2. Scope
- In scope: write the missing requirements and acceptance criteria for three of the four tools
  in #803, and record the user's choices.
- Out of scope: any code. A drawn POINT entity (the user chose to skip it).
- Smallest change: spec text only.

## 3. Architectural boundary check
- [x] Yes — spline and wipeout are new entity kinds and change the DXF/DWG formats, so they
      need a recorded decision before any code (CLAUDE.md §2, §5). Revision cloud adds no
      entity kind (stored as a bulge polyline, REQ-316).

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| 1 | Delivery shape | 2026-10-20 | Spec first, then one tool per PR |
| 2 | Point entity | 2026-10-20 | Skip it |
| 3 | Spline control | 2026-10-20 | Fit points plus control-vertex mode |
| 4 | Wipeout scope | 2026-10-20 | Polygon, hides in view and PDF |
| 5 | Revision cloud creation | 2026-10-20 | Click path, or convert a closed shape |

## 5. Delivery plan (after the user accepts the proposals)
1. REQ-444 Revision Cloud (no new entity kind; smallest, proves the ribbon wiring).
2. REQ-443 Spline.
3. REQ-442 Wipeout (touches render draw order and PDF plot).
Each is its own branch, PR into `beta`, with its own task file.

## 6. Completion report
- Spec-only change; no build or tests needed. Awaiting the user's acceptance of the three
  proposed requirements.
