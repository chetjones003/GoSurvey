# TASK-174 — Extrude in the B-rep kernel (REQ-314 increment 1a, GitHub issue #147)

## Requirement authority

- **REQ-314** — Feature operations on the solid kernel (accepted 2026-09-02, D-2026-09-02-a).
- **ADR-046** — analytic feature operations; delivery order puts **extrude first** (increment 1).
- Builds on REQ-313 / ADR-045 (the kernel), REQ-311 (`ucs::Ucs`), REQ-101, REQ-201, REQ-301.
- GitHub issue #147, Phase 4 of #120.

## Scope of this task — the kernel half only

ADR-045's own delivery split (kernel first, document integration second) is the precedent: its
increment 1 "changed no existing source file at all". This task follows it. It adds **only**
`brep::Extrude` and its tests — no command, no viewport, no `.gs`, no renderer change. Those are
increment 1b (a later task).

Extrude a **single closed planar profile** of straight and circular-arc edges, **perpendicular to
its own plane**, by a signed distance. No taper, no arbitrary direction, no path, no multi-loop
profile — those are later increments per ADR-046 and REQ-314's stated scope boundaries.

## Implementation approach

- New public types in `brep.hpp`: `ProfileEdge`, `Profile`, and `bool Extrude(const Profile&,
  double distance, Solid*, Problem*)`. `Profile` is the shared input `Revolve` (increment 2) will
  also consume — the second concrete use, per REQ-301, arrives immediately.
- Each straight profile edge sweeps a **plane** face; each arc edge sweeps a **cylinder** face
  (axis = plane normal through the arc centre, radius = arc radius, height = |distance|). Two cap
  faces close the ends. Every face is therefore already one of ADR-045's five surface kinds and
  every new edge is a line or an arc — no kernel-representation change.
- The builder accepts either profile winding and orients the result itself (signed area about the
  effective "up" direction; reverse the walk when it is negative).
- Reuses the existing construction helpers (`AddVertex`, `AddLine`, `AddArc`, `MakePlaneFace`,
  `AddSingleShell`) and the existing `Validate` gate — nothing is stored unless `Validate` passes
  (REQ-201).
- The result carries `PrimitiveKind::None` and no recipe. ADR-046 (e) permits an extrude recipe but
  defers it to the increment that first persists one, so that `kGsFormatVersion` is untouched here.
