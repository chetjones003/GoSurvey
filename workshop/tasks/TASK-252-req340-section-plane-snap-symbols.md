# TASK-252 — Snapping the section plane, and drawing its handles as what they do

- Type:    feat (new requirement)
- Status:  review
- Opened:  2026-09-11
- Owner:   Workshop
- GitHub:  #479 slice 3.

## 1. Authority

- **REQ-340** (new, accepted 2026-09-11, D-2026-09-11-d) — the requirement this delivers.
- **REQ-339 / D-2026-09-11-c** — the handles this snaps and re-draws. Its frozen drag axis is what
  the snapped parameter is measured against.
- **REQ-062 / REQ-326 / REQ-325** — the object snap system. Reused whole; this adds a caller, not a
  snap kind.
- **REQ-101** — ±0.002 ft, which is the point of snapping at all.
- **REQ-121** — the object-selection-step suppression, which must keep applying.
- Constraints: CON-06 smallest change.

## 2. Problem

The user, 2026-09-11, with two screenshots:

> i want a sectionplane to be able to snap onto a section just like the way that autocad has it set
> up ... another thing i want to add is some symbols to make the grabbing, flipping, and the
> horizontal and vertical extenders

The first screenshot shows a section plane mid-drag with the Midpoint marker up and the plane about
to land on it. The second shows AutoCAD's grip symbols: arrowheads at the section line's ends, a
back-to-back arrow pair for flip, and small triangles for the height extents.

REQ-339's drag ignored OSNAP entirely and drew all six handles as identical squares.

## 3. Approach

**Snapping** turned out to need no new snap machinery at all, only a caller.

The gate at `CadUi.cpp` is `midCmd`, and it already counts `dimGripMoveActive`,
`entityGripMoveActive` and `mtextGripMoveActive` — grip drags during which no `Kind` is active. A
section-plane drag is exactly that shape, so it joins the list. Everything downstream — the marker,
the aperture, the cursor pull, REQ-121's suppression — then applies unchanged.

The interesting part is what to DO with the snapped point. It almost never lies on the drag axis:
the axis is a line through the handle, and a midpoint is somewhere out in the model. The handle goes
where the point **projects** onto the axis. That is the only reading a one-degree-of-freedom drag
allows, and it is the useful one — the plane is perpendicular to the axis it slides along, so the
projection puts the whole plane through the snapped point exactly.

**Symbols** are drawn in the plane's own basis, sized from the rectangle's diagonal, as triangle
fans plus outlines in the existing overlay block. Move's is the exception and deliberately so: its
double-headed arrow runs along the NORMAL and pokes out of both faces, because it is the one handle
whose travel leaves the plane.

## 4. Two things that moved, and why

**The drag update moved in the frame.** It used to run with the hover, a few hundred lines above the
snap computation. Left there it would have read the PREVIOUS frame's snap — one frame of lag, which
at drag speed shows up as the plane trailing the marker it is supposed to be locked to. The hover
stayed where it was; only the drag moved, and both sites say why.

**The guard I wrote first came out.** The initial version compared the snapped point's distance from
the axis against the cursor's, to decide whether to honour it. That is this code second-guessing the
snap system with a worse rule: `CadSnap::FindBest` already answers only inside a pixel-derived
aperture around a real feature, so a point that reaches here is by definition one the user is
pointing at — and the case the guard rejected is a midpoint out in the model, deliberately reached
for, which is the whole feature.

## 5. A test fixture bug worth recording

The first stretch-snap case displaced its snapped point in world X and Y to put it "off the drag
axis". But the plane's u axis is **not** a world axis — for a level plane it comes out as −Y — so
part of that displacement lay *along* u, and the expected 3 ft became 8. The test was measuring its
own arithmetic.

Displacements in these fixtures are now stated in the plane's own basis, taken from
`SectionClipPlaneBasis`. This is the third time in this issue that a fixture written in world axes
has quietly meant something else in the plane's; the first two were rays lying *in* a horizontal
plane and grazing every handle at once.

## 5a. The bug this shipped with, and why no test saw it

Reported from the app the same day, after the first build:

