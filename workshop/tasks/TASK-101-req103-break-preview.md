# TASK-101 — BREAK: live preview of the material the break will remove

- Type:    feature
- Status:  done
- Opened:  2026-08-24
- Owner:   Claude (agent)

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         REQ-103 — Modify-command completeness (GOAL per spec/project.md D-2026-08-23-j)
- Requirements: REQ-103 (step 4, BREAK) — `accepted`, acceptance amended by D-2026-08-24-e
- Constraints:  REQ-101 (±0.01 ft tolerance), REQ-056 (pre-click feedback on entity picks),
                REQ-058 (previews tessellate as the renderer does)
- Acceptance:   the condition added by D-2026-08-24-e, restated: "between the two picks a live
  preview shows the material that will be removed: the span from break point 1 to the cursor —
  projected onto the picked entity by the same `ClosestPointOnEntity` the second pick commits,
  never the raw cursor — is drawn in the preview style, with a marker at each break point. The
  previewed span follows the same ordering rule its commit does: position-ordered on an open entity
  (click order irrelevant), and on a closed entity the complement of the span the commit keeps, so
  reversing the click order visibly previews the other side. A repeated pick ('break at point')
  previews a zero-length span with both markers still shown. Model space only — the GL preview pass
  is skipped whenever the active space is not model space."
- Owning subsystem: preview geometry (`src/viewport/TransformPreview.{hpp,cpp}`), with one
  visibility change in Commands (`ClosestPointOnEntity`).

## 2. Scope
- In scope: the removed-span preview for every entity kind BREAK accepts (Line, Circle, Arc of any
  sweep, open and closed Polyline), the two break-point markers, and headless coverage of the span.
- Out of scope: a paper-space equivalent (paper space has no preview pass — stated in the
  acceptance, not a silent omission); any change to what BREAK actually commits.
- Smallest change: one span builder that mirrors each committing branch's ordering rule, called
  from `BuildTransformPreview`.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — proceed, with two visibility changes worth a reviewer's eye, neither of which moves
          a boundary:
          (a) `ClosestPointOnEntity` stops being `static` and gains a header declaration. Not a new
              abstraction — it is the existing function, and its paper-space sibling
              `ClosestPointOnPaperEntity` is already declared in the header for the same reason
              (a caller outside CadCommands.cpp needs the same projection). Sharing it is the
              point: a second implementation is exactly how a preview starts disagreeing with its
              commit.
          (b) `src/viewport/TransformPreview.cpp` moves from the GoSurvey-only source list into
              `GOSURVEY_DOMAIN_SOURCES`. It is pure geometry with no GL — it builds vertex lists,
              it does not draw them — so it belongs beside `CadSnap.cpp`, which that list already
              carries for the identical reason. That list's own comment says a source in the
              application "must not be silently absent from the headless driver".
    - [ ] Yes → STOP.
- The preview itself is an acceptance addition, escalated properly: asked, decided, recorded as
  D-2026-08-24-e before any code was written.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | What should the preview draw — the removed span plus end markers, markers only, or a ghost of the surviving pieces? | 2026-08-24 | Removed span + end markers |

## 5. Assumptions  (workflow.md §8)

