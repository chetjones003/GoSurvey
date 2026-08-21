# TASK-083 — A DXF polyline must import as a polyline, not as loose lines

- Type:    bug
- Status:  done
- Opened:  2026-08-21
- Owner:   Workshop

## 1. Authority
- Goal:         n/a — `spec/project.md` defines no numbered GOAL-NN ids; authority is the REQs below.
- Requirements: REQ-204 (accepted) — invariant "DXF export → import → export is stable";
                REQ-053 (accepted) — "Every polyline, rectangle or not, is written to DXF as an
                `LWPOLYLINE` … re-opening that DXF shows the rectangle";
                REQ-201 (import/export reports what it did).
- Constraints:  CON-07 (artifacts under the build tree only).
- Acceptance (restated):
  - REQ-204: "DXF export → import → export is stable" — a second export of the imported
    document is byte-identical to the first; and the survival half, entity counts before
    export equal entity counts after import.
  - REQ-053: "exporting a drawing containing a rectangle writes an `LWPOLYLINE` … re-opening
    that DXF shows the rectangle" — a rectangle, i.e. ONE selectable closed polyline, not four
    lines.
  - REQ-201: the import log states what was read.
- Owning subsystem: IO (`src/io/DxfIo.cpp`). No other layer is touched.

## 2. Scope
- In scope: the DXF **importer**'s `LWPOLYLINE` and `POLYLINE` branches — store the vertices in
  the existing polyline store (`userPolyline*`) instead of decomposing them into
  `userLinesFlat` segments. Plus the two places that counted only lines/circles: the
  "no geometry → re-read *MODEL_SPACE" fallback, and the import log line.
- Out of scope:
  - #63 (the exporter has no ARC/ELLIPSE branch) — a different defect in the same oracle;
  - carrying per-vertex Z back out through `LWPOLYLINE` group 38 (pre-existing debt, TASK-034);
  - polyface/polygon **mesh** `POLYLINE` variants (flags 70 bit 16/64), which this reader has
    always mangled and continues to — see §8 OBS-3;
  - the two exporter book-keeping gaps found while reading the code — see §8 OBS-1 and OBS-2.
- Smallest change: one new `appendPolylineXF` helper beside `appendSegXF`, and a branch in each
  of the two polyline readers choosing it over decomposition.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership change / global state / public-API or
  data-format change / unspecified algorithm?
    - [x] No — proceed. The polyline store already exists (REQ-053) and is already written by
          RECT, POLYLINE, 3DPOLY and the contour extractor; the importer simply starts using
          the store that owns this entity instead of the wrong one. No format changes: the
          exporter is untouched.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| — | none — the requirement and the reproducer are unambiguous | — | — |

## 5. Assumptions
```
ASSUMPTION-1: A polyline carrying bulges (arcs) must still be tessellated into line segments.
- Because:       the polyline store (userPolylineVerts, stride 3) has no per-vertex bulge field,
                 and adding one is a data-format change — architectural, not the Workshop's call.
- Risk if wrong: an arc-carrying LWPOLYLINE from Civil 3D still loses object identity on import.
                 Shape and position are preserved; only "is it one object" is lost.
- Validate by:   a follow-up REQ if bulge-carrying polylines must keep identity. Recorded as
                 DEBT-1 below.
```

## 6. Plan
- Approach: in `ParseEntityRegion` add `appendPolylineXF(pts, closed, attrs)` next to
  `appendSegXF`, applying the same INSERT transform and the same
  `local = world - worldDocumentOrigin` rebase (the storage invariant). Both polyline readers
  gain a bulge test: no bulge → one polyline; any bulge → the existing tessellation, unchanged.
- Files/functions to touch:
  - `src/io/DxfIo.cpp` — `ParseEntityRegion` (`appendPolylineXF`, `appendBulgeXF` gains a Z
    pass-through, the `POLYLINE` branch, the `LWPOLYLINE` branch);
    `ImportDxfFile_Impl` (`noGeom`, the import log line).
  - `tests/headless/transcripts/regression-64-dxf-polyline-identity.txt` — new.
  - `docs/fuzz-harness.md`, `TRACKER.md`, `tests/headless/transcripts/dxf-export-stable.txt`
    header — record what is now fixed and what still is not.
- Test approach:
  - happy path = the issue's reproducer as a headless transcript: draw 2 lines + 1 polyline,
    export, NEW, import, `EXPECT LINES 2` / `EXPECT POLYLINES 1`, export again,
    `EXPECT SAMEFILE`. Fails before the fix (4 lines / 0 polylines, files differ).
  - failure mode = a closed polyline (RECT) survives closed; and a bulge-carrying LWPOLYLINE
    still imports (as tessellated segments) rather than being dropped.
- Steps:
  - [x] read the two readers and the polyline store's four parallel arrays
  - [x] add `appendPolylineXF`; give `appendBulgeXF` the elevation it was dropping for LWPOLYLINE
  - [x] branch both readers
  - [x] fix `noGeom` and the log line
  - [x] regression transcript; run the suite
  - [x] **unplanned, and the substance of the second half of this fix**: the exporter's
        `$EXTMIN`/`$EXTMAX` sweep — see FINDING-1 below
  - [x] update docs/tracker

## 7. Workflow-specific notes
- Bug: root cause = the importer has no polyline sink. `ParseEntityRegion` was written when
  `userLinesFlat` was the only line-shaped store; the polyline store arrived later with REQ-053
  and only the *exporter* was taught about it (REQ-053's own revision note says the export gap
  was found during that task). So the asymmetry is not a mistake inside a shared path — the two
  directions were written at different times against different stores. Regression test fails
  before the fix: yes, measured below.

