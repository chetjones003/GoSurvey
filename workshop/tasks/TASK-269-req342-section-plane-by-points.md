# TASK-269 — REQ-342: SECTIONPLANE places a plane on a section line, not only on a face

- Type:    feat
- Status:  in progress (user testing in the app before it is sent)
- Opened:  2026-09-18
- Owner:   Workshop
- GitHub:  — (asked for directly from the real app; REQ-342 revision of this date)

## Requirement authority

REQ-342, revised on this date under D-2026-09-18-b. REQ-201 (a refusal names its reason).

## Why

Reported from the real app, with AutoCAD screenshots: *"i have not selected a face but have instead
selected a different part, for this instance it is the mid point of this torus. once i have placed the
sectionplane it cut off what was on that side of the plane just like we would want it to do. ours
currently can only do sectionplanes off of a face of an object. i would like it to be more versitile.
this seems like a better way to get a section for 3d objects that do not have a face such as a sphere
or torus."*

AutoCAD asks for both in one breath — "Select face or any point to locate section line", then
"Specify through point". A sphere and a torus have **no flat face at all**, so GoSurvey's face-only
form could not aim a plane at one by any click.

## What changed

- `src/commands/CadCommands.hpp`:
  - `AppCommandState::SectionPlanePhase` (`PickFaceOrPoint`, `WaitThroughPoint`) and
    `sectionPlaneP1`. The old comment said a phase enum would be an abstraction with no second use;
    there is now a second use.
  - `CadSectionPlanePromptText` takes the state, because the prompt is now per phase.
- `src/commands/CadCommands.cpp`:
  - `ApplySectionPlaneFromLine`: the plane through two points, square to the work plane — normal
    `cross(b - a, workZ)`. Same clean start as the face form (offset 0, no flip, fresh extent,
    selected on creation). Refuses two points in the same place, and a line square to the work plane.
  - `SubmitSectionPlaneFacePick`: a flat face still answers in one click. Anything else — curved face,
    edge, vertex, empty space — is a point, taken **on the geometry it hit** (`solidpick::Pick::point`)
    or otherwise where the ray meets the work plane, and the command asks for the through point.
  - `SubmitSectionPlanePointPick` and `HandleSectionPlaneTextInput`: the same two points from a plan
    click or typed, so the command is drivable without a mouse.
- `src/ui/CadUi.cpp`: the prompt call passes the state.

## Tests

- `headless.req342-section-plane-by-points` (new): a torus given a plane from two points in empty
  space (normal across the line, level with the work plane); a torus given one from two points **on
  the torus itself**; a sphere from typed points; the same-point refusal, and ESC leaving the previous
  plane where it was.
- `headless.req342-section-plane`: the "click that misses" case is now the first point of a section
  line — the case's own rule (the command stays open, nothing placed) still asserted, plus ESC.
- `SubObjectSelectionTests [sectionplaneface]`: a miss, an edge and a cylinder wall now start a line
  at the point that was hit; two points place the plane; the same point twice is refused by name; the
  flat face and flat cap cases are unchanged.
- Full suite: 1625/1632; the 7 failures are `beta`'s own.

## Open

- The user is testing this in the app before it is sent. The app binary could not be relinked while
  their GoSurvey was running (`LNK1104`), so the build for testing is pending.
- Not yet decided: whether this ships as its own PR after #541, or folded into it.
