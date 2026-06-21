# TASK-015 — Hatch selection/editing + HATCH command

## Authority
- REQ-042 (hatch fills selectable/editable) — accepted (ADR-016)
- REQ-043 (HATCH command + boundary trace + patterns + dynamic ribbon) — accepted
  (ADR-017 boundary trace, ADR-018 pattern storage+render, ADR-019 dynamic ribbon)

## Architectural-boundary check
All four ADRs were recorded as spec decisions (project.md, 2026-06-20) BEFORE
implementation. Workshop makes no new architectural decision.

## Plan (incremental, re-verified per phase)
- **Phase 1 (REQ-042) — DONE.** `SelectedEntity::Type::FilledRegion`; point-in-polygon
  pick (outer minus holes); hover; window/crossing box-select; selection-highlight
  outline; DELETE / MOVE / COPY translate all loop verts; clipboard copy/paste; undo
  + `.gs` via the existing ADR-011 snapshot thread. Pure geometry in
  `commands/HatchGeom.hpp` (tested, `HatchGeomTests`).
- **Phase 2 (REQ-043)** — HATCH command: internal-point prompt, planar boundary trace,
  live preview, SOLID fill, dynamic ribbon (color/transparency/layer live).
- **Phase 3 (REQ-043/ADR-018)** — pattern fields on `CadFilledRegion`, clipped line-
  pattern render path driven by angle/scale, thumbnails.

## Completion report — Phase 2 — 2026-06-20
- Requirements satisfied:  REQ-043 (Acceptance met: yes for SOLID; line patterns are
                           Phase 3 — angle/scale stored but not yet rendered)
- Summary:                 HATCH command (HATCH/BHATCH/H + Draw-ribbon button): prompts
                           for an internal point, traces the smallest enclosing loop from
                           model geometry, previews the candidate fill live, and on click
                           creates a SOLID CadFilledRegion that is immediately selectable/
                           movable/deletable (Phase 1). No closed boundary → reports it and
                           places nothing (REQ-201). Dynamic contextual "Hatch" ribbon tab
                           (shown only while active): placeholder pattern thumbnails + live
                           color / transparency / layer; angle + scale stored for Phase 3.
- New code:                commands/HatchBoundary.hpp (pure planar trace);
                           StartHatchCommand / CadHatchTraceAt / CadHatchCommitLoop;
                           CadUi click+preview+ribbon; RibbonIconKind::Hatch.
- Tests:                   HatchGeomTests boundary cases (closed rect → loop; gap → none;
                           outside → none; nested → smallest) — green; full suite 41 cases
                           / 267 assertions green.
- Verification verdict:    PASS (build clean; no regressions)
- Assumptions:             ASSUMPTION-2 — boundary geometry meets at shared endpoints
                           (within a snap tol); crossing-without-shared-vertex may yield
                           "no boundary" (the accepted ADR-017 first-version failure mode).
- Architectural decisions: none made by Workshop (ADR-017/018/019 recorded in spec first)
- Dependencies:            none added
- Technical debt noted:    line-pattern rendering + island/hole detection for traced
                           regions deferred to Phase 3 (angle/scale are stored, inert now).
- Build:                   clean (GoSurvey + GoSurveyTests)

## Addendum — .pat pattern library — 2026-06-20 (ADR-018 addendum)
- Summary:                 The pattern source is now the bundled AutoCAD `acadiso.pat` (83 patterns).
                           `commands/HatchPat.hpp` parses the file (pure, tested); the generator
                           builds the clipped dashed line family from a parsed definition (origin/
                           spacing/stagger/dashes honoured, × scale, + angle). The HATCH ribbon
                           lists every parsed pattern in a combo with a live preview swatch. The
                           file is vendored into `resources/hatches/` and copied beside the exe by
                           CMake (REQ-200). Renderer/ribbon load it once via a cached HatchLibrary().
- Tests:                   HatchGeomTests [patparse] (names, comma-in-description, family lines,
                           dashes) + [pattern] updated to drive from a Def — full suite 46 cases
                           / 344 assertions green.
