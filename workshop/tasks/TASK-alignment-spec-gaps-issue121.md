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

Domain: `tests/AlignmentSpecTests.cpp`, `tests/AlignmentAnnotationTests.cpp` (new), `tests/AlignmentTableTests.cpp`,
`tests/AlignmentDwgTests.cpp`, `tests/AlignmentLabelTests.cpp`. `[alignment]`: 257 cases, 207 006 assertions, all pass
(Release). Full Release suite: see "Verification".

## Architectural-boundary check

Domain code stays in `src/alignment` (sampling, cache, tables, definitions); commands in `CadCommands_Alignment.cpp`;
drawing in `QtAnnotations.cpp` / `ViewportRenderer.cpp`; persistence in `GsIo.cpp`. The display cache feeds only the
renderer (REQ-495.1: never a query or export). The snap re-samples near the cursor from the exact geometry.

## Decisions needed (raised at check-in)

1. REQ-487 AC2 says reverse-twice is byte-identical; tests hold it to 1e-9 (floating point).
2. REQ-496 AC4 says a re-save is byte-identical; tests compare GoSurvey's trailer, not the whole DWG.
3. REQ-494: the CSV now writes at display precision (D-2026-11-04-a) — confirm for CSV as well as text.
4. REQ-483.4 "station index" format: undefined; not built.
5. REQ-483.5 orphaned equations: listed in the log and undoable instead of a blocking confirmation.
6. REQ-481.5a minimum transition length: needs a criteria rule (speed / rate of change); not built.
7. ALIGNMENTMETA (REQ-480.1) metadata model is a GoSurvey rule (named text values).
8. REQ-502 tangent-intersection check is a GoSurvey rule (maximum deflection at a curveless PI).
9. REQ-503 Q3: GoSurvey's and Civil 3D's clothoids agree to about 7e-6 ft, not 1e-8.
10. REQ-499 acceptance cites REQ-500 (typo).
11. REQ-492 AC3 writes `Δ = 15°00'00"`; GoSurvey's angle format does not zero-pad minutes and seconds
    (`15°0'0"` at 0 decimals). Changing the shared formatter affects every angle in the program.
12. REQ-492.2 metric default: major 20 m (SPEC); minor 4 m is a GoSurvey rule (the imperial 1:5 ratio).
13. REQ-493 Split Table: parts tile across, the last part of a dynamic table takes new rows (GoSurvey rule).
14. REQ-481.4: the Civil 3D record's spiral type is not decoded, so imported spirals are read as clothoids and the log
    says so; the SPEC asks for a user confirmation before treating another type as a clothoid.
15. REQ-492.5 labels are not draggable objects yet (station/offset labels can be moved with a command).
16. Qt UI changes need the three-reviewer Civil 3D screenshot panel: Node snap glyph, Properties design status row,
    tag labels, Add Tables menu, marker shapes, label orientation.

## Verification

- Build: `.\build.bat` Release, no warnings in changed files.
- Full Release suite (`ctest --preset ninja-release`, at df69d30c): 194/194 passed.
- Debug: `.\build.bat debug` builds. `[alignment]` in Debug: 257 cases pass (after removing a Debug-only assert in the
  per-PI `BuildFromPis` that contradicted its own refusal test). The full Debug suite does **not** complete, for
  reasons outside this work (no commit on this branch touches them; they reproduce on their own):
  - `LibreDWG TU encoders preserve section and object string conventions` — heap corruption (0xC0000374);
  - `OnlineMapTests.cpp` REQ-363 and REQ-364 cases — Debug break (0x80000003);
  - `qt.editor.*` — `build/debug` has the release Qt DLLs plus `Qt6Testd.dll`, which needs the missing
    `Qt6Cored.dll` (0xC0000135), so no Qt test starts;
  - `headless.block-command-bad-numbers` — did not finish in 45 minutes (likely a hidden Debug assert dialog).
- Documentation: `resources/wiki/Alignments.md`, `Command-Reference.md`, `Object-Snaps.md` and the command pages for
  ALIGNMENTTABLE, ALIGNMENTLABEL, ALIGNMENTLABELSTYLE, ALIGNMENTSTYLE, ALIGNMENTEDIT and ALIGNMENTCHECKSET updated.
  README and PRIVACY reviewed: no change needed (no new data collection, no new top-level feature list entry). Public
  wiki and gosurveycad.com: not yet pushed (waiting for the check-in).

## Technical debt

- `RunAlignmentStyle`, `RunAlignmentLabelSet` and `RunAlignmentEdit` exceed 60 lines (pre-existing); new verbs were
  added as helpers.
- Paper-space viewports on screen do not apply per-viewport layer freeze to entities other than alignments
  (pre-existing).