> when trying to snap to the section with the sectionplane it seems to be a little off ... it looks
> like it is going to snap too far and then snaps too close

**Cause.** The snapped point's projection onto the drag axis is the distance the handle must travel
— an **absolute** placement, measured from the anchor, which *is* the handle's position at the grab.
The code then subtracted `sectionPlaneGripStartParam` from it, as the cursor path rightly does. That
value is where the **cursor** crossed the axis when the handle was grabbed, and it is zero only when
the click lands exactly on the handle's centre. Anywhere else inside the grab aperture and the plane
came out wrong by precisely that much, in whichever direction the click was off — which is exactly
"too far" one way and "too close" the other.

Two kinds of quantity sharing one variable. The fix is to branch: absolute when snapping, relative
when following the cursor, with the reason written at both.

**Why every test passed.** Every fixture aimed its grab ray straight at the handle
(`RayAtGrip` targets `g.at[k]`), which makes the offending term exactly zero. A whole family of
cases — six of them, 60 assertions — was blind to it in the same way, because they all built their
rays the same way.

`[req340]` now grabs **off-centre on purpose**, at 0, +1.5 and −2.25 ft along the drag axis, and
asserts all three land the plane in exactly the same place: where the grab landed carries no
information about where the snap is, so it must not influence the result at all. Reinstated, the bug
puts the plane 2.25 ft out on the 2.25 ft grab — the reported symptom, reproduced to the foot.

A companion case pins the other half of the distinction: with **no** snap, an off-centre grab must
still drag *relatively*, moving the plane by how far the cursor moved rather than jumping the handle
under the cursor.

**The lesson, which generalises past this bug:** a fixture helper shared by every case in a family
makes them all blind to the same thing. `RayAtGrip` was written to make the tests readable, and it
silently fixed one of the inputs at the single value that hid the defect.

## 6. Verification

**Full suite 1512/1512**, up from 1506.

`SubObjectSelectionTests` `[req340]`, 6 cases inside `[sectionplanegrip]` (16 cases / 157
assertions):

- **an off-centre grab lands the plane in the same place as a dead-centre one** — §5a, the case that
  was missing;
- an off-centre grab with no snap still drags relatively;
- the plane lands **exactly** through a snapped point while the cursor ray is aimed elsewhere,
  asserted both at 1e-9 and separately at REQ-101's 0.002 ft;
- four held frames on one snapped point do not drift — which would catch a version that accumulated
  the projection instead of taking it absolutely;
- releasing the snap hands back to the cursor, measured from the ORIGINAL grab rather than from
  where the snap left the plane;
- a stretch handle snaps on the same terms and still changes nothing about what the cut hides.

**Proven to bite:** ignoring the snapped point fails 3 cases and 7 assertions; reinstating the
relative-instead-of-absolute placement fails the off-centre case with the plane 2.25 ft out.

## 7. Assumptions and debt

- **DEBT-1.** Nothing automated sees the symbols. There is no GL context in the suite, so their
  geometry is unit-tested through `SectionPlaneGripsFor` and their appearance is the user's check.
  This is the same gap REQ-338 and REQ-339 record, unchanged.
- **DEBT-2.** The section line still carries no direction arrows, so which half is kept is shown
  only by what has disappeared and by the Flip symbol's orientation.
- **ASSUMPTION-1.** Snapping every draggable handle, rather than only Move, is what the user wants.
  Stated because the screenshot shows only a Move drag; the stretch case is a reasonable reading of
  the same request and is cheap to narrow if it proves noisy.
- **Repair carried in this task:** TASK-251's commit left **REQ-100** split — its heading and
  statement in one place, its four-cost-profile body stranded inside REQ-339 — from an insert that
  landed on a blank line inside REQ-100 and a later relocation that carried the orphan along. The
  heading is reunited with its body here. No text was lost; verified by heading count (194 → 195,
  the new one being REQ-340) and by the body appearing exactly once before and after.

## 8. Result

**PASS** for the two things asked for. Remaining on #479: direction arrows, the plane as a real
entity (Properties, `.gs`, undo), and the contextual ribbon.
