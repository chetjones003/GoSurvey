# TASK-055 — REQ-074: spot elevation and grade readout

- Type:    feature
- Status:  plan — verification review complete, verdict APPROVE (§4); implementation follows
- Opened:  2026-08-15
- Owner:   Workshop

## 1. Authority

- Goal:         M-Surfaces **step 4** (roadmap). Unblocked today by TASK-046 closing REQ-068 — there
                was no surface to interrogate until this afternoon.
- Requirements: **REQ-074** (accepted 2026-08-12)
- Constraints:  **REQ-101** (±0.01 ft — the acceptance conditions are stated against it), ADR-028,
                architecture §11.4 (no new abstraction), §11.8 (interleaved XYZ), REQ-201, REQ-300,
                REQ-301
- Acceptance, restated verbatim:
  - elevation at a point inside a triangle of known plane equals the planar interpolation within
    REQ-101;
  - a pick outside the surface, or inside a hide-boundary void, reports "outside surface" and no
    elevation;
  - grade between two points on a known plane matches the hand-computed value within REQ-101;
  - two picks at the same location report zero distance rather than dividing by zero.
- Owning subsystem: `util/` (the pure query) + Commands (the pick flow and the report) + UI (one
  ribbon button). No renderer, IO or format change.

## 2. Questions

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | REQ-074 says "a surface", but a drawing can hold several. Which does the readout use? | 2026-08-15 | **Report every surface that covers the pick**, one named line each, and a grade per surface for two picks. Chosen over prompting for a surface or silently taking the first: overlapping surfaces are the *existing vs proposed* case, which is exactly the grading question REQ-074 exists to answer, and an unnamed number from an arbitrary surface would be actively misleading there |

## 3. Architectural boundary check (workflow.md §4)

- New abstraction / layer / dependency / ownership change / global state / public-API or
  data-format change / unspecified algorithm?
    - [x] **No — proceed.**
- The query is one free function in `util/tinbuild`, the module that already owns the triangulation
  and produced the arrays being queried. It takes the raw arrays rather than `CadTin`, so it stays
  GL-free and GUI-free and the tests link it directly — the same rule `BuildTin` follows for the
  same reason. The command is a `Kind` beside `SurveyInverse`, whose two-pick shape it copies.
  Nothing is persisted, so `.gs` is untouched.

## 4. Verification review

### 4.1 Architectural impact
Confined to `util/` and `commands/`, plus three lines of ribbon. Dependencies flow downward:
`tinbuild` gains no include and keeps its "pure, dependency-free" promise, so REQ-101 stays
enforceable in a unit test rather than only through the UI. No ownership question arises — the query
reads a `shared_ptr<const CadTin>` that the surface already owns and never mutates it (ADR-028 (a)).

### 4.2 Risks
```
RISK-1 (correctness, HIGH) — float storage vs double predicates. Vertices are float (§11.8) and
  REQ-101 is ±0.01 ft. A barycentric solve done in float at state-plane magnitudes loses far more
  than that to cancellation — the same trap BUG-001 hit for picking and ADR-028 (d) recorded for
  the triangulation predicates. Mitigation: widen to double at the query, exactly as BuildTin does,
  and assert the interpolation against a known plane in the test rather than against itself.
```
```
RISK-2 (correctness, MEDIUM) — "outside" must mean outside. A convex-hull test would report a point
  in a concave notch as inside and then extrapolate a number for it, which REQ-074 explicitly
  forbids. Mitigation: containment is decided per TRIANGLE, so anything the triangulation does not
  cover is outside by construction, concavities included.
```
```
RISK-3 (usability, MEDIUM) — a wrong-surface reading is worse than no reading. Resolved by Q1: every
  covering surface is reported by name, so the user reads which surface produced which number.
```
```
RISK-4 (performance, LOW) — the query is a linear scan of triangles: ~200,000 orientation tests at
  the REQ-100 surface density, a few milliseconds, once per click. It is not on a frame path, so an
  index would be optimisation without a measured problem (CLAUDE.md rule 1). Recorded so that if a
  future caller does put it on a frame path, the cost is already written down.
```
```
RISK-5 (regression, LOW) — a new Kind must not disturb the existing pick dispatch. Mitigation: added
  beside SurveyInverse in the same if-chain, cancel/ESC handled by the same ResetAllCadDraftTools
  path every other command uses; the suite guards the rest.
```

