# Verification: TASK-1016 MVIEW implementation

Date: 2026-11-03

## Scope and architecture

REQ-490 is the implementation authority. MVIEW remains owned by Commands; viewport clipping is stored as a copied value on `Viewport`; Qt Renderer owns screen composition; PDF plotting clips vector strokes; DWG/DXF IO carries the native clip-boundary reference. No new dependency, layer, or upward dependency was introduced. The existing rectangular rendering path remains unchanged when no clip boundary is present.

**Architecture: PASS.**

## Evidence reviewed

- Recheck on 2026-11-03: full Windows Release build passed; `[req490],[issue768]` passed, 258 assertions across 26 cases.
- PDF vector stroke clipping: passed; the test reads PDF path segments and confirms the horizontal stroke endpoints match the triangular boundary.
- Qt viewport clipping: the affected editor test passed on the hidden desktop and saved `build/artifacts/Release/req480-paper-viewport-clipping.png`. All six `qt.editor.shard-*` tests passed on 2026-11-03 recheck (430.61 seconds total).
- Build output layout: passed.
- GoSurvey DWG/DXF round-trips: passed for viewport geometry, including bulge values.
- AutoCAD 2025 accoreconsole AUDIT: passed for isolated clipped-viewport DWGs saved as R2000, R2010, and R2018; each reported `Total errors found 0 fixed 0`. This verifies database integrity, not visible clipping behavior. The DXF oracle fixture was generated but has not been opened and resaved by AutoCAD.
- AutoCAD 2025 accoreconsole DXFOUT: opened the R2018 DWG and exported `autocad_roundtrip.dxf`. The VIEWPORT retained status 32800 and clip-boundary reference `340 → 39`; handle 39 is the closed bulged LWPOLYLINE with expected vertices. This verifies AutoCAD's DWG read and DXF write paths for clip data; visible clipping behavior remains unverified.

## Findings

1. **Accepted limitation — PDF text glyph overflow.** Per the user's 2026-11-03 decision, PDF viewport text stays searchable. Labels whose insertion anchor is outside the polygon are omitted; a label anchored inside may extend beyond the polygon edge. The bundled PDFium edit API does not expose arbitrary polygon clipping for text objects. This behavior is now recorded in REQ-490.
2. **External interoperability is partially verified.** AutoCAD 2025 accoreconsole AUDIT opened clipped-viewport DWGs saved as R2000, R2010, and R2018 and reported zero errors and zero repairs. DXFOUT then preserved the viewport clip reference and its bulged closed polyline. A full AutoCAD UI render check remains outstanding; the console does not provide a useful viewport display comparison. Civil 3D is not installed.
3. **Visual comparison is inconclusive.** The GoSurvey screenshot and pixel assertions confirm the clipping mask. A paired Civil 3D capture and the required independent review panel cannot be completed without Civil 3D.

## Verdict

**IMPLEMENTATION COMPLETE FOR THE APPROVED SCOPE.** Command behavior, persistence, Qt clipping, and PDF stroke clipping have focused coverage. AutoCAD 2025 AUDIT accepted R2000, R2010, and R2018 clipped-viewport DWGs without repairs. PDF text follows the user's accepted searchable-text behavior. AutoCAD UI clipping and paired Civil 3D visual review remain unverified because Civil 3D is unavailable; this limitation is recorded in the PR and no product-parity pass is claimed. Per the user's batch-PR instruction, issue #1016 is closed with this limitation disclosed.
