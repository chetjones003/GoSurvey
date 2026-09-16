# TASK-258 — SECTIONPLANE: put the clip plane on a face, and make it visible

- Type:    feat (new requirement + new ADR)
- Status:  review
- Opened:  2026-09-11
- Owner:   Workshop
- GitHub:  #479 slice 1 of 4 — acceptance 1, 2 and 3.

## 1. Authority

- **REQ-342** (new, accepted 2026-09-11, D-2026-09-11-b) — the requirement this delivers.
- **ADR-059** (new) — the six decisions behind it. A new selection kind and a new render behaviour
  need an ADR and an accepted REQ before implementation (CLAUDE.md §4); both were drafted locally
  and ship here with the code, per the one-PR rule.
- **REQ-341 / ADR-058 / D-2026-09-10-e** — the clip plane this aims. Slice 1 changes how the plane
  is *chosen*, not how it is *applied*: `SectionClipToShaderVec4` and the `gl_ClipDistance` path are
  untouched.
- **REQ-311 / D-2026-08-31-e** — `ucs::Ucs` IS the plane abstraction. This is the decision that makes
  create-from-face a handoff rather than a conversion.
- **REQ-318 / ADR-049** — the sub-object pick this borrows. It is *borrowed*, not extended: the face
  is an answer to a question, not a lasting selection.
- **REQ-201** — refuse by name, leave the previous state intact.
- **REQ-101** — ±0.002 ft, checked on the hatch at E 2,196,000.
- **TASK-099 / REQ-121** — the routing layer, which is where most of this task's risk turned out to be.
- Constraints: CON-06 smallest change.

## 2. Problem

#479 acceptance 1–3: click a flat face, get the section plane on it, drawn so it can be found.

Two gaps, one obvious and one not.

**The obvious one.** REQ-341's plane is "the active UCS plane, offset along its Z". A face-derived
plane is an arbitrary world plane that must *not* follow the UCS, so that representation cannot
express it at all.

**The one that mattered more.** A command asking for a *face* had no way to receive the click.
`ViewportClickRouteFor` had no route meaning "this command wants a face", and the sub-object pick was
dispatched **above** the route table, gated on `Ctrl` — with the hover carrying a **second, separate**
`Ctrl` gate (`CadUi.cpp:13702` and `:14578`). Left alone, that reproduces both bugs the previous
slice shipped and the user found by hand:

- *"it takes me out of the section and just selects the object by itself"* — the click falling
  through to entity selection;
- *"that click works, it is just not highlighting the object"* — the hover gate, still closed after
  the click gate was fixed.

## 3. Approach

Written up in full in `Desktop\Notes for claude\issue479-sectionplane\GAMEPLAN.md`.

**Probes first.** `probes/p1_face_frames.cpp` links the shipping kernel (`brep.cpp` + `nurbs.cpp` +
`curveintersect.cpp`) and asks two questions:

- **P1 — is a planar face's frame usable as the plane directly?** Yes, with no conversion. 22 of 22
  planar faces across BOX, WEDGE, PYRAMID, CYLINDER and CONE had their frame origin on their own
  plane with deviation exactly `0.000e+00`, and Z the outward normal, identically at the origin and
  at E 2,196,000 / N 1,400,000.
- **P2 — does that hold for faces no primitive built?** Yes. `BooleanSubtract`, `BooleanUnion` (46
  planar faces each) and an **oblique** `Slice`: no counterexample.

P1 also produced the thing that would otherwise have been assumed wrong: **the frame origin is not
the face's centre.** On a 40 × 30 × 12 box's side face it sits 6 ft below it, on the bottom edge.
Irrelevant to the plane maths, fatal to the drawing.

**Then the code**, in the order the risk runs:

1. `SectionPlaneGraphicsFor` in `SectionClip.hpp` — GL-free, header-only, so the geometry is testable
   without a window (ADR-002).
2. The route: `ViewportClickRoute::SubObjectFacePick` + `ViewportIsFacePickStep`.
3. The command: `StartSectionPlaneCommand`, `SubmitSectionPlaneFacePick`, and
   `CadEffectiveSectionClipFrame` as the single place that decides which plane is in force.
4. The draw, beside REQ-341's existing indicator block, in the overlay pass.

## 4. A wrong answer the probe produced first

Worth recording, because believing it would have sent this task somewhere it had no business going.

The first outward-normal test asked whether the solid's **centroid** lay behind each face plane. It
reported **3 inward planar faces on the SUBTRACT result and 2 on the UNION** — a kernel bug, if
believed.

The probe was wrong, not the kernel. That test is only valid for a **convex** solid, and a boolean
result is not one: on a box with a notch the centroid legitimately sits *in front of* a concave
face's plane while that face's normal is perfectly outward. The fixture did not reach the check —
the same failure shape recorded in `gosurvey-fixture-reaches-the-check`.

What settled it needed no new test: the reported volumes were `13632.000000` and `15552.000000`,
exactly 14400 − 768 and 14400 + 1920 − 768 by hand. `ComputeMassProperties` integrates volume by the
divergence theorem **over the face normals**, so one flipped normal changes the answer by that face's
whole contribution. An exact volume is a collective proof that all 46 are right.

The rewritten test steps ±0.05 off each face along its frame Z and asks the shipping tessellation
whether each point is inside, by parity ray casting in **three** directions, reporting *untrusted*
rather than guessing when they disagree. 15/46 and 16/46 boolean faces come back untrusted — the
interior point is the face's vertex mean, which is not inside an L-shaped face — and those rest on
the volume evidence. Every face the cast could answer for answered "outward".

