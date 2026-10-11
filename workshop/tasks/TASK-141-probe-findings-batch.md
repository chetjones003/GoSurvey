# TASK-141 — Probe findings batch: elevation, space scoping, import precision, and the test oracle

- Type:    fix (batch)
- Status:  review
- Opened:  2026-08-28
- Owner:   Workshop

Source: nine findings from the hands-on learning programme (probes 14–35), written up in
`Desktop\Issues found\`. Not filed as GitHub issues — the user files those on his own timing.

## 1. Authority

- Goal:         GOAL-02 (a drawing is what the surveyor measured)
- Requirements: all **existing and accepted**. No new REQ was drafted, because every item here is a
                defect against a requirement that already says the right thing:
                REQ-057 / ADR-025 (elevation is a stored property, absolute);
                REQ-039 (paper edits do not change model geometry);
                REQ-041 / REQ-083 (point import), REQ-101 (coordinate tolerance);
                REQ-068 / ADR-036 (a surface is display-only and refusals must be visible);
                REQ-201 (refuse with a stated reason rather than acting silently).
- Constraints:  CON-06 smallest change; no new layer; no new dependency
- Owning subsystems: Commands (elevation, space scoping, registry), IO (CSV import),
  tests/headless (the oracle), UI to Commands (one relocation)

## 2. What was fixed

| # | finding | outcome |
|---|---|---|
| 06 | harness cannot assert a coordinate; 3 commands undriveable | **fixed** — `EXPECT VERTEX` / `EXPECT ELEVATION`, `EXPORT POINTS`, `HATCH` + `PDFATTACH` click routes |
| 01 | eight editing commands discard elevation | **fixed** — BREAK / FILLET / CHAMFER / JOIN / OVERKILL, lines and polylines |
| 02 | OVERKILL and JOIN reach into model space from a sheet | **fixed** — refuse, matching ARRAY's existing guard |
| 03 | CSV point import loses up to 0.123 ft per point | **fixed upstream during the rebase** — ADR-054 Phase F (#447 / #461) widened `SurveyPoint` to `double`. This task's own fix was measured redundant and deleted; only its transcript remains. See §8b |
| 08 | JOIN and OVERKILL skip surfaces silently | **fixed** — both now name what they left out |
| 05 | QUICKSELECT untestable; SELECTSIMILAR unreachable | **fixed** — function moved to the command layer; registry entry added |
| 09 | DIMANGULAR not persisted | **already fixed upstream** (PR #127, `63ab63c`) — no change |
| 07 | update dialog buttons clipped | **already fixed upstream** (`ec2fe54` and successors) — no change |
| 04 | REQ-064 statement gaps (per-viewport style, optional edges) | **NOT fixed — escalated, see section 5** |

## 3. Architectural boundary check

- [x] **One relocation, recorded here rather than done silently.** `ExecuteQuickSelect` moved from
  `src/ui/CadUi.cpp` (static) to `src/commands/CadCommands.cpp`. Dependencies flow the right way
  afterwards — it was the only `Execute*` outside the command layer, 1 of 12 — and it touches no
  ImGui at all, so this removes a layering violation rather than creating one. No behaviour change.
- [x] **One behaviour change that is a product decision, flagged in section 5**: OVERKILL's duplicate
  test now counts elevation.
- No new dependency, no new abstraction, no new layer. `PolyVert` is a three-float struct replacing
  `std::pair<float,float>` in two file-local helpers — it removes the place the bug lived rather than
  layering over it.

## 4. Assumptions

```
ASSUMPTION-1: A cut point's elevation is the linear interpolation along the segment it fell on.
- Because: BreakPoint already carries `param` and `segIndex`, which say exactly where the cut landed;
  interpolation is exact on a level segment and correct on a sloped one
- Prior art was looser: ApplyBreakToLine carried the two ENDPOINT elevations through without
  interpolating at the cut. That is now consistent — the cut interpolates everywhere