### 4.3 Required tests
All four acceptance conditions are provable against the pure function, which is why it takes raw
arrays:

| # | Test | Acceptance condition |
|---|------|----------------------|
| T1 | elevation inside a triangle of a known tilted plane matches the plane equation within 0.01 ft, at several interior points | 1 |
| T2 | a point outside the hull, and a point in a concave notch of an L-shaped surface, both report not-inside | 2 |
| T3 | picks exactly on a vertex and on a shared edge return that plane's elevation (no gap between triangles) | 1 |
| T4 | grade between two points on a known plane matches the hand-computed rise/run within REQ-101 | 3 |
| T5 | two picks closer than `kTinPlanEpsilon` report zero distance and no division | 4 |
| T6 | the query never reads out of range on a degenerate/empty triangulation | robustness |

T4/T5 test the arithmetic where it lives; the command layer formats it.

### 4.4 Implementation plan
1. `src/util/tinbuild.{hpp,cpp}` — `TinElevationAt(vertsXyz, indices, x, y, double* outZ)`, double
   barycentric containment + planar interpolation, returning false when no triangle covers the point.
2. `src/commands/CadCommands.hpp` — `Kind::SurfaceElevGrade`, a two-phase state beside
   `SurveyInverse`'s, and the `Start…` declaration.
3. `src/commands/CadCommands.cpp` — command-table entry (`surfelev`, alias `se`), dispatch, the start
   function, the pick handler (first pick reports elevation per surface; second reports grade per
   surface), and the status-bar prompt.
4. `src/ui/CadUi.cpp` — an Inquiry-group button beside ID Point. Included because REQ-074's stated
   purpose is "the constant, small question while grading", and a command with no button is not
   constant-use; it mirrors the existing button in three lines.
5. `tests/TinQueryTests.cpp` — T1–T6.

### 4.5 Verdict
**APPROVE.** One ambiguity existed and was escalated and answered (Q1) before planning. No
architectural decision is required of the Workshop; the change is additive and sits entirely in the
subsystems that own it.

## 5. Assumptions
```
ASSUMPTION-1: "inside a hide-boundary void" in acceptance condition 2 cannot be tested yet.
- Because:       hide boundaries are REQ-069, which is not built. A void is not representable, so
                 there is nothing to pick inside of.
- Risk if wrong: none today. The per-triangle containment rule is what will make it true when
                 REQ-069 lands, because a void will simply have no triangles.
- Validate by:   REQ-069's own task. Recorded rather than quietly claimed as met.
```

## 6. Implementation log

- 2026-08-15 — `TinElevationAt` in `util/tinbuild`: barycentric containment and planar interpolation
  in double over the raw arrays. Three details worth their comments in the code — containment is
  per triangle so a concavity cannot be interpolated across; both windings are accepted, because a
  surface read from a file was not necessarily written by us and should read its elevation rather
  than report a hole; and a zero-area triangle is skipped, since dividing by it would produce an
  infinity that formats as a plausible elevation.
- 2026-08-15 — the command: `Kind::SurfaceElevGrade` beside `SurveyInverse`, whose two-pick shape it
  copies exactly, including typed `X,Y` and `@dx,dy` input and a status-bar prompt per phase.
  Grade is computed **within each surface**, never between two — a rise taken from the existing
  surface and a run measured to the proposed one would be a number with no meaning.
- 2026-08-15 — surfaces on an off or frozen layer are skipped, matching `AppendSurfaceEdgeLines`
  (REQ-068). Reporting an elevation for a surface the user has hidden would be the readout
  describing something that is not on screen.
- 2026-08-15 — `SURFELEV` refuses to start when the drawing has no surfaces, saying so *before* the
  first pick rather than after it (REQ-201).
- 2026-08-15 — **the ribbon button did not fit.** Added under ID Point, it was clipped mid-icon: the
  Inquiry panel is three small buttons tall. Moved to a second column, which is the idiom the
  Modify section already uses. Found by looking at the ribbon, not by reading the code — and the
  same look showed that the **Survey group's "Groups" button is clipped the same way today**. That
  one is pre-existing and not mine to fix in this task; recorded in TRACKER as BUG-014.