## 5. The bug this task nearly shipped

The first implementation added the face branch inside `CadUi.cpp`'s `case ViewportClickRoute::IdleSelection:`,
beside the existing `Ctrl`+click block, and OR'd `ViewportIsFacePickStep` into that block's two gates.

That code is **unreachable**. `IdleSelection` is a *different route*: a command routed to
`SubObjectFacePick` never enters that case, so the click would have fallen out of the switch doing
nothing at all — the exact failure `ViewportClickRouteFor` exists to prevent, reintroduced one level
down.

**It compiled clean.** The switch has no `default:`, and the codebase's comments say the compiler
objects when an enumerator is added — but `/W4` does **not** include C4061/C4062, so nothing did.
Caught by reading the switch after the build passed.

Recorded because the lesson generalises: *"the switch is exhaustive so the compiler will tell me" is
only true if the warning is enabled.* Four route enumerators and one `Kind` were added across this
and the previous slice; none of them would have been flagged.

## 6. What was built

| file | change |
|---|---|
| `src/render/SectionClip.hpp` | `SectionPlaneGraphics`, `SectionPlaneGraphicsFor`, the two density constants |
| `src/render/ViewportRenderer.{hpp,cpp}` | `sectionPlaneGraphics` in `RenderTuning`; hatch drawn between fill and outline, section line after, all in the overlay pass |
| `src/commands/CadCommands.hpp` | `Kind::SectionPlane`; `viewportSectionClipFrame{,Valid}`; five declarations |
| `src/commands/CadCommands.cpp` | the command, the face rules, `CadEffectiveSectionClipFrame`, dispatch, help row, ESC |
| `src/viewport/ViewportPickPolicy.hpp` | `SubObjectFacePick` route, `ViewportIsFacePickStep`, the route entry, the selection-step entry |
| `src/ui/CadUi.cpp` | the hover gate; a dedicated `case` for the route; the prompt hint |
| `src/app/main.cpp` | the effective frame, and building the graphics each frame |
| `tests/headless/HeadlessDriver.cpp` | `EXPECT SECTIONCLIPFRAME`, `EXPECT SECTIONCLIPNORMAL`, and a `CLICK` case for the route |

## 7. Verification

**Full suite 1489/1489**, up from 1478.

| | |
|---|---|
| `SectionClipTests` `[sectionplane]` | 7 cases / 390 assertions |
| `SubObjectSelectionTests` `[sectionplaneface]` | 3 cases / 52 assertions |
| `ViewportPickPolicyTests` `[req342]` | the route, the predicate, and the exhaustive-command list |
| `headless.req342-section-plane` | 71 steps |

**Every new behaviour was proven to bite**, by reinstating it as a bug and watching the test fail:

| mutation | result |
|---|---|
| `K::SectionPlane` routes to `Ignore` | `ViewportPickPolicyTests` fails, 2 assertions |
| hatch drawn across the full range instead of clipped | endpoint 107.26 in a 78-wide rectangle — 29 ft outside |
| hatch spacing a world constant instead of a fraction of the diagonal | 2 segments where 512 were expected |

The transcript uses **CLICK**, not PICK. `PICK` calls `SubmitViewportPick` directly and never touches
the routing layer, which is how REQ-103's five commands shipped with green transcripts and dead
clicks; `CLICK` asks `ViewportClickRouteFor` the same question the viewport asks.

**Two things the transcript deliberately does not test**, both recorded in the file itself rather
than left to be discovered:

- **the curved-face refusal**, because it turns on the pick tolerance and the driver cannot state
  one — `CadOffsetEntityPickTolWorld` is screen-derived and collapses to ~0.002 ft with no window, so
  whether a wall click resolves to the face or a rim would be decided by arithmetic rather than by
  the rule. It lives in `SubObjectSelectionTests` with the tolerance as an argument.
- **the bottom face in plan view.** A plan camera looks straight down, so a ray aimed anywhere on
  the box meets the top face first whatever third coordinate is written. The transcript orbits first
  and takes a side face, and `EXPECT SECTIONCLIPNORMAL` is what proves which face was reached —
  without it every face of a box satisfies `FRAME FACE` and `OFFSET 0` equally.

## 8. Assumptions and debt

- **ASSUMPTION-1.** A planar face's frame Z is outward for faces produced by kernel paths P1/P2 did
  not exercise (`Extrude`, `Revolve`, `Loft`, `Sweep`, `Polysolid`). The `inward` flag is documented
  as meaningless for a plane, and no counterexample exists in what was measured. A wrong answer here
  flips which half is kept on that face — visible immediately, and `FLIP` corrects it.
- **DEBT-1.** `SECTIONCLIP`'s report line still says "along the UCS Z" for a plane that may now be on
  a face. Correct for the offset's meaning, misleading about the plane. Left alone because slice 2
  gives the plane a Properties entry, which is where its identity should be read.
- **DEBT-2.** No automated test asserts anything is **drawn**. There is no GL context in any test,
  and the devshell's test engine drives items rather than pixels. The hatch geometry is measured; the
  fact that it reaches a framebuffer is not.

## 9. Result

**PASS** for #479 acceptance 1, 2 and 3.

Acceptance 4–8 are the remaining slices and are stated as such in REQ-342's revision note: the plane
is not yet an entity (no selection, erase, Properties or `.gs`), has no grips, and its section line
carries no direction arrows.
