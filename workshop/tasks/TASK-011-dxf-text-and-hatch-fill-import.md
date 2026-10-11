# TASK-011 — DXF text annotations + solid-hatch fill import

- Type:    bug + feature
- Status:  implement
- Opened:  2026-06-17
- Owner:   chetj (worktree: worktree-text-annotations-dxf)

## 1. Authority
- Goal:         interoperable DXF (title-block templates round-trip faithfully)
- Requirements: REQ-002 (round-trip fidelity); ADR-010, ADR-011 (decision log 2026-06-17)
- Constraints:  CON-07 (build reproducibility); local-storage invariant (geometry stored local; world = local + worldDocumentOrigin)
- Acceptance:
  - A DXF with `TEXT`/`MTEXT` in model space imports as readable native annotations
    (correct position, height, rotation, content); MTEXT inline control codes are flattened.
  - GoSurvey annotations exported to DXF then re-imported reproduce the same text
    (REQ-002), no longer degrading to line strokes.
  - A `SOLID` `HATCH` (e.g. the logo) imports as a filled region rendered filled, and
    survives `.gs` save/load and a DXF round-trip (re-exported as `HATCH`).
- Owning subsystem: IO (DXF) primarily; UI (overlay render); commands (document/snapshot data model)

## 2. Scope
- In scope:
  - Phase 1: import `TEXT` → `CadAnnotation{Kind::Text}`, `MTEXT` → `CadAnnotation{Kind::Mtext}`.
  - Phase 2: import `SOLID` `HATCH` → new `CadFilledRegion`; render, persist, export.
- Out of scope:
  - Pattern (non-solid) hatch fills — keep importing as boundary outlines.
  - Hatch holes / island detection (fill each loop independently for now).
  - TEXT alignment codes 72/73 beyond left/baseline; multi-column MTEXT.
- Smallest change: Phase 1 reuses the existing annotation model end-to-end; only the
  importer changes. Phase 2 adds one primitive threaded through the existing geometry plumbing.

## 3. Architectural boundary check
- New data-format addition (`CadFilledRegion` in `.gs` + DXF) and new entity type → YES, architectural.
  Escalated as ADR-010 / ADR-011 (decision log, accepted 2026-06-17) before implementation.

## 5. Assumptions
```
ASSUMPTION-1: TEXT/MTEXT in the reference DXF are model space (verified: no group 67).
ASSUMPTION-2: plottedHeightInches = group40 / modelUnitsPerPlottedInch keeps export height identical.
  Risk if wrong: text renders at wrong size; Validate by: round-trip group 40 equality.
```

## 6. Plan
- Phase 1 (DxfIo import):
  - [ ] Add MTEXT control-code → plain-text flattener (handle `\P`,`\~`,`\\`,`\{`,`\}`,`{}`,`\X;`-style code groups).
  - [ ] TEXT branch: build `CadAnnotation` (insX/insY local, plottedHeightInches, rotationRad, text); push to side buffer.
  - [ ] MTEXT branch: build `CadAnnotation{Mtext}` with box from insertion + attachment(71) + width(41); push to side buffer.
  - [ ] Merge imported annotations into `st.cadAnnotations` after parse (like embedded points).
- Phase 2 (filled regions):
  - [ ] Add `CadFilledRegion` to CadEntities.hpp; vectors in DrawingDocument/snapshot/AppCommandState/clipboard.
  - [ ] Thread through SaveDocumentToSnapshot/RestoreDocumentFromSnapshot + undo snapshot.
  - [ ] GsIo save/load + docs/gs-file-format.txt.
  - [ ] DxfIo: import SOLID HATCH boundary loops → CadFilledRegion; export CadFilledRegion → HATCH SOLID.
  - [ ] CadUi overlay: render filled via AddConcavePolyFilled.
- Test approach: unit round-trip (annotation + filled region) where a test target exists; manual import of SURVEYBORDER.dxf.

