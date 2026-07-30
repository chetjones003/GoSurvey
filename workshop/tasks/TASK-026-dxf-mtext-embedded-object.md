# TASK-026 — Ignore the AutoCAD 2018+ MTEXT "Embedded Object" on DXF import

- Type:    bug
- Status:  done
- Opened:  2026-07-30
- Owner:   chetj

## 1. Authority
- Goal:         interoperable DXF (a real-world AutoCAD drawing imports faithfully)
- Requirements: REQ-023 (accepted — runtime DXF round-trip, IO); REQ-049 (accepted — TEXT/MTEXT
  appear at correct position/size); REQ-002 via TASK-011's recorded Acceptance
- Constraints:  local-storage invariant (geometry stored local; world = local + worldDocumentOrigin);
  CON-07 (build reproducibility)
- Acceptance (restated from TASK-011, the shipped unit this defect regresses):
  - "A DXF with `TEXT`/`MTEXT` in model space imports as readable native annotations
    (correct position, height, rotation, content); MTEXT inline control codes are flattened."
- Owning subsystem: IO (DXF) — `src/io/DxfIo.cpp`, MTEXT branch of `ParseEntityRegion`.

### Spec bookkeeping note (not a blocker)
REQ-002 is still marked `proposed` in `spec/requirements.md` and has no traceability row, even
though TASK-011 shipped against it and REQ-023/REQ-049 (both `accepted`) depend on the same import
path. That is a pre-existing spec-status gap, not a new decision: this task adds no behavior, it
restores the already-accepted behavior for a file the parser mis-reads. Flagged for the spec layer;
work proceeded under REQ-023/REQ-049.

## 2. Scope
- In scope:   MTEXT parsing in `ParseEntityRegion` — do not apply the embedded object's group codes
              to the parent MTEXT entity.
- Out of scope:
  - Consuming the embedded object's data (columns, extents) — nothing in it is needed; the main
    record already carries insertion, height, reference width, attachment, style, and direction.
  - `ATTRIB`/`ATTDEF` import (still skipped as unsupported — 14 in the reference file, all paper space).
  - The empty-string MTEXTs in the reference file (2 on layer "0 Check Dimension", group 1 = " ");
    they are correctly dropped as empty.
- Smallest change: one guard in the MTEXT group-code loop. No data-model, renderer, or export change.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership change / global state / public-API or data-format
  change / unspecified algorithm?
    - [x] No — proceed. A parser conformance fix confined to the owning subsystem.

## 4. Questions
None — the DXF group codes and the correct reading are unambiguous from the file itself (see §7).

## 5. Assumptions
```
ASSUMPTION-1: the main AcDbMText record is authoritative for every field GoSurvey imports; the
  group-101 embedded object is a separate object whose codes must not be applied to the entity.
- Because:       AutoCAD writes both, reusing the same numeric codes with different meanings.
- Risk if wrong: text imports at the wrong attachment/size.
- Validate by:   cross-checking the reference file (done, §7): main 41 == embedded 40 (reference
  rectangle width) for all 118 embedded-object MTEXTs, so the records agree where they overlap; the
  8 rotated labels carry their real direction vector in the MAIN record's 11/21 as well, so nothing
  needed is unique to the embedded object. Embedded 70/71/72 are constant (1/2/1) across all 75
  model-space MTEXTs regardless of the main record's 71 (1 or 5) — i.e. flags, not per-entity
  attachment — so the main record's 71 is the attachment point.
```

## 6. Plan
- Approach: in the MTEXT branch of `ParseEntityRegion`, track a `inEmbedded` flag. On group 101,
  stop applying codes to the entity; resume at the first code >= 1000 so trailing XDATA (the
  GOSURVEY survey-label marker, REQ-023) is still read.
- Files/functions to touch: `src/io/DxfIo.cpp` — `ParseEntityRegion`, `typ == "MTEXT"` branch only.
- Test approach: happy path = the reference drawing's 73 embedded-object MTEXTs import at their real
  insertion points, heights, and rotations. Failure mode = an MTEXT with no embedded object, and a
  GoSurvey-exported MTEXT carrying GOSURVEY XDATA, are unchanged (the guard never engages / XDATA
  still read).
- Steps:
  - [x] confirm root cause against the reference DXF by group-code audit
  - [x] add the guard
  - [x] build (release compile + debug link) and run the suite

## 7. Workflow-specific notes (Bug)
- Reported: importing `26-084 - Master.dxf` (AutoCAD AC1032, 4.1 MB) shows no text at all.
- Root cause (mechanism): AutoCAD 2018+ appends an **"Embedded Object" (group 101)** to MTEXT — a
  second serialization that reuses the entity's group codes with different meanings. The parse loop
  ran to the end of the entity and kept assigning, so the last value won:

  | code | main AcDbMText | embedded object | value the parser ended up with |
  |------|----------------|-----------------|-------------------------------|
  | 10/20 | insertion point (2839.543, 5210.108) | X-axis direction (1.0, 0.0) | **insertion = (1, 0)** |
  | 11/21 | X-axis direction (absent here) | insertion point (2839.543, 5210.108) | **rotation = atan2(5210,2839) ≈ 61.4°** |
  | 40   | text height 0.189 | reference-rect width 37.303 | **height 37.303 (~197× too large)** |
  | 41   | reference width 37.303 | rect height 0.0 | reference width 0 |
  | 71   | attachment 1 | flag 2 | attachment 2 |

  Every label therefore landed at local (−1562, −2825) — thousands of units off the drawing, which
  sits at local (703…1560, 2127…2825) — rotated 61° and ~200× oversized. Nothing was visible, and
  zoom-extents (computed from lines/circles) never brought them into view.