- 2026-08-15 — verified in the running application against `samples/surface-demo.gs` with a surface
  built from the "Existing Ground" group (500 points, 982 triangles). All four acceptance
  conditions, by their command-line output:

  | condition | observed |
  |---|---|
  | elevation at a point | `SURFELEV — Surface: elevation 109.5859` |
  | grade between two points | `SURFELEV — Surface: grade 5.44%  slope 18.39:1  horiz 100.0000  vert 5.4390` |
  | outside the surface | `SURFELEV — outside surface. No elevation at that point.` |
  | two picks at one location | `SURFELEV — both picks are at the same location: horizontal distance 0. No grade.` |

  The grade line is self-consistent by hand: 5.4390 / 100.0000 = 5.44%, and 100 / 5.439 = 18.39:1.
  The `SE` alias was used for one of the runs, so it is exercised too. The sample file was **not**
  saved — `git status` on `samples/` is clean, so the fixture is unchanged for the next test.

## 7. Self-verification

- [x] build-project        — **PASS**. Clean MSVC build, no new warnings. One `-Wc++20-extensions`
      warning appeared mid-work (capturing a structured binding in a lambda) and was fixed at the
      cause rather than suppressed: the file is built as C++17.
- [x] architecture-review  — **PASS**. The query is a free function in the module that owns the
      triangulation, taking raw arrays so it stays GUI-free and unit-testable — the rule this header
      already follows. No new abstraction, layer, dependency, global state or format change; nothing
      is persisted.
- [x] code-review          — **PASS**. Each non-obvious decision carries its reason: per-triangle
      containment, double arithmetic over float storage, the `kTinPlanEpsilon` threshold reused
      rather than a fresh magic number, grade never computed across two surfaces.
- [x] dependency-audit     — n-a. Nothing added.
- [x] performance-review   — **PASS with a recorded limit**. The query is a linear scan: ~200,000
      orientation tests at REQ-100's surface density, a few milliseconds, once per click and never
      on a frame path. An index would be optimisation with no measured problem behind it
      (CLAUDE.md rule 1). Written down so a future caller that *does* put it on a frame path knows
      what it is paying.
- [x] testing              — **PASS**. `tests/TinQueryTests.cpp`: 7 cases / 49 assertions, asserting
      against a hand-computable plane rather than the query's own output. Full suite **339/339**.

## 8. Verification result

- Submitted:  2026-08-15
- Verdict:    **PASS**
- Findings:   none blocking. One pre-existing defect found while verifying (BUG-014, the clipped
              Survey ribbon button) — reported, not silently fixed, because it is not this task's.

---

COMPLETION REPORT — TASK-055 — 2026-08-15
- Requirements satisfied:  **REQ-074** (Acceptance met: yes — all four conditions, demonstrated in
                           the running application as well as in unit tests; the hide-boundary half
                           of condition 2 is unreachable until REQ-069, stated in ASSUMPTION-1
                           rather than claimed)
- Summary:                 `SURFELEV` / `SE` reports interpolated surface elevation at a pick and
                           grade, slope, horizontal and vertical distance between two — for every
                           surface covering the point, by name.
- Tests:                   `tests/TinQueryTests.cpp`, 7 cases / 49 assertions (happy path: plane
                           interpolation, grade; failure modes: outside the hull, degenerate
                           triangle, corrupt index, null output, zero distance). Suite 339/339 green.
- Verification verdict:    PASS (findings resolved: none blocking; BUG-014 raised)
- Assumptions:             ASSUMPTION-1 documented and open — it closes with REQ-069.
- Architectural decisions: none made by Workshop. One ambiguity escalated and answered first (Q1).
- Dependencies:            none added
- Technical debt noted:    none added. Two limits recorded: the linear scan (§7) and the
                           single-surface-at-a-time grade rule (§6).
- Build:                   reproducible, clean, MSVC via the pinned preset
- Docs updated:            REQ-074 status; roadmap M-Surfaces step 4; TRACKER BUG-014; this log
