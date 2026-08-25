# TASK-106 — A polyline drawn in paper space belongs to the sheet

- Type:    bug
- Status:  done (2026-08-25)
- Opened:  2026-08-25
- Owner:   Nathan Johnson

Upstream issue: chetjones003/GoSurvey#84 (`bug`, `sev:corrupt`).

## 1. Authority
- Requirements: **REQ-039** (accepted) — native paper-space geometry parity.
- Acceptance:   restated verbatim from REQ-039's acceptance list —
  - **(5)** "CIRCLE, ARC, ELLIPSE, **POLYLINE**, and MTEXT draw onto the sheet, and
    SCALE/JOIN/TRIM/OFFSET operate on the paper selection";
  - **(6)** "none of these paper edits change model geometry";
  - **(7)** "a `.gs` round-trip restores the edited paper objects per layout".
- Constraints:  Windows 11 / MSVC. No new dependency.
- Owning subsystem: `Commands` (`src/commands/CadCommands.cpp`).

## 2. Scope
- In scope: route `CommitPolylineDraft` by active space.
- Out of scope:
  - **CIRCLE / ARC / ELLIPSE**, which share the defect by inspection
    (`CommitCircle`, `CommitArcThreePoints`, `FinishEllipseFromRatio` — none has an
    `ActivePaperGeometryTarget` branch). Reported on #84 as a lead, **not reproduced**,
    and deliberately not fixed here: each is a separate commit path with its own store
    and its own acceptance, and bundling four unverified fixes behind one verified test
    is how a fix ships that nobody has actually seen work.
  - The paper-view **placement** quirk observed while verifying: paper geometry draws
    offset from the sheet outline (a paper RECT does the same, before and after this
    change). Pre-existing, shared with RECT, unrelated to which store the commit picks.
- Smallest change: one `if (PaperLayout* L = ActivePaperGeometryTarget(st))` branch,
  mirroring `CommitRectangle`'s.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public API / data format?
    - [x] **No** — proceed. The paper polyline store (`paperPolyVerts` / `paperPolyOffsets` /
          `paperPolyClosed` / `paperPolyAttrs`) already exists and is already written by paper
          RECT, paste, and grip edits. This command simply starts using the store that owns
          this entity in this space. No format change: `.gs` already persists both.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Fix POLYLINE only, or all four commands sharing the gap? | 2026-08-25 | POLYLINE only — the other three are unreproduced leads (§2). |

## 5. Assumptions
```
ASSUMPTION-1: The draft's vertices are already in the correct space when they reach the commit.
- Because:       SubmitPolylineVertex does no space conversion — it stores what the caller
                 passed, and the caller (viewport pick / typed coordinate) resolves the space.
                 Paper RECT relies on the same property.
- Risk if wrong: paper polylines would land at model-scale coordinates on the sheet.
- Validate by:   the transcript asserts paper coordinates survive a .gs round trip, and the
                 GUI run drew at paper inches and rendered at paper scale. DISCHARGED.
```

## 6. Plan
- Approach: branch on `ActivePaperGeometryTarget` exactly as `CommitRectangle` does, keeping
  the model path byte-identical in the `else`.
- Files: `src/commands/CadCommands.cpp` (`CommitPolylineDraft`); driver + transcript for the test.
- Test approach: happy path = a polyline drawn in paper space lands on the sheet and not in the
  model; failure mode = the model path still works, and both survive a `.gs` round trip.
- Steps:
  - [x] give the transcript driver a way to reach paper space at all
  - [x] write the regression transcript — confirm it fails first
  - [x] add the paper branch
  - [x] full suite green
  - [x] GUI verification

## 7. Workflow-specific notes
- **Bug — root cause:** `CommitPolylineDraft` wrote `st.userPolyline*` unconditionally. It is the
  single place the polyline pipeline decides which store to use — `SubmitPolylineVertex` only
  accumulates into `polylineDraftVerts` and `StartPolylineCommand` has no paper guard — so a
  polyline drawn on a sheet was committed to the model, with nothing along the way to catch it.
