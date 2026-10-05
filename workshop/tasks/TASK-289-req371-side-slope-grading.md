# TASK-289 — GRADING: side slopes out to a surface, and the daylight line

- Type:    feat
- Status:  review
- Opened:  2026-10-05
- Owner:   Workshop
- GitHub:  REQ-371, decision D-2026-10-05-b

## Requirement authority

**REQ-371** (proposed here, with this implementation — the fork workflow's rule 1: draft the REQ,
build against it, ship both in one PR). **D-2026-10-05-b** records the decisions and reverses
ADR-028's "grading design objects" exclusion. Constrained by **REQ-074** (slope wording; never
extrapolate past a surface edge), **REQ-087/088** (the feature line this grades from), **REQ-101**
(±0.002 ft), **ADR-054** (`double` stores), **REQ-361** (new objects on object layers).

## Why this exists, in one paragraph

It started with the user asking what PADSOLID was actually *for*, and correctly pointing out that
`PADSOLID` is a name GoSurvey invented which exists in no AutoCAD or Civil 3D release. The honest
answer: it is the volume engine grading will call, not a feature a designer would use — it builds a
pad with **vertical walls**, and no site can be built that way. Side slopes are the half that makes
it buildable, and unlike PADSOLID they correspond to a real tool (Civil 3D's Grading Creation
Tools), so the result can be checked against a reference instead of against our own claims.

## What it does

`GRADING` — select a feature line, then sit on an options prompt:

```
GRADING - Enter to grade, or [Cut/Fill/Surface] <cut 2:1 (50.00%), fill 3:1 (33.33%), Existing Ground>. ESC cancels.
```

`C` and `F` set the two slopes, `S` the surface, `D` the side (open baselines only). **Enter grades.**
Each slope accepts `3:1`, a bare `3`, or `25%`. Everything is remembered between runs, because a site
is normally graded to one surface at one pair of slopes and only the baseline changes.

The result is the **daylight line**: the chain of points where the side slopes meet existing ground,
committed as a **feature line** named `<baseline> Daylight`.

## Choices made from the code, recorded rather than asked

- **The output is a feature line, not a polyline.** The daylight line carries a real elevation at
  every point, which is REQ-087's definition of a feature line. It also means REQ-361 is satisfied
  with `ObjectLayerKind::FeatureLine` — adding a `Grading` kind would bump
  `kObjectLayerKindCount`, which is persisted, for no gain. And it sets up the follow-on: a graded
  design surface wants the daylight line as a breakline, which only a feature line can be.
- **Separate cut and fill slopes**, per D-2026-10-05-b. A cut face stands steeper than placed fill.
- **The march step is scaled to the surface's own triangle span**, not a constant, so REQ-371's
  stated known limit tracks the data. Derived as `sqrt(plan bbox area / triangle count) / 2`,
  clamped to [0.05, 10] ft.
- **Elevation points are graded too**, not just PIs. Both carry a design elevation and both lie on
  the line, so including them gives a denser daylight line at no cost.
- **A closed baseline decides its own outward from its signed area**, not from vertex order —
  vertex order is an accident of how it was drawn, the enclosed side is not.

## The risk, taken first

The whole feature is one piece of arithmetic, and the failure mode is a *plausible wrong answer*: an
offset out by a slope factor looks perfectly reasonable in a viewport. So the solver was written as a
throwaway probe and proven against **hand-computed** answers before any requirement text, command or
entity existed — the same discipline that caught `MakeHeightFieldSolid`'s `+Z` normal bug.

15 probe cases passed first time, including a 3:1 fill up a 20% ramp (18.75 ft), the same fill *down*
it (75 ft), a 2:1 cut up it (16.667 ft), both honest refusals, and every offset reproduced at
E 2,196,000 / N 1,400,000. The probe is not committed; `tests/DaylightTests.cpp` is its permanent form.

## Two real bugs the tests caught

Both were in work written after the probe, which is the point of promoting the probe into tests:

1. **The outward normals pointed INWARD.** `sideSign` was applied to the right-hand normal while the
   code reasoned about the left-hand one, so a closed ring graded into its own interior. Caught by
   asserting the dot product of each normal with the vector from the ring's centre is positive —
   i.e. asserting the *claim*, not the implementation.
2. **`leftSide` produced the right-hand normal.** The parameter and the behaviour disagreed. The
   first version of that assertion had the two backwards *and agreed with the code* — exactly the
   trap a test written from the implementation falls into. Both are now expressed against the
   left-hand normal, so the naming is honest.

A third defect was in the fixture, not the product: the plane fixture folded its reference x into an
intercept against the footprint corner and silently put the ground 200 ft above where every comment
said it was. The fixture now takes the reference point explicitly.

## Tests

- **`GoSurveyTests [daylight]`** — 12 cases, 74 assertions, all against hand arithmetic stated in the
  test itself. Fill, cut, cut-and-fill-differ (the same 6 ft travelling 24 ft one way and 12 ft the
  other, so one slope used twice cannot pass), sloping ground in both directions, already-on-grade,
  the four refusals, slope-percent conversion, closed-ring winding independence, open-baseline sides,
  survey magnitudes, and step independence.
- **`headless.req371-grading-daylight`** — 94 steps through the **registry and the typing box**
  rather than around them. That is deliberate: PADSOLID's reported defect lived exactly there, where
  unit tests that call the command directly cannot reach.

### One fixture note worth keeping

The transcript's first site used a 20% ramp and a 22 ft cut, and GRADING **correctly refused it** —
at 4:1 that needs ~200 ft of run and the surface ran out at 40. The refusal was right (REQ-074), but
a fixture that only ever exercises the refusal proves nothing about the success path, so the relief
was reduced until the slopes actually land.

## Not in scope (REQ-371 says so explicitly)

- the **earthwork volume** between baseline, slopes and ground — without a design surface there is
  nothing for `ComputeSurfaceVolume` to measure against, so it would mean new integration code;
- a **graded design surface**, which is what Civil 3D's grading groups produce and which would give
  volumes, contours and statistics free from tools already built. **The valuable follow-on;**
- **linking** the daylight line to its baseline or surface (baked only, EXTRACT precedent);
- **intermediate slope breaks** (a bench or berm part-way down).
