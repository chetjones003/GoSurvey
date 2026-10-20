# TASK-768 — Paper-space multileader store (increment 1 of REQ-113 widening)

- Type: feature
- Status: in progress
- Opened: 2026-10-20
- GitHub issue: #768

## 1. Authority

- Requirement: REQ-113 (accepted; widened by D-2026-10-20-a). Related: REQ-037, REQ-039, REQ-367.
- Acceptance: REQ-113 items 5-8 are delivered across increments; this one only gives the sheet a store for multileaders (the other new types already have a home — see scope).

## 2. Scope

- Audit result: paper TABLE already exists as a `CadAnnotation` of kind Table in `paperTexts`; fills already have `paperFilledRegions`. Spline, image and point have no model-space type on `beta` to mirror (SPLINE is open PR #980; the #803 spec dropped Point), so they are follow-ups.
- In scope: `PaperLayout::paperMultileaders` (+attrs); `PaperEntityRef::Type::Leader`; click pick, window/crossing select, snap (tip, landing), MOVE/COPY/ERASE, Qt sheet display, PDF plot (paths + label).
- ROTATE/SCALE/MIRROR refuse it by name, as in model space (REQ-367).
- Out of scope (later increments): drawing an MLEADER on a sheet, clipboard (model-space clipboard does not carry multileaders either), DWG/DXF read/write (increments 2-4), spline/image/point stores.

## 3. Architecture check

Mirrors the existing per-type paper stores (ADR-009); no new layer or abstraction.

## 4. Tests

`tests/PaperLeaderTests.cpp` — pick, box select, snap, move/copy, erase, refusal of rotate/scale/mirror.

## 5. Assumptions / debt

- Creation path is import (increment 2) until a paper MLEADER command is specified.