- **Regression test fails-before?** Yes: `PAPERPOLYLINES: expected 1, got 0`.

## 8. Implementation log
- 2026-08-25 opened. Confirmed on `v0.5.5` in the GUI before writing anything: Layout1 →
  `POLYLINE 1,1 / 5,1 / 5,4 / END` → the polyline is in **Model**. `CLOSE` behaves identically.
  Control in the same session: a paper `RECT` does **not** appear in model, because
  `CommitRectangle` has the branch this function lacked.
- 2026-08-25 **the test gap had to be closed first.** Switching layouts is UI-only in the shipped
  app — `CadUi.cpp`'s layout tab and the MODEL/PAPER status button are `SetActiveSpace`'s only
  callers, and there is no command-line verb — so no transcript could reach paper space, and every
  paper-space acceptance condition in REQ-039 was manual-test-only. Added `SPACE PAPER|MODEL` to
  the driver, calling the same `SetActiveSpace` those buttons call rather than setting the field.
- 2026-08-25 added `EXPECT PAPERPOLYLINES` / `EXPECT PAPERLINES`. Model-side counts cannot express
  this defect: a command writing to the wrong store leaves the model count *correct* and the paper
  count zero, which every existing count reports as success.
- 2026-08-25 **fails-before confirmed**: `PAPERPOLYLINES: expected 1, got 0`.
- 2026-08-25 fixed. Full suite **594/594 green**; `dxf-export-stable` still the only DISABLED case
  (#63, untouched).
- 2026-08-25 GUI re-verified on the fixed build: the polyline renders as paper geometry and Model
  is empty. Same sequence that failed before.
- 2026-08-25 renamed `TASK-102-issue071-072-…` → **TASK-105**. Upstream `cb3cc1f` had taken
  TASK-102/103 for FILLET/CHAMFER while that task was in flight, so two `TASK-102-*.md` files
  existed after PR #79 merged. Renumbering the DXF one is the smaller correction — the FILLET and
  CHAMFER files are cited from REQ-103's own revision history.

## 9. Self-verification
- [x] build-project        — PASS, clean, MSVC Release
- [x] architecture-review  — PASS. One branch in the subsystem that owns the command, mirroring
                             the routing two sibling commit paths already use. No new store, no
                             new abstraction, no dependency direction touched.
- [x] code-review          — PASS. The `else` is the previous body verbatim, so the model path
                             cannot have shifted; `closed` is carried through rather than
                             hardcoded, unlike paper RECT which may legitimately write `1u`.
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a (one command commit, not a frame path)
- [x] testing              — PASS. Fails-before verified, passes-after, suite 594/594, plus GUI.

## 10. Verification result
- Submitted:  2026-08-25
- Verdict:    PASS
- Findings:   none outstanding

## 11. Outcome
- Requirements satisfied: REQ-039 (5), (6), (7) for POLYLINE (Acceptance met: yes)
- Tests added:            `headless.regression-84-paper-polyline-space`
                          (+ `SPACE` driver verb, + `EXPECT PAPERPOLYLINES` / `PAPERLINES`)
- Docs updated:           none (no user-visible behaviour change beyond the geometry landing
                          where the user drew it)
- Technical debt noted:   **DEBT-1** — CIRCLE, ARC and ELLIPSE still lack the same branch (§2).
                          *Removal condition:* each reproduced and fixed with its own test.
                          Tracked on #84.
- Done:                   2026-08-25

## 12. Note for #80 / REQ-118
This unblocks the paper-space third of issue #80. Verified before the fix: the polyline draft's
live rubber-band preview already follows the cursor in paper space, and `CLOSE` — the exact call
REQ-118's click-the-start-vertex would make — already fires there and logs "POLYLINE closed."
Everything REQ-118 needs in paper space already worked; only the destination was wrong. With this
routed, REQ-118's three touchpoints (start-vertex snap candidate, click-to-close, Enter-to-end)
all funnel through this one commit and cover model, 3DPOLY and paper space together.
