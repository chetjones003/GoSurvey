# TASK-079 — Feature lines through the modify commands

- Type:    feature
- Status:  done (stage 3 of 3)
- Opened:  2026-08-20
- Owner:   chetjones003

## 1. Authority
- Goal:         M-Grading step 3 (`spec/roadmap.md`); ADR-035, accepted 2026-08-19.
- Requirements: **REQ-087** (feature line entity) — `proposed`, promoted to `accepted` when this
                lands and stage 2b's gaps are either closed or recorded. REQ-076 (stable ids),
                REQ-201 (every command reports), REQ-204 (invariants), REQ-101 (±0.01 ft).
- Acceptance clause this stage owns (REQ-087, verbatim): *"It selects, **moves**, snaps, hides by
  layer, persists to `.gs`, and is undoable in one step per operation, like every other entity."*
  Note that REQ-087 names **move**; it does not name trim, offset or join. That asymmetry drives
  §2's split between handle and refuse.
- Owning subsystem: `commands/`, `viewport/`.

## 2. Scope

### Handle
`MOVE`, `COPY`, `ROTATE`, `SCALE` — the four whole-entity transforms. A feature line's vertices are
a CSR chain over a stride-3 array, structurally identical to a polyline's, so all four are the same
walk with a different point function.

### Refuse, out loud (REQ-201)
`TRIM` (both as cutting edge and as target), `OFFSET`, `JOIN`, and `COPYCLIP`. Each has a defined
meaning for a 2D chain and an **undefined** one for a 3D chain carrying elevations — what elevation
does a trimmed end take, what does an offset copy sit at, which of two elevations wins at a joined
endpoint? REQ-087 does not answer that and REQ-088 does not either. Inventing an answer here would
be the Workshop making an architectural decision (CLAUDE.md layer rule 3), so instead each command
says it cannot and why. Silence is the failure mode ADR-035 (g) names; a refusal is not.

`EXTEND`, `FILLET` and `BREAK` were in this task's first draft. **They have no commands in this
codebase at all**, for any entity type, so there was nothing to refuse. Corrected rather than left
standing, because a plan that lists work which cannot exist makes the completion report unfalsifiable.

`COPYCLIP` was not in the first draft either and was found while implementing. It is worse than the
others were: the clipboard has no feature-line store, so the copy silently skipped them **and the
report still counted them**, telling the user "1 object(s) copied" before pasting nothing. Now it
reports what it actually took and names what it did not.

### Out of scope
- Grips and single-PI editing (stage 2b, still open). **This matters for ASSUMPTION-1** — see §5.
- REQ-088's elevation editor.
- `MIRROR` — no mirror command exists in the codebase yet for any entity.

## 3. Architectural boundary check
- No new abstraction: the visitor added in §6 has **five** present-day concrete uses (translate,
  rotate, scale, and the two duplicate paths), clearing CLAUDE.md rule 2's two-use bar.
- No new store, no data-format change, no dependency, no algorithm swap.
- The refusals add no behaviour — they replace a silent skip with a message.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Should TRIM/OFFSET/JOIN refuse, or silently ignore feature lines as they do today? | — | Not asked. REQ-201 already requires every command to report, so refusing is the spec-compliant reading and no decision is needed. Recorded here so the reasoning is visible rather than assumed. |

## 5. Assumptions

```
ASSUMPTION-1 (inherited from TASK-077, and now RESOLVED DIFFERENTLY THAN PLANNED):
  TASK-077 §5 said "stage 3's move test asserts the elevation points stay collinear after a PI
  moves." That test does not belong in this task, and writing it here would be a false negative.
- Because:       MOVE, ROTATE and SCALE transform EVERY vertex of the feature line by the same rigid
                 (or uniformly scaling) map. Collinearity is preserved by construction — a test
                 asserting it would pass no matter how the code were written, and would therefore
                 prove nothing about the risk ADR-035 (b) actually names.
- The real risk:  moving ONE PI while its neighbours stay put. That is the GRIP path, which is
                 stage 2b and not yet built. The obligation moves there, not away.
- Recorded as:   a test IS still added here (§7, T5) asserting collinearity after MOVE — cheap, and
                 it locks the property in before grips arrive to threaten it — but it is labelled a
                 REGRESSION GUARD, not the validation of ASSUMPTION-1. The validation is owed by
                 stage 2b and is carried forward as an open obligation.
```

```
ASSUMPTION-2: A COPY of a feature line is a new entity and must receive a NEW id, not the source's.
- Because:       REQ-076 makes ids identity. Two entities sharing one id means every id-keyed
                 lookup — surface breakline references (REQ-069) most of all — resolves to whichever
                 comes first in the sweep. A surface would silently track the wrong line.
- Risk if wrong: silent, and it corrupts surfaces rather than geometry.
- Validate by:   §6 BUG-1's test, which fails on today's code.
```

## 6. Plan

### BUG-1 — COPY duplicates entity ids on EVERY entity type (found while surveying, 2026-08-20)