## 8. Implementation log
- 2026-06-17 open; diagnosis complete (TEXT/MTEXT imported as strokes; SOLID HATCH as outline). ADR-010/011 recorded.
- 2026-06-17 Phase 1: TEXT/MTEXT import as native annotations; MTEXT/%% code flatteners; group 50 fixed to
  DEGREES on export+import (was radians — AutoCAD-incompatible). Phase 2: CadFilledRegion threaded through
  document/snapshot/undo/shift/extents/gsio/dxf; SOLID HATCH import→fill, fill→HATCH export; overlay fill via
  AddConcavePolyFilled. Builds clean.
- 2026-06-17 review tweaks after first run: (a) solid-hatch spike fixed — per-edge scanner now stops at codes
  97/98 so the last edge no longer absorbs the seed point; (b) TEXT vertical placement — store top = baseline +
  height on import, export baseline = top − height (round-trips); (c) MTEXT justification — added mtextAttach
  (group 71), renderer centers/justifies in-box, persisted in .gs, exported with matching insertion point.
  Rebuilt clean.
- 2026-06-17 spike fix correction: the 97/98 stop-condition had only been applied to the arc-edge loop;
  the LINE-edge loop (used by every solid hatch here) still over-read into the seed point, so spikes
  persisted. Applied 97/98 to the line-edge loop too. Rebuilt clean.
- 2026-06-17 fill cleanup (user requested z-order + holes + crisp): reworked fills from the ImGui overlay
  to the GL viewport pass. CadFilledRegion is now multi-loop (outer + holes); import groups all of a
  hatch's boundary paths into one region; export emits one path per loop; .gs stores {verts,loops} (loads
  legacy flat arrays as one loop). FBO/MSAA depth buffer upgraded to DEPTH24_STENCIL8; fills drawn before
  linework via stencil even-odd (handles concave + holes, crisp, plottable z-order). Overlay fill pass
  removed. PDF-plot fills deferred (ADR-007 monochrome-vector — would render solid black). Rebuilt clean.
- 2026-06-17 text scaling: CAD TEXT and plain MTEXT were floored at viewportTextMinPx/viewportMtextMinPx
  (8px) for readability, so zoomed out they stopped shrinking and ballooned over the title-block cells.
  Removed the min-px floor for CAD text/plain MTEXT (cap retained); survey-point-label MTEXT keeps its
  floor. Text now stays model-proportional across zoom. Rebuilt clean.
- 2026-06-17 text styling + fonts (ADR-012): new FontReg registry lazily loads any installed Windows TTF
  into the ImGui 1.92 dynamic atlas (cached), substituting SHX names with the closest TTF. DXF STYLE table
  parsed → TEXT/MTEXT base font + italic; TEXT %%u → underline; MTEXT \L/\f/%%u/{} converted to rich
  [[b]]/[[i]]/[[u]]/[[font:…]] tags (was stripped). TEXT renders with resolved font + underline + real or
  faux bold/italic; MTEXT rich renderer resolves per-run fonts (added [[font:…]] tag). New CadAnnotation
  fields fontFamily/bold/italic/underline persisted in .gs; underline round-trips via %%u. MTEXT→DXF
  styling export deferred (still flattens). Rebuilt clean.
- 2026-06-17 SHX stroke fonts (ADR-012a): new Shx module parses the real AutoCAD .shx files (unifont
  format: [u16 code][u16 len][name\0 + shape bytecode]) and interprets the shape bytecode (vectors,
  displacements, push/pop/scale/subshape, octant/fractional/bulge arcs, vertical-text skip) into line
  strokes. Files located by scanning installed Autodesk font folders. TEXT/MTEXT whose font ends in .shx
  render as strokes (exact AutoCAD match) at the resolved cap-height; else TTF. Verified the parser/glyph
  table against romans.shx with a throwaway probe (codes = correct ASCII, table ends at EOF). Visual
  fidelity of arcs to confirm in-app. Built clean.
