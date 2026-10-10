# TASK-075 — 3DPOLY: a polyline whose vertices each carry their own elevation

- Type:    feature
- Status:  done
- Opened:  2026-08-19
- Owner:   chetjones003

## 1. Authority
- Goal:         M-Grading step 2 (`spec/roadmap.md`), decision D-2026-08-19-a.
- Requirements: **REQ-085** — promoted `proposed` → `accepted` 2026-08-19 on the user's go-ahead.
                REQ-058 (snap supplies the commit elevation) and REQ-101 (typed-coordinate precision)
                both constrain it. REQ-201 — every action reports.
- Acceptance (REQ-085, verbatim):
  - a `3DPOLY` drawn with three vertices at three different typed elevations stores three different
    Z values, and a `.gs` round trip preserves each;
  - snapping a vertex to a survey point gives that vertex the point's elevation, with ELEV set to
    an unrelated value;
  - the result is accepted by `DESIGNATEBREAKLINE` and the surface honours its per-vertex elevations;
  - the ordinary `POLYLINE` command is unchanged.
- Owning subsystem: `commands/`.

## 2. Scope
- In scope: the `3DPOLY` command, per-vertex elevation entry (`X,Y,Z` and `@dx,dy,dz`), and its tests.
- Out of scope: REQ-086 point files — the other half of M-Grading step 2, still `proposed`.
- Smallest change: a flag on the existing POLYLINE draft. **No new store, no new `Kind`.**

## 3. Architectural boundary check
- **No.** Three findings, each verified at the source rather than assumed, collapse this to an entry
  mode instead of a feature:
  - `userPolylineVerts` is already stride-3 XYZ, so the store needs nothing;
  - `CadCommitElevation` (`CadCommands.hpp:1745`) already returns the snapped point's own Z per click
    (REQ-058), so a snapped 3DPOLY needs no new code at all;
  - the two commands differ **only** in where a vertex's Z comes from.
  A separate `Kind` would have had to be added to both viewport dispatch lists, the status text, the
  prompt table and the ESC path — the twelve-site tax, paid for no behavioural difference, and the
  first of those lists is exactly where PR #65 and TASK-073 both found silent hangs. A flag on the
  existing draft avoids all of it.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Alias `3P`, as AutoCAD uses? | 2026-08-19 | **No** — `3p` is already CIRCLE's three-point option (`CadCommands.cpp:3307`). Aliases are `3dp` / `3dpolyline`, so nothing reads two ways depending on what is active. |

## 5. Assumptions
```
ASSUMPTION-1: `X,Y` with no third field means "use the ordinary commit elevation", not "error".
- Because:       REQ-085 says a vertex elevation "may be typed", not must be. Requiring Z on every
                 vertex would make the common case — snapping along a ridge — harder than POLYLINE.
- Risk if wrong: a user expecting to be forced to state Z gets the work plane silently.
- Validate by:   every vertex REPORTS the elevation it committed at, so the value is never invisible.
                 Covered by the transcript's ELEV section.
```

## 6. Plan  (as built)
- `polylineDraft3d` + `polylineTypedZ{,Valid,Relative}` on `AppCommandState`.
- `StartPolyline3dCommand` = `StartPolylineCommand` + the flag (set *after*, since the reset inside
  clears it).
- The typed-input path peels a trailing `,z` off before the shared 2D parser sees the string.
- `SubmitPolylineVertex` prefers a typed Z, else `CadCommitElevation`, and **consumes** the typed Z
  so it cannot leak into the next vertex.

## 7. Workflow-specific notes (Feature)
- Tests-first? No — transcript written alongside. The command is observable enough that after-the-fact
  tests are honest here, unlike TASK-074's legacy-`.gs` case.

## 8. Implementation log
- 2026-08-19 The shared 2D point parser (`ParseWorldPointD` → `ParseTwoDoubles`) is REQ-101-critical
  and used by every command, so it was **left alone**; the third field is peeled off before it. That
  is the whole reason this change is small and low-risk.
- 2026-08-19 First attempt shadowed the `line` parameter with the peeled string. Ill-formed — the
  name was already used earlier in the same block — and caught before building. The parse call takes
  `pointText` explicitly instead.
- 2026-08-19 **Found while checking my own edge case:** `ParseTwoDoubles` reads two numbers and
  ignores whatever follows, with no trailing-junk check. So `1,2,3,4` would land silently at (1,2).
  3DPOLY now refuses a wrong field count outright. See the finding below — the same silent drop
  applies to LINE / POLYLINE / RECT and is NOT changed here.
- 2026-08-19 Every vertex reports its committed elevation, which is what makes the transcript able to
  assert on elevations at all — otherwise they are invisible until the file is saved.

## 9. Self-verification
- [x] build-project        — PASS (clean; no new warning)
- [x] architecture-review  — PASS (no new store, no new Kind, shared parser untouched)
- [x] code-review          — PASS
- [x] dependency-audit     — n/a
- [x] performance-review   — n/a
- [x] testing              — PASS (446 ctest cases; `req085-3dpoly.txt`, 55 steps)

## 10. Verification result
- Submitted: 2026-08-19
- Verdict:   **PASS**, with one acceptance condition explicitly not automated (below).
- Coverage gap, recorded not hidden: **the snap condition has no automated test.** Snapping cannot be
  driven from the REQ-203 driver — `viewportSnapPickValid` is set by the UI hover path, which the
  driver has no equivalent of. The code path is `CadCommitElevation`, shared verbatim with POLYLINE
  and already covered by REQ-058. Stated in REQ-085's revision note as well as here.

### Finding NOT fixed — the shared 2D parser silently ignores extra coordinates
`ParseTwoDoubles` extracts two numbers and never checks what follows, so `LINE` given `100,50,25`
commits (100,50) and drops the 25 with no message — as do POLYLINE, RECT and every other command
through `ParseWorldPoint`. It is pre-existing and orthogonal to this task; fixing it means changing
input handling for every command at once, which is its own change with its own regression risk. Left
as a finding rather than folded in. 3DPOLY itself is strict.

## 11. Outcome
- Requirements satisfied: REQ-085 (Acceptance met: yes, 3 of 4 automated — see §10).
- Tests added: `tests/headless/transcripts/req085-3dpoly.txt` (55 steps).
- Architectural decisions: none made by Workshop.
- Technical debt: none introduced.
- Docs updated: this log; REQ-085 promoted to `accepted` with its coverage gap recorded.
- Done: 2026-08-19.