- Risk if wrong: a sloped polyline's cut vertex sits where the slope puts it rather than at an end
- Validate by: regression-issue01, which draws at ELEV 286.715 and asserts every vertex

ASSUMPTION-2: A fillet ARC spanning two elevations takes their midpoint.
- Because: CadArc holds ONE elevation and stays parallel to XY (REQ-057 / ADR-025); a tilted arc
  would need a plane normal no accepted requirement asks for
- Exact whenever the two tangent points are level with each other, which is the ordinary case
- Risk if wrong: a fillet across a grade break sits at the mean rather than sloping
- Validate by: regression-issue01's FILLET section. The CHAMFER connector is a Line and DOES slope,
  because a Line can hold two elevations and an arc cannot

ASSUMPTION-3: The CSV pre-scan uses kLargeCoordinateRebaseThreshold (1e5), not the DXF entity
              pre-scan's own 1e4.
- Because: it has to agree with the MaybeRebaseLargeCoordinates call immediately after it — one rule
  for "far from the origin", not two that can disagree inside one function
- Risk if wrong: a drawing between 1e4 and 1e5 still narrows at magnitude (a float steps 0.0078 ft at
  5e4). Whether 1e5 is the right value at all is a separate and deliberate question
- Validate by: regression-issue03 at state-plane magnitude (2.39e6), byte-identical round trip
```

## 5. Escalations — decisions that are NOT the Workshop's to make

**(a) Issue 04 / REQ-064 — a requirement that claims more than the build does.** REQ-064's Statement
says the visual style "is per-viewport state ... each paper-space viewport carries its own", and that
Shaded is filled triangles "plus optional edges". Neither is built, and neither appears in REQ-064's
acceptance list — which is exactly how the requirement can read as "fully delivered". Nothing is
broken; the feature works well in the cases it covers (occlusion, camera-following lighting and the
2D round trip were all confirmed by pixel-diffing). The choice is **build the two clauses** —
per-viewport style is moderate, the real work being that `RenderTuning::visualStyle` is assigned once
per frame in `main.cpp` and would have to become per-viewport, while `struct Viewport` already carries
`frozenLayers` and `vpColorLayers` so the override pattern exists there twice over — or **amend
REQ-064 to describe what was actually built**. Either is defensible. Leaving it as-is is the option
worth avoiding, because the next person to read the requirement will assume it works. **Not started:
this is a Specification decision, not a Workshop one.**

**(b) OVERKILL's duplicate test now counts elevation.** Two segments identical in plan at different
heights — a fence line and the contour beneath it — used to be duplicates, and one was deleted. They
are now different objects. This is the safer of the two defaults and it is what stops the silent data
loss, but it **is** a behaviour change: the same drawing cleaned before and after this will differ.
AutoCAD exposes the alternative as an explicit **Ignore Z** option; that option is **not** built here.
If chetjones003 wants Ignore Z it is a small follow-up, and this decision should be recorded either
way rather than left as a consequence of `LSeg` having gained two floats.

**(c) The `.gs` loader has issue 03's ordering too, and was deliberately left alone.** `GsIo.cpp`
narrows coordinates to float while the document origin may still be 0, exactly as the CSV importer
did. It is **latent, not live**: a `float` wrote the file, so it cannot contain a value a `float`
cannot hold, and a legacy `.gs` rebases losing exactly zero. Fixing it properly means pre-scanning a
dozen different coordinate arrays before parsing any of them, and missing one would produce a *wrong*
origin — trading a dormant defect for a live one. It becomes real the day `.gs` coordinates become
`double`, which is the natural direction for large-coordinate drawings. **Recorded rather than
fixed**, so whoever widens that storage finds this note.

**(d) Space guards were added to the two commands that were measured, not to every candidate.**
Because a model selection survives `SPACE PAPER`, *any* selection-driven model command is reachable
from a layout. Fourteen commands already carry the guard; OVERKILL and JOIN were the two found
missing it. A full audit of the rest is worth about an hour and was not done here.

## 6. Tests

Six new transcripts, each named for the finding it pins:

| transcript | steps | what it can fail on |
|---|---|---|
| `regression-issue06-coordinate-oracle` | 38 | the oracle itself, plus the HATCH route |
| `regression-issue01-editing-keeps-elevation` | 101 | every command in the issue-01 matrix |
| `regression-issue02-editing-respects-space` | 27 | the refusal AND that model geometry is untouched |
| `regression-issue03-csv-import-precision` | 6 | a byte-identical state-plane round trip |
| `regression-issue05-selectsimilar` | 22 | SELECTSIMILAR narrows by layer and colour, not just type |
| `regression-issue08-surface-skips-are-reported` | 20 | both disclosures |

**Red-before verified by measurement, not assertion**, for issue 03: with `SurveyCsv.cpp` stashed and
the tree rebuilt, the round trip returns `1993804.6250` for an input of `1993804.6540` — reproducing
the figures in the issue write-up exactly, to the digit. The elevation family's before-state is
measured in `Desktop\Issues found\01-...` and `Notes for claude\z-flattening-sweep\`.

**Why these needed a new oracle at all.** A drawing with every Z replaced by 0 is correctly strided,
entirely finite, unchanged in count and unchanged in id — so it passes `CheckDocumentInvariants`,
which is a *corruption* oracle answering the question it was built to answer. That is how 640 tests
stayed green over eight commands discarding elevation, a CSV importer losing 0.123 ft on every point,
and two commands editing a space the user could not see. "The tests pass" was a true statement about
the oracle and not about the code, and the useful follow-up question is always: what would this
oracle be unable to see?

## 7. Verification result

Self-run against `verification/skills/`:

- **build-project** — clean full build, MSVC/Ninja release, no new warnings.
- **architecture-review** — one layering violation removed, none introduced; no new layer, global,
  dependency or abstraction. The one relocation and the one behaviour change are both recorded above
  rather than made silently.
- **code-review** — every fix follows a convention the codebase already had and had applied
  elsewhere: `CadCommitElevation` (18 call sites, RECT as the model), ARRAY's paper-space guard (14
  call sites), JOIN's own feature-line disclosure, and the DXF importer's establish-the-origin-first
  ordering. None of these is a new idea; each is a place that bypassed an existing one.
- **dependency-audit** — none added.
- **performance-review** — the CSV pre-scan is one extra parse of the imported file, taken
  deliberately over buffering every parsed row because the import loop assigns auto-ids as it goes
  and skips duplicates against points it has already pushed. OVERKILL gains one elevation comparison
  per candidate pair inside tests that already run.
- **testing** — full suite **743/743 green**, including every pre-existing test.

COMPLETION REPORT — TASK-141 — 2026-08-28
- Requirements satisfied:  defects against REQ-039 / 041 / 057 / 068 / 083 / 101 / 201, all accepted
- Summary:                 six of nine findings fixed, two were already fixed upstream, one escalated
- Tests:                   6 new transcripts (214 steps total); ctest 743/743 green
- Verification verdict:    PASS for what was built; SPEC GAP raised for issue 04 (section 5a)
- Assumptions:             ASSUMPTION-1..3 above, all validated by the transcripts
- Architectural decisions: one relocation (section 3); one behaviour change escalated (section 5b)
- Dependencies:            none added
- Technical debt noted:    sections 5b, 5c, 5d
- Build:                   reproducible, clean on Windows/MSVC
- Docs updated:            this task log

---

## 8. Rebase onto `beta`, 2026-09-09

PR #130 was **auto-closed on 2026-08-29** when `beta` was briefly deleted and recreated — collateral
of that deletion, not a review verdict. Nothing in it was rejected and no reviewer comment was ever
left. The work sat unmerged for eleven days; this section records what changed in bringing it
forward, because the branch was authored against a `beta` that has since moved a long way.

**Every finding was re-verified against current `beta` before rebasing**, rather than assumed still
present. Five of the six are still live and unfixed:

| finding | state on `beta` at 31c907c |
|---|---|
| 01 — elevation, OVERKILL | `struct LSeg { double x0, y0, x1, y1; … }` — still XY-only, still rebuilds `userLinesFlat` unconditionally |
| 01 — elevation, BREAK / FILLET / CHAMFER | six `std::vector<std::pair<float, float>>` vertex records remain in the model- and paper-space helpers |
| 01 — elevation, **JOIN** | **fixed upstream** — see below |
| 02 — space scoping | no sheet guard on OVERKILL or JOIN |
| 03 — import precision | **fixed upstream mid-rebase** — see §8b |
| 05 — SELECTSIMILAR | still absent from the command registry |
| 08 — surface skips | still unreported |

### JOIN's elevation half is now upstream's, and this branch yields to it

Issue **#373** gave `ExecuteJoinSelection`'s `Edge` its own `z0`/`z1` and carried Z through the
cluster representatives, which is what this task's JOIN change did. Upstream's version is the one
kept: it also carries REQ-316/ADR-047 bulges and REQ-325/ADR-053 tilted-arc planes, which this
branch predates and knows nothing about. Six of the eleven rebase conflicts were that overlap, and
all six were resolved in upstream's favour. **This branch no longer changes JOIN's elevation
handling at all** — only its space guard (issue 02) and its surface-skip message (issue 08).

### Three adaptations the eleven days made necessary

1. **ADR-054 (REQ-101 ±0.002 ft) landed underneath this work**, widening the coordinate stores to
   `double` in five PRs (#446, #448, #449, #451, #454). Three of this branch's records read from
   those stores into `float` fields and became ill-formed narrowing conversions. Each is now an
   explicit `static_cast` with the reason written at the declaration (`PolyVert`, and the
   `EXPECT VERTEX` oracle's collection) rather than a silent one: both sit downstream of interfaces
   that are still `float`-in — `readVert`, `CadCoord::WorldFromLocal` — so widening the record alone
   would move the narrowing one line down and buy nothing. **Widening those paths is ADR-054's own
   audit to make, not this fix's.**
2. **Issue #264 Stage 1 retired the standalone `.gs` document format** while this was parked.
   `regression-issue08` opened `samples/surface-demo.gs`; it now opens the `.dwg` every other surface
   transcript uses.
3. **`QUICKSELECT` gained `BlockRef` support upstream** (three sites) after this branch moved
   `ExecuteQuickSelect` from `CadUi.cpp` to the command layer. The relocated copy carries those
   three additions forward; the move would otherwise have silently reverted them.

### Verification after the rebase

- Clean release build, MSVC/Ninja. Two orphaned locals in `ApplyBreakToOpenPolyline` (`v0`, `v1`,
  unused once the helper reads through `PolylineVertsOf`) removed rather than left warning.
- **ctest 1369/1369 green** — up from 743/743 when the task was written, so the suite this now
  passes is nearly twice the one it was verified against.
- **Issue 03 was re-proven to bite** against `beta` at `31c907c`, not merely asserted: disabling the
  pre-scan and rebuilding turned `regression-issue03` red on a *value* difference, with the round
  trip wrong on four of five points —

  | point | column | in the file | without the fix | lost |
  |---|---|---|---|---|
  | 446 | E | 2385261.0250 | 2385261.0000 | 0.025 ft |
  | 447 | E | 2385330.8350 | 2385330.7500 | 0.085 ft |
  | 448 | E | 2385331.1420 | 2385331.2500 | **0.108 ft** |
  | 449 | E | 2385261.3320 | 2385261.2500 | 0.082 ft |

  Against the tolerance that now applies, **0.108 ft is 54× REQ-101's ±0.002 ft** — when this task
  was written REQ-101 was ±0.01 ft and the same error was 10.8×.

  **And then ADR-054 fixed it outright, mid-rebase.** See §8b.
- The `.gitattributes` `eol=lf` pin (the branch's second commit) proved itself during the rebase for
  the second time and for the same reason: the fixture was already in the working tree as CRLF, so
  the attribute did not apply until the file was re-checked-out. `SAMEFILE` failed at byte 9 on a
  six-byte length difference — one byte per line — which is exactly the failure that commit exists
  to prevent, and a fresh CI checkout would not have seen it.

---

## 8b. Second rebase, same afternoon — and issue 03's fix deleted

The PR opened against `beta` at `31c907c` came back `CONFLICTING` within a minute: three more
commits had landed while the first rebase was being verified, the last of them **fifteen minutes
earlier**.

| | |
|---|---|
| `8e5a9f8` | Phase E — 0.01 → 0.002 tolerance sweep (#454) |
| `2f0e4eb` | DWG-trailer state-plane + legacy-float round trip (#459) |
| `a32e613` | **Phase F — `SurveyPoint` easting / northing / elevation → `double` (#447, #461)** |

Phase F is the fix for issue 03. `SurveyCsvImportFile` now reads

```cpp
pr.pt.easting  = pr.worldE - st.worldDocumentOriginX;   // no cast at all
pr.pt.northing = pr.worldN - st.worldDocumentOriginY;
```

against a `SurveyPoint` whose members are `double`. There is no narrowing left for a pre-scan to get
in front of.

**Measured rather than assumed, because the same claim had been true four hours earlier:** the
pre-scan was disabled and the tree rebuilt against the new `beta`. `regression-issue03`'s round trip
came back **byte-for-byte exact on all five points**, where the same experiment against `31c907c` had
lost up to 0.108 ft. The only remaining difference was the CRLF artefact §8 already describes.

**So the pre-scan was deleted, not shipped.** It costs a second full parse of every imported CSV
file, and it now buys nothing: `src/io/SurveyCsv.cpp` is byte-identical to `beta` and drops out of
this task's diff entirely. Keeping a redundant fix because it was already written is how a codebase
acquires two mechanisms for one problem — which is the argument this repo already makes for having
exactly one plane type and one document origin.

**The transcript stays.** It was red on the pre-Phase-F kernel and is green now, so it is the
end-to-end round-trip guard the migration does not otherwise have, and it only exists at all because
this task's `EXPORT POINTS` driver support made the CSV round trip expressible. Its header now says
which change actually fixed it, so nobody reads it as evidence for a fix that is no longer there.

One more relocation hazard caught, of the same shape as the `BlockRef` one in §8: Phase F also
narrowed `ExecuteQuickSelect`'s three survey-point property cases at the comparison
(`matchNum(static_cast<float>(sp.easting))` and its two neighbours). The relocated copy carries those
casts forward. Without that, moving the function would have silently reverted a change made fifteen
minutes earlier, through a clean merge and a clean build — the [[gosurvey-borrowed-predicate-drift]]
failure mode exactly.

**Final state: ctest 1369/1369 green**, five findings fixed, four now upstream's.

### Two earlier notes this supersedes

- **ASSUMPTION-3** (§4) was about the threshold the CSV pre-scan should use. There is no pre-scan any
  more, so the assumption is withdrawn rather than validated. The question it raised in passing — that
  `kLargeCoordinateRebaseThreshold` is 1e5 while a float already steps 0.0078 ft at 5e4 — belongs to
  ADR-054, not here, and Phase F has made it moot for survey points specifically.
- **Escalation (c)** (§5) recorded that `GsIo.cpp` had the same establish-the-origin-late ordering,
  left alone because it was latent, and said it "becomes real the day `.gs` coordinates become
  `double`". That day did not come: **issue #264 Stage 1 retired the standalone `.gs` document format
  outright** while this branch was parked. The escalation is closed by deletion of its subject.