- **Tessellation:** `Tessellate`'s plane-face path currently fans from the loop centroid, which is
  correct only for convex faces, and refuses anything else with `PlaneFaceNotSimple`. An L-shaped
  profile's cap is non-convex, so this task adds **ear-clipping** for a single non-convex plane
  loop (ADR-045 named this "Phase 4's problem, when a boolean first produces a face that needs
  one" — extrude is the first thing that needs it). The convex path is kept unchanged so every
  REQ-313 primitive tessellates byte-for-byte as before.
- New `Problem` values: `NonPositiveDistance`, `ProfileMalformed`, `ProfileTooFewEdges`,
  `ProfilePointOffPlane`, `ProfileArcRadiusMismatch`, `ProfileSelfIntersects`, each with
  user-facing text.

## Test approach (`tests/BrepTests.cpp`, `[brep][req314]`)

- A rectangle extruded `h` is the REQ-313 box: same volume, area, counts, Euler characteristic,
  and `Validate` clean — asserted against the box builder so a formula error in either shows.
- A full-circle profile extruded `h` reproduces the REQ-313 cylinder to a relative 1e-9.
- An L-shaped (non-convex) profile: valid closed solid, hand-computed volume = base area × h,
  tessellation re-derives the same volume by the divergence theorem (the existing cross-check).
- A profile with one arc edge: the swept face is a `Cylinder`, `ClosestPointOnSurface` lands on it.
- Placement and survey-magnitude invariance (tilted frame at easting ~3.5e6): volume/area to 1e-6.
- Negative `distance` extrudes the other way and still validates (outward normals, positive volume).
- Refusals, each by name: zero / non-finite distance, fewer than two edges, an off-plane point, an
  arc whose endpoints are not equidistant from its centre, a figure-eight self-intersecting loop.
- `.gs`-parity is out of scope here (no persistence in 1a).

## Verification

`build-project`, `testing` (full `BrepTests` + the wider suite for regressions), `code-review`,
`architecture-review` (kernel stays graphics-free and directly unit-tested).

## Status

**Implemented — 2026-09-02.** `brep::Extrude` added; `Tessellate` gained ear-clipping for a single
non-convex plane loop (convex faces keep the centroid fan, so every REQ-313 primitive tessellates
unchanged). 10 new `[brep][req314]` cases. Full suite green: **979/979 ctest**, 315k+ Catch2
assertions.

### Known limitations carried forward (all named, none silent — REQ-201)

- Reflex (inward-curving) arcs are refused (`ProfileArcReflex`) — `Surface` has no inward-normal
  form; the booleans force the general answer.
- Multi-loop profiles (a hole in the section) are not accepted — a later increment.
- Oblique / directional / path extrusion and taper are out of scope for increment 1 (ADR-046).
- The ear-clipper falls back to a fan if it cannot find an ear within `4n` iterations (only a
  pathological polygon; visual only — validity and mass properties never read the tessellation).
- The self-intersection screen tests chords, not arc bulges; `Validate` still gates every result,
  so a subtler self-overlap is refused as broken topology rather than stored.

### Increment 1b — the EXTRUDE command, prompted + one-line (done, 2026-09-02, PR #208)

**Bare `EXTRUDE`** is the prompted form: if nothing is selected it asks *"select closed polylines
or circles, Enter when done"* (the accumulate-and-Enter selection shape MOVE/COPY use); then
*"specify height of extrusion <cursor>:"* — a typed value, or **the cursor height dragged live**
with a wireframe ghost of the solid and a measuring line along the axis. **`EXTRUDE <height>`** is
the one-line shortcut on the current selection. `EXT` is an alias. One undo step; source entities
kept. Ineligible selections and a bad height are reported by name (REQ-201).

Wiring — the "six places" a 3D command touches:

- **Kind + state:** `AppCommandState::Kind::Extrude`, `ExtrudePhase`, `extrudeProfiles`,
  `extrudeHeightPick{,Valid}`.
- **Command layer** (`CadCommands.cpp`): `StartExtrudeCommand` / `HandleExtrudeTextInput` /
  `SubmitExtrudeViewportPick` / `CadResolveExtrudePick` / `CadBuildExtrudeSolids` /
  `CadExtrudePromptText` / `CancelExtrudeCommand`; `ExtrudeProfileFromSelection` (closed polyline or
  circle → `brep::Profile` in storage coords, plane by Newell's method oriented "up").
- **Click routing** (`ViewportPickPolicy.hpp`): `SelectProfiles → SelectionAccumulate`,
  `WaitHeight → SnappedPointPick`; added to `ViewportUseRawWorldForSelectionRectPick`.
- **Pick dispatch** (`CadCommands.cpp` `SubmitViewportPickImpl`): a fence merges into the selection
  during SelectProfiles; a click commits at the cursor height during WaitHeight.
- **Enter handling** (`ProcessCommandLineSubmit`): routes to `HandleExtrudeTextInput("")`.
- **Prompt + preview** (`CadUi.cpp` `CommandInputHint`, `CadResolveExtrudePick` call site;
  `CadRubberPreview.cpp` ghost) — the ghost and the commit both go through `CadBuildExtrudeSolids`.
- **Cancel** (`CancelActiveCommand`): named, `CancelExtrudeCommand`.

Reuses REQ-313's solid store, `.gs` serialization (recipe-less solids already round-trip),
rendering, snapping and selection unchanged.

Transcript `tests/headless/transcripts/req314-extrude.txt` covers both flows: rectangle == box,
circle == cylinder, undo/redo, line-in-selection reported, the bare-`EXTRUDE` → select → Enter →
typed-height flow, and `.gs` save/reopen with topology intact. The **live drag preview** is
manual-verification only (no headless frame loop / mouse — `project_gui_hover_not_automatable`).

**Not in 1b:** arc-bulge polylines; multi-loop profiles; DELOBJ (source always kept); picking a
height in plan view (no ray — the prompt says to type one or orbit).
