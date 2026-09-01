# TASK-171 — Picking a solid's dimensions with a live preview (issue #146, REQ-313 amended)

## Requirement authority

- **REQ-313** amended — every dimension with a natural mouse gesture is picked, with a live preview.
- **ADR-045 (f)** amended — a torus may pass through itself.
- **D-2026-09-01-f** — the recorded decision behind both.
- REQ-058 (off plan view the viewport is a projection), REQ-100, REQ-201, REQ-301, REQ-304.
- GitHub issue #146. Follows TASK-169 (the prompted form), merged.

## What the user asked for

Reference screenshots for each primitive: a rubber-band preview while choosing a radius, height,
length or width, on every 3D object. Plus the AutoCAD options visible in them — `D`iameter, pyramid
`I`nscribed with a live angle — and a torus whose tube can grow past its ring and self-intersect.

Two further pieces of the request are **deliberately not here**, sequenced with the user:
POLYSOLID (a new object needing a sweep operation, #120 Phase 4) and the AutoCAD-style wireframe
isolines. Each gets its own PR.

## What it does

Every dimension that has a mouse gesture is picked, and the candidate solid is drawn live:

| Primitive | Picked, in order |
|---|---|
| BOX, WEDGE | first corner → opposite corner (length **and** width) → height |
| CYLINDER | centre → radius → height |
| CONE | centre → base radius → height (top radius defaults to an apex) |
| SPHERE | centre → radius |
| PYRAMID | centre → base radius (turning the base) → height (4 sides by default) |
| TORUS | centre → ring radius → tube radius |

`D` takes a diameter at any radius prompt; `I` toggles the pyramid between inscribed and
circumscribed; typing still works everywhere it did.

**Before the whole solid is determined the preview shows the BASE** — the circle, rectangle or
polygon the cursor has already decided. That is what the screenshots show, and it is the half that
cannot come from the solid builder: at the radius prompt there is no solid yet.

## Design

**One builder.** `CadBuildSolidFromCommand` is called by the preview, by the click that commits a
dimension, and by Enter. A preview computed separately from the commit is a preview that eventually
shows a solid the click does not build — worse than no preview at all — so there is exactly one
function that turns a set of numbers into a shape.

**The cursor is resolved in the COMMAND layer**, not the viewport. `CadResolveSolidPick` takes the
cursor point and the pick ray and publishes what the cursor is currently worth. It is geometry — a
distance in a plane, an angle, a ray/axis closest approach — and putting it in the domain is what
lets a transcript drive the same arithmetic the mouse does. A test that resolved a radius its own way
would be a test of its own arithmetic.

**Height needs the ray.** The cursor is *on* the work plane, so its offset along the axis is always
zero; the height is the closest approach between the cursor ray and the solid's axis. Plan view has
no ray, so a height cannot be picked there — the command says so and asks for a typed value rather
than inventing a number.

**Pick order is data.** `SolidParamSpec::pick` marks each dimension `Radius`, `Height`, `CornerXY` or
`Typed`, and the pick sequence is the non-`Typed` ones in table order. That table order is still the
one-line form's argument order, so `PYRAMID x,y 4 6 0 15` is unchanged while the prompted form asks
only for the two dimensions that have gestures.

**A picked dimension that completes the set creates the solid**; a typed one waits for Enter. A click
is a deliberate final act, where typing leaves you at a prompt with a value you may still want to
correct — which the previous increment's transcript already relies on.

## The torus amendment

ADR-045 (f) refused any tube radius at or above the ring radius, on the grounds that it was the one
way a primitive could self-intersect. The user's screenshot shows why that was too strict: it is a
shape AutoCAD makes and people draw on purpose.

Now only the **exactly equal** case is refused — there the inner equator collapses to a point and the
inner rim edges have zero radius, so it is not a solid at all. A tube *larger* than the ring builds,
validates and draws.

It reports **no volume and no surface area**. `brep::SelfIntersects` gates `ComputeMassProperties`,
and both authoring forms print "it passes through itself, so volume and area are not reported"
through one shared message. A surface that encloses part of space twice makes `2·π²·R·r²` a number
rather than an answer, and printing it would be the silent wrong answer REQ-201 exists to prevent.

## Test approach

`headless.req313-solid-picked` (149 steps). Two new driver verbs make the feature testable at all:

- **`HOVER <x> <y> [z]`** — move the cursor without clicking. The preview is the whole point of the
  feature and it is the half a `CLICK` cannot show: by the time a click has landed the value is
  committed and the rubber is gone.
- **`EXPECT PREVIEWBOUNDS`** — the bounding box of the rubber the viewport would draw. Asserting a
  segment *count* would prove only that something was drawn; asserting the bounds proves the preview
  is the shape the cursor implies, and matching it against the committed solid proves the two come
  from one builder rather than two that happen to agree today.

`CLICK` gained an optional third coordinate, because a height is read off the *ray* and aiming at a
plan XY resolves to whatever height that sight line happens to cross — geometrically right, and
impossible to write an expectation for. `VIEWANGLES` orbits the view, without which no height is
pickable at all.

Covered: a full mouse-only cylinder; a radius picked off-axis at (3,4) proving it is a **distance**
and not a copied coordinate; `D` inline and armed; a box corner dragged both positive and negative;
wedge; cone defaulting to an apex; pyramid circumscribed *and* inscribed; torus ring and tube; the
self-intersecting torus; Esc mid-command leaving nothing; and the preview matching the commit for
both a cylinder and a box.

## Verification

- `ctest`: **969/969 green.**
- Negative-tested three ways, each turning the suite red:
  1. Resolving a radius as a coordinate instead of a distance → `volume is 113.097336, expected
     523.598776`.
  2. Building the preview without the cursor → `PREVIEWBOUNDS: mnX is 0.000000, expected -6.000000`.
  3. (From the previous increment, still standing) dropping `faces` from the `.gs` write.

## Assumptions

- **ASSUMPTION-1 (stated):** a picked dimension completing the set should create the solid, while a
  typed one waits for Enter. Taken from AutoCAD's behaviour and from the previous increment's
  "correct a value before Enter" case, which a typed auto-commit would remove.

## Technical debt / stated boundaries

- **DEBT-1 — the wireframe still shows only topological edges.** A sphere draws two meridians where
  AutoCAD draws a grid of isolines. Raised by the user in the same request and sequenced as its own
  PR; nothing here depends on it.
- **DEBT-2 — POLYSOLID does not exist.** A new object needing a sweep operation (#120 Phase 4), its
  own REQ and its own ADR. Sequenced as the last of the three.
- **DEBT-3 — the preview is verified as geometry, not pixels.** `EXPECT PREVIEWBOUNDS` asserts the
  rubber the viewport would draw; that it is *drawn* is a GUI pass, the same category REQ-064's own
  styles sit in.
- **DEBT-4 — no dimension text at the cursor.** The screenshots show AutoCAD's live value box; this
  increment draws the geometry and the measuring line, and the value is in the prompt. Its own work.

## Status

Complete and verified. Goes to review, not done; the issue is not closed here.
