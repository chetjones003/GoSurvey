# TASK-283 — hover and click pick what is visible, through one resolution

- Type:    feat
- Status:  review
- Opened:  2026-09-28
- Owner:   Workshop
- GitHub:  #564 section 2 (third increment; §3 = #566, §1 = #568)

## Requirement authority

REQ-058 as amended 2026-09-28 under D-2026-09-28-e. D-2026-09-16-b (wireframe edges only),
D-2026-09-02-g (hover aperture), REQ-201, issue #166 (HoverPickGate budget).

## Decisions (D-2026-09-28-e)

- **Q1 (from the issue): pick what's drawn** — Wireframe see-through, Hidden/Shaded opaque.
- **Tie rule (found in the code): nearest the eye wins, then nearest the cursor** — for hover and
  click alike. The default click used to take the first-DRAWN line on a height tie.

## Why

The hover resolved by family priority, not depth: linework always beat a solid in front of it; text,
tables and fills were tested at the work-plane point, not on the ray; and hover and click were two
code paths with different tie rules.

## Plan / architectural-boundary check

The resolution is geometry, so it lives in the command layer (`ResolveViewportPick`,
`ResolveViewportClickPick`), reachable from Catch2. The UI keeps only the per-family side effects
(grips, MTEXT links, the popup) and asks the resolver which family answered.

## What changed

- `src/commands/CadCommands.hpp/.cpp`
  - `ViewportPickFamily`, `ViewportPickRequest`, `ViewportPickResult`, `ResolveViewportPick`,
    `ResolveViewportClickPick`.
  - `PickCadAnnotationAt` / `PickFilledRegionAt` take an optional ray: orbited, each item is tested
    where the ray meets its own plane.
- `src/ui/CadUi.cpp` — the hover, the idle click and the SelectionAccumulate click all ask the
  resolver; `PickSolidUnderCursor` removed (its rule moved into the resolver); the idle click's own
  depth re-pick removed (the resolver applies the same rule the hover does). A dim under the cursor
  now answers the hover too but is not lit (dims never pre-highlight), so the hover no longer lights a
  line UNDER a dim that the click would not take.

## Tests (`tests/ViewportPickDepthTests.cpp`, GoSurveySnapTests)

Plan Shaded hides a line under a box; Wireframe does not; a line above / on the top face still wins;
orbited back line hidden in Shaded, visible in Wireframe, front line wins; text under a box hidden in
Shaded; flat parallel lines — the one under the cursor wins and both stay in the popup list; plan with
no solids matches `PickClosestCadEntity`; a 25×13 orbited grid where every hover-lit pixel's click
takes the same entity. Full suite 1833/1841 — the same 8 pre-existing failures.

## Found while testing

The first orbited wireframe case failed because the ray grazed a box edge within the 3-unit pick
tolerance — which is correct behaviour (the edge is what is drawn there); the test was re-aimed clear
of the edges rather than the rule changed.

## Not in scope

Tables are still hit at the work-plane point (they carry no elevation). The GUI hover itself needs a
hand check — synthetic mouse input cannot produce a hovered frame.
