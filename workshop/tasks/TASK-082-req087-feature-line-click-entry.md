# TASK-082 — FEATURELINE: click for X,Y, then prompt for the elevation

- Type:    bug + feature
- Status:  done
- Opened:  2026-08-20
- Owner:   chetjones003

## 1. Authority
- Goal:         M-Grading (`spec/roadmap.md`).
- Requirements: **REQ-087** — *"It may be created by drawing it (`FEATURELINE`)"*. "Drawing it"
                means with a mouse; today it can only be typed. Also REQ-201 (every command
                reports) and REQ-203 (headless drivability), which shape how this is built and
                tested.
- User request, 2026-08-20, verbatim: *"i need the feature line creation to be driven like the line
  command, (x,y) coming from a click, then prompt for a typed elevation."*
- Owning subsystem: `commands/`, `viewport/`, `ui/`.

## 2. What is actually wrong

### BUG-1 — a viewport click during FEATURELINE does nothing at all
`K::FeatureLine` is absent from **both** routing lists a point-picking command must appear in:

- `src/ui/CadUi.cpp:9351`, the list that decides a click is a coordinate;
- `SubmitViewportPick` in `CadCommands.cpp`, which has a branch per drawing command.

The comment sitting directly above the first one names this exact failure:

> *"A command missing from this list silently ignores every viewport click and appears to hang on
> its first prompt — which is exactly what RECT did before it was added here."*

That is what FEATURELINE has been doing since TASK-077 shipped it. It is precisely the silent-miss
class ADR-035 (g) warned the separate store would produce, and neither transcript caught it because
a transcript types coordinates — it never clicks.

### BUG-2 — the feature-line draft has no rubber-band preview
`featureLineDraftVerts` is read by nothing in `viewport/`, `render/` or `ui/`. Every other chain
command previews its draft. Typing coordinates without a preview is survivable; **clicking** without
one is not — you would be picking points blind. So BUG-2 has to be fixed for BUG-1's fix to be worth
having.

### The feature
Once a click is a coordinate, it supplies X and Y only. The command then prompts for the elevation,
which is the request.

## 3. Architectural boundary check
- No new store, no new entity, no data-format change, no dependency.
- Adds a phase to one command's existing state machine, which is what every other draw command
  already has. No new abstraction.
- BUG-1's fix is adding a name to two lists and a branch beside the `K::Polyline` branch.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Does a typed `X,Y,Z` still commit without prompting? | — | Yes. It already carries the elevation, so prompting for one would be asking a question that is already answered — and every existing transcript types that form. A typed `X,Y` prompts, exactly as a click does; the rule is "a point that arrives WITHOUT an elevation prompts for one", not "clicks prompt". |
| Q2 | Does an object snap skip the prompt? | — | No — it seeds the DEFAULT. Snapping to a 3D point should offer that point's elevation (the AutoCAD rule `CadCommitElevation` already encodes), but the user asked to be prompted, and a snap that silently committed an elevation would be the one case where the prompt vanished without explanation. Enter accepts it. |

## 5. Assumptions

```
ASSUMPTION-1: The elevation prompt's default is, in order: the snapped point's Z if an object snap
  is active; else the PREVIOUS vertex's elevation; else the work plane (ELEV).
- Because:       grading a feature line means most points sit near the last one, so the previous
                 vertex is the answer that needs least typing. The snap override is the existing
                 CadCommitElevation rule (REQ-058) and inverting it here would make snapping to a
                 3D object silently ignore the object.
- Risk if wrong: LOUD — the wrong default appears in the prompt, in angle brackets, before the user
                 commits anything.
- Validate by:   T3 asserts the default is the previous vertex's elevation, and T4 that a bare
                 Enter accepts it.
```

```
ASSUMPTION-2: ESC during the elevation prompt cancels the COMMAND, not just the pending point.
- Because:       that is what ESC does in every other command in this app, and CancelActiveCommand
                 is the single path for it. A per-phase ESC would make FEATURELINE the one command
                 where ESC means something different.
- Risk if wrong: mild and loud — the user loses a draft and redraws it.
- Validate by:   not asserted; stated so the choice is visible rather than discovered.
```

## 6. Plan

### State (one pending point, plus the armed elevation-point flag)
```cpp
bool  featureLinePendingPoint = false;   ///< X,Y are known; waiting for the typed elevation
float featureLinePendingX = 0.f;
float featureLinePendingY = 0.f;
float featureLinePendingDefaultZ = 0.f;  ///< what Enter accepts — ASSUMPTION-1
bool  featureLineNextIsElevPoint = false;///< armed by a bare `E`, consumed by the next point
```

`E` alone currently errors with *"E marks an elevation point; follow it with X,Y,Z."* — which is
correct today and wrong the moment clicking works, because the click IS the X,Y. Bare `E` now arms
the flag and says so; `E X,Y,Z` keeps working unchanged.

### Sites
| # | site | change |
|---|------|--------|
| 1 | `CadUi.cpp:9351` | add `K::FeatureLine` to the point-picking list — BUG-1 |
| 2 | `SubmitViewportPick` | a `K::FeatureLine` branch beside `K::Polyline` — BUG-1 |
| 3 | `SubmitFeatureLinePoint` (new) | holds X,Y, computes the default, prompts |
| 4 | FEATURELINE command-line block | when a point is pending, the line is an ELEVATION |
| 5 | bare `E` | arms rather than errors |
| 6 | `CadRubberPreview.cpp` | draft chain + rubber segment to cursor — BUG-2 |
| 7 | `CancelActiveCommand` / `ResetAllCadDraftTools` | clear the pending point |

