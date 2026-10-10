# TASK-076 — A point file as a surface data source

- Type:    feature
- Status:  done
- Opened:  2026-08-19
- Owner:   chetjones003

## 1. Authority
- Goal:         M-Grading step 2b (`spec/roadmap.md`), decision D-2026-08-19-a.
- Requirements: **REQ-086** — promoted `proposed` → `accepted` 2026-08-19. Builds on REQ-069 (surface
                definition + dynamic rebuild), REQ-083 (`.csv`/`.txt` point files), REQ-075 (the
                panel node), REQ-001 (reject, never absorb), REQ-201 (report).
- Constraints:  architecture §8 rule 1 — the rebuild worker touches no `AppCommandState`; CON-07.
- Acceptance (REQ-086, verbatim):
  - a surface built from a linked file has the file's points in its triangulation and the drawing's
    survey point count is unchanged;
  - editing the file and rebuilding changes the triangle count;
  - breaking the link creates survey points and a point group, and the surface still builds
    identically afterwards;
  - a missing file is named in the log, the surface is marked not-current, and the previous
    triangulation is retained;
  - a `.gs` round trip preserves the link, and a legacy `.gs` with no such array loads unchanged.
- Owning subsystem: `commands/` + `io/`, with the panel node in `ui/`.

## 2. Scope
- In scope: linked point files as a surface source, the break-the-link import, the commands, the
  panel node, `.gs`.
- Out of scope: watching the filesystem for changes (a rebuild re-reads; nothing polls). Point file
  formats beyond REQ-083's.

## 3. Architectural boundary check
- **New public function in `io/SurveyCsv`** — `SurveyCsvReadPointsOnly`. Declared rather than slipped
  in, but not a SPEC GAP: it is inside the subsystem that owns point-file parsing, and it exists
  precisely so a linked file and an imported file **share `ParseDataRow`**. Duplicating the format
  reader would have been the architectural problem.
- **New field on a persisted struct** — `CadSurface::sourcePointFiles`. Additive; a legacy `.gs`
  simply lacks the array. Same declaration rule as TASK-074's.
- No new entity, no new store, no new dependency.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Store the column layout with the link, which REQ-086's statement did not mention? | 2026-08-19 | **Yes.** A point file does not describe its own column order; a link that re-guessed would swap northing for easting on reload. Recorded in REQ-086's revision note. |

## 5. Assumptions
```
ASSUMPTION-1: A file that cannot be read ABANDONS the rebuild rather than building without it.
- Because:       REQ-086 says the previous triangulation is retained "rather than silently
                 shrinking", which is only true if the build does not run on what survived.
- Risk if wrong: a user who deleted a file on purpose sees a stale surface until they unlink it.
- Validate by:   the missing-file fixture asserts the triangle count does NOT fall back to the
                 no-file baseline. Covered.
```

## 6. Plan  (as built)
- `SurveyCsvReadPointsOnly` — parses to world doubles, touches no drawing state.
- `CadSurfacePointFile { path, layoutIndex, skipFirstRow }` on `CadSurface`.
- `ResolveSurfaceInputs` reads each linked file **on the UI thread**; the worker stays pure.
- `SurfaceBuildInputs::inputsIncomplete` stops both the sync and async paths before triangulating.
- Commands `SURFACEADDFILE`, `SURFACEIMPORTFILE`, and `UNDESIGNATE … POINTFILE …`.
- Panel: the Point Files node, an Add Point File dialog with Browse + layout, and per-item
  "Import into drawing" / "Remove link" — both routed through the command line so the panel and the
  commands cannot disagree.

## 7. Workflow-specific notes (Feature)
- Tests-first? The missing-file case was, in effect: it cannot be produced through the UI at all
  (SURFACEADDFILE refuses an unreadable path up front), so it needed a hand-built fixture before the
  behaviour could be checked.

## 8. Implementation log
- 2026-08-19 The linked read shares `ParseDataRow` with the importer, so a file a surface links to
  and the same file imported cannot disagree about what it contains.
- 2026-08-19 Coordinates come out of the file already in **world double**, which is the frame the
  triangulator wants — no local round trip, and no origin subtraction to get wrong.
- 2026-08-19 **First cut re-read a missing file every frame.** `TickSurfaceRebuilds` re-resolves any
  surface whose `builtAtRevision` is behind, so leaving the revision un-advanced on failure meant
  reopening a missing path 60×/second. Fixed by advancing the revision AND adding
  `CadSurface::lastBuildIncomplete`, which carries the not-current state instead; the retry now comes
  with the next drawing change or an explicit rebuild.
- 2026-08-19 **Bug found by a failing test, not by reading.** The transcript's `EXPECT SURVEYPOINTS`
  showed the break-the-link import adding zero points: the fixture's point ids collided with the
  drawing's, and REQ-083 skips duplicate ids. My code broke the link anyway — which would have
  **silently deleted the file's whole contribution**, since the link was the only thing still
  supplying those points. Now refused with an explanation when nothing imported. The fixture was
  renumbered to 9001+ as well, but the guard is the real fix.
- 2026-08-19 The missing-file fixture (`samples/missing-point-file.gs`) is a real saved drawing whose
  stored link path was edited to one that does not exist — it cannot be produced any other way,
  because linking refuses a bad path up front. The file has to go missing *after* the link was made.

## 9. Self-verification
- [x] build-project        — PASS (clean; no new warning)
- [x] architecture-review  — PASS; the worker still touches no state and no filesystem
- [x] code-review          — PASS
- [x] dependency-audit     — n/a (no dependency added)
- [x] performance-review   — PASS; the per-frame re-read found and fixed above was the only concern
- [x] testing              — PASS (447 ctest cases; `req086-surface-point-files.txt`, 46 steps)

## 10. Verification result
- Submitted: 2026-08-19
- Verdict:   **PASS** on the command layer and `.gs`, all five acceptance conditions covered by the
             transcript. The panel half — the Point Files node, the Add Point File dialog, Browse,
             and the per-item Import/Remove — compiles and routes through commands that ARE covered,
             but ImGui behaviour is not testable here and has **not** been visually verified.

## 11. Outcome
- Requirements satisfied: REQ-086 (Acceptance met: yes, all five).
- Tests added: `tests/headless/transcripts/req086-surface-point-files.txt` (46 steps);
  fixtures `samples/pad-points.csv`, `samples/pad-points-more.csv`, `samples/missing-point-file.gs`.
- Technical debt: none introduced.
- Docs updated: this log; REQ-086 promoted with two implementation findings recorded in its revisions.
- Done: 2026-08-19 (pending the panel's visual check).
