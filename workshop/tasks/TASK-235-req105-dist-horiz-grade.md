# TASK-235 — DIST reports horizontal distance and grade (REQ-105 amended)

- Type:    feat (amendment to an accepted requirement)
- Status:  review
- Opened:  2026-09-09
- Owner:   Workshop
- GitHub:  #149 acceptance 1 (3D Phase 6 — Analysis). REQ-105's DIST scope came from #382.

## 1. Authority

- **REQ-105** — accepted for DIST 2026-09-07 (GitHub #382); **amended here** (D-2026-09-09-f) to add
  the horizontal distance and the grade, with acceptance bullets of its own.
- **REQ-074** — accepted 2026-08-12. Not changed. It is the source of the wording, and the reason
  this amendment invents no vocabulary.
- **REQ-101** — ±0.002 ft since D-2026-09-08-i. All four figures must agree with hand-computation
  within it; see §5 for the one place this task knowingly does not use that number.
- **REQ-201** — refuse with a stated reason rather than acting silently. Both degenerate cases here
  are refusals, not divisions.
- Constraints: CON-06 smallest change; no new layer, dependency or abstraction.
- Owning subsystems: Commands (the command), tests (unit + transcript), spec.

## 2. Problem

GitHub #149 is Phase 6 of the 3D programme, and its acceptance 1 reads:

> 3D distance, horizontal distance, vertical difference and grade are each reported and agree with
> hand-computed values within REQ-101.

These are the four numbers a surveyor reads together. `DIST` reported **one** of them — the slope
(3D) distance — plus the three deltas:

```
DIST — dX = 3.0000  dY = 4.0000  dZ = 3.0000  slope dist = 5.8310
```

No horizontal distance, and no grade. The vertical difference was present only as `dZ`, which is the
same number but not named as the surveyor names it.

**The gap was narrower than the issue implies**, which is why this is an amendment and not a new
requirement: `SURFELEV` (REQ-074) has reported grade, run:rise, horizontal and vertical between two
picks **on a surface** since 2026-08-12, in a settled format. What was missing was the same four
numbers between two *arbitrary* points.

## 3. What was built

One `snprintf` and a three-way branch in `CommitDistSecondPoint`, emitting a **second log line**.

| case | line |
|---|---|
| ordinary | `DIST — grade 60.00%  slope 1.67:1  horiz 5.0000  vert 3.0000` |
| no vertical difference | `DIST — level (0.00%)  horiz 50.0000  vert 0.0000` |
| no horizontal separation | `DIST — vertical: horiz 0.0000  vert 18.0000. No grade.` |

Three decisions inside that, all recorded in D-2026-09-09-f:

**(a) The wording is REQ-074's, verbatim.** `grade <n>%  slope <n>:1  horiz <n>  vert <n>`, and
`level (0.00%)` when flat. Not "consistent with" — the same format string shape, so a user who picks
one pair of points in `SURFELEV` and the same pair in `DIST` cannot be shown one slope described two
ways. Inventing a second phrasing was the easy thing to do here and would have been wrong.

**(b) The grade goes on its own line.** DIST's accepted line already says `slope dist` for the 3D
distance; REQ-074's ratio token is `slope <n>:1`. Both in one sentence is two meanings of "slope" a
few characters apart. Two lines also leave the accepted line **byte-identical**, so this amendment
is purely additive and nothing that greps the old output changes behaviour.

**(c) Both degenerate cases are refusals with a stated reason, not divisions.** A vertical pair has a
zero run in the denominator; a level pair has a zero rise in the run:rise ratio. Each says what it is
instead. REQ-201's shape, and REQ-074 already handles the level half the same way.

`run` and `rise` are taken **in the active UCS**, the frame the deltas above them are already
reported in — so under a tilted UCS "horizontal" means horizontal in the frame the user is working
in. That is the only reading under which the four numbers on the two lines are consistent with each
other.

## 4. Tests

| case | where | why there |
|---|---|---|
| grade + run:rise + horiz + vert, hand-computed | `DistCommandTests [req105]` | 3-4-5 scaled: run = 5, rise = 3, so 60.00% and 1.67:1 are **exact** at display precision rather than rounded |
| level pair | unit **and** `headless.req105-dist-horiz-grade` | the one case a transcript can drive end to end — see below |
| vertical pair | `DistCommandTests [req105]` | needs two points at one plan position and different elevations |
| `horiz` is a distance, not a delta | transcript | picks dX = −18, dY = 0: an implementation reporting dX would print −18.0000 and pass everything else |
| the accepted line is unchanged | transcript | asserted as an exact string, so a future edit to it has to be deliberate |

**Why the graded case is a unit test and not a transcript, stated rather than left as a hole:** typed
coordinate entry is two-dimensional everywhere except 3DPOLY — REQ-085 peels a third component only
for that command, on the recorded grounds that widening the shared REQ-101-critical parser to serve
one command would put every other command's coordinate handling at risk. `ELEV` sets the current
elevation, and DIST consumes the command line for its own point prompts, so it cannot be changed
*between* a DIST's two points. A transcript therefore cannot give one pick a different elevation from
the other without snapping onto existing 3D geometry. The unit tests drive
`ProcessCommandLineSubmit` — the same entry point the transcript uses — and vary Z through
`resolvedPointZ`, which is what `CadCommitElevation` reads.

The `%%` escape and the em dash both survive to the rendered string: the transcript asserts
`"DIST — level (0.00%)  horiz 50.0000  vert 0.0000"` as an exact match, so the output is pinned as
the user sees it rather than as the format string spells it.

## 5. Assumption, and one knowingly-unresolved tension

```
ASSUMPTION-1: Below 0.01 ft of horizontal separation, DIST reports a pair as vertical.
- Because: SURFELEV (REQ-074) already calls two picks closer than this "the same location" and
  refuses a grade for them. A user picking one pair of points in both commands must not be told a
  grade exists in one and not the other.
- Risk if wrong: between REQ-101's +/-0.002 ft and this 0.01 ft, DIST calls a pair vertical while
  their plan separation is still measurable. That is the conservative end -- the alternative reports
  grades in the tens of thousands of percent -- but it IS two accepted requirements disagreeing
  about a threshold.
- Validate by: DistCommandTests' vertical case; and by chetjones003 deciding whether REQ-074 and
  REQ-105 should converge, and on which number. Escalated in D-2026-09-09-f rather than settled here:
  moving SURFELEV to +/-0.002 ft would change REQ-074's behaviour on real surfaces to serve a command
  that did not exist when it was written, which is a bigger change than this amendment should make.
```

The value is duplicated under DIST's own name, `kDistVerticalRunFt`, rather than referenced as
`kTinPlanEpsilon`. That constant's name means "the TIN builder treats these as one shot", and DIST
has nothing to do with a TIN — depending on it would be borrowing a predicate whose name describes
another subsystem's concern, which is exactly how an upstream redefinition silently changes a branch
here. One number, two names, and the dependency stated in a comment at each end.

## 6. One stale record corrected in passing

`spec/requirements.md`'s **summary traceability table** still had REQ-105 as
*"proposed — not yet scoped"*. The requirement body has said **accepted (DIST only — issue #382)**
since 2026-09-07. The two sit ~4,500 lines apart and had drifted for two days.

Corrected here because this task amends REQ-105 and would otherwise have left the table describing a
requirement that is now built twice over. The row now records what is accepted, what is built, which
tests cover it, and that AREA / LIST / MASSPROP remain proposed and unbuilt — so the table stops
understating the requirement without overstating it either.

## 7. Verification result

- **build-project** — clean release build, MSVC/Ninja, no new warnings.
- **architecture-review** — no new layer, global, dependency or abstraction. One file-scope constant
  added next to its only caller; no header changed.
- **code-review** — the format strings, the level branch and the same-location refusal are REQ-074's,
  reused rather than re-derived. The one place a value is duplicated instead of shared is argued in
  §5 rather than done quietly.
- **dependency-audit** — none added.
- **performance-review** — one `std::hypot` and one `snprintf` per completed DIST, a command the user
  invokes by hand.
- **testing** — full suite **1368/1368 green**, including four new cases and the pre-existing DIST
  tests (one adjusted: the distances line is now second-from-last in the log, and the test reads it
  by position rather than as `log.back()`).

COMPLETION REPORT — TASK-235 — 2026-09-09
- Requirements satisfied:  REQ-105 as amended (D-2026-09-09-f); GitHub #149 acceptance 1
- Summary:                 DIST reports the surveyor's four numbers, in REQ-074's words
- Tests:                   3 new unit cases + 1 new transcript; 1 existing case adjusted; 1368/1368
- Verification verdict:    PASS
- Assumptions:             ASSUMPTION-1 (§5), with its unresolved tension escalated in the decision
- Architectural decisions: none; one constant duplicated by name, argued in §5
- Dependencies:            none added
- Technical debt noted:    the REQ-074 / REQ-105 threshold mismatch (§5) — chetjones003's call
- Build:                   reproducible, clean on Windows/MSVC
- Docs updated:            REQ-105 statement + acceptance + revisions, its traceability row,
                           spec/project.md decision D-2026-09-09-f, this task log