### Prompt wording (REQ-201)
`FEATURELINE — elevation for point N <12.500>:` — the default in angle brackets, which is the
convention SURFELEV and TRIMSTATE already use in this codebase.

## 7. Test approach
Run against unpatched code first; each must fail there.

| id | asserts | oracle |
|----|---------|--------|
| T1 | a `PICK` during FEATURELINE prompts for an elevation | fails — the click is ignored |
| T2 | typing the elevation commits the vertex at that X,Y,Z | fails |
| T3 | the prompt's default is the previous vertex's elevation | fails |
| T4 | bare Enter accepts the default | fails |
| T5 | bare `E` then a click makes an elevation point | fails |
| T6 | typed `X,Y,Z` still commits with no prompt (no regression) | passes — regression guard |
| T7 | typed `X,Y` prompts, same as a click | fails |
| T8 | a non-numeric elevation is refused and the prompt stays | fails |
| T9 | `CHECK ALL` clean; the finished line matches what was clicked | fails |

BUG-2's preview is **not** headlessly testable — `CadRubberPreview` builds vertex buffers for a
viewport the driver does not have. Stated here rather than left as an unexplained gap.

**Oracle result.** The transcript was split into its four sections and run against a build with
`src/` reverted:

```
s1 FAIL no log line contains: FEATURELINE — elevation for point 1 <0.000>:   (click ignored)
s2 FAIL no log line contains: FEATURELINE — elevation for point 1 <0.000>:   (typed X,Y)
s3 PASS  — T6, the regression guard. Correct: typed X,Y,Z is unchanged.
s4 FAIL no log line contains: FEATURELINE — elevation for point 1 <0.000>:   (ESC / draft reset)
```

s3 passing unpatched is the point of it: it is the guard that proves this task did not change the
form every earlier transcript uses.

### Two more omissions found while implementing
Both are the same class as BUG-1 — a feature line missing from a list that every other entity is in:

- **`CancelActiveCommand` had no `FeatureLine` branch**, so ESC out of FEATURELINE reported nothing
  at all. REQ-201 says every command reports. Now says "FEATURELINE canceled."
- **`ResetAllCadDraftTools` never touched the feature-line draft.** `StartFeatureLineCommand`
  cleared it by hand, so a cancelled draft survived until the next FEATURELINE overwrote it. That
  was invisible while nothing read `featureLineDraftVerts` — and BUG-2's preview is exactly a reader,
  so the stale draft would have started being drawn. Fixed with a `ResetFeatureLineDraft` that
  `ResetAllCadDraftTools` calls, which is what every other draft already had.

### One behaviour decided during implementation, not planned
A **second click while an elevation is owed** moves the pending point rather than being ignored. The
click is unambiguous, and silently dropping it would be BUG-1 in miniature — a click that does
nothing. It reports "point moved."

## 8. Completion report

```
COMPLETION REPORT — TASK-082 — 2026-08-20
- Requirements satisfied:  REQ-087's "created by drawing it" — now actually drawable. REQ-201 for
                           the prompt, the refusal, and the cancel message that was missing.
- Summary:                 A viewport click during FEATURELINE places X,Y and prompts for the
                           elevation, with the previous point's elevation as the default. Fixed
                           BUG-1 (clicks silently discarded — the command was mouse-inoperable),
                           BUG-2 (no rubber-band preview), a missing ESC message, and a draft that
                           survived cancellation.
- Tests:                   tests/headless/transcripts/req087-feature-line-click-entry.txt, 76 steps.
                           Three of four sections verified to fail against reverted src/; the
                           fourth is the regression guard and correctly passes there.
                           458 ctest cases green.
- Verification verdict:    PASS
- Assumptions:             ASSUMPTION-1 validated by T3/T4. ASSUMPTION-2 (ESC cancels the command)
                           is a stated choice, not a validated one.
- Architectural decisions: none made by Workshop.
- Dependencies:            none added.
- Technical debt noted:    BUG-2's preview has NO automated coverage — CadRubberPreview builds
                           vertex buffers for a viewport the driver does not have. Manually
                           verified only.
- Build:                   clean, MSVC 14.50.
- Docs updated:            spec/requirements.md (REQ-087 revision note), this task log.
```

## 9. Follow-ups filed
- **FU-1**: the elevation prompt takes an absolute number only. `@2` for "two above the last point"
  would fit the relative-entry convention the coordinate parser already has, and grading work is
  full of "same grade, one foot up". Not built — it was not asked for, and inventing entry syntax
  mid-task is how a prompt grows a language.
- **FU-2**: Civil 3D's elevation prompt offers a **Surface** option — take the elevation from the
  surface under the point. This app has surfaces and `SurfaceElevationsAt` already computes exactly
  that, so it is cheap; what stops it being trivial is that several surfaces can cover one point and
  REQ-088 does not say which wins. Worth a small task with that question answered first.
- **FU-3**: BUG-1 existed because a command was omitted from two hand-maintained lists, and the
  comment above one of them records that RECT was omitted the same way. That is now twice. A
  compile-time check — a switch over `Kind` with no `default:` — would make the next omission a
  build error rather than a silently dead command.
