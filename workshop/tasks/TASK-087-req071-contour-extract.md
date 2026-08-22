# TASK-087 — EXTRACT: bake the displayed contours into ordinary polylines

- Type:    feature
- Status:  done — 2026-08-22. Self-verification PASS; independent review not yet run.
- Opened:  2026-08-22
- Owner:   Workshop

## 1. Authority

- Goal:         GOAL-05 (terrain modelling — M-Surfaces). `spec/roadmap.md` **Next**, first entry:
                "REQ-071 — contour extraction (EXTRACT bakes displayed contours into unlinked
                polylines)", moved out of step 6 by D-2026-08-21-a so a verification FAIL on styles
                could not block it. TASK-085 has since landed, so the dependency it was waiting on —
                displayed contours to extract — now exists.
- Requirements: **REQ-071** (contour extraction, `should`, accepted 2026-08-12).
                **REQ-201** (a command reports what it did) — REQ-071 cites it directly.
                **REQ-076 / ADR-027** (stable ids — an extracted polyline is an ordinary entity and
                gets an id like any other).
- Constraints:  **REQ-101** (±0.01 ft — the vertex tolerance REQ-071's first bullet names);
                architecture §11.8 (interleaved XYZ, float storage / double predicates);
                §11.9 (reference by stable id, never index);
                CON-07 / REQ-200 (nothing written into the source tree).
- Authority for the architectural shape: **ADR-028 (b)** — contours are display geometry and EXTRACT
  is "the deliberate, explicit escape hatch when real polylines are wanted"; **ADR-036 (e)** (the
  display cache this reads from); **ADR-036 (f)** (`util/contourgen`, the generator it re-runs).

### Acceptance, restated verbatim from REQ-071

- "extraction produces polylines at exactly the displayed contour elevations, each vertex within
  REQ-101 of the linear interpolation along the triangle edge it came from";
- "extracting twice produces two independent sets, neither affecting the other";
- "rebuilding the surface afterwards leaves already-extracted polylines untouched";
- "the created count and interval are reported";
- "extracting from a surface whose style has contours disabled creates nothing and says so, rather
  than silently extracting a hidden interval."

Plus the statement's standing rules: the result is "normal drawing geometry — editable, snappable,
exportable" and is "**deliberately not linked to the surface**: a later rebuild does not change it,
and it is not removed when the surface is erased."

- Owning subsystem: **Commands** (the EXTRACT command and the polyline creation), **Domain** (reading
  the resolved style and re-running the generator). No renderer change, no IO change, no new module.

## 2. Scope

- **In scope:** an `EXTRACT <surface>[, <layer>]` command; polylines created in
  `userPolyline*` carrying each contour component's resolved appearance; the target layer created if
  absent; the REQ-201 report naming count, interval(s) and layer; the five acceptance conditions and
  their tests, including a headless transcript.
- **Out of scope, each for a stated reason:**
  - **Contour labelling** — no requirement. REQ-071 exists precisely so that contours which must be
    labelled become ordinary polylines first; labelling them is then the *text* feature's job.
  - **Extracting the border, the triangle network or the points** — REQ-071 says "contours". The
    border is a style component, not a contour, and nothing asks for it as geometry.
  - **Any link back to the surface** — forbidden by the statement, not merely unneeded. There is no
    back-reference to add and none is added.
  - **Smoothing / weeding the extracted polylines** — ADR-028 leaves smoothing undesigned, and a
    vertex the user did not ask to lose is worse than a dense polyline they can weed themselves.
  - **A dialog** — the command is the whole feature. REQ-075's Surface Manager consolidation is where
    a button for it would belong, and that is a different task.
- **Smallest change:** one command, one loop over a `ContourResult` the existing generator already
  produces in the exact layout `userPolylineVerts` / `userPolylineOffsets` / `userPolylineClosed`
  uses. No new module, no new store, no change to `contourgen`.

## 3. Architectural boundary check  (workflow.md §4)

- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] **No — proceed.** Every piece is already decided upstream:
      - **The command itself** is ADR-028 (b) verbatim — EXTRACT is named there as the escape hatch,
        and REQ-071 is its accepted requirement. Adding a command is ordinary Workshop output.
      - **The geometry** is `util/contourgen`, already built and already emitting flat verts +
        per-contour offsets + a closed flag, which is the `userPolyline*` layout. Nothing new is
        computed; the same function is called with the same inputs.
      - **The entities** are ordinary polylines in the existing store, picking up stable ids from the
        existing sweep. No new entity kind, no new store, no `.gs` change — an extracted polyline
        serialises as the polyline it is.
      - **No ownership change:** the extracted polylines are owned by the drawing exactly as any
        other polyline, and the surface gains no reference to them. That is the requirement's own
        rule, not a choice this task makes.