- Technical debt:          DXF export still writes patterned hatches as a valid SOLID HATCH (the
                           pattern name is not yet emitted as a DXF pattern definition); .gs is
                           lossless. Pattern preview is still a translucent solid.

## Addendum — edit selected hatches via the ribbon — 2026-06-20
- The contextual Hatch ribbon now also appears when filled-region hatch(es) are selected (not just
  during HATCH creation). In that mode its controls edit the selected object(s)' pattern / color /
  transparency / layer / angle / scale live; one undo snapshot is pushed per edit interaction (on
  widget grab / discrete combo change). Create mode still writes the AppCommandState defaults. Unified
  via apply lambdas (`setPattern/setColor/setTrans/setLayer/setAngle/setScale`). Full suite green.

## Completion report — Phase 3 — 2026-06-20
- Requirements satisfied:  REQ-043 (Acceptance met: yes — incl. line patterns driven by
                           angle/scale; created hatch honors pattern/angle/scale/color/
                           transparency/layer and survives a .gs round-trip)
- Summary:                 CadFilledRegion gained patternName/angle/scale (ADR-018; empty =
                           SOLID, so legacy/imported fills read back solid). The ribbon's
                           pattern + angle + scale are stored on the created hatch. A pure,
                           tested generator (commands/HatchPattern.hpp) builds the clipped
                           line family (ANSI31 single 45°, ANSI37 cross, NET grid), even-odd
                           clipped so holes carve gaps; spacing scales with the region size ×
                           scale. Patterns render as lines in the ImGui overlay in the region's
                           resolved colour; the GL solid-fill pass skips non-solid regions.
                           Pattern fields persist in .gs (omitted for solids → legacy files
                           unchanged).
- Tests:                   HatchGeomTests [pattern] (solid → none; family clipped inside;
                           hole carves gaps; larger scale → fewer lines) — green; full suite
                           45 cases / 572 assertions green.
- Verification verdict:    PASS (build clean; no regressions)
- Assumptions:             ASSUMPTION-3 — pattern line spacing is region-relative (× scale),
                           not absolute drawing units, so it reads well at any scale without
                           the renderer needing the plot scale. Revisit if absolute AutoCAD-
                           compatible spacing is required.
- Architectural decisions: none made by Workshop (ADR-018 recorded in spec first)
- Dependencies:            none added
- Technical debt noted:    (1) DXF export writes patterned hatches as a valid SOLID HATCH —
                           a pattern→DXF→GoSurvey round-trip flattens to solid (proper
                           non-solid HATCH needs pattern-definition lines + import support;
                           .gs round-trip is lossless). (2) Pattern preview is a translucent
                           solid (not the live pattern). Both follow-up items.
- Build:                   clean (GoSurvey + GoSurveyTests)

## Completion report — Phase 1 — 2026-06-20
- Requirements satisfied:  REQ-042 (Acceptance met: yes)
- Summary:                 Filled regions (solid hatches) are now a selectable entity —
                           click-inside pick, hover, box-select, highlight, delete, move,
                           copy, clipboard copy/paste; undo + .gs via existing snapshot.
- Tests:                   `HatchGeomTests` (ContainsPoint inside/outside/in-hole;
                           OuterAreaAbs winding+hole; smallest-enclosing pick priority;
                           OuterBounds; Translate) — 5 cases / 20 assertions green;
                           full suite 37 cases / 257 assertions green.
- Verification verdict:    PASS (build clean; no regressions)
- Assumptions:             ASSUMPTION-1 — fills sit under linework, so the fill pick is
                           lowest priority (runs only after annotation + geometry picks
                           miss). Validated by the click/hover ordering in CadUi.
- Architectural decisions: none made by Workshop (ADR-016 recorded in spec first)
- Dependencies:            none added
- Technical debt noted:    rotate/scale/mirror/grips for fills intentionally deferred
                           (named in REQ-042); a later REQ.
- Build:                   clean (GoSurvey + GoSurveyTests)
- Docs updated:            spec/requirements.md (REQ-042/043 + traceability),
                           spec/project.md (ADR-016..019)
