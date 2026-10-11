# TASK-080 — Feature line elevation editing

- Type:    feature
- Status:  open (stage 1 of 2)
- Opened:  2026-08-20
- Owner:   chetjones003

## 1. Authority
- Goal:         M-Grading (`spec/roadmap.md`); ADR-035, accepted 2026-08-19.
- Requirements: **REQ-088** (feature line elevation editing) — `proposed`. Constrained by REQ-087
                (the entity), REQ-069 (breaklines and dynamic rebuild), REQ-101 (±0.01 ft),
                REQ-201 (every command reports), REQ-203 (headless drivability), REQ-204.
- Acceptance (REQ-088, verbatim):
  - typing an elevation updates grade back and grade ahead on the neighbouring rows and nowhere else;
  - typing a grade ahead moves the next point's elevation and leaves the current one alone;
  - stations and lengths agree with the feature line's plan geometry to REQ-101's tolerance;
  - an elevation point changes the surface when the feature line is used as a breakline, and does
    not add a plan vertex;
  - every edit is undoable in one step and the surface rebuilds with no user action.
- Owning subsystem: `commands/`, then `ui/` in stage 2.

## 2. Scope

**ADR-035 (e) decides the shape of this before any code:** *"The elevation editor is a view, not a
store. Station, length, grade back and grade ahead are all derived from the vertex chain on demand.
Storing a grade would create a second source of truth for the same elevation, and the two would
disagree the moment geometry moved."* So this task adds **no field to any store**. It adds a
derivation and a set of edits that write elevations back into the existing stride-3 vertex array.

- **Stage 1 — this task.** The derivation, the six edit operations, the `FLELEV` command surface,
  undo, surface rebuild, and the blocker in §6 BUG-1. Fully drivable and testable headlessly.
- **Stage 2 (TASK-081).** The ImGui table panel: editable cells routed through stage 1's commands,
  the way `CadUi_Surfaces.cpp` routes through `ProcessCommandLineSubmit`. No new domain logic.

### Out of scope
- Grips and single-PI editing (still owed by REQ-087 stage 2b, TASK-078).
- Snap, DXF, PDF plot for feature lines (same).
- Anything in ADR-035 (f): alignments, corridors, stepped offsets, grading objects, styles.

## 3. Architectural boundary check
- **No new store, no new field, no data-format change** — ADR-035 (e) already decided this and the
  task implements it. `.gs` is untouched, so no migration and no round-trip risk.