- Blast radius in the reference file: **118 of 159 MTEXT entities** carry an embedded object —
  73 of the 75 model-space MTEXTs in `ENTITIES` (the other 2 are empty strings) and 43 inside
  `BLOCKS`. That is 100% of the drawing's visible model text, matching the report exactly.
  Only MTEXT uses group 101 in this file; `TEXT` and `LINE`/`CIRCLE`/`HATCH` are unaffected, which
  is why geometry imported fine.
- Regression test fails-before? No automated test added — see §9 (testing).

## 8. Implementation log
- 2026-07-30 audited the DXF by group code; confirmed the 101 embedded object and the exact
  field-by-field corruption above; confirmed layers are all on and none of the affected MTEXTs are
  paper-space (group 67), ruling out the layer/paper-space skip paths.
- 2026-07-30 added the `inEmbedded` guard to the MTEXT branch; release compile clean, debug link
  clean, suite green.
- 2026-07-30 release link initially blocked (running instance held `GoSurvey-0.4.0.exe`); relinked
  once the app exited. The stale binary is why the first runtime check showed no change.
- 2026-07-30 built a headless probe (`scratchpad/dxfprobe.cpp`) linked against the project's own
  compiled objects, so it drives the **production** `ImportDxfFile`, not a reimplementation. Ran it
  against the fixed `DxfIo.cpp.obj` and against `git show HEAD:src/io/DxfIo.cpp` compiled separately
  — see §9 for the A/B result. Note: `build/CMakeFiles/GoSurvey.dir/src/ui/ShxFont.cpp.obj` is a
  stale leftover from the ADR-022 relocation and must be excluded when linking by object glob.

## 9. Self-verification
- [x] build-project        — PASS (release + debug both link clean; suite green. Only pre-existing
      warning: unused const in `PdfAttach.cpp`.)
- [x] architecture-review  — PASS (no Workshop architectural decision; change confined to IO/DXF)
- [x] code-review          — PASS (one local flag; no new abstraction; comment states why)
- [x] dependency-audit     — n-a (no dependency change)
- [x] performance-review   — n-a (strictly fewer assignments per MTEXT)
- [x] testing              — PASS via headless A/B against the reference drawing (both runs import
      73 MTEXT — the 75 model-space entities less the 2 empty-string ones):

      | measure | before (HEAD) | after (fixed) | DXF says |
      |---|---|---|---|
      | annotations inside the line-geometry bbox | **0 / 73** | **72 / 73** | — |
      | insertion bbox (local) | X −2982…−2697, Y −5448…−5304 | X −437…400, Y −334…349 | — |
      | geometry bbox (local) | X −2818…429, Y −5172…349 | X −2827…420, Y −5172…349 | — |
      | "FDN TYPE 6" height | 37.3030 | **0.1890** | 0.189 (group 40) |
      | "MON #1" height | 1.4383 | **0.0630** | 0.063 (group 40) |
      | "**HOLD**" height | — | **0.5670** | 0.567 (group 40) |
      | rotations | 59.44°, 61.41°, 64.75°, 65.48° … | **0.00°** | no group 50/11 → 0 |

      The one annotation outside the bbox after the fix is "MON #16" at Y = 349.219 vs the geometry
      top of 349.210 — 0.009 units proud of the line bbox, i.e. inside the drawing.
      Heights are cross-checked against the embedded object's read-only extents (group 43), which
      the parser now ignores but which confirm the reading: MON #1 is 2 lines → 0.063 + 0.063 ×
      1.6667 = 0.168 = its group 43 exactly; FDN TYPE 6 is 1 line → 0.189 × 1.01389 = 0.19163 = its
      group 43 exactly. So group 40 in the main record is unambiguously the text height.
- [ ] Still open: `GoSurveyTests` (611 assertions / 98 cases) is green but does **not** cover
      `DxfIo.cpp` — the target links only header-only domain modules. The A/B above is a manual
      runtime check, matching REQ-023's established evidence type. See DEBT-1.

## 10. Verification result
- Submitted:  2026-07-30
- Verdict:    PASS
- Findings:   none outstanding

## 11. Outcome
- Requirements satisfied: REQ-023 / REQ-049 (Acceptance met: yes — position, height, rotation and
  content all match the source DXF)
- Tests added:            none automated — see debt below
- Technical debt:         DEBT-1 — `DxfIo.cpp` has no automated coverage because the test target
  cannot link it (it pulls in `CadCommands.hpp` → UI/GL). Removal condition: a seam that lets DXF
  parsing be exercised headlessly, or a fixture-based import test target. Follow-up task to open
  when that seam is designed — it is an architectural decision, not a Workshop one.
- Docs updated:           none
- Note for the user:      the reference drawing's text is genuinely small — 0.063…0.567 model units
  (`$INSUNITS` = 6, metres) against a robust drawing extent of ~875 × 699 units. At zoom-extents the
  viewport shows ~0.97 units/px, so a 0.189-unit label is ~0.2 px and clamps to the 1 px floor —
  present but unreadable. Reading a foundation label needs the view narrowed to roughly 17 units
  (~50× in from extents). That matches AutoCAD; it is not a residual defect.
- Done:                   2026-07-30
