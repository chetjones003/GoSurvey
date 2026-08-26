# TASK-114 — Arcs and ellipses survive a DXF round trip (#63)

- Type:    bug
- Status:  review — #63 fixed and green; the oracle stays DISABLED for a THIRD defect this task
           found and did not cause (§12 DEBT-1). Awaiting user decision on filing it.
- Opened:  2026-08-26
- Owner:   Nathan Johnson

Upstream issue: chetjones003/GoSurvey#63, filed 2026-08-18 by TASK-056 (silent data loss).
Found by the REQ-204 `dxf-export-stable` oracle, which has been registered `DISABLED TRUE` in
`CMakeLists.txt:716` ever since. Its other half (#64, LWPOLYLINE shattered on import) was
closed 2026-08-21 by TASK-083.

> **This task opened believing #63 was the last defect holding that oracle off the suite. It
> was not.** #63 is fixed and its regression runs, but the oracle stays DISABLED for a third
> defect — pre-existing, unrelated, and measured on the unmodified writer. See §12 DEBT-1.
> The claim above is left in place rather than edited away because the belief is what shaped
> the acceptance condition in §1, and the correction is the finding.

## 1. Authority
- Goal:         DXF is the interchange format REQ-052 builds DWG on top of. A drawing that
                loses geometry on the way out is worse than one that fails to export — the
                user has a file, and no reason to distrust it.
- Requirements: **REQ-204** (accepted 2026-08-16) — invariant row, verbatim: *"DXF export →
                import → export is stable"* / *"An entity type silently dropped by an
                exporter with no branch for it."* This defect is not merely covered by that
                row; it is the exact thing the row was written to describe. Same authority
                TASK-107 used for #71/#72.
- Also honoured: REQ-201 (nothing happens silently — today the export log counts what it
                wrote and simply never mentions the arc); REQ-057 / ADR-025 (group-30
                elevation is absolute, and arcs/ellipses stay parallel to XY).
- Constraints:  Windows 11 / MSVC (project.md §7). No new dependency.
- Acceptance:   restated from the governing source —
  - REQ-204 invariant: DXF export → import → export is stable.
  - REQ-204 acceptance: *"each listed invariant has a fixture that deliberately breaks it and
    proves the check fires — a check that has never failed is not known to be a check."*
    The fixture already exists and already fires: `dxf-export-stable.txt`. Done means it is
    **re-enabled and green**, not that a new check was written.
  - **This condition is NOT met, and the task does not claim it.** The fixture's SURVIVAL half
    — the half #63 breaks — passes, and is held by a regression that runs. Its STABILITY half
    fails on DEBT-1, which this task neither caused nor can close without widening into an
    import-semantics decision. Recorded rather than quietly re-scoped: the acceptance was
    written when #63 looked like the only thing in the way.
- Owning subsystem: `io` (`src/io/DxfIo.cpp`), both directions. ADR-031 records `DxfIo`'s
  dependency direction; nothing here changes it.

## 2. Scope

The issue title says "the exporter has no ARC/ELLIPSE branch", but recon found the defect is
**two-sided, and each side covers both entity types**. All of it had to close together for the
survival half of the oracle to pass.

- In scope:
  1. **Export ARC branch** — `userArcs` is named nowhere in the export path.
  2. **Export ELLIPSE branch** — `userEllipses`, the same way. Measured separately by the
     oracle's author: a drawing holding one ellipse and nothing else exports a DXF
     containing **zero entities of any kind**.
  3. **`entityHandleCount`** (`DxfIo.cpp:2160`) gains both stores. Not optional housekeeping
     — TASK-107 left the instruction in place at `:2155`: *"`userArcs` and `userEllipses` are
     absent on purpose: they have no export branch at all (#63), so they consume no handles
     today. Adding that branch means adding them here in the same change."* Skipping it
     reintroduces #71's duplicate handles.
  4. **`addLayerName` sweep** (`:2096`) gains `userArcAttrs` / `userEllipseAttrs`, or an arc
     on an imported layer names a LAYER the file never defines — #72, exactly.
  5. **`$EXTMIN` / `$EXTMAX` sweep** (`:2255`) gains both. Same omission family as the
     polylines TASK-083 had to add there: the importer sets the document origin FROM those
     extents, so an arc outside the linework's box makes the round trip never settle.
  6. **Import ARC sink** — `DxfIo.cpp:1153` reads the ARC correctly and then throws its
     identity away, tessellating into `constexpr int nseg = 48` line segments. There is no
     `appendArcXF`.
  7. **Import ELLIPSE sink** — `appendEllipseXF` (`:685`) looks like a sink and is not; it
     tessellates too (`:713`). So ellipses lose identity in *both* directions.
  8. ~~Re-enable `headless.dxf-export-stable`.~~ **Not done** — it stays DISABLED on DEBT-1.
     Its note in `CMakeLists.txt` is rewritten instead, to name the real remaining blocker
     rather than #63.
- Out of scope:
  - **Paper-space** arcs/ellipses. DXF paper-space export is REQ-113, still `proposed`.
  - **Elliptical arcs** (a partial ELLIPSE). `CadEllipse` has no start/end parameter, so the
    range cannot be stored — see DEBT-2. Adding one is a `.gs` data-format change, i.e. a
    SPEC GAP, and is not taken on here.
  - Per-vertex polyline bulge (TASK-083 DEBT-1), untouched.
- Smallest change: two emit branches mirroring the existing CIRCLE branch verbatim, two terms
  in one sum, two sweep loops, and two import sinks mirroring `appendCircleXF`'s existing
  identity-vs-tessellate split. No new helper, no signature change.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public API / data format?
    - [x] **No.** Every piece has an existing precedent in this same file: the CIRCLE export
          branch is the template for both new emit branches, and `appendCircleXF`'s "real
          sink when the INSERT transform is identity, tessellate otherwise" is the template
          for both new import sinks. The stores (`userArcs`, `userEllipses`) and their attr
          vectors already exist and are already persisted by `.gs`. No file format changes:
          ARC and ELLIPSE are stock DXF entities, and this writes the same records the
          importer already parses.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Import sinks change existing behaviour: an arc in a DXF users already import is 48 selectable line segments today, and becomes one arc object. In scope, or export-only? | 2026-08-26 | **In scope.** REQ-204's invariant is a round trip, and `EXPECT ARCS 1` after import cannot pass without it. Export-only would leave the oracle red and the defect half-fixed. |
| Q2 | Partial (trimmed) ellipse on import — tessellate as today, or extend the store? | 2026-08-26 | **Tessellate, as today.** Extending `CadEllipse` is a `.gs` format change → SPEC GAP. Recorded as DEBT-2 rather than smuggled in. |

## 5. Assumptions
```
ASSUMPTION-1: A non-identity INSERT transform must still tessellate.
- Because:       CadArc stores one radius and CadEllipse a major-axis vector + ratio; a
                 non-uniform or skewed INSERT scale produces a curve neither can represent.
- Precedent:     appendCircleXF already makes exactly this split (`if (xf.isIdentity())`
                 real sink, else a 64-segment ring) — this follows it rather than inventing
                 a second rule.
- Risk if wrong: a scaled block's arcs silently change shape instead of degrading to lines.
- Validate by:   a transcript importing a DXF whose INSERT carries a scale, asserting the
                 arc count does NOT rise and the geometry still matches.

ASSUMPTION-2: DXF ARC always runs CCW from group 50 to group 51; CadArc::sweepRad is SIGNED.
- Because:       a CW arc (negative sweep) written as 50=start, 51=start+sweep describes the
                 COMPLEMENT of the intended arc — the one failure mode here that produces a
                 plausible-looking file rather than an obviously broken one.
- Handling:      normalize on export (swap ends for a negative sweep) so 50 → 51 always runs
                 CCW over the intended span.
- Validate by:   the regression transcript draws BOTH a CCW and a CW arc and asserts the
                 round trip returns two arcs whose endpoints match the originals. This is the
                 assertion most likely to catch a real mistake, so it is written first.
```

## 6. Plan
- Steps:
  - [x] 1. Regression transcript first, proven RED: `ARCS: expected 2, got 0`.
  - [x] 2. Export ARC branch (mirrors the CIRCLE branch).
  - [x] 3. Export ELLIPSE branch (group 11/21/31 is the major-axis endpoint **relative to
           centre**, 40 = ratio, 41/42 = 0 / 2pi for a full ellipse).
  - [x] 4. `entityHandleCount`, `addLayerName`, `$EXTMIN`/`$EXTMAX` — all three sweeps. The
           extents one was written twice; see §8 for why the obvious version is wrong.
  - [x] 5. Import ARC sink; import ELLIPSE sink. Both keep the tessellating path for the
           non-identity-transform case (ASSUMPTION-1).
  - [x] 5b. **Unplanned, found while implementing:** the importer's `noGeom` fallback had to
           learn about both stores too, or a curves-only DXF is read twice. In the transcript.
  - [~] 6. ~~Re-enable `headless.dxf-export-stable`; confirm green.~~ **Not done** — DEBT-1.
  - [x] 7. Full suite: **602/602**, `regression-71-dxf-export-handle-uniqueness` still green —
           it is the check that holds step 4 honest.
- Test approach: transcript-driven, and unlike TASK-113 this one is **fully testable
  headlessly** — no GUI needed. The oracle was to be the acceptance; in the event it could
  only supply the survival half of it (§1).

## 7. Workflow-specific notes
- Bug: the reproducer already exists and already fails. Step 1 adds a *narrowed* regression
  beside it, on TASK-083's precedent — `regression-64-…` is `dxf-export-stable` with the arc
  removed, so one open defect could not keep the other's test off the suite. Here the two
  finally converge.

## 8. Implementation log
- 2026-08-26 Recon. Read the export path and both import branches rather than trusting the
  issue text — which was right about the exporter and **understated the defect**: it names
  the ARC tessellation at `:1153` but not that `appendEllipseXF` tessellates as well, so
  ellipses are lost in both directions, not one.
- 2026-08-26 Regression written first and proven RED: `ARCS: expected 2, got 0`.
- 2026-08-26 **The `$EXTMIN`/`$EXTMAX` sweep was written twice.** The first version used the
  arc's whole CIRCLE as its box, on the reasoning that over-estimating is harmless because
  the extents are padded anyway. That reasoning was wrong, and the oracle caught it. DXF
  import ends by calling `RebaseDrawingToLocalOrigin`, which re-centres the document origin
  on `ComputeWorldExtents` — so a sweep that disagrees with THAT sweep makes the rebase
  non-zero, and the rebase shifts every stored coordinate through `float`. The second
  version samples arcs and ellipses exactly as `ComputeWorldExtents` does, which drives the
  rebase delta to zero so it takes its own early-out and no lossy shift happens. The comment
  at the sweep records this, because "make it tighter" and "make it safer" are both wrong
  here and the next reader will otherwise try one of them.
- 2026-08-26 **A trap the fix opened, found by reading rather than by test.** The importer's
  `noGeom` fallback ("no geometry — re-read the *MODEL_SPACE block") tested three stores.
  Arcs and ellipses never needed to be in that list because they always landed in
  `userLinesFlat` as tessellation. With stores of their own, a DXF holding nothing but arcs
  would have been judged empty and read a SECOND time, duplicating it. Added to the test and
  to the transcript's failure-mode section.
- 2026-08-26 **A THIRD defect found, measured, and deliberately NOT fixed here** — see
  DEBT-1. It is why the oracle stays disabled.

## 9. Self-verification
- [x] build-project        — PASS (clean; the three C4456 warnings in `DxfIo.cpp` are
                             pre-existing and in untouched code)
- [x] architecture-review  — PASS (no new type, field, signature or dependency; every piece
                             mirrors an existing precedent in the same file)
- [x] code-review          — PASS
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a (two extra loops over stores that were already swept)
- [x] testing              — **602/602 ctest green**, 1 pre-existing disabled
                             (`dxf-export-stable`, see DEBT-1). New transcript proven
                             red-before / green-after.

## 10. Verification result
- Submitted:  2026-08-26
- Verdict:    PASS for #63. **NOT** a verdict on REQ-204's stability invariant, which DEBT-1
              leaves open and which no longer has anything to do with this issue.
- Findings:   none outstanding against #63.

## 11. Outcome
- Requirements satisfied: REQ-204's invariant row *"an entity type silently dropped by an
                          exporter with no branch for it"* — met. REQ-204's **stability**
                          invariant is NOT met, for the unrelated reason in DEBT-1; the
                          acceptance condition "the oracle is re-enabled and green" is
                          therefore **not** met, and this task does not claim it.
- Tests added:            `tests/headless/transcripts/regression-63-dxf-arc-ellipse-identity.txt`
                          — survival across the round trip for both arc directions and a full
                          ellipse, plus `HANDLESUNIQUE` / `LAYERSDEFINED` on the written file
                          and the curves-only double-read failure mode.
- Refactors:              none
- Docs updated:           `CMakeLists.txt` (the disabled-oracle note now names the real
                          remaining blocker instead of #63)
- Done:                   #63 closed 2026-08-26. The oracle stays off; see DEBT-1.

## 12. Technical debt
```
DEBT-1: DXF export -> import -> export is still not byte-stable, for a reason unrelated to #63.
        THIS IS A DEFECT WORTH FILING, not debt in the usual sense — it is recorded here
        because this task found it and deliberately did not widen to fix it.
- What:      `$EXTMIN`/`$EXTMAX` are written at six decimals. The importer sets the document
             origin from those ROUNDED values, and then `RebaseDrawingToLocalOrigin`
             re-derives the origin from the UNROUNDED geometry. The two disagree by up to
             ~5e-7, which clears the `< 1e-9` no-op guard in `ApplyDocumentOriginRebase`, so
             `ShiftAllStorageBy` moves every stored coordinate through `float` and re-rounds
             it. The second export lands ~1e-6 off in every coordinate.
- Evidence:  MEASURED, not inferred, and measured against the UNMODIFIED writer. Two LINEs at
             coordinates that are not exact at six decimals — no arc, no ellipse anywhere —
             reproduce it byte-for-byte: same step, same first differing byte (250), same
             file sizes (7154 vs 7156), with this task's changes stashed. So it is neither
             caused by #63 nor fixable within it. Drawings whose extents happen to be exact
             at six decimals are unaffected, which is why the corpus never caught it.
- Cost:      REQ-204's stability invariant stays unproven, and `dxf-export-stable` stays
             DISABLED even though both survival defects it found (#63, #64) are now closed.
- Remove by: deciding the origin policy deliberately — plausibly "do not re-derive the origin
             when the header already supplied a sane one", or "widen the no-op guard to the
             precision the file was written at". That is an import-semantics decision
             affecting every DXF, not bug-fix work, so it wants its own task and probably a
             recorded decision.
- Follow-up: filed 2026-08-26 as **chetjones003/GoSurvey#94**, carrying the reproducer above
             verbatim. Re-enable `dxf-export-stable` when #94 closes.

DEBT-2: A trimmed ellipse still round-trips as line segments.
- What:      CadEllipse has no start/end parameter, so an ELLIPSE carrying groups 41/42 short
             of a full turn cannot be stored as an ellipse and is tessellated.
- Forced by: adding the range is a `.gs` data-format change — SPEC GAP, not bug-fix work.
- Cost:      trimmed ellipses from other CAD keep losing identity. Full ellipses, which are
             what GoSurvey's own ELLIPSE command produces, are unaffected.
- Remove by: a requirement for elliptical arcs, then the field + a `.gs` migration (REQ-079).
- Follow-up: not filed; raise it if a user meets it in a real drawing.
```
