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
| 03 | CSV point import loses up to 0.123 ft per point | **fixed** — origin derived from the incoming rows |
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
