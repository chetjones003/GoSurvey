# TASK-109 — A polyline closes by clicking its start, and ends on Enter

- Type:    feature
- Status:  done (2026-08-25)
- Opened:  2026-08-25
- Owner:   Nathan Johnson

Upstream issue: chetjones003/GoSurvey#80.

## 1. Authority
- Requirements: **REQ-118** — accepted 2026-08-25 by **D-2026-08-25-j**.
- Also honoured: REQ-085 (3DPOLY per-vertex elevation), REQ-039 (5)/(6) (paper space),
  REQ-201 (a refusal states its reason).
- Acceptance: REQ-118's eight conditions, restated in §6's test map.
- Owning subsystem: `Commands` (the state machine), `Viewport` (the snap candidate).

## 2. Scope
- In scope: the start vertex as an Endpoint snap candidate; click-the-start closes;
  bare Enter ends open; prompts updated; model, 3DPOLY and paper space.
- Out of scope: removing `CLOSE`/`END` (REQ-118 keeps them); a dedicated close glyph
  (the user chose to reuse `Endpoint`); the paper-view placement quirk noted in TASK-108.
- Smallest change: one snap candidate, one early-return in the vertex path, one
  blank-Enter branch, three prompt strings.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public API / data format?
    - [x] **No** — proceed. `CadSnap::FindBest` already takes `const AppCommandState&`, so the
          draft is reachable with no new data flow; its doc comment was the only thing that
          became wrong, and is updated. No new `SnapKind` (the user chose to reuse `Endpoint`).
          No store, format or persisted-state change: a closed polyline was already
          representable, and `.gs`/DXF already carry the closed flag.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Which spaces? | 2026-08-25 | All three — model, 3DPOLY, paper. |
| Q2 | Does Enter exit the command or restart it like LINE? | 2026-08-25 | **Exit.** A polyline is one object; LINE's chain is a run of segments. Asymmetry recorded, not inherited. |
| Q3 | New close glyph or reuse the Endpoint snap? | 2026-08-25 | Reuse `Endpoint` — no new enum value, no new glyph, existing OSNAP toggle governs it. |
| Q4 | Remove `CLOSE`/`END`? | 2026-08-25 | Keep. #80 says "no longer required", which is not "gone". |

## 5. Assumptions
```
ASSUMPTION-1: A snapped point compares EQUAL to the stored first vertex, so close detection can
              be coordinate-based rather than asking the snap system what the user meant.
- Because:       CadSnap::Consider is handed the stored coordinate and returns it verbatim; it
                 does not re-derive or round it.
- Risk if wrong: clicking the marked start vertex would add a duplicate vertex instead of closing
                 — the exact failure #80 asks to remove, and it would look like nothing happened.
- Validate by:   the GUI run below (clicked the marked vertex; logged "POLYLINE closed.").
                 DISCHARGED 2026-08-25.
```

## 6. Plan and test map
| REQ-118 acceptance condition | Where it is proven |
|---|---|
| four picks, fourth on the start → one **closed** polyline, command ends | transcript part 1 (+ a following LINE proves the command ended) |
| three picks then Enter → one **open** polyline, no closing segment, ends | transcript part 2 (+ a following CIRCLE) |
| two vertices → a pick on the start does **not** close | transcript part 3 |
| `CLOSE`/`END` still work, still report | transcript part 4 |
| other snapping unaffected while drafting | full suite (every existing snap transcript) |
| Esc mid-draft changes no count | transcript part 5 |
| 3DPOLY identical, per-vertex elevation | transcript part 7 |
| holds in paper space too | transcript part 6 |
| the start vertex is a **snap candidate with a marker** | **GUI only** — see §8 |

## 7. Workflow-specific notes
- **Feature — pre-flight answered?** Yes, Q1–Q4 before any code. **Tests first?** Yes: the
  transcript was written and shown to fail before the implementation existed.

## 8. Implementation log
- 2026-08-25 opened against REQ-118, accepted the same day (D-2026-08-25-j). Blocked-on-first:
  TASK-108 (#84), because paper-space polylines committed to the model store until that landed.
- 2026-08-25 `CadSnap::FindBest` — the draft's first vertex is offered as an `Endpoint` candidate
  when a POLYLINE/3DPOLY draft holds **three or more** vertices. The minimum is expressed by
  withholding the affordance rather than refusing the close afterwards: a snap never offered
  cannot be mis-clicked, which is #80's "invalid attempts handled gracefully" with no message to
  read. Header doc comment updated — this is the one candidate not from committed geometry.
- 2026-08-25 `SubmitPolylineVertex` — a vertex coinciding with `polyFirstX/Y` commits closed
  instead of appending. Coordinate-based, **not** snap-gated (D-2026-08-25-j): the driver's `PICK`
  never calls `FindBest`, so a snap-gated rule would have been manual-test-only forever — the trap
  REQ-039's paper conditions fell into (#84) — and a coordinate rule also closes on a typed
  coordinate, which a snap-gated one would refuse whenever OSNAP is off. Tolerance is deliberately
  tight (`1e-4`): "the same point", not "near it". A near-miss stays an ordinary vertex.
- 2026-08-25 blank-Enter block — a `K::Polyline` branch commits **open** and ends the command.
  Placed directly below LINE's, which restarts instead; the difference is deliberate (Q2).
- 2026-08-25 three prompts updated (POLYLINE, 3DPOLY, per-vertex) to lead with the two gestures
  and note that CLOSE/END still work.
- 2026-08-25 **fails-before confirmed** by stashing only the source changes and rebuilding:
  `POLYLINES: expected 1, got 0` — the start-point pick was being appended as a 4th vertex with
  the command still waiting. Restored; full suite **595/595 green**.
- 2026-08-25 **GUI verified** on the built app, model space, by clicking rather than typing:
  three clicks, then hovering the first vertex showed the **green Endpoint snap marker** with the
  rubber band running to it, and clicking it logged "POLYLINE closed." and left a closed triangle
  with the command ended. This is the one acceptance condition no transcript can reach, since the
  driver bypasses the snap system entirely.

## 9. Self-verification
- [x] build-project        — PASS, clean, MSVC Release
- [x] architecture-review  — PASS (§3; no Workshop architectural decision)
- [x] code-review          — PASS. The close check sits before the append and returns, so the
                             vertex path is unchanged for every non-closing point.
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a. One extra `Consider` per snap query, only while a draft is
                             open — a single point against stores already iterated in full.
- [x] testing              — PASS. Fails-before verified, passes-after, suite 595/595, plus GUI.

## 10. Verification result
- Submitted:  2026-08-25
- Verdict:    PASS
- Findings:   none outstanding

## 11. Outcome
- Requirements satisfied: REQ-118 (Acceptance met: yes — eight of nine conditions by transcript,
  the snap-marker condition by the GUI run in §8)
- Tests added:            `headless.regression-118-polyline-close-enter`
- Docs updated:           three in-app prompts; `CadSnap.hpp`'s `FindBest` doc comment
- Technical debt noted:   none new. TASK-108's DEBT-1 (CIRCLE/ARC/ELLIPSE paper routing) is
                          untouched and unrelated.
- Done:                   2026-08-25