`DuplicateCadSelectionTranslated` and `DuplicateCadSelectionRotated` copy the source's
`EntityAttributes` wholesale, `id` included. `MakeNewEntityAttrs` deliberately leaves `id == 0` so
that `EnsureEntityIds`' sweep assigns a fresh one; the duplicate paths bypass that by carrying a
non-zero id across. Reproduced on a bare LINE:

```
NEW / LINE 0,0 → 10,0 / ESC / BOX / COPY 0,0 → 0,50 / CHECK ALL
  → FAIL [invariant] entity-ids: id 1 is used by both userLineAttrs[0] and userLineAttrs[1]
```

This is **pre-existing and unrelated to feature lines** — it affects lines, circles, arcs, ellipses,
polylines, annotations and filled regions alike. It is fixed here rather than deferred for one
reason: this task adds feature lines to both duplicate paths, and the only way to add them is to
either copy the defect or fix it. Fix: zero `id` on every duplicated attribute record, which is the
mechanism the code already has.

Technical debt note: this went unnoticed because no transcript ran `CHECK ALL` after a `COPY`.
Adding that assertion to the existing modify transcripts is a follow-up, filed in §9.

### The visitor (CLAUDE.md rule 2: seven present-day uses)

As built — the plan's single `ForEachSelectedFeatureLineVertex` split in two once the duplicate
paths were written, because they **append** while they walk rather than mutating in place:

```cpp
/// Ranges are collected FIRST and visited afterwards: two callers grow featureLineOffsets while
/// they work, and walking the table as it reallocates reads freed memory.
template <class Fn> void ForEachSelectedFeatureLine(const AppCommandState&, Fn&&);

/// Mutating form, over vertices. Deliberately offers no Z — MOVE/ROTATE/SCALE are plan operations
/// here, and a transform that could silently alter Z would change the surface a feature line feeds
/// (REQ-069) without touching its plan geometry.
template <class Xf> void TransformSelectedFeatureLinesInPlace(AppCommandState&, Xf&&);

/// Appending form. Validates the whole vertex range BEFORE the first push, so it cannot bail out
/// half way and leave featureLineVerts longer than featureLineOffsets claims.
template <class Xf> void AppendFeatureLineCopy(AppCommandState&, int fi, int v0, int v1, Xf&&);
```

Seven concrete uses: translate, rotate, scale, both duplicate paths, the centroid and the extent
helper — plus a fourth in `TransformPreview.cpp` (`appendFeatureLineStrip`) shared by the three
previews and the selection highlight. That sharing is the point. ADR-035 (g) names a missed case as
this entity's whole risk, and eleven hand-copied CSR walks would be eleven chances to miss one.

### Sites (counted, not remembered)

| # | site | file:line | change |
|---|------|-----------|--------|
| 1 | `ApplyTranslationToSelection` | `CadCommands.cpp:4822` | `+= dx, dy` per vertex |
| 2 | `ApplyRotationToSelection` | `CadCommands.cpp:4703` | `RotateAroundBase` per vertex |
| 3 | `ApplyScaleToSelection` | `CadCommands.cpp:5170` | `ScalePtAroundBase` per vertex |
| 4 | `DuplicateCadSelectionTranslated` | `CadCommands.cpp:4138` | append translated chain, id 0 |
| 5 | `DuplicateCadSelectionRotated` | `CadCommands.cpp:4516` | append rotated chain, id 0 |
| 6 | `ComputeSelectionCentroidWorld` | `CadCommands.cpp:4953` | chain centroid, as polyline |
| 7 | `ComputeMaxSelectionDistanceFromPoint` | `CadCommands.cpp:5043` | max vertex distance |
| 8 | `TransformPreview.cpp` x3 | `:139, :301, :395` | move / rotate / scale rubber-band |
| 9 | refusals | TRIM x2, OFFSET, JOIN, COPYCLIP | count and report, do not skip |
| 10 | `AppendEntityHighlight` | `TransformPreview.cpp` | **added during implementation** — see below |

Sites 4 and 5 must also carry `featureLineClosed`, `featureLineElevPt` and `featureLineInfo` across
— the elevation-point flags in particular, since `EraseFeatureLineByIndex` already showed that the
flag array and the vertex array are cut on different strides and a length mismatch is silent.

Site 10 was not planned. A selected feature line drew **no highlight at all**: it picked, box-selected
and moved correctly while showing the user nothing on screen to confirm what was selected. Strictly
this belongs to stage 2b, but MOVE without it is a command you drive blind, so it is closed here.

## 7. Test approach
Every test is run against unpatched code first and must FAIL there; a regression test that has never
failed is not evidence (verification/verification.md).

| id | asserts | oracle |
|----|---------|--------|
| T1 | COPY of a line yields two distinct ids (`CHECK ALL` clean) | fails today — BUG-1 |
| T2 | MOVE shifts every feature-line vertex, Z unchanged | fails today — silently ignored |
| T3 | COPY yields two feature lines, distinct ids, elevations and closed-flag carried | fails today |
| T4 | ROTATE 90° about origin maps (100,0,12) → (0,100,12) | fails today |
| T5 | elevation points stay collinear after MOVE — REGRESSION GUARD (§5) | passes today (vacuously) |
| T6 | SCALE 2x doubles plan distance and leaves Z alone | fails today |
| T7 | TRIM/OFFSET/JOIN on a feature line report a refusal, not silence | fails today |
| T8 | `CHECK ALL` clean after each, and UNDO restores in one step | — |