- **No new abstraction.** The derivation is one function with two present-day callers (the `FLELEV`
  readout and stage 2's panel), meeting CLAUDE.md rule 2's bar. It lives beside the other feature
  line code in `commands/`, not in a new module.
- **No dependency, no algorithm swap.**
- BUG-1 (§6) adds a branch to an existing function; it introduces no new concept.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Grade in percent or ratio? | — | Not asked. `SURFELEV` (REQ-074) already reports `grade %.2f%%` with rise over the 2D run; matching it is the only consistent answer, and inventing a second convention in the same app would be the defect. |
| Q2 | Does editing GRADE BACK move the current point or the previous one? | — | Resolved from the spec, see ASSUMPTION-1. Recorded because it is the one place the Statement and the Acceptance could be read apart. |

## 5. Assumptions

```
ASSUMPTION-1: A grade edit always holds the EARLIER point and moves the LATER one.
  Grade ahead on row N moves row N+1. Grade back on row N moves row N ITSELF (holding N-1).
- Because:       REQ-088's Statement says "editing a grade updates the DOWNSTREAM elevations", which
                 is directional. Grade back on row N is the grade of segment N-1 → N; downstream of
                 that segment is N. The Acceptance pins only the grade-ahead half ("moves the next
                 point's elevation and leaves the current one alone"), and this reading satisfies it
                 while making the two halves one rule instead of two. It is also what Civil 3D does,
                 which matters because the user asked for this feature with Civil 3D on screen.
- Risk if wrong: LOUD. Editing grade back would move a point the user expected to stay put, visibly
                 and immediately, on the very first edit. Not a silent failure.
- Validate by:   T3 and T4, which assert both halves against a hand-computed elevation.
```

```
ASSUMPTION-2: "Updates the downstream elevations" (plural) means ONE point, not a cascade.
- Because:       the Acceptance is specific and singular where the Statement is loose — "moves the
                 NEXT POINT'S elevation". A cascade would also move every point after it, which the
                 Acceptance's "and leaves the current one alone" gives no support for, and which
                 would make a single grade edit rewrite an entire designed profile.
- Risk if wrong: loud again — the user sees the whole line move.
- Validate by:   T3 asserts the point AFTER the moved one is unchanged.
```

```
ASSUMPTION-3: Station and length are PLAN (2D) distances, not slope distances.
- Because:       REQ-088's own Acceptance says "stations and lengths agree with the feature line's
                 PLAN GEOMETRY". Grade is then rise / plan-run, which is what SURFELEV already
                 computes (Q1) and what "grade" means to a surveyor.
- Risk if wrong: silent — a 10% grade over a long run differs from the slope-distance answer by ~0.5%,
                 which looks plausible on screen.
- Validate by:   T2 uses a 3-4-5 triangle so plan length (4) and slope length (5) cannot be confused.
```

## 6. Plan

### BUG-1 — a feature line cannot be a surface breakline at all (blocker, found 2026-08-20)

`ResolveDefinitionChain` (`CadCommands.cpp:1302`) handles `EntityKind::Line` and
`EntityKind::Polyline` and ends `return false;  // any other entity kind is not a valid
breakline/boundary source`. `EntityKind::FeatureLine` therefore resolves as **absent**, and
`ResolveSurfaceInputs` treats an unresolvable id as an entity that no longer exists — so it does not
merely skip the breakline, it **removes it from the stored definition** and reports "breakline(s) no
longer exist".

This blocks acceptance conditions in **both** requirements:
- REQ-087: *"adding a feature line to a surface forces triangulation edges along it"*
- REQ-088: *"an elevation point changes the surface when the feature line is used as a breakline"*

Fix: a `FeatureLine` branch in `ResolveDefinitionChain`, structurally identical to the `Polyline`
one. Elevation points need no special handling there — ADR-035 (a) is precisely that they are
ordinary vertices, so the triangulator folds their elevations in with everyone else's. That is the
decision paying off, and T6 is what proves it rather than assuming it.

### The derivation (ADR-035 (e) — a view, computed, never stored)

```cpp
struct FeatureLineElevRow {
  int    vertexIndex;      ///< index within the feature line, 0-based
  bool   isElevationPoint; ///< flagged vertex, not a PI (ADR-035 (a))
  double station;          ///< cumulative PLAN distance from the first point (ASSUMPTION-3)
  float  elevation;
  double lengthAhead;      ///< plan distance to the next point; 0 at the last point of an open line
  double gradeBackPct;     ///< rise/run x100 over the segment BEHIND; NaN at the first point
  double gradeAheadPct;    ///< rise/run x100 over the segment AHEAD;  NaN at the last point
};
bool BuildFeatureLineElevTable(const AppCommandState&, int fi, std::vector<FeatureLineElevRow>*);
```

`NaN` rather than `0.0` for the absent grades: a first point has **no** grade back, and reporting
that as `0.00%` is a lie that reads as "level". The readout prints `—`.

### The six edits

| operation | holds | moves |
|---|---|---|
| set elevation at N | — | N |
| grade ahead at N | N | N+1 |
| grade back at N | N-1 | N (ASSUMPTION-1) |
| raise/lower by delta | — | every point |
| insert elevation point at station S | — | adds a flagged vertex ON the line |
| delete elevation point at N | — | removes it; **refuses if N is a PI** |

Insert places the vertex by interpolating the plan position at station S along the chain, so it lies
exactly on the line by construction and ADR-035 (b)'s drift risk does not arise. Deleting refuses a
PI because that is geometry editing, not elevation editing — REQ-087 lists insert/delete PI as its
own operation and it is not built yet.

### Command surface (REQ-203 — all of it drivable with no window)

One verb with sub-actions, following `UNDESIGNATE`'s shape rather than adding seven top-level words:

```
FLELEV <n>                       list the table
FLELEV <n> SET <point> <elev>
FLELEV <n> GRADEAHEAD <point> <pct>
FLELEV <n> GRADEBACK <point> <pct>
FLELEV <n> RAISE <delta>         negative lowers
FLELEV <n> INSERT <station> <elev>
FLELEV <n> DELETE <point>
```

Every edit pushes one undo snapshot and bumps the GPU revision, which is what `TickSurfaceRebuilds`
watches — so "the surface rebuilds with no user action" comes for free from REQ-069's existing
machinery rather than needing anything new.

## 7. Test approach
Run against unpatched code first; each must fail there.

| id | asserts | oracle |
|----|---------|--------|
| T1 | the table's stations, lengths and grades match hand-computed values | fails — no command |
| T2 | 3-4-5 triangle: length reads 4 (plan), not 5 (slope) — ASSUMPTION-3 | fails |
| T3 | grade ahead at N moves N+1 only; N and N+2 unchanged — ASSUMPTION-1, -2 | fails |
| T4 | grade back at N moves N, holds N-1 — ASSUMPTION-1 | fails |
| T5 | setting an elevation changes both neighbouring grades and no others | fails |
| T6 | an elevation point changes the surface, and adds no plan vertex | fails — BUG-1 |
| T7 | DELETE refuses a PI, out loud | fails |
| T8 | every edit undoes in one step; `CHECK ALL` clean throughout | fails |

**Oracle result.** The transcript was split into its four sections and each run against a build with
`src/` reverted. All four failed there; all four pass now:

```
s1 FAIL no log line contains: feature line 1 "Ridge": 3 points          (the table)
s2 FAIL no log line contains: 0.000        0.000          4.000 …       (plan vs slope)
s3 FAIL no log line contains: grade ahead of point 2 = 5.00%; point 3 … (the edits)
s4 FAIL no log line contains: added a breakline                         (BUG-1)
```

s4 is worth reading twice: unpatched, it fails at the **designate** step, not at a later assertion.
A feature line could not be made a breakline at all.

### Two findings from writing the tests
- **An early draft of T8 passed for the wrong reason.** It asserted `EXPECT LOG "elevation 500.000"`
  after designating a breakline at elevation 500 — and that matched `FEATURELINE PI 1 at elevation
  500.000`, the vertex-entry log from twelve lines earlier, not SURFELEV's output at all. The
  surface was never being checked. Caught by raising the line to 600 and finding the assertion still
  passed. Every surface assertion now carries SURFELEV's own prefix.
- **The pick has to be full-precision.** World (50,40) is local (36.39257518, 39.69194649) in this
  drawing. Picking `36.393 39.692` lands a thousandth off the constraint edge, interpolates into the
  neighbouring triangle, and reads 499.9 instead of 500.000 — which would have looked like a
  tolerance problem in the triangulator rather than a rounded test coordinate.

## 8. Completion report

```
COMPLETION REPORT — TASK-080 (stage 1) — 2026-08-20
- Requirements satisfied:  REQ-088 — four of five acceptance conditions verified headlessly; the
                           fifth ("rebuilds with no user action") is verified only to the point the
                           driver can reach — see Technical debt. REQ-088 stays `proposed` until
                           stage 2 puts the table on screen, since its Statement says "editable
                           through a TABLE" and stage 1 is a command line.
                           Also unblocks REQ-087's "adding a feature line to a surface forces
                           triangulation edges along it", which BUG-1 had made impossible.
- Summary:                 Feature line elevations are editable: a derived station/elevation/length/
                           grade-back/grade-ahead table, and six edits (set elevation, grade ahead,
                           grade back, raise/lower as a set, insert elevation point, delete
                           elevation point) behind one FLELEV verb. Fixed BUG-1, which made a
                           feature line silently unusable as a surface breakline.
- Tests:                   tests/headless/transcripts/req088-feature-line-elevation-editor.txt,
                           125 steps, 4 sections, each verified to fail against reverted src/.
                           457 ctest cases green.
- Verification verdict:    PASS (findings resolved: BUG-1)
- Assumptions:             ASSUMPTION-1 validated by T3 and T4 (both halves of the grade rule).
                           ASSUMPTION-2 validated by T3 (the point after the moved one is
                           unchanged). ASSUMPTION-3 validated by T2's 3-4-5 triangle.
- Architectural decisions: none made by Workshop. ADR-035 (e) had already decided the editor is a
                           view rather than a store, and this task implements that: no new field,
                           no `.gs` change, no migration.
- Dependencies:            none added.
- Technical debt noted:    FU-1 below. Also: the deferred-snapshot design in
                           EditFeatureLineElevations exists because PushUndoSnapshot clears the
                           redo stack, so an eagerly-pushed-then-popped snapshot would destroy redo
                           history as the price of a rejected keystroke. `fn` must call `commit()`
                           before its first mutation; forgetting would make an edit silently
                           un-undoable. Each of the six is covered by an UNDO assertion.
- Build:                   clean, MSVC 14.50; no new warnings.
- Docs updated:            spec/requirements.md (REQ-088 revision note), this task log.
```

## 9. Follow-ups filed
- **FU-1**: REQ-088's "the surface rebuilds with no user action" is only partly verifiable here. The
  dynamic rebuild is dispatched by `TickSurfaceRebuilds`, which `main.cpp`'s frame loop drives and
  the REQ-203 driver has no frame loop for — the same limitation
  `req069-surface-definition-commands.txt` already records. What the transcript proves is everything
  up to the tick: each edit bumps `cadGpuRevision`, which is exactly the dirty condition
  `TickSurfaceRebuilds` tests. The tick stays manually verified. A driver-side frame tick would
  close this for REQ-069 and REQ-088 together and is worth its own task.
- **FU-2**: stage 2 (TASK-081) — the ImGui table. Editable cells routed through stage 1's `FLELEV`
  commands, no new domain logic.
- **FU-3**: `FLELEV ... INSERT` places an elevation point by station. Civil 3D also lets one be
  placed by clicking a point on the line; that is a viewport interaction and belongs with grips
  (REQ-087 stage 2b, TASK-078), not here.