```
ASSUMPTION-1: the preview draws the span even when the resulting break would be REFUSED (both
points on the two existing endpoints — "would remove the entire entity").
- Because:       the acceptance says what the preview shows, not what it hides.
- Risk if wrong: a user could read the preview as permission and be surprised by the refusal on
                 click. Bounded: the refusal is logged with its reason (REQ-201) and nothing is
                 destroyed.
- Validate by:   the preview answers "what would this remove", which is true in that case too;
                 suppressing it would leave the user with no feedback at all in exactly the
                 situation they most need an explanation. Revisit if the GUI pass disagrees.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: a file-local `appendBreakRemovedSpan` builds the removed material per entity kind,
  each branch mirroring its committing counterpart's ordering rule. `BuildTransformPreview` calls
  it while BREAK is in `SelectSecondPoint`, then adds a view-scaled X at each break point. A public
  `BuildBreakRemovedSpan` wraps the same builder so the span alone (no markers) is assertable.
- Files/functions to touch: `TransformPreview.{hpp,cpp}`; `CadCommands.{hpp,cpp}`
  (`ClosestPointOnEntity` visibility); `CMakeLists.txt`; `tests/headless/HeadlessDriver.cpp`
  (`DUMP BREAKSPAN`); new transcript; `docs/fuzz-harness.md`.
- Test approach: happy path = the previewed span's endpoints and total length match a
  hand-computed figure for Line, Circle and closed Polyline.
  failure mode = reversing the click order on a closed entity previews the OTHER side (asserted as
  a different length), and a repeated pick previews a zero-length span.
- Steps:
  - [x] 1. `appendBreakRemovedSpan` (Line, Circle, Arc incl. full sweep, open + closed Polyline)
  - [x] 2. Markers; hook into `BuildTransformPreview`
  - [x] 3. Export `ClosestPointOnEntity`; expose `BuildBreakRemovedSpan`
  - [x] 4. `TransformPreview.cpp` into the domain sources; `DUMP BREAKSPAN` verb
  - [x] 5. `break-preview-removed-span.txt` with hand-computed spans; full regression

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1). Not tests-first — the geometry had to exist before a dump verb
  could read it — but every expected value in the transcript was hand-computed and written into the
  transcript's own header BEFORE being compared to what the code produced, and all six matched.

## 8. Implementation log  (append as you work)
- 2026-08-24 the load-bearing decision, made first: the preview resolves the cursor through the
  same `ClosestPointOnEntity` the second pick commits. `project_3d_preview_commit_point` records
  this codebase being bitten once already by a preview reading the raw cursor while the pick
  committed a snapped point (a CENTRE snap accepted at the rim committed a full radius away). One
  function, two callers, no second implementation.
- 2026-08-24 each branch written against its committing counterpart rather than from scratch, so
  the ordering rules cannot drift: open entities order by position along the entity
  (ApplyBreakToLine/Arc/OpenPolyline); closed entities keep the span running forward from point 2
  to point 1 (CircleBreakStartSweep, ApplyBreakToClosedPolyline), so the preview is its complement.
- 2026-08-24 hand-computed expectations, all six confirmed by the dump:
  - Line (0,0)-(100,0), breaks at 20 and 70 → 1 segment, length 50.000. Same in either click order.
  - Circle centre (300,0) r 10, point 1 at theta 0, point 2 at theta pi/2 → the quarter,
    10*pi/2 = 15.708. Reversed → the other three quarters, 47.124 true / 47.113 as tessellated.
  - Closed rectangle (700,0)-(710,0)-(710,10)-(700,10), points at (710,5) and (700,5) → 3 segments,
    length 20.000 — exactly the complement of the 20-unit span
    break-circle-and-closed-polyline.txt (T8) already pins as the part the commit KEEPS.
  - Repeated pick → length 0.000, markers still drawn.
- 2026-08-24 **fails-before confirmed, not assumed**: inverted the circle's removal direction
  (`p1.theta - p2.theta`), rebuilt, and the transcript went red on the quarter-arc assertion.
  Probe reverted, suite green again.
- 2026-08-24 caught one of my own bad assertions while doing it: `EXPECT LOG` searches the WHOLE log
  history, so a sloppy expectation was matching an earlier, unrelated dump line and passing
  vacuously. Every assertion in the transcript now names its span's endpoints, which are unique.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — PASS (no new warnings; headless links the preview TU cleanly, which is
      the check that it really is GL-free)
- [x] architecture-review  — PASS, with §3 (a) and (b) flagged for a reviewer's eye
- [x] code-review          — PASS. Every branch bounds-checks its store before indexing; the
      default case is BREAK's own refused set.
- [x] dependency-audit     — n/a
- [x] performance-review   — PASS. Runs per frame while BREAK awaits its second point, so the
      cursor is projected ONCE per frame (`BuildTransformPreview` calls the shared builder directly
      rather than the public wrapper, which would resolve it a second time). Arc spans tessellate
      at the renderer's own density (REQ-058), not a fixed count.
- [x] testing              — PASS. Full suite green (573/573).

## 10. Verification result
- Submitted:  2026-08-24
- Verdict:    **PASS** — self-verification green (§9), and the user confirmed the manual model-space
              GUI pass on 2026-08-24. That pass is what closes this task in particular: appearance
              is the one thing no test here can assert, and the first attempt was geometrically
              correct and still invisible (§12).
- Findings:   none open

## 11. Outcome
- Requirements satisfied: REQ-103 step 4, as amended (Acceptance met: yes)
- Tests added:            `tests/headless/transcripts/break-preview-removed-span.txt`;
                          `DUMP BREAKSPAN` driver verb
- Refactors:              `ClosestPointOnEntity` shared rather than duplicated;
                          `TransformPreview.cpp` moved into the headless-linked domain sources
- Docs updated:           `spec/project.md` (D-2026-08-24-e), `spec/requirements.md` REQ-103,
                          `docs/fuzz-harness.md`
- Technical debt noted:   the two break-point markers are the one part of the preview no transcript
                          asserts — the dump deliberately returns the span alone, since the markers
                          are view-scaled decoration. Visual check only.
- Done:                   2026-08-24

```
COMPLETION REPORT — TASK-101 — 2026-08-24
- Requirements satisfied:  REQ-103 step 4, as amended by D-2026-08-24-e (Acceptance met: yes)
- Summary:                 BREAK now previews the material the break would remove, plus a marker at
                           each break point, resolved through the same ClosestPointOnEntity the
                           second pick commits, on its own opaque render channel.
