# TASK-287 — dynamic input shows which mode it is in

- Type:    feat
- Status:  review
- Opened:  2026-09-28
- Owner:   Workshop
- GitHub:  #564 section 5 (seventh increment; §3 = #566, §1 = #568, §2 = #569, §4 = #570, §6 = #576,
  §7 = #577)

## Requirement authority

REQ-354 (new, D-2026-09-28-j), amending REQ-024 (the dynamic-input field group). Issue #564 Q3 (a Z
field whenever the view is not plan-to-the-current-UCS) was answered on the issue.

## Decisions (D-2026-09-28-j)

Asked in plain English, both decided as recommended:

1. **Every point prompt reads a typed Z.** The shared point parser (`ParseStoragePointZ`) read two
   numbers and silently dropped a third at LINE, CIRCLE, RECT…, so a Z box there would have been a
   field nobody reads. It now accepts `x,y,z` / `@dx,dy,dz` and publishes the Z through the
   existing `resolvedPointZ` channel that `CadCommitElevation` already reads.
2. **At an anchored prompt (Distance < Angle), a comma switches to absolute X / Y** — the command
   line's own grammar (`5,5` is a coordinate), not AutoCAD's relative default. `@` switches to
   ΔX / ΔY.

Recorded without a question: at a prompt with no relative base (a first point), `@` still switches
the boxes, and the command refuses the relative point by name as it always has; PIPERUN gains
relative `@dx,dy[,dz]` from its last vertex (it had no relative entry at all), and its next point is
an anchored Distance < Angle prompt like LINE's (REQ-024: "any rubber-banded segment"), with a
distance typed alone still handed to PIPERUN's compass direct-distance entry.

## Starting state (measured)

| Item | Before |
|---|---|
| `@` typed in X | locked Y, submitted X verbatim; labels unchanged; Y box dead |
| `<` typed in X | same — and the command line has no polar grammar, so it failed to parse |
| `5,5` typed in LINE's Distance box | read as distance 5 along the cursor (silent) |
| keyword (`C`, `U`, `END`) in LINE's Distance box | read as the live distance (silent segment) |
| typed `x,y,z` at LINE / CIRCLE / RECT | Z silently dropped (`ParseTwoDoubles` reads two) |
| Z field | none |
| PIPERUN | no point dynamic input (single field); no `@` relative entry |

## Plan / architectural-boundary check

- **Commands** — new `src/commands/CadDynInput.{hpp,cpp}` (no ImGui): the prompt classification
  moved out of `CadUi.cpp` (`CadCommandExpectsPointEntry`, `CadUcsPolarPromptBase`) plus
  `CadDynInputPromptFor` (default mode, relative base, Z shown, direct-distance), and the
  `dyninput::Group` model: mode, fields, locks, mode-character handling, Backspace revert, labels,
  and `Compose` (the submitted text). The UI and the headless driver both drive this one model.
- **Parser** — `ParseStoragePointZ` accepts an optional third component (strict count), absolute
  or relative (from the caller's base Z, else the work plane). `ParseStoragePoint` passes a base Z
  through; LINE / POLYLINE pass the anchor's. PIPERUN's `ParseSolidBasePoint` gains a relative base.
- **UI** — the X/Y block and the anchored Distance/Angle block in `DrawDrawingViewport` become one
  block drawn from the model (labels per field, `@` badge, a character filter feeding the model,
  Backspace revert, a one-shot forced text push). REQ-154's UCS polar pair is untouched.
- **Headless** — `DYN <text>`, `DYNKEY TAB|BACKSPACE|ENTER`, `EXPECT DYNLABELS …` directives.
- No new dependency, no persistence change.

## Tests

Unit (`tests/DynInputTests.cpp`): mode transitions, Backspace revert keeps typed text, labels per
mode (2 / 3 fields), Compose per mode, keyword pass-through, parser Z.
Headless transcripts (`issue564-dyninput-*.txt`): each mode's labels and committed point in LINE,
MOVE, COPY and PIPERUN, and a typed Z at LINE.

## Results

- `[req354]` (GoSurveySnapTests): 12 cases, 120 assertions, all pass.
- Transcripts `issue564-dyninput-modes`, `issue564-dyninput-modify-z`: pass.
- GUI (Developer Shell, `build/devshell`): `req354-dyninput-modes` passes — real keystrokes into the
  boxes (`,`, `@`, `<` with a bearing, Backspace undoing `<`) land the asserted points.
- Full suite: 1884/1891. The 7 failures (issue233, issue402-offset-ucs, regression-58, req068,
  req087, req313-solid-isolines, req313-solid-primitives) are the same set that fails on unmodified
  `beta`; none types a three-number point.

## Found in the final review (code review on #578)

1. PIPERUN read `x,y` in the UCS but the typed Z as a WORLD elevation (`ParseSolidBasePoint`'s rule
   for the solid commands), while its new Z box shows a UCS Z — under a Front UCS the typed Y was
   lost. PIPERUN now reads `x,y,z` wholly in the UCS (`zInUcs`); the solid commands are unchanged.
2. The field callback re-processed a mode character the model deliberately leaves in a box (a comma
   in Z, `5@`) every frame, pinning the caret. It now processes only text the model has not seen.
3. A typed Z lingered as the base of a later `@dx,dy,dz` in the headless driver. Every parse now
   drops a Z typed for an earlier point, and the GUI clears the flag when it re-publishes the cursor.

Each of 1 and 3 has a regression test; 2 is covered by the GUI test still passing.

## Verification

- build-project: release + devshell trees build clean.
- architecture-review: PASS — the model is in Commands (no ImGui), UI and headless driver both
  depend downward on it; the function-static UI state replaces the old blocks' own statics.
- code-review: PASS — see the PR's final review.
- dependency-audit: PASS — none added.
- performance-review: PASS — per frame, one prompt classification and up to three live readings.
- testing: PASS — above.

## Not in scope / technical debt

- PIPERUN's wall prompt ignores a bare Enter on `beta` (the blank-Enter block, fixed on the
  unmerged `feat/pipe-fitting-palette` branch); the transcript types the wall.
- A relative point with no dz still lands on the work plane rather than at the base point's Z —
  unchanged behaviour, not what §5 asks about.
- The command line itself still has no `d<a` polar grammar; the boxes resolve polar entry.
- The Δ labels rely on the UI font (Tahoma) having Greek glyphs, which it does.
