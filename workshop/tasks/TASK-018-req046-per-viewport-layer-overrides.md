# TASK-018 — REQ-046: per-viewport layer overrides (VP Freeze + VP Color; VPFREEZE/VPTHAW)

- Type:    feature
- Status:  done — user verified in app 2026-08-18
- Opened:  2026-07-13
- Owner:   chetjones003

## 1. Authority
- Goal:         Paper Space milestone (M-PaperSpace) — AutoCAD-parity layer control
- Requirements: REQ-046 (accepted 2026-07-13); revises REQ-028 UI; extends REQ-031 persistence
- Constraints:  CLAUDE.md rules 1–6; no new dependency; ADR-021; amends ADR-007 (per-viewport plot color)
- Acceptance (verbatim, REQ-046): see spec/requirements.md REQ-046 — panel removed; VP Freeze/VP Color
  columns gated on a current viewport; freeze hides a layer in the current vp only; VP Color recolors a
  layer in the current vp only (screen + plot); VPFREEZE/VPTHAW pick-freeze/thaw; `.gs` round-trips frozen
  + color; plot shows frozen absent + override colored; global/model rendering unchanged.
- Owning subsystem: UI / Commands / Domain / Renderer / IO (per REQ-046 owner-layer).

## 2. Scope
- In scope:     Viewport color-override data + helpers; VPFREEZE/VPTHAW commands; "current viewport"
                helper; Layer Manager VP Freeze + VP Color columns; remove the old panel; on-screen +
                PDF plot override color; `.gs` persistence; tests + docs.
- Out of scope: general true-color viewport rendering (viewport linework stays kVpModelCol except for
                overridden layers — ASSUMPTION-1); DXF persistence of overrides; snap/pick honoring frozen
                (pre-existing TASK-017 debt F2).
- Smallest change: parallel string arrays on Viewport (mirrors frozenLayers); two new Kind states; two
                Layer-Manager columns; color resolved at the existing render/plot emit sites.

## 3. Architectural boundary check
- [x] Yes → escalated and RECORDED before coding (SPEC GAP resolved): new REQ-046 + ADR-021 + ADR-007
      amendment + decision-log entries in spec/project.md (2026-07-13). Data-format addition to Viewport
      and per-viewport plot color were the architectural pieces; both are now recorded decisions. No new
      abstraction/global/dependency (reuses the frozenLayers pattern + existing color resolver).

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Target viewport for VP columns / VPFREEZE? | 2026-07-13 | "Whichever is current" — floating else single-selected else none. |
| Q2 | VP Color scope — screen only or plot too? | 2026-07-13 | On screen AND in plots (amends ADR-007). |
| Q3 | VPFREEZE behavior / inverse? | 2026-07-13 | Freeze current vp only + add VPTHAW. |

## 5. Assumptions
```
ASSUMPTION-1: VP Color colors only the overridden layers; other viewport linework keeps kVpModelCol.
- Because:       the on-screen viewport overlay draws model linework in a fixed color (the GL true-color
                 pass is deferred/reverted debt), so general true-color rendering is a separate effort.
- Risk if wrong: user may expect the whole viewport to show true colors; if so that is a follow-up REQ.
- Validate by:   user manual verification (override shows in color; rest stays neutral).
```

## 6. Plan  → 8. Implementation log (append-as-built)
- 2026-07-13 Spec recorded: REQ-046 (+ REQ-028 revision, traceability), ADR-021 + ADR-007 amendment,
  decision log — all in spec/. THEN implemented:
- PaperSpace.hpp — Viewport gains vpColorLayers/vpColorValues; helpers FreezeLayerInViewport,
  ThawLayerInViewport, SetViewportLayerColor, ClearViewportLayerColor, ViewportLayerColorOverride.
- CadCommands.hpp — Kind::VpFreeze / Kind::VpThaw + KindName; decls for CurrentViewport /
  StartVpFreezeCommand / StartVpThawCommand.
- CadCommands.cpp — registry {"vpfreeze","vpf"} {"vpthaw","vpt"}; dispatch; CurrentViewport (floating
  else single-selected else nullptr); StartVpFreezeThaw (guards: needs a current viewport); Esc exit in
  CancelActiveCommand; Enter exit in ProcessCommandLineSubmit.
- CadUi.cpp — removed the bottom "Frozen Layers" panel; Layer Manager: +VP Freeze +VP Color columns
  (13-col table) gated on CurrentViewport; viewport render resolves a per-layer override color
  (vpBaseCol) for lines/polylines/circles/arcs/survey points; VPFREEZE/VPTHAW pick routed in the
  floating-viewport click path (hover enabled during the command; picked entity's layer frozen/thawed
  in the floating vp).
- PdfPlot.cpp — segments grouped by stroke color (std::map, deterministic) so overridden layers print
  in their color; border + non-overridden stay black.
- GsIo.cpp — save/load vpColorLayers/vpColorValues under each viewport (guarded; parallel arrays).
- tests/PaperSpaceTests.cpp — VPFREEZE/VPTHAW idempotence; color override set/replace/clear; per-viewport
  independence (3 new cases). docs/gs-file-format.txt — documented the two new arrays.

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1–Q3). Domain logic (freeze/thaw/color helpers) unit-tested; the UI +
  render + plot + `.gs` integration is manual per the project's UI-REQ convention (test target is
  pure-compute — excludes UI/GL/PDFium/GsIo).

## 9. Self-verification
- [x] build-project        — PASS (clean Ninja build; both exes link; only pre-existing ViewportRenderer warning)
- [x] architecture-review  — PASS (decisions recorded as ADR-021/ADR-007 amendment before coding; reuses
      the frozenLayers pattern + existing color resolver; no new global/abstraction/dependency)
- [x] code-review          — PASS (smallest change per subsystem; parallel arrays kept in step; readable)
- [x] dependency-audit     — n-a (no dependency change)
- [x] performance-review   — n-a (O(overrides) membership per drawn/plotted entity; overrides are tiny;
      plot groups by color via std::map — deterministic, negligible cost)
- [x] testing              — PASS (56/56 ctest green incl. 3 new REQ-046 cases); UI/plot/`.gs` = manual
- [x] user manual verification — PASS 2026-08-18 (user confirmed in app)

## 10. Verification result
- Submitted:  2026-07-13 (self)
- Verdict:    PASS — user manual verification confirmed 2026-08-18
- Findings:   none blocking. Residual debt (unchanged): snap/pick can still target a layer frozen in the
              floating viewport (TASK-017 F2). VP Color covers only overridden layers (ASSUMPTION-1).

## 11. Outcome
- Requirements satisfied: REQ-046 (Acceptance met: yes — UI, plot and .gs round-trip parts confirmed by the user in app 2026-08-18)
- Tests added:            tests/PaperSpaceTests.cpp — 3 REQ-046 cases (VPFREEZE/VPTHAW; color set/clear; independence)
- Refactors:              PdfPlot single-path → per-color paths; entStyle gains a base-color parameter
- Docs updated:           spec/requirements.md (REQ-046 + REQ-028 revision + traceability), spec/project.md
                          (REQ-046 + ADR-021 + ADR-007 amendment), spec/roadmap.md (implicitly), docs/gs-file-format.txt
- Technical debt noted:   ASSUMPTION-1 (override-only viewport color); TASK-017 F2 (snap/pick vs frozen) unchanged
- Done:                   2026-08-18 (user verified in app)