- Tests:                   break-preview-removed-span.txt — six hand-computed spans across Line,
                           Circle (both click orders), closed Polyline and the break-at-point case;
                           DUMP BREAKSPAN driver verb. Run green, and shown red by inverting the
                           circle's removal direction.
- Verification verdict:    PASS  (findings resolved: none open)
- Assumptions:             ASSUMPTION-1 (§5) — the span is previewed even when the break would be
                           refused; validated by the user's GUI pass
- Architectural decisions: none made by Workshop (escalated: none; §3 (a) ClosestPointOnEntity's
                           visibility and (b) TransformPreview.cpp moving into the domain sources
                           were both flagged for review and accepted as following existing
                           precedent)
- Dependencies:            none added
- Technical debt noted:    the two break-point markers are the one part of the preview no transcript
                           asserts — they are view-scaled decoration, visual check only. And
                           appearance itself is untestable here, which is why the first attempt
                           shipped correct-but-invisible (§12).
                           Not fixed, raised for the user's call: TRIM's own removed-material
                           preview (CadTrimAppendCutLineRemovedPreview) still appends into the
                           translucent transform batch and has exactly this weakness — a one-line
                           redirect into the new channel, deliberately left outside this scope.
- Build:                   reproducible, clean on target platform (MSVC/Ninja Release)
- Docs updated:            spec/project.md (D-2026-08-24-e), spec/requirements.md,
                           docs/fuzz-harness.md
```

## 12. Follow-up — the preview was correct and still invisible (2026-08-24)

The user reported BREAK "not showing preview properly" after the above shipped. Diagnosis, with
evidence rather than a guess:

- The span geometry was right — `break-preview-removed-span.txt` was green, six hand-computed
  lengths all matching.
- So the question was whether it reached the screen at all. A temporary dump of what
  `BuildTransformPreview` (the on-screen path, distinct from the span builder the transcript
  asserted) produced showed exactly span + 4 marker segments in all six cases. It was reaching the
  viewport batch.
- Therefore the fault was in the painting. `ViewportRenderer.cpp` draws the whole preview batch with
  `glUniform4f(locCol, 1.f, 0.88f, 0.35f, 0.55f)` at `kLwMain` (1.35) — pale yellow at **55% alpha,
  the same line width as ordinary geometry**. That is correct for what the batch was built for: a
  MOVE/COPY/ROTATE ghost showing geometry somewhere it is not yet, floating in empty space. BREAK's
  removed span is the first preview in this codebase that lies exactly ON TOP of the object it
  describes, so it blended into the full-opacity line beneath it and read as nothing.

**Fix:** removal previews get their own channel — `BuildBreakRemovalPreview` (replacing
`BuildBreakRemovedSpan`), two new `RenderScene` parameters, drawn opaque in a warning colour
(0.95, 0.27, 0.22) at `kLwHiLine` (2.65), painted after the transform batch so it wins where they
overlap. The span and the markers are returned separately so the transcript can still assert the
span's length without the markers' decoration in the total, and the markers now carry an explicit
view scale rather than inheriting a file-global one that only `BuildTransformPreview` had been
setting.

**Lesson recorded:** the transcript asserted the span builder, not the on-screen path, and no test
can assert *appearance* at all. That is a real limit, now stated in the transcript's own header
rather than left implicit — this task's coverage proves the preview describes the right material,
never that a user can see it.

**Not changed, deliberately:** TRIM's own removed-material preview
(`CadTrimAppendCutLineRemovedPreview`, main.cpp) appends into the same translucent transform batch
and has exactly this weakness. It is a one-line redirect into the new channel, but TRIM is outside
this task's scope and outside what the user asked for — raised for their call rather than changed
unasked. Follow-up task if wanted.
