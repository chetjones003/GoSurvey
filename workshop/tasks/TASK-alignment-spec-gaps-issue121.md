# TASK — Issue #121 remaining acceptance criteria (REQ-480..503 spec-gap pass)

Branch `feat/implement-issue-121`, [PR #1152](https://github.com/chetjones003/GoSurvey/pull/1152). User request
(2026-11-04): "Satisfy the remaining reqs then check back in with me."

## Requirement authority

Accepted REQ-480..503 (spec/requirements.md) and the decisions D-2026-11-02-b, D-2026-11-04-a..n (spec/project.md).
Each batch below names the clauses it closes. No new requirement was invented; GoSurvey rules taken where the SPEC
leaves a detail open are listed under "Decisions needed" for the user to confirm.

## Batches (commits)

| Commit | REQs | What |
|---|---|---|
| 780196af | 484, 481, 482, 483, 486, 499 | GetStation/GetOffset, extrapolate opt-in, reverse-curve note, spiral X/Y/P/K/LT/ST, Free-curve refusals naming both tangents, equation tolerance |
| 56dc48f8 | 485, 487, 488, 489 | Case-insensitive unique names, EQ EDIT, offset ranges and naming, intersections with arcs and placement |
| f3c25b35 | 490 | Node snap (drawing points, PIs), SPI / reverse-spiral codes, own-chord INT skip |
| 8fae4b3d | 494, 501, 480 | Per-entity criteria marks in the report and CSV, design status in Properties |
| d05a173d | 483, 480 | STATIONFORMAT, reference point, dropped-equation report, ALIGNMENTMETA |
| de30fb11 | 502 | Tangent-intersection deflection check, criteria file LOAD/SAVE |
| bbcf4764 | 493 | Spiral table, Civil 3D tags L/C/S, tag labels, SPLIT=, STYLE=, Annotate ▸ Add Tables menu; ALIGNMENTADD refreshes dynamic tables and linked offsets |
| 54be8b9d | 491, 492 | Label style precision/prefix/suffix/orientation, repeated Geometry Points items, Profile/Superelevation placeholders, metric 20 m default, single segment labels, per-code markers, current style |
| c6f4e571 | 495 | Adaptive spiral sampling, display cache per alignment and zoom bucket, layer / viewport-freeze visibility, PDF plot, BENCH ALIGNMENT |
| 7ee9a351 | 481.5a | Curve definitions (degree, tangent, chord, external, middle ordinate, through point), spiral by A |
| df69d30c | 484.4, 496.3 | GETSTATIONOFFSET pick mode; Civil 3D import names what it did not decode |

## Evidence (SPEC GAP pass)

- **Tags (REQ-493.1):** Autodesk Civil 3D training material and the Autodesk forum: tag labels number tangents L1, L2,
  curves C1, spirals S1; "when you create a table for an alignment that already has labels, the labels are
  automatically converted to tag labels" (Roadway Alignments instructor guide; Civil 3D Essentials, "Using Tag Labels
  and Tables"). Implemented as written.
- Earlier evidence for this session (Civil 3D 2026 on this machine, scratch drawings only): D-2026-11-04-m (AECC_ALIGNMENT
  record layout), D-2026-11-04-n (reverse clears equations).

## Tests

Domain: `tests/AlignmentSpecTests.cpp`, `tests/AlignmentAnnotationTests.cpp`, `tests/AlignmentTableTests.cpp`,
`tests/AlignmentDwgTests.cpp`, `tests/AlignmentLabelTests.cpp`. This continuation adds the full-DWG byte comparison
for a stable second save, checks CSV values at full precision, and updates DMS expected strings for zero-padded
minutes and seconds. The label drag interaction already has live-move, commit/undo and cancel coverage.

## Architectural-boundary check

Domain code stays in `src/alignment` (sampling, cache, tables, definitions); commands in `CadCommands_Alignment.cpp`;
drawing in `QtAnnotations.cpp` / `ViewportRenderer.cpp`; persistence in `GsIo.cpp`. The display cache feeds only the
renderer (REQ-495.1: never a query or export). The snap re-samples near the cursor from the exact geometry.

## Decisions confirmed (user choices, 2026-11-05)

1. D-2026-11-05-a (already recorded): reverse twice returns the original geometry within 1e-9 drawing units,
   preserving entity kinds, directions, constraints and locks.
2. REQ-496: no alignment data is added to drawings without alignments; a subsequent save is byte-identical.
3. REQ-494: CSV numeric fields use full available precision.
4. REQ-483: StationIndex uses Civil 3D notation, such as `1+23.45`.
5. REQ-483: orphaned equations are removed, logged and included in undo with the edit.
6. REQ-481: spiral length must be positive; only numerically degenerate length is refused as geometry. Criteria
   minimum length remains a warning check.
7. REQ-480: ALIGNMENTMETA stores arbitrary named strings.
8. REQ-502: tangent intersections are reported when they exceed the configured maximum deflection.
9. REQ-503: accept Civil 3D spiral agreement within about 7e-6 ft.
10. REQ-499: correct the incorrect REQ cross-reference.
11. REQ-492: zero-pad angle minutes and seconds.
12. REQ-492: retain the derived metric minor station interval of 4 m.
13. REQ-493: split tables continue in adjacent sections with repeated headers.
14. REQ-481: assume an unidentified imported spiral is a clothoid and log the assumption.
15. REQ-492: keep draggable labels. The viewport drag, one-step undo and cancel behavior are implemented and covered by
    `AlignmentLabelOffsetTests.cpp` and `QtEditorTests.cpp`.
16. The required three-reviewer Civil 3D screenshot panel for alignment surfaces has passed; see
    `build/artifacts/panel-121-dms-padding/RESULT.md` and prior panel evidence in `build/artifacts/panel-followups/`.
    Reviewed surfaces include the Node snap glyph, Properties design status row,
    tag labels, Add Tables menu, marker shapes, label orientation.

## Verification

- Build: `.\build.bat` Release passed after the changes.
- Focused run: `[alignment],[angle]` passed (262 cases, 207,044 assertions).
- Full Release suite: the first `ctest --preset ninja-release` run passed 193/194 tests; the single failure was the
  pre-existing angle-label expectation for unpadded DMS text, now updated. Re-running `unit.catch.shard-1` passed.
  The other shards, including all Qt editor shards and headless tests, passed in the full run.
- Screenshot capture: `issue121Phase6DesignScreenshots` passed and produced fresh padded-label screenshots in
  `build/artifacts/Release/`. The hidden test runner continued into the full Qt suite and was stopped at its 120-second
  timeout; the capture itself passed. The paired Civil 3D panel at
  `build/artifacts/panel-121-dms-padding/` passed: all rubric medians are 1 or higher, with no blockers.
- Debug: `.\build.bat debug` builds. `[alignment]` in Debug: 257 cases pass (after removing a Debug-only assert in the
  per-PI `BuildFromPis` that contradicted its own refusal test). The full Debug suite does **not** complete, for
  reasons outside this work (no commit on this branch touches them; they reproduce on their own):
  - `LibreDWG TU encoders preserve section and object string conventions` — heap corruption (0xC0000374);
  - `OnlineMapTests.cpp` REQ-363 and REQ-364 cases — Debug break (0x80000003);
  - `qt.editor.*` — `build/debug` has the release Qt DLLs plus `Qt6Testd.dll`, which needs the missing
    `Qt6Cored.dll` (0xC0000135), so no Qt test starts;
  - `headless.block-command-bad-numbers` — did not finish in 45 minutes (likely a hidden Debug assert dialog).
- Documentation: `resources/wiki/Alignments.md`, `Command-Reference.md`, `Object-Snaps.md`, `Drafting-Aids.md`,
  `Properties.md` and the command pages for
  ALIGNMENTTABLE, ALIGNMENTLABEL, ALIGNMENTLABELSTYLE, ALIGNMENTSTYLE, ALIGNMENTEDIT and ALIGNMENTCHECKSET updated.
  README and PRIVACY reviewed: no change needed (no new data collection, no new top-level feature list entry). The
  GitHub Wiki URL currently redirects to the repository, and `gosurveycad.com` was inaccessible to the documentation
  review tool; check the public manual update path before marking the PR ready.

## Technical debt

- `RunAlignmentStyle`, `RunAlignmentLabelSet` and `RunAlignmentEdit` exceed 60 lines (pre-existing); new verbs were
  added as helpers.
- Paper-space viewports on screen do not apply per-viewport layer freeze to entities other than alignments
  (pre-existing).