## 4. Questions  (workflow.md §5)

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | REQ-071 says "a chosen layer" without saying how it is chosen. (a) `EXTRACT <surface>[, <layer>]` defaulting to the current layer; (b) split major and minor onto two auto-named layers, the Civil 3D convention; (c) always the current layer. | 2026-08-22 | **(a).** Comma-separated like every other surface command, because surface names contain spaces. No layer argument means the current layer; a named layer is created if it does not exist; the layer used is named in the report. (b) was declined as inventing a naming scheme no requirement asks for, against a statement that says "a chosen layer", singular. |
| Q2 | Does an extracted polyline carry the contour component's colour / linetype / lineweight, or come out plain ByLayer? REQ-071 says nothing about appearance. | 2026-08-22 | **Carry the style's appearance.** The statement is "bakes a surface's **currently displayed** contours", and plain ByLayer would make a major and a minor contour indistinguishable the instant they are extracted — losing exactly the distinction the user was looking at when they ran the command. |
| Q3 | With major and minor contours both displayed there are TWO intervals, and REQ-071 says "the created count and interval are reported". Which interval? | 2026-08-22 (self-resolved — the requirement's own wording) | Both, each with its own count, plus a total. Reporting one interval while having extracted two sets would be a report that is true and misleading, which is what REQ-201 exists to prevent. |
| Q4 | REQ-071's last bullet says "a surface whose style has contours **disabled** creates nothing and says so". Does that mean both components off, or either? | 2026-08-22 (self-resolved) | Refuse only when **nothing would be extracted** — both components off, or on but producing no contours. With one component off and the other on, the enabled one is extracted and the report says which was skipped and why. Refusing a half-enabled style would refuse work the user can plainly see on screen. |

## 5. Assumptions  (workflow.md §8)

```
ASSUMPTION-1: EXTRACT re-runs GenerateContours with the SAME triangulation pointer and the SAME
              resolved style the display pass used, rather than reading back the display cache's
              GL_LINES buffers or keeping a second copy of the polyline form.
- Because:    REQ-071's first bullet is "at exactly the displayed contour elevations". The generator
              is a pure function of (verts, indices, levels), so the same inputs give byte-identical
              output — "exactly" is then true by construction rather than by two code paths agreeing.
              The cache stores the FLATTENED GL_LINES form, which has thrown away the per-contour
              offsets and the closed flag that a polyline needs; re-running recovers both at no
              standing cost, whereas caching the polyline form would double contour memory for a
              feature used occasionally.
- Risk if wrong: extraction silently produces a different set from what is on screen — precisely the
              defect that acceptance bullet is written to catch.
- Validate by: a test that extracts and compares the resulting polyline vertices against the display
              cache's own contour segments for the same surface and style, vertex for vertex.
```

```
ASSUMPTION-2: The suppressed-by-cap case (TASK-085's kMaxContourLevels) counts as "no contours to
              extract" and takes the same refusal path as contours being switched off.
- Because:    REQ-071's last bullet forbids "silently extracting a hidden interval". A style whose
              interval is too fine to draw is displaying no contours, so extracting some would be
              exactly that — creating geometry the user cannot see and did not choose.
- Risk if wrong: EXTRACT produces hundreds of thousands of polylines from an interval the display
              path already refused, which is the runaway the cap exists to prevent.
- Validate by: the transcript sets the pathological interval and asserts EXTRACT creates nothing and
              says why.
```

## 6. Plan  (workflow.md §6)

### Approach

One command, in the Commands layer, in four parts. It is deliberately small: TASK-085 already built
everything that computes anything, so this task is a translation from one representation to another
plus the reporting REQ-201 requires.

1. **Resolve.** Find the surface by name (`FindSurfaceIndex`, the case-insensitive user-facing one —
   this is a name the user typed). Resolve its style through `SurfaceStyles::Resolve`, the same call
   the display pass makes. Refuse, with the reason, when: the surface does not exist; it has never
   been built; both contour components are off; or the level count exceeds the display cap
   (ASSUMPTION-2).
2. **Generate.** Compute the surface's Z range and split the levels exactly as
   `RefreshSurfaceDisplayGeometry` does — majors removed from minors so no level is extracted twice —
   then call `GenerateContours` once per component. **The split and the cap must be the same code the
   display pass uses, not a second copy**: two functions that agree today drift tomorrow, and the
   acceptance condition is precisely that they never disagree.
3. **Bake.** For each contour, append its vertices to `userPolylineVerts`, push the offset, push the
   `closed` flag straight through from the `ContourResult`, and push an `EntityAttributes` carrying
   the target layer plus the component's resolved colour / linetype / lineweight (Q2). Ids are left
   at 0 and picked up by the next `EnsureEntityIds` sweep, like every other created entity.
4. **Report.** Count per component, the interval each was extracted at, the layer, and whether a
   component was skipped and why (REQ-201, Q3, Q4).

### The one thing that is not mechanical

`SplitContourLevels`, `ContourLevelCount` and `kMaxContourLevels` are currently file-static in
`CadCommands.cpp`, written for `RefreshSurfaceDisplayGeometry`. EXTRACT needs the same three, and
copying them is what would break the "exactly the displayed contours" condition six months from now.
They are lifted into one small internal helper that both callers use — a function extraction inside
one translation unit and one subsystem, not a new abstraction (rule 2 needs two present-day concrete
uses; this has exactly two).

### Files / functions to touch

| File | Change |
|---|---|
| `commands/CadCommands.cpp` | one shared `ResolveSurfaceContours(...)` helper used by both the display pass and EXTRACT; `ExecuteExtractCommand`; registry entry + dispatch. |
| `commands/CadCommands.hpp` | declare the command entry point. |
| `tests/headless/HeadlessDriver.cpp` | nothing new expected — `EXPECT POLYLINES` already exists, which is the count that matters. |
| `tests/headless/transcripts/req071-contour-extract.txt` | **new** — the acceptance conditions end to end. |

### Test approach

- **Happy path:** EXTRACT on the demo surface creates polylines whose count equals the displayed
  contour count; the report names the count, both intervals and the layer; a named layer is created.
- **The REQ-101 / "exactly displayed" condition:** the extracted vertices are compared against the
  display cache's own contour segments — same surface, same style, vertex for vertex. This is the
  assertion ASSUMPTION-1 stands on, and it is stronger than re-deriving an expectation, because it
  compares the two things the requirement says must be equal.
- **Failure modes, one per acceptance bullet:**
  - extracting twice → two independent sets; erasing one leaves the other (independence, not merely
    "twice the count");
  - **rebuilding the surface afterwards leaves the polylines untouched** — asserted on the count AND
    on the vertices, since a rebuild that regenerated them would keep the count identical;
  - **erasing the surface leaves them** — the statement says this separately from the rebuild case;
  - both contour components off → creates nothing, and says so;
  - the pathological interval (ASSUMPTION-2) → creates nothing, and says so;
  - an unbuilt surface, and a name that does not resolve → refused by name, not a crash.

### Steps

- [x] 1. Lift the level-splitting and cap into a shared helper; prove the display path is unchanged
        (the TASK-085 transcript must still pass, byte for byte, before anything else is written).
- [x] 2. `ExecuteExtractCommand` + registry + dispatch.
- [x] 3. The transcript, one section per acceptance bullet.
- [x] 4. Self-verification (§9) and the completion report.

## 7. Workflow-specific notes

- Feature (workflow.md A). Pre-flight: owning subsystem named; no new abstraction (§3); the failure
  modes are decided in step 1 above, before the happy path; Q1 and Q2 answered by the user before any
  code, Q3 and Q4 self-resolved from the requirement's own wording and recorded so they are
  reviewable rather than silent.
- **Not tests-first here**, and the reason is worth stating: the generator this depends on is already
  tested to the vertex (`ContourGenTests`), so the risk in this task is not "is the geometry right"
  but "is it the SAME geometry the screen shows, and does it stay put afterwards". Both are
  end-to-end properties, so the transcript is where they belong.

## 8. Implementation log

- 2026-08-22 — opened. Authority, scope, boundary check and plan written. Q1/Q2 put to the user and
  answered before any code; Q3/Q4 self-resolved. Baseline confirmed green (519/519 ctest) with
  TASK-086's in-flight `util/surfaceanalysis` present in the tree but untouched by this task.

- 2026-08-22 **step 1 done, and done first for a reason.** `ResolveSurfaceContourLevels` now owns the
  whole contour decision — Z range, the arithmetic cap check, then the level split with the majors
  removed from the minors — and `RefreshSurfaceDisplayGeometry` was rewritten to call it. The full
  suite was run **before any EXTRACT code existed**, and TASK-085's transcript passed with its exact
  segment counts unchanged (867 minor / 203 major / 16 border), which is what proves the extraction
  was behaviour-preserving rather than merely compiling.

- 2026-08-22 **steps 2-3 done.** `EXTRACT <surface>[, <layer>]` in `CadCommands.cpp`, plus
  `EXPECT EXTRACTMATCHESDISPLAY` in the REQ-203 driver and
  `tests/headless/transcripts/req071-contour-extract.txt` (76 steps). Full suite **521/521 green**.

  Four decisions worth recording:

  1. **The contours are REGENERATED, not read back from the display cache** (ASSUMPTION-1). Same
     triangulation pointer, same resolved style, same `ResolveSurfaceContourLevels`, same pure
     `GenerateContours` — so "exactly the displayed contour elevations" holds by construction. Reading
     the cache back would also have been *harder*: it stores the flattened `GL_LINES` form, which has
     already discarded the per-contour offsets and the closed flag a polyline needs.
  2. **`closed` is carried straight through from the generator**, never re-derived. Deciding whether a
     polyline closes by comparing its first and last vertex would reintroduce exactly the float
     comparison `contourgen` chains topologically to avoid.
  3. **One undo snapshot for the whole command**, including the layer it may create.
     `CadAddDrawingLayer` pushes a snapshot of its own, so the obvious implementation would cost two
     presses of undo to reverse one command; the layer row is added inline instead. Pinned by the
     transcript's UNDO/REDO block rather than left as a claim.
  4. **A contour of fewer than two vertices is skipped.** A level sitting exactly on a peak is a
     single point (see `contourgen`), and a one-vertex polyline is not geometry anyone can use.

  **The load-bearing test is `EXTRACTMATCHESDISPLAY`, and it was checked for teeth.** It compares the
  display cache's own contour segments against the segments the created polylines carry, as sorted
  multisets, exactly — not to a tolerance, because both sides are float copies of the same doubles out
  of one pure function. A wrong-but-plausible extraction (one interval out, or the style's *previous*
  contours) has the same polyline count as a correct one, so a count assertion could not catch it. It
  was then **deliberately made to fail** — extract, move the style's interval, and the two sides
  diverge — and that negative case is now in the transcript so the assertion cannot quietly become
  vacuous later.

  One rough edge found by self-review rather than by a test: when levels existed but every contour at
  them was degenerate, the report read `"...": — 0 polyline(s) on layer "0"`, a malformed sentence
  that also read as a success. It now says nothing was created, and why.

## 9. Self-verification (run BEFORE submitting — verification/skills/)

- [x] build-project — clean, no new warnings. Reconfigured so the new transcript is picked up by ctest
      (a glob-added file is invisible until then).
- [x] architecture-review — no Workshop architectural decision. EXTRACT is named in ADR-028 (b) and
      required by an accepted REQ; the geometry comes from an existing pure module; the entities are
      ordinary polylines in the existing store with no new kind, no new store and no `.gs` change.
      `ResolveSurfaceContourLevels` is a **function extraction with two present-day concrete callers**
      (rule 2's bar), not a new abstraction — and it exists specifically to stop the display path and
      the extraction path drifting, which is the acceptance condition.
- [x] code-review (self) — found the degenerate-report wording above. Also confirmed: the layer is
      resolved and validated BEFORE the snapshot, so a bad layer name refuses without leaving an undo
      step that did nothing; an existing layer is matched case-insensitively and reused under its own
      spelling, so `c-topo` cannot create a second `C-TOPO`.
- [x] dependency-audit — n/a (no manifest touched; no new module).
- [x] performance-review — EXTRACT is a one-shot command, not a per-frame path, so REQ-100 does not
      reach it. The one performance-shaped rule that DOES apply is ASSUMPTION-2's cap, and EXTRACT
      shares the display path's ceiling rather than having its own — which is both the correct
      behaviour and the cheaper one.
- [x] testing — 521/521 ctest green. One section per acceptance bullet, plus the negative case that
      proves the central assertion can fail.

## 10. Verification result

- Submitted:  2026-08-22 (self-verification; independent review not yet run)
- Verdict:    **PASS (self)** — every acceptance condition is asserted end to end and green.
- Findings:   one, self-raised and resolved: the degenerate-extraction report read as a success. No
              blocking findings outstanding.

## 11. Outcome

- Requirements satisfied: **REQ-071** (Acceptance met: **yes** — all five conditions, each mapped to a
  named section of `req071-contour-extract.txt`).
- Tests added:            `tests/headless/transcripts/req071-contour-extract.txt` (76 steps);
                          `EXPECT EXTRACTMATCHESDISPLAY` in the REQ-203 driver.
- Refactors:              `ResolveSurfaceContourLevels` — the shared contour decision, proven
                          behaviour-preserving before any new code was written.
- Docs updated:           this task log. `spec/roadmap.md`'s **Next** entry for REQ-071 is now done and
                          should move when the roadmap is next revised — **not edited here**, because
                          the roadmap records decisions and closing an item is one (CLAUDE.md: the
                          spec changes by a recorded decision, never as a side effect).
- Done:                   2026-08-22

```
COMPLETION REPORT — TASK-087 — 2026-08-22
- Requirements satisfied:  REQ-071 (Acceptance met: yes)
- Summary:                 EXTRACT bakes a surface's displayed contours into ordinary, unlinked
                           polylines on a chosen layer, carrying the style's appearance, and reports
                           the count and interval of each component.
- Tests:                   req071-contour-extract.txt (76 steps, happy + every failure mode);
                           EXTRACTMATCHESDISPLAY, proven able to fail. ctest 521/521 green.
- Verification verdict:    PASS (self) — findings resolved: 1 (degenerate-extraction wording)
- Assumptions:             ASSUMPTION-1 validated by EXTRACTMATCHESDISPLAY; ASSUMPTION-2 validated by
                           the too-fine-interval section.
- Architectural decisions: none made by Workshop (escalated: none)
- Dependencies:            none added
- Technical debt noted:    none new. TASK-085's DEBT-1 (surfaces are never plotted) is unchanged by
                           this task and, if anything, slightly relieved: extracted contours ARE
                           plotted, because they are ordinary polylines. That is a workaround, not a
                           fix — it requires the user to extract before plotting, and plotting a
                           styled surface directly still has no requirement behind it.
- Build:                   reproducible, clean
- Docs updated:            workshop/tasks/TASK-087-req071-contour-extract.md
```