## 8. Implementation log
- 2026-08-21 opened from GitHub issue #64 (fuzz signature `dxflwpolyasym`).
- 2026-08-21 OBS-1 (exporter, NOT fixed here): `ExportDxfFile_Impl`'s `entityHandleCount` sums
  lines + circles + points + annotations and omits **polylines** (and arcs/ellipses, which have
  no branch at all — #63). `objDictRoot` is derived from that sum, so a drawing containing a
  polyline emits OBJECTS handles that **collide** with entity handles, and `$HANDSEED` is below
  the highest handle used. Invisible to the round-trip oracle because both exports collide
  identically. Reported separately rather than folded in — it is an export defect with its own
  reproducer.
- 2026-08-21 OBS-2 (exporter, NOT fixed here): the export's `addLayerName` sweep covers line,
  circle, annotation and survey-point layers but not `userPolylineAttrs`, so a polyline is the
  one entity that can name a layer the LAYER table does not define. Masked in practice because
  `drawingLayerTable` names are added too.
- 2026-08-21 OBS-3 (pre-existing, NOT fixed here): the `POLYLINE` reader ignores flags 70 bit
  16/64, so polygon/polyface meshes are read as if they were ordinary polylines. They were
  already read as garbage lines; they are now read as one garbage polyline. No behaviour is
  being newly broken, and no requirement covers pre-R14 meshes.
- 2026-08-21 DEBT-1: bulge-carrying polylines keep tessellating (ASSUMPTION-1). Removal
  condition: a requirement for per-vertex bulge in the polyline store.
- 2026-08-21 **FINDING-1, found by the regression test, in scope and fixed.** With the importer
  fixed, `EXPECT SAMEFILE` still failed — 7691 vs 7697 bytes, differing at the same byte 243 the
  issue named, but now on `$EXTMIN`/`$EXTMAX` rather than on entities. Two faults, one on top of
  the other:
    1. the export's extents sweep reads lines, circles, annotations and survey points and **never
       polylines**, so the header described only part of the drawing (REQ-053 gave the exporter an
       entity branch but not an extents branch); and
    2. the sweep reads the **local** store while every entity is written through `worldX`/`worldY`
       in **world**, so the header and the body were in different frames. The file therefore
       changed whenever the document origin moved — and importing is exactly what moves it, since
       the importer sets the origin FROM `$EXTMIN`/`$EXTMAX`. The cycle could never settle.
  Both are fixed here because REQ-204's stability invariant cannot be met with either standing;
  the issue is not closed otherwise. Same subsystem (IO), same file, no new abstraction.
  Side effects, both improvements: the VPORT view centre (groups 12/22, derived from the same
  extents) stops pointing at the wrong place for any drawing with a non-zero origin; and a
  state-plane DXF that GoSurvey wrote now re-imports with a correct precision origin instead of
  one taken from local-frame numbers.
- 2026-08-21 group 42 is now read by the LWPOLYLINE reader. It has to be — it is what decides
  "one polyline or an arc chain" — and having read it, the tessellated path uses it. The arcs were
  previously flattened to their chords, so this corrects geometry, not just identity.
- 2026-08-21 measured, fails-before: with `src/io/DxfIo.cpp` stashed and only the transcript in
  place, `regression-64-dxf-polyline-identity` fails at step 21 with `LINES: expected 2, got 8`.
  With the fix: `PASS 31 steps`. Full suite 464/464.

## 9. Self-verification
- [x] build-project       — PASS. MSVC 19.4x / Ninja, Release, clean. (`GoSurvey.exe` itself could
      not relink because the app was running; `GoSurveyTests` and `gosurvey_headless` — the targets
      this change is verified through — build clean, and `DxfIo.cpp` is compiled by all three.)
- [x] architecture-review — PASS. Both edits stay inside IO. `appendPolylineXF` writes the existing
      polyline store through the same rebase every other import path uses; no new store, no new
      type, no format change, no ownership change. Dependencies still flow downward.
- [x] code-review         — PASS. Two self-review findings, both fixed before the build: a stray
      `z` argument applied to the HATCH boundary reader's `appendSegXF`, where no `z` is in scope
      (would not have compiled), and a Doxygen `\p` that a `perl -pe` had eaten.
- [x] dependency-audit    — n/a, no dependency touched.
- [x] performance-review  — n/a. The importer does the same work per vertex and stores three floats
      instead of six; the extents sweep gains one pass over polyline vertices, which is O(n) inside
      a function already O(n) in every other store.
- [x] testing             — PASS. New `regression-64-dxf-polyline-identity.txt` (31 steps): counts
      on both sides of the round trip plus `EXPECT SAMEFILE`; proven to fail against unpatched code
      (`LINES: expected 2, got 8`). 464/464 ctest, 0 failed.

## 10. Verification result
- Submitted:  2026-08-21
- Verdict:    PASS
- Findings:   FINDING-1 (extents sweep) — fixed in this task, see §8. No blocking finding open.

## 11. Outcome
- Requirements satisfied: REQ-204 (stability invariant, for polylines — Acceptance met: yes),
  REQ-053 ("re-opening that DXF shows the rectangle" — met literally for the first time),
  REQ-201 (the import log now names polylines).
- Tests added:            `tests/headless/transcripts/regression-64-dxf-polyline-identity.txt`
- Refactors:              none
- Docs updated:           `docs/fuzz-harness.md` (#64 row), `TRACKER.md` (CHANGES entry),
                          `CMakeLists.txt` + `dxf-export-stable.txt` (why that oracle is still
                          DISABLED — for #63 alone now)
- Still open:              #63 (ARC/ELLIPSE have no export branch) keeps `dxf-export-stable`
                          DISABLED; it now reaches `ARCS: expected 1, got 0`. OBS-1 and OBS-2
                          above are unfiled exporter defects.
- Done:                   2026-08-21