**Oracle result.** The transcript was split into its seven sections and every one was run against a
build with `src/` reverted. All seven failed there and all seven pass now:

```
s1 FAIL entity-ids: id 1 is used by both userLineAttrs[0] and userLineAttrs[1]   (BUG-1)
s2 FAIL no log line contains: PI      1: 0.000, 50.000, elevation 10.000         (MOVE)
s3 FAIL no log line contains: Feature line 2: "Pad" ... entity id 2              (COPY)
s4 FAIL no log line contains: PI      2: 50.000, -100.000, elevation 12.000      (ROTATE)
s5 FAIL no log line contains: PI      2: 200.000, 0.000, elevation 12.000        (SCALE)
s6 FAIL no log line contains: PI      1: 10.000, 10.000, elevation 10.000        (elev pts)
s7 FAIL no log line contains: JOIN — 1 feature line ignored                      (refusals)
```

T6 is listed in §5 as passing vacuously once MOVE works; unpatched it fails for the plainer reason
that MOVE did nothing at all. Both statements are true and neither makes it a validation of
ASSUMPTION-1.

### Two test-reachability findings
- **T4's first draft asserted the wrong rotation.** ROTATE takes a bearing **clockwise from north**
  (the survey convention), so 90° sends (100,0) to (0,−100), not (0,100). The code was right. The
  second vertex was then moved off the axis to (100,50), because a vertex landing exactly on an axis
  reads back as `-0.000` and the test would have been asserting printf behaviour, not geometry.
- **TRIM's picks are not headlessly drivable.** `src/ui/CadUi.cpp` routes them straight to
  `SubmitTrimViewportPick`; nothing outside the GUI calls it, so the driver's `PICK` verb does
  nothing at all during TRIM — silently. Every other pick-driven command, OFFSET included, goes
  through `SubmitViewportPick`. This is a REQ-203 gap in **TRIM's whole behaviour for every entity
  type**, not something feature lines introduced. Rerouting a command's input belongs to a task that
  owns TRIM, so this task added a `TRIMPICK` driver verb instead — test infrastructure, no product
  change, and exactly what `BOX` already does when it arms the selection-box fields itself.

## 8. Completion report

```
COMPLETION REPORT — TASK-079 — 2026-08-20
- Requirements satisfied:  REQ-087 (Acceptance met: partly — the "moves" clause, yes; the whole
                           requirement, no: snap, grips, DXF and PDF plot remain in stage 2b, so
                           REQ-087 stays `proposed`). REQ-076 restored by BUG-1. REQ-201 for the
                           four refusals.
- Summary:                 Feature lines move, copy, rotate and scale, with live previews and a
                           selection highlight; TRIM, OFFSET, JOIN and COPYCLIP refuse them out
                           loud instead of ignoring them. Fixed a pre-existing defect where COPY
                           gave every duplicated entity its source's id.
- Tests:                   tests/headless/transcripts/req087-feature-line-modify.txt, 128 steps,
                           7 sections, each verified to fail against reverted src/. 456 ctest
                           cases green.
- Verification verdict:    PASS (findings resolved: BUG-1; findings recorded not fixed: FU-3, FU-4)
- Assumptions:             ASSUMPTION-1 REASSIGNED to stage 2b, not validated here — see §5, and
                           note the task it was inherited from expected otherwise.
                           ASSUMPTION-2 validated by T3.
- Architectural decisions: none made by Workshop. Two were declined and escalated as scope notes
                           rather than taken: what elevation a trimmed/offset/joined feature line
                           carries (refused instead of invented), and rerouting TRIM's input
                           (driver verb instead).
- Dependencies:            none added.
- Technical debt noted:    FU-3, FU-4 below — both REQ-203 reachability gaps that predate this task
                           and both now written down rather than left as unexplained silence.
- Build:                   clean, MSVC 14.50; no new warnings.
- Docs updated:            spec/requirements.md (REQ-087 revision note), this task log.
```

## 9. Follow-ups filed
- **FU-1**: add `CHECK ALL` after `COPY` to the existing modify transcripts — BUG-1 hid behind its
  absence for the entire life of the COPY command, on every entity type.
- **FU-2**: stage 2b still owes snap, grips, DXF and PDF plot, and with them the real validation of
  ASSUMPTION-1 (a single PI moved by a grip, with its elevation points re-projected).
- **FU-3**: TRIM's picks bypass `SubmitViewportPick`, so TRIM is undrivable headlessly for every
  entity type. `TRIMPICK` works around it for tests; the routing itself is still a REQ-203 gap.
- **FU-4**: `COPYCLIP` has no command-line verb — Ctrl+C only — so it too is unreachable from a
  transcript. Its feature-line refusal is therefore **untested**, which is stated here rather than
  implied by its absence. Carrying feature lines through the clipboard properly needs clipboard
  arrays and is a task of its own.
