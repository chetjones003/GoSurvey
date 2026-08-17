# Requirements Specification

> **Template.** Requirements are the concrete, testable expression of the project
> purpose in `project.md`. If a behavior is not written here, it is an
> assumption — not a requirement. Every requirement must be specific enough that
> a reviewer can render a clear **pass/fail** judgment by pointing at an
> artifact (output, log, test, profile, or code structure).

---

## How to write a requirement

Each requirement is a numbered block with a stable ID (`REQ-NNN`). The ID never
changes or is reused, so tasks, tests, and reviews can cite it forever.

```
### REQ-NNN — Short imperative title
- Purpose:     which project goal / user this serves
- Priority:    must | should | may          (RFC-2119 sense)
- Type:        functional | performance | quality | constraint
- Statement:   what must be true, phrased testably
- Acceptance:  the observable condition that proves it (the pass/fail test)
- Owner-layer: which architecture layer is responsible
- Status:      proposed | accepted | implemented | verified
- Revisions:   date — note
```

**Testable vs. not:**

- ✅ "Importing a malformed record logs an error and writes no value to the
  model." (observable)
- ❌ "The importer should be robust." (unmeasurable)
- ✅ "A 100k-vertex scene renders within a 16 ms frame budget on the reference
  GPU." (measurable)
- ❌ "Rendering should be fast." (opinion)

Prefer **must** sparingly; everything cannot be a must. A flood of `must`
requirements is a planning failure, not a sign of rigor.

---

## Functional requirements

### REQ-001 — Reject malformed input, never absorb it
- Purpose: data integrity (interoperability goal)
- Priority: must
- Type: functional
- Statement: When the importer encounters a record it cannot interpret, the
  import fails for that record with a logged error; no partial or guessed value
  is written into the model.
- Acceptance: feeding a known-malformed fixture yields a logged error and the
  record is absent from the result.
- Owner-layer: IO
- Status: accepted
- Revisions: `<date>` — initial.

### REQ-002 — `<Round-trip fidelity>`
- Purpose: `<interoperability>`
- Priority: must
- Type: functional
- Statement: `<A file imported and re-exported reproduces source geometry within tolerance.>`
- Acceptance: `<round-trip of the reference dataset matches within CON tolerance.>`
- Owner-layer: `<IO>`
- Status: proposed
- Revisions: `<date>` — initial.

> Add functional requirements until the in-scope list in `project.md` is fully
> covered — no more, no less.

---

## Traverse measurement & adjustment requirements

> These cover the Traverse Editor's raw-measurement display and the least-squares
> closure analysis (extends FEAT-002). Numeric acceptance asserts against
> tolerance per REQ-101, never exact float equality.

### REQ-010 — Display every raw observation
- Purpose: surveyor review of field data (FEAT-002)
- Priority: must
- Type: functional
- Statement: After importing a raw-data file, the editor displays every
  individual F1/F2 observation retained per leg (horizontal circle, slope
  distance, vertical/zenith angle) — not only the reduced per-leg values.
- Acceptance: importing the sample FBK shows a detail row for each F1/F2
  observation; the visible observation count equals the count in the file.
- Owner-layer: UI (data from Domain)
- Status: accepted
- Revisions: 2026-06-10 — initial.

### REQ-011 — Per-leg observation statistics
- Purpose: blunder/quality review
- Priority: must
- Type: functional
- Statement: For each leg the editor computes and displays the mean, sum, and
  standard deviation from the mean of the repeated observations (horizontal
  angle, distance, vertical angle).
- Acceptance: computed mean/sum/std-dev match an independent hand calculation
  within tolerance (REQ-101).
- Owner-layer: Domain (compute), UI (display)
- Status: accepted
- Revisions: 2026-06-10 — initial.

### REQ-012 — Complementary distance reduction
- Purpose: show both slope and horizontal distance regardless of which was given
- Priority: must
- Type: functional
- Statement: When slope distance is provided the editor computes and shows the
  horizontal distance, and when horizontal distance is provided it shows the
  slope distance, using the leg's vertical/zenith angle.
- Acceptance: complementary distance matches a hand calculation within ±0.01 ft.
- Owner-layer: Domain (compute), UI (display)
- Status: accepted
- Revisions: 2026-06-10 — initial.

### REQ-013 — Raw measurements are protected from accidental edits
- Purpose: protect raw field data from accidental edits
- Priority: must
- Type: functional
- Statement: The computed-output cells of the main editor (bearing, deltas,
  coordinates, status) are read-only, and the individual F1/F2 observation values
  are not editable from the summary grid — they can be edited only inside a leg's
  explicit per-leg expander (REQ-018). Editing raw observations requires the
  deliberate act of expanding a leg. (The summary grid's manual-entry fields —
  H.Angle, H.Dist, S.Dist, V.Angle — remain editable for legs entered by hand.)
- Acceptance: code/UI review confirms no computed-output cell is editable and no
  control is bound to an individual F1/F2 observation outside the per-leg
  expander.
- Owner-layer: UI
- Status: accepted
- Revisions: 2026-06-10 — initial; 2026-06-11 — scoped view-only to the
  collapsed summary; editing happens in the per-leg expander (REQ-018, ADR-003).

### REQ-014 — Closure window: unadjusted vs least-squares, side by side
- Purpose: let the surveyor compare and accept an adjustment
- Priority: must
- Type: functional
- Statement: A "Calculate Closure" action opens a window presenting the existing
  unadjusted closure and the least-squares result side by side, across two tabs
  (closure summary; per-observation residuals), and lets the user accept the
  least-squares result.
- Acceptance: both columns populate for a closed loop; an Accept action records
  the least-squares result as the chosen adjustment.
- Owner-layer: UI (data from Domain)
- Status: accepted
- Revisions: 2026-06-10 — initial.

### REQ-015 — Least-squares adjustment of a closed-loop traverse
- Purpose: rigorous adjustment (FEAT-002)
- Priority: must
- Type: functional
- Statement: The editor adjusts a closed-loop traverse by weighted least squares,
  using configurable a-priori standard errors (defaults: σ_angle = 5″,
  σ_dist = 0.02 ft + 2 ppm) to weight observations. Only closed loops are
  adjusted in this increment. A loop closes on the start monument, which may be
  re-observed under a suffixed name (e.g. start "KCP2" closing as "KCP2.1");
  import detects this and the closing foresight is held as the start.
- Acceptance: on a synthetic closed loop with a known injected misclosure, the
  adjusted coordinates reduce the misclosure to ~0 within tolerance (REQ-101).
- Owner-layer: Domain (compute)
- Status: accepted
- Revisions: 2026-06-10 — initial.

### REQ-016 — Per-observation residuals
- Purpose: blunder detection
- Priority: must
- Type: functional
- Statement: The closure window's residuals tab shows each observation's angular
  residual and distance residual from the least-squares adjustment.
- Acceptance: residuals match an independently worked least-squares example
  within tolerance (REQ-101).
- Owner-layer: Domain (compute), UI (display)
- Status: accepted
- Revisions: 2026-06-10 — initial.

### REQ-018 — Editable per-leg observation sets (expander)
- Purpose: let the surveyor add, edit, or remove individual observations per leg
  and have the traverse re-derive from them (FEAT-002)
- Priority: should
- Type: functional
- Statement: Each leg can be expanded inline to show its observation sets as
  editable controls (per-set F1/F2 horizontal circle reading, slope distance,
  zenith angle, with per-face presence). The user can add a set and remove a set.
  Editing, adding, or removing a set re-reduces the leg from its sets — the
  leg's horizontal angle (circle reading − backsight reading), zenith angle, and
  slope distance are recomputed — and the traverse and closure update
  accordingly. Sets retain the literal field circle readings, not pre-reduced
  directions.
- Acceptance: importing the sample FBK and then editing a set's circle reading
  changes that leg's computed bearing and the loop closure; adding a set changes
  the per-leg statistics (REQ-011); removing all but one set still computes.
- Owner-layer: Domain (reduction), UI (editing)
- Status: accepted
- Revisions: 2026-06-11 — initial (ADR-003).

### REQ-017 — Insufficient redundancy is surfaced, not absorbed
- Purpose: no silent failure (REQ-201)
- Priority: must
- Type: functional
- Statement: When a traverse has insufficient redundancy for adjustment (e.g. an
  open traverse, redundancy ≤ 0) or the normal-equation system is singular, the
  closure window reports "least squares unavailable" with a reason and shows no
  adjusted values; it does not crash or emit NaN/silent results.
- Acceptance: an open traverse yields the message and no adjusted output; a
  singular system logs an error and produces no value.
- Owner-layer: Domain (compute), UI (display)
- Status: accepted
- Revisions: 2026-06-10 — initial.

---

## Display & units requirements

> These cover the Drawing Units (`UNITS`) feature: how the user configures the
> precision and format of the linear and angular values the application
> **displays**. They own display formatting only — stored coordinates and the
> internal angle convention are unchanged (REQ-101 fidelity is preserved).

### REQ-020 — UNITS command and Drawing Units dialog
- Purpose: give the user one AutoCAD-style place to control displayed units
- Priority: should
- Type: functional
- Statement: A `UNITS` command (command line + menu) opens a modal Drawing Units
  dialog with Length, Angle, Insertion-scale, and a live Sample Output. The
  Length group (Type = Decimal; adjustable precision) is the single owner of the
  display precision for all non-survey linear/coordinate readouts, replacing the
  interim Display-tab "Coordinate precision" control. Cancel/Esc reverts to the
  values present when the dialog opened; OK applies and persists.
- Acceptance: typing `UNITS` opens the dialog; changing Length precision changes
  the ID, status-bar, dimension, and property readouts to that many decimals;
  Cancel makes no change; settings persist across restart.
- Owner-layer: UI (command + dialog), IO (persistence)
- Status: accepted
- Revisions: 2026-06-11 — initial.

### REQ-021 — Configurable angle display
- Purpose: surveyor-appropriate bearing/angle presentation
- Priority: should
- Type: functional
- Statement: Non-survey angle/bearing **readouts** (INVERSE bearing, angular
  dimensions, rotation-relative-north properties, Sample Output) render according
  to a chosen angle format ∈ {Decimal Degrees, Deg/Min/Sec, Surveyor's Units},
  an adjustable precision, a direction (clockwise / counter-clockwise), and a
  base angle. This governs **display only**: typing an angle into a command keeps
  the existing CW-from-north entry convention. At default settings the rendered
  output matches the pre-feature bearing format.
- Acceptance: Surveyor's Units renders a representative bearing as `N 45°30'00" E`;
  Decimal Degrees and DMS render correctly at the chosen precision; changing
  direction/base changes displayed values consistently across readouts and Sample
  Output; angle entry is unchanged; a parity test confirms default-settings output
  equals the previous formatter (assert against tolerance per REQ-101 where
  numeric).
- Owner-layer: Domain (pure formatter), UI (readouts + dialog)
- Status: accepted
- Revisions: 2026-06-11 — initial.

### REQ-022 — Drawing unit (INSUNITS-style relabel), persisted to .gs and DXF
- Purpose: tell the drawing what unit it is in, AutoCAD-faithfully, without ever
  altering geometry
- Priority: may
- Type: functional
- Statement: The Drawing Units dialog sets the drawing's unit — Unitless, Feet,
  or Meters — as a **relabel only**, mirroring AutoCAD's INSUNITS: it never
  scales, converts, or otherwise alters any coordinate, length, survey point, or
  text height. The unit is a **document property**: it is persisted in the `.gs`
  file and written to the DXF `$INSUNITS` header on export (Feet=2, Meters=6,
  Unitless=0). On DXF import, a present `$INSUNITS` sets the drawing's unit but
  coordinates are read **unscaled** (1:1), so round-trip fidelity (REQ-002) is
  preserved. Display precision and angle-format settings remain app-global user
  prefs and are unaffected; survey-point display precision remains independent.
- Acceptance: changing the unit changes no coordinate anywhere; export writes
  `$INSUNITS` matching the unit; importing a DXF that carries `$INSUNITS` adopts
  the unit with coordinates unchanged (a known point exported then re-imported is
  identical within REQ-101 tolerance); a `.gs` save/load preserves the unit;
  survey-point precision is unaffected.
- Owner-layer: UI (dialog), IO (DXF + .gs persistence)
- Status: accepted
- Revisions: 2026-06-11 — initial (stored-only, user-pref). 2026-06-12 — amended
  to an INSUNITS relabel persisted in .gs/DXF; no geometry scaling (decision log).

### REQ-023 — Survey points survive a DXF round-trip
- Purpose: DXF is a safe interchange/backup for survey data, not just `.gs`
- Priority: should
- Type: functional
- Statement: A GoSurvey drawing exported to DXF and re-imported reconstructs its
  survey points with identity intact — id, easting, northing, elevation,
  description, layer, and label style — and re-links each point's label. Identity
  is carried in DXF XDATA under a registered `GOSURVEY` application id; a `POINT`
  without that XDATA (e.g. from another program) still imports as a snappable
  cross-line marker, so foreign-point behavior is unchanged. Coordinates
  round-trip within REQ-101 tolerance (the existing world-origin translation is
  preserved; nothing is scaled).
  DXF import replaces the **CAD geometry** but **preserves survey points already
  in the session** (so importing points then a DXF, or a DXF then points, both
  keep all points); the DXF's reconstructed survey points are **merged** with the
  existing ones. Points are stored in local space (`world = local +
  worldDocumentOrigin`), so import converts each merged point into the document's
  current local frame. When an imported point's id does not collide it is added
  directly; when it collides with an existing point the user is prompted to either
  **overwrite** the existing point or **offset** the imported ids by a chosen
  amount.
- Acceptance: a drawing with N survey points exported then re-imported yields N
  survey points with matching id, coordinates (within REQ-101), description, and
  label style; each reconstructed point has a single linked label (no duplicate or
  orphan MTEXT); a `POINT` from a non-GoSurvey DXF still imports as cross-lines.
  Importing a DXF while M survey points already exist keeps all M (they remain on
  the linework in world coordinates) and adds the DXF's non-colliding points; a
  colliding id triggers the overwrite/offset prompt rather than dropping or
  silently duplicating a point.
- Owner-layer: IO (DXF)
- Status: accepted
- Revisions: 2026-06-12 — initial (resolves issue #37). 2026-06-15 — amended: DXF
  import preserves existing session survey points and merges reconstructed points,
  with an overwrite/offset prompt on id conflict (decision log).

### REQ-024 — AutoCAD-style dynamic input at the cursor for point prompts
- Purpose: familiar, readable coordinate entry that matches AutoCAD dynamic input
- Priority: should
- Type: functional
- Statement: When the active command prompt expects a coordinate point, the
  cursor dynamic-input shows a prompt label plus a **single coordinate field**
  that continuously displays the crosshair's current **world** coordinates
  (`x,y`) at the configured display precision (REQ-020 `displayLinearPrecision`).
  Typing overrides (locks) the field to the typed value; the field accepts any
  point input the command line understands — absolute `x,y`, relative `@dx,dy`,
  bearing/distance, etc. Enter — or a viewport click — commits the point. Prompts
  that do not expect a point (bearing/angle/distance/option/command-name entry)
  likewise keep a single input field. There is no Send button; commit is by Enter
  or click.
- Acceptance: starting LINE shows the "first point" prompt with a single box that
  tracks the cursor's easting/northing as `x,y`; typing locks the field; entering
  `@dx,dy` or a bearing/distance places the relative point; Enter commits the
  shown/typed value and a viewport click still places the point; a non-point
  prompt (e.g. circle radius, bearing entry) also shows a single field.
- Owner-layer: UI
- Status: accepted
- Revisions: 2026-06-12 — initial; 2026-06-19 — single coordinate field instead
  of two X/Y boxes, so relative/bearing/distance entry works in the same field.

### REQ-025 — Model and Paper space with layout tabs and a space toggle
- Purpose: compose a model onto sheets, the way AutoCAD model/paper space works
- Priority: should
- Type: functional
- Statement: Each drawing has a **Model space** (today's modeling area) and zero or
  more named **Paper space layouts**. The UI shows a tab/selector to switch the
  active space, and layouts can be **added, renamed, and deleted**. A status-bar
  button reads **MODEL** or **PAPER** for the active space and **clicking it
  toggles** between model space and the current/last paper layout. Switching space
  changes what the viewport edits and displays; model geometry is unaffected by
  paper-space edits.
- Acceptance: a drawing shows a Model entry plus at least one Paper layout entry;
  switching changes the active space; ≥2 layouts can be added, renamed, and deleted
  and coexist; the status button shows MODEL/PAPER and clicking it toggles the
  active space.
- Owner-layer: UI / Domain
- Status: accepted
- Revisions: 2026-06-15 — initial (Paper Space milestone, decision log).

### REQ-026 — Sheet definition: paper size and orientation
- Purpose: a layout represents a real sheet of paper
- Priority: should
- Type: functional
- Statement: A paper layout has a selectable **paper size** (a preset set covering
  common ANSI A–E and ARCH sizes, plus a custom width×height) and **orientation**
  (portrait/landscape). The sheet outline (and printable margin, if modeled)
  renders in paper space at the chosen physical size.
- Acceptance: choosing a size + orientation renders the sheet outline at that
  physical size in paper space; changing the size updates the outline.
- Owner-layer: UI / Domain
- Status: accepted
- Revisions: 2026-06-15 — initial.

### REQ-027 — Layout viewports at independent scales
- Purpose: show one or more scaled views of the model on a sheet
- Priority: should
- Type: functional
- Statement: A paper layout may hold **one or more viewports**, each a rectangular
  window onto **model space** with its own **scale** and **pan/center**. Viewports
  can be **created, moved, resized**, and have their scale set (model units per
  paper unit). Model geometry renders inside each viewport clipped to its rectangle
  at the viewport's scale; the model itself is unchanged.
- Acceptance: the user can add a viewport and set its scale and model geometry
  appears inside it at that scale; a layout can hold ≥2 viewports showing the model
  at **different** scales simultaneously; viewports can be moved and resized.
- Owner-layer: UI / Domain / Renderer
- Status: accepted
- Revisions: 2026-06-15 — initial.

### REQ-028 — Per-viewport layer freeze
- Purpose: control which layers show in each viewport independently
- Priority: should
- Type: functional
- Statement: Each viewport carries its own set of **frozen layers**. A layer frozen
  in a viewport is hidden **only in that viewport** — not in other viewports, other
  layouts, or model space.
- Acceptance: freezing a layer in one viewport hides its geometry in that viewport
  while it remains visible in other viewports and in model space; thawing restores
  it.
- Owner-layer: UI / Domain / Renderer
- Status: accepted
- Revisions: 2026-06-15 — initial. 2026-07-13 — the freeze **UI** moved from the standalone
  "Frozen Layers" panel to the Layer Manager's **VP Freeze** column and the **VPFREEZE/VPTHAW**
  commands (REQ-046); the per-viewport freeze data model and semantics are unchanged. 2026-07-13
  — plotted output now honors per-viewport frozen layers (TASK-017), matching the on-screen render.

### REQ-029 — Plot a single layout to PDF at true scale
- Purpose: produce a printable sheet at correct plot scale
- Priority: should
- Type: functional
- Statement: The user can **plot a single paper layout to a PDF** sized to the
  layout's paper size, with geometry placed at **true plot scale** (1:1 on the
  sheet; each viewport's model content at the viewport's scale). Output is vector
  where practical, produced with the already-bundled PDFium edit API (ADR-006) — no
  new dependency.
- Acceptance: plotting a layout produces a one-page PDF at the layout's paper size
  where a measured distance on the sheet matches the intended plot scale within
  REQ-101 tolerance.
- Owner-layer: IO / Renderer
- Status: accepted
- Revisions: 2026-06-15 — initial.

### REQ-030 — Batch plot multiple layouts
- Purpose: plot many sheets in one action
- Priority: should
- Type: functional
- Statement: The user can select **multiple paper layouts** and plot them in one
  action to a **multi-page PDF** (one page per layout), each at its own paper size
  and true plot scale (REQ-029).
- Acceptance: selecting ≥2 layouts and batch-plotting produces a single multi-page
  PDF with one correctly sized/scaled page per selected layout.
- Owner-layer: IO / Renderer
- Status: accepted
- Revisions: 2026-06-15 — initial.

### REQ-031 — Persist layouts and viewports in .gs
- Purpose: layouts survive save/reload
- Priority: should
- Type: functional
- Statement: Paper layouts, their paper size/orientation, their viewports
  (rectangle, scale, center) and per-viewport frozen layers are **persisted in the
  native `.gs` file** and restored on load. DXF persistence of layouts/viewports is
  **deferred** to a later requirement (decision log, 2026-06-15).
- Acceptance: a drawing with multiple layouts and viewports (with set scales, paper
  sizes, and per-viewport frozen layers) saved to `.gs` then reloaded restores all
  of them identically.
- Owner-layer: IO
- Status: accepted
- Revisions: 2026-06-15 — initial.

### REQ-032 — Contextual "Layout" ribbon in paper space
- Purpose: surface paper-space commands only when they apply
- Priority: should
- Type: functional
- Statement: While a paper layout is the active space, the ribbon presents a
  **Layout** context with the paper-space commands (rectangular viewport, polygonal
  viewport, and future paper-space tools); in model space the normal ribbon shows.
- Acceptance: switching to a paper layout shows the Layout ribbon with the viewport
  commands; switching back to Model restores the normal ribbon.
- Owner-layer: UI
- Status: accepted
- Revisions: 2026-06-15 — initial (Paper Space Inc 3a).

### REQ-033 — Rectangular viewport command with live preview
- Purpose: create viewports by drawing them, like AutoCAD MVIEW
- Priority: should
- Type: functional
- Statement: A command (ribbon **Layout → Rectangular Viewport**, or a command-line
  alias) lets the user create a viewport on the active layout with **two clicks**
  defining opposite corners, showing a **rubber-band preview** between the first
  click and the cursor. The new viewport defaults to a sensible scale/center, then
  is editable (REQ-027). Esc cancels before the second click.
- Acceptance: starting the command then clicking two corners creates a viewport of
  that rectangle showing the model; a preview rectangle tracks the cursor between
  clicks; Esc before the second click creates nothing.
- Owner-layer: UI / Commands
- Status: accepted
- Revisions: 2026-06-15 — initial (Inc 3a).

### REQ-034 — Polygonal viewport command  *(WITHDRAWN)*
- Purpose: non-rectangular viewports
- Priority: could
- Type: functional
- Statement: A command lets the user define a viewport boundary by clicking
  **vertices until "close"**, with a preview; the viewport clips the model to that
  polygon. Polygonal clipping depends on the GL viewport render pass (ADR-006 /
  ADR-008), so this lands after that pass exists.
- Acceptance: clicking ≥3 vertices then closing creates a viewport that clips the
  model to the polygon; preview tracks the in-progress boundary.
- Owner-layer: UI / Commands / Renderer
- Status: **withdrawn** (2026-07-13 — see decision log). Never implemented.
- Revisions: 2026-06-15 — initial (Inc 3d; depends on the GL clip pass).
  2026-07-13 — **withdrawn**: rectangular viewports (REQ-033) cover current needs;
  polygonal viewports were blocked on the deferred/reverted GL per-viewport clip
  pass and judged unneeded complexity. May be re-proposed if a real need arises.

### REQ-035 — Viewports are selectable; MOVE/COPY/DELETE operate on them
- Purpose: edit viewports with the same UX as model objects
- Priority: should
- Type: functional
- Statement: In paper space, viewports are **selectable objects** (single click and
  window selection, with grips), and the **MOVE**, **COPY**, and **DELETE** commands
  operate on the selected viewport(s) — mirroring model-space selection/editing.
- Acceptance: clicking a viewport selects it (window-select selects those inside);
  MOVE relocates it, COPY duplicates it, DELETE removes it; grips move/resize it.
- Owner-layer: UI / Commands
- Status: accepted
- Revisions: 2026-06-15 — initial (Inc 3b).

### REQ-036 — Floating model space (edit the model through a viewport)
- Purpose: AutoCAD-style MSPACE — work in model space inside a viewport on the sheet
- Priority: should
- Type: functional
- Statement: **Double-clicking a viewport** enters **floating model space**: the
  viewport becomes the active model view and model **draw/edit/snap commands operate
  through the viewport's transform**, clipped to its boundary. Double-clicking
  outside (or Esc, or a PSPACE toggle) returns to paper space. The viewport's
  scale/center reflect the navigation done while active.
- Acceptance: double-clicking a viewport activates it; drawing/snapping/editing apply
  to model space (visible at the viewport's scale, clipped to its rect); leaving the
  viewport returns to paper space with the model unchanged outside the viewport edits.
- Owner-layer: UI / Commands / Renderer
- Status: accepted
- Revisions: 2026-06-15 — initial (Inc 3c).

### REQ-037 — Native paper-space geometry (annotations / title blocks)
- Purpose: draw and edit geometry that lives on the sheet itself — title blocks,
  notes, borders — independent of model space and of viewport content
- Priority: should
- Type: functional
- Statement: A paper layout owns its own set of **paper-space entities**, stored in
  **paper inches** with the sheet origin at (0,0) and **separate** from model-space
  geometry and from the model content shown inside viewports. The first version
  supports **lines and text** (extensible to polylines/circles/arcs). When a paper
  layout is the active space **and not in floating model space**, the standard
  **draw** (line, text) and **edit** (move, copy, rotate, delete) commands and
  **object snapping** operate on that layout's paper-space entities — mirroring the
  model-space UX. Survey-specific tools (survey points, CSV import) remain
  **model-only**. Snapping in paper space resolves to **paper-space entities only**
  (snapping to viewport-displayed model geometry is deferred). Paper-space entities
  **persist per layout in the native `.gs` file** and are unaffected by model edits.
- Acceptance: in a paper layout the user can draw lines and place text on the sheet;
  each can be moved/copied/rotated/deleted and snapped to; they do **not** appear in
  model space or in other layouts; a drawing with paper-space entities saved to `.gs`
  then reloaded restores them per layout.
- Owner-layer: Domain / UI / Commands / Renderer / IO
- Status: accepted
- Revisions: 2026-06-16 — initial (Paper Space Inc 5; SPEC GAP resolution, ADR-009).

### REQ-038 — Clipboard copy/paste within and across model & paper space
- Purpose: reuse existing geometry — e.g. copy a DXF title block from model space
  onto a paper-space sheet layout — and duplicate by copy/paste like AutoCAD
- Priority: should
- Type: functional
- Statement: **Ctrl+C** copies the **active space's** current selection (model
  entities when model/floating-model space is active; the active paper layout's
  entities when a paper layout is active) into an in-process clipboard. **Ctrl+V**
  begins an **interactive paste**: a live preview tracks the cursor and a single
  viewport click places the copied entities **into the currently active space** at
  the click point. Crossing spaces uses **1:1 raw units** — coordinates transfer
  verbatim (model local units ↔ paper inches) with **no scale conversion**; this is
  an explicit, user-initiated coordinate transfer (the sanctioned exception to
  ADR-009's "no implicit coordinate-space mixing"). Pasted entities become the new
  active-space selection (immediately editable) and preserve their properties
  (layer, color, linetype, text style/typeface). Copy/paste works **model→paper,
  paper→model, and within the same space**. **Ctrl+V with an empty clipboard does
  nothing** (no crash). Survey points and survey-specific tools remain model-only.
  The clipboard is in-memory only (not persisted; DXF persistence of the new paper
  types stays deferred per the ADR-013 amendment).
- Acceptance: (1) a model-space selection + Ctrl+C, switch to a paper layout +
  Ctrl+V shows a cursor-tracking preview and a click places the copies in paper
  space; (2) the reverse (paper→model) works the same way; (3) same-space copy/paste
  produces a duplicate placed by click; (4) a copied DXF title block (lines + text,
  plus any circle/arc) appears on the sheet with geometry intact; (5) pasted entities
  are the active selection immediately after placement; (6) they keep layer, color,
  and text style; (7) Ctrl+V with an empty clipboard is a no-op; (8) crossing spaces
  uses 1:1 raw units (a copied known length transfers numerically unchanged).
- Owner-layer: Commands / UI / Domain / IO
- Status: accepted
- Revisions: 2026-06-17 — initial (clipboard copy/paste across spaces; ADR-013).

### REQ-039 — Paper-space objects have full model-space parity
- Purpose: paper space is a peer object space, not a second-class one — the user edits a
  sheet's native geometry with the exact UX they use in model space (the user asked for
  full parity)
- Priority: should
- Type: functional
- Statement: While a paper layout is the active space and **not** in floating model space,
  the layout's native paper-space objects (lines, text, circles, arcs, ellipses, polylines —
  REQ-037/038, ADR-009/013) support the **same** interaction surface as model-space objects:
  (a) **selection** — single click (Shift to add/toggle) and **window/crossing box** selection
  using the same left-to-right=window / right-to-left=crossing rule, with hover pre-highlight;
  (b) **grips** — selected objects show grips whose drag edits geometry, mirroring model grips
  (line endpoints/midpoint, circle center/quadrants, arc, ellipse, polyline vertices, text
  insertion); (c) the **Properties panel** shows and **edits** the selected paper object(s) —
  General (layer/color/linetype/lineweight/transparency) and per-type Geometry and Text
  (contents/height/rotation/style) — the same panel used for model selection; (d) **object
  snapping** to paper objects; (e) **draw** commands LINE, TEXT, MTEXT, CIRCLE, ARC, ELLIPSE,
  POLYLINE create into the active layout's paper store (paper inches); (f) **modify** commands
  MOVE, COPY, ROTATE, SCALE, DELETE, JOIN, TRIM, OFFSET operate on the paper selection.
  Additionally, **double-clicking a text object opens an in-place editor** to edit its
  contents — implemented for **both** model space and paper space (the same shared editor).
  Survey-specific tools (survey points, CSV import) remain **model-only**; paper edits never
  alter model geometry; coordinates never cross spaces implicitly (REQ-038's 1:1 paste is the
  one sanctioned exception). Paper-space objects and their edits **persist per layout in the
  native `.gs`** (REQ-031/037 pattern); DXF persistence of paper objects stays deferred.
- Acceptance: in a paper layout — (1) a window box (L→R) selects paper objects fully inside it
  and a crossing box (R→L) selects any it touches, for every paper object type; (2) clicking a
  paper text selects it with no vertical offset, and double-clicking any text (model or paper)
  opens an inline editor whose committed text replaces the object's contents; (3) selecting
  paper object(s) populates the Properties panel and edits made there apply to the object(s);
  (4) selected paper objects show grips and dragging a grip edits the geometry; (5) CIRCLE,
  ARC, ELLIPSE, POLYLINE, and MTEXT draw onto the sheet, and SCALE/JOIN/TRIM/OFFSET operate on
  the paper selection; (6) none of these paper edits change model geometry; (7) a `.gs`
  round-trip restores the edited paper objects per layout.
- Owner-layer: UI / Commands / Domain / IO
- Status: accepted
- Revisions: 2026-06-18 — initial (paper-space object parity; ADR-014). Delivered incrementally:
  Phase 1 selection/text-pick/Properties, Phase 2 in-place text editor, Phase 3 grips, Phase 4
  draw + modify parity.

### REQ-040 — AutoCAD-style floating command line with fading history and an F2 console
- Purpose: the command line should read and behave like AutoCAD's — a compact
  floating input over the drawing, a brief glance at recent prompts, and an
  expandable text console — without sacrificing the existing input affordances
  (the user asked for this redesign from reference screenshots)
- Priority: should
- Type: functional
- Statement: In addition to the existing dockable "Command line" window (which
  **remains available** via a toggle), the application provides a **compact
  floating command bar** overlaid on the drawing area, anchored bottom-center by
  default, **draggable**, with its on-screen position and visibility **persisted**
  across sessions (UserPrefs, the `UserPrefs`/`AppCommandState` settings pattern —
  no new global). The bar carries: a drag grip, a **close (×)** control that hides
  the bar, a **settings (wrench)** control that opens command-line settings (fade
  delay, opacity, history-line count), a prompt glyph with a **history dropdown**
  of recently entered commands, the **command input** (placeholder "Type a
  command"), and an **expand** control. When hidden, the bar is restored by
  **Ctrl+9** and/or a View-menu toggle. After commands run, the **last few**
  (default 3, configurable) command-log lines float **above** the input on
  semi-transparent backgrounds and **fade out** after a configurable idle delay
  (default ~4 s). **F2 toggles** an **expanded console** that shows the recent log
  (default ~15 visible lines, scrollable through the full log) on a near-opaque
  background and stays until F2 is pressed again. **ESC always cancels the active
  command** and never closes the console. Console/history text is **selectable and
  copyable** (replacing the prior "Copy log" button). The redesign **preserves**:
  the command-name autocomplete popup, the at-crosshair dynamic-cursor input
  (REQ-024), and the clickable `[A]`/`[2P]` footer hints.
- Acceptance: (1) on launch a compact floating bar shows at bottom-center with the
  "Type a command" placeholder, can be dragged, and its position/visibility are
  restored next session; (2) running commands makes the last ~3 log lines appear
  above the bar and fade out after the idle delay; (3) F2 expands the console
  (recent lines, scrollable) and F2 again collapses it; (4) ESC cancels the active
  command whether or not the console is open; (5) console/history text can be
  selected with the mouse and copied; (6) × hides the bar and Ctrl+9 (or the menu)
  restores it; (7) autocomplete, the dynamic-cursor input, and the clickable
  `[A]`/`[2P]` hints still work; (8) the dockable Command line window is still
  available.
- Owner-layer: UI
- Status: accepted
- Revisions: 2026-06-19 — initial (floating command bar + fading history + F2
  console; ADR-015). 2026-06-19 — the active command's hint is the prompt rendered
  **in the input line** (replacing the placeholder), with `[token]` markers shown as
  **clickable links** that submit the option (the standard for any command offering
  keyword options); idle shows a "Type a command" placeholder. 2026-06-19 — the bar is
  **pinned to the viewport bottom edge** (vertical position locked); the user slides it
  left/right and resizes its **width** (right-edge grip) and the F2 console **height**
  (top-edge grip); position/width/console-height persist.

### REQ-041 — Import Points pre-import validation with specific diagnostics
- Purpose: data integrity and no silent failure for CSV point import, surfaced to
  the user before import (serves the import goal; the UI expression of REQ-001 /
  REQ-201 — the user asked to know *why* a file will not import)
- Priority: should
- Type: functional
- Statement: The Import points window validates the selected file **before any
  import** and surfaces specific, actionable diagnostics. It distinguishes and
  names file-state failures — **file not found**, **file is empty**, and **file
  exists but cannot be opened** (e.g. open/locked in another application) — with
  distinct messages, not one generic read error. The validation summary flags
  **duplicate point IDs** both **within the file** (naming the ID and the
  conflicting line numbers) and **against survey points already in the session**
  (naming the ID and line), in addition to the existing per-row column/number/ID
  parse errors. The summary shows an **overall status**: "Ready to import — N
  point(s)" when importable, or "Cannot import — <reason>" when a file-level
  problem blocks it. **File-level problems** (not found, empty, unreadable/locked,
  or zero valid rows) **disable the Import action**. When **only row-level
  problems** exist, Import remains enabled but pressing it first **asks the user to
  confirm** importing the N valid rows and skipping the M bad rows before any
  change is made. A skipped row still writes no partial or guessed value into the
  model (REQ-001 preserved). This is a display/validation layer over the existing
  importer; stored coordinates, the local-storage invariant, and import behavior
  for accepted rows are unchanged.
- Acceptance:
  - selecting a path that does not exist shows "File not found"; an **empty** file
    shows "File is empty"; a file **open/locked in another application** shows a
    message naming that it could not be opened (not a generic error); in all three
    the Import action is **disabled**;
  - a file containing a **duplicate ID within itself** shows "Duplicate point ID N
    (line X and line Y)"; a file whose ID equals an **existing session point**
    shows "Point ID N already exists in the drawing (line X)";
  - a fully valid file shows "Ready to import — N point(s)" and Import is enabled;
  - a file with **only row-level problems** shows the per-row messages, keeps
    Import enabled, and pressing Import **prompts to confirm** importing the valid
    rows and skipping the bad rows before any change;
  - a skipped row writes no value into the model (REQ-001 preserved).
- Owner-layer: UI (window, overall status, confirm prompt), IO (file-state
  classification, duplicate/parse diagnostics)
- Status: accepted
- Revisions: 2026-06-20 — initial. 2026-06-20 — for PENZD layouts the trailing
  description column is **optional**: a P,N,E,Z row (4 columns, no description) is a
  valid point and imports with an empty description (a missing description is no
  longer flagged as "too few columns"). 2026-06-20 — **elevation (Z) is also
  optional**: the required minimum is the ID (if present) plus the two horizontal
  coordinates (P,N,E for PENZD; N,E/E,N for NEZ/ENZ); a missing or blank Z defaults
  to 0. A Z that is present but unparseable is still an error (REQ-001).

### REQ-042 — Hatch fills are selectable, editable entities
- Purpose: a hatch (imported `SOLID` `HATCH` → `CadFilledRegion`, ADR-011, or one created by
  REQ-043) must be a first-class object the user can pick and remove/transform, not a stuck
  background fill (the user could not select or delete imported hatches)
- Priority: should
- Type: functional
- Statement: Solid filled regions (`CadFilledRegion`) become a selectable entity type
  (`SelectedEntity::Type::FilledRegion`), with the same interaction surface as other CAD
  objects in model space: (a) **single-click pick** — clicking anywhere **inside** the fill
  (point-in-polygon against the outer loop, **excluding** hole loops) selects it, with Shift to
  add/toggle and a **hover pre-highlight**; (b) **window/crossing box** selection (same
  L→R=window / R→L=crossing rule) — a fill is hit when its loops are inside the window box, or
  the box crosses/contains it for crossing; (c) a **selection highlight** that reads clearly
  over the fill; (d) **DELETE** removes the selected fill(s); (e) **MOVE** and **COPY**
  translate the fill (all loop vertices) and (COPY) clipboard-paste it, replacing the prior
  bbox-enclosure copy heuristic for directly selected fills (ADR-013 addendum) with true
  selection; every such edit is **undoable** and persists through `.gs` (REQ-031/ADR-011) and
  the existing `HATCH` DXF export. Rotate/scale/mirror/grips for fills are **out of scope** for
  this requirement (a later REQ). Pattern hatches that import as boundary **outlines** are
  already selectable as their line entities and are unaffected.
- Acceptance:
  - clicking inside an imported solid hatch selects and highlights it; clicking outside it (or
    inside one of its holes) does not;
  - a window box that encloses the fill selects it; a crossing box that touches it selects it;
  - hovering a fill shows a pre-highlight;
  - DELETE removes the selected fill and **Undo** restores it (geometry + color + layer);
  - MOVE relocates the fill and Undo restores the original position; COPY + paste produces an
    offset duplicate; a `.gs` round-trip restores the result;
  - existing selection of lines/circles/arcs/etc. and existing hatch fill rendering are unchanged.
- Owner-layer: Commands / Domain / UI / Renderer / IO
- Status: accepted
- Revisions: 2026-06-20 — initial (ADR-016; amends ADR-013 addendum "fills aren't a selectable
  entity type").

### REQ-043 — HATCH command with internal-point boundary detection, live preview, patterns, and a dynamic ribbon
- Purpose: the user can fill a region bounded by existing geometry by picking an internal point
  (AutoCAD `HATCH`/`BHATCH` pick-point workflow), choosing pattern and appearance, and seeing a
  live preview (the user asked for a HATCH command)
- Priority: should
- Type: functional
- Statement: A **HATCH** command fills a closed region of existing geometry. (a) **Internal
  point** — the command prompts for a point inside the area to hatch. (b) **Boundary
  detection** — from the candidate point the command traces the **smallest closed loop** that
  encloses it, built from a planar graph of nearby boundary geometry (lines, polylines, arcs,
  circles); arcs/circles are tessellated for the trace. If **no closed region** contains the
  point, the command reports it and places nothing (REQ-201 — no silent failure, no guessed
  fill). (c) **Live preview** — while the cursor is inside a detected region the candidate fill
  is previewed with the active pattern/appearance; outside any region no preview shows. (d)
  **Placement** — clicking inside a detected region creates a hatch filling it; the created
  hatch is a `CadFilledRegion` (REQ-042 — immediately selectable/movable/deletable) and persists
  (`.gs`, `HATCH` DXF export). (e) **Patterns** — the hatch may be **SOLID** or a line **pattern**
  (e.g. ANSI31) rendered clipped to the boundary and driven by **angle** and **scale**; pattern
  name + angle + scale are stored on the region. (f) **Dynamic ribbon tab** — while HATCH is
  active a contextual ribbon tab (the ADR-008 contextual-ribbon pattern) shows hatch-type
  **thumbnail** swatches (a **placeholder** set this pass) and **live** controls for **color**,
  **transparency**, **layer**, **angle**, and **scale** that apply to the hatch being created.
- Acceptance:
  - running HATCH prompts for an internal point;
  - moving the cursor **inside** a closed region shows a preview fill; moving it where no closed
    region contains it shows **no** preview;
  - clicking inside a detected region creates a hatch that exactly fills that region;
  - if the point is in no closed region, the command reports "no closed boundary found" and
    creates nothing;
  - the created hatch honors the ribbon's selected pattern, angle, scale, color, transparency,
    and layer;
  - the created hatch is immediately selectable/deletable/movable (REQ-042) and survives a `.gs`
    round-trip.
- Owner-layer: Commands (command + boundary trace) / Renderer (pattern fill + preview) / UI
  (dynamic ribbon) / Domain+IO (pattern fields on `CadFilledRegion`)
- Status: accepted
- Revisions: 2026-06-20 — initial. ADR-017 (boundary tracing), ADR-018 (pattern storage +
  rendering; amends ADR-011 "solid fills only"), ADR-019 (dynamic HATCH ribbon; extends ADR-008).
  Delivered incrementally: Phase 2 = command + trace + preview + SOLID + ribbon (color/
  transparency/layer live); Phase 3 = line patterns driven by angle/scale + thumbnails.

### REQ-044 — Named text styles (create/manage, live reference with per-text overrides)
- Purpose: let the user define and reuse AutoCAD-style named text formatting instead of the
  single de-facto font/height — the user asked to create text styles like AutoCAD and switch
  between them
- Priority: should
- Type: functional
- Statement: A drawing owns a table of named **text styles**, each defining **font**, **height**
  (plotted inches), **oblique angle**, and **bold/italic**. (Color is **not** a style property —
  it remains a layer/object property edited in the Properties panel, matching AutoCAD STYLE.) A
  default style **"Standard"** always exists and cannot be deleted. The user **creates, renames,
  deletes, and edits** styles in a **management dialog** (AutoCAD STYLE-like) and selects the
  **active** style from a **dropdown**; newly created text adopts the active style. Each text
  object keeps a **live reference** to its style: **editing a style updates every text using it**,
  **except** properties the user has **overridden** on specific selected text via the Properties
  panel (font, height, oblique, bold/italic — each overridable independently). **Switching the
  active style affects only newly created text**, never existing text. Text styles and each text's
  style reference + overrides **persist in the native `.gs` file**; **older `.gs` files load with
  every existing text rendered exactly as before** (text with no style reference resolves from its
  own stored fields). **DXF import registers the drawing's STYLE table as live text styles**: each DXF
  `STYLE` record becomes a named text style (font + oblique/italic), and every imported TEXT/MTEXT holds a
  **live reference** to its style (DXF group 7), so editing that style updates the imported text — the same
  as native text. The imported per-text **height is a per-text override** (DXF group 40), so a style edit
  changes font/oblique, not each label's height. A pre-existing drawing style is never clobbered (an unset
  font is filled from the DXF; a user-set font is kept). Stored coordinates, the local-storage invariant,
  and all non-text behavior are unchanged.
- Acceptance:
  - creating a style in the dialog, setting it active, then drawing text produces text in that
    style's font/height/oblique/bold/italic;
  - switching the active style changes only **new** text; existing text is unchanged;
  - **editing a style** updates every text referencing it, except properties overridden on
    specific text;
  - overriding font/height/oblique (or color, via the existing General color) on selected text in
    the Properties panel changes only that text and survives a later edit of its style;
  - saving and reloading a `.gs` file preserves all styles and each text's reference + overrides;
  - opening an **older** `.gs` file leaves every existing text visually unchanged;
  - **importing a DXF** registers its STYLE table as text styles, and editing an imported style's font
    updates every imported TEXT/MTEXT that references it (heights unchanged, being per-text overrides).
- Owner-layer: Domain (style table + resolution) / UI (dialog, dropdown, Properties) / IO (.gs)
- Status: accepted
- Revisions: 2026-06-21 — initial (ADR-020). Delivered incrementally: Phase 1 = data model +
  Standard + active-style dropdown + create path + `.gs` persistence; Phase 2 = STYLE management
  dialog (create/rename/delete/edit + re-bake referencing text); Phase 3 = Properties per-text
  overrides + oblique rendering. 2026-07-29 — DXF STYLE-table round-trip un-deferred: import now
  registers the STYLE table as live text styles and links imported TEXT/MTEXT to them (imported height
  is a per-text override); editing an imported style's font ripples to the imported text.

### REQ-045 — PAN command (interactive view pan via the command line)
- Purpose: AutoCAD-style typed panning — the user asked for a PAN command because only
  middle-mouse drag panned the view
- Priority: should
- Type: functional
- Statement: A `PAN` command (alias `P`), recognized at the command line like every other
  command, enters an interactive **pan mode**: the cursor becomes a **hand** and dragging with
  the **left** mouse button pans the active view 1:1 with the cursor. Pressing **Esc**,
  **Enter**, or **right-clicking** exits pan mode and restores the prior cursor and active tool.
  Pan mode operates in model space, paper space, and floating model space, reusing the **same
  view transform** as the existing middle-mouse-drag pan — which continues to work unchanged.
  This is a UI-layer interaction over the existing view pan; no geometry, coordinate, or storage
  behavior changes (REQ-101 fidelity untouched).
- Acceptance:
  - typing `PAN` or `P` enters pan mode and the cursor changes to a hand;
  - left-mouse drag moves the view by the drag delta (1:1) in the active space;
  - Esc, Enter, or right-click exits pan mode and restores the prior cursor and active tool;
  - existing middle-mouse-drag pan still works unchanged;
  - pan mode works in both model space and floating/paper space.
- Owner-layer: UI
- Status: accepted
- Revisions: 2026-06-21 — initial.

### REQ-046 — Per-viewport layer overrides: VP Freeze + VP Color in the Layer Manager, and VPFREEZE/VPTHAW commands
- Purpose: AutoCAD-style per-viewport layer control — the user manages how each layer
  appears **in a given viewport** (frozen or recolored) from the Layer Manager and by
  picking objects, replacing the ad-hoc "Frozen Layers" panel
- Priority: should
- Type: functional
- Statement: Per-viewport layer state is controlled two ways, both targeting the **current
  viewport** — defined as the **floating** viewport when the user is inside one (REQ-036),
  else the **single selected** viewport in paper space, else **none** (controls disabled).
  (a) **Layer Manager columns** — the LAYER manager gains a **VP Freeze** column (checkbox)
  that freezes/thaws the row's layer in the current viewport (the REQ-028 per-viewport freeze
  set), and a **VP Color** column (color picker) that sets a **per-viewport color override**
  for the row's layer; both are editable only when a current viewport exists. The standalone
  "Frozen Layers" panel is **removed**. (b) **VPFREEZE / VPTHAW commands** — `VPFREEZE`
  prompts "Select objects" and freezes each picked entity's layer in the current viewport;
  `VPTHAW` is the inverse (thaws the picked layers). Esc or an empty selection changes nothing.
  A **VP Color override** recolors that layer's entities **only within its viewport** — on
  screen and in the **PDF plot** (this amends the ADR-007 monochrome-vector plot for
  per-viewport layer color; layers with no override keep the existing rendering). Because the
  on-screen viewport currently draws model linework in a fixed color (the GL true-color pass is
  deferred), the override colors **only the overridden layers**; general true-color viewport
  rendering is out of scope. All per-viewport freeze and color state is **strictly per
  viewport** — model space and other viewports are unaffected — and **persists per viewport in
  the native `.gs`** (extends REQ-031, the `frozenLayers` pattern; missing/garbage → empty, no
  crash). DXF persistence of per-viewport overrides stays deferred.
- Acceptance:
  - the standalone "Frozen Layers" panel no longer appears;
  - with a current viewport, the Layer Manager shows the **VP Freeze** and **VP Color** columns;
    with no current viewport, they are disabled;
  - checking **VP Freeze** for a layer hides that layer's geometry in the current viewport only
    (still visible in other viewports and in model space); unchecking restores it (REQ-028);
  - setting **VP Color** for a layer renders that layer's entities in the override color within
    the current viewport only; clearing the override reverts to the normal color;
  - **VPFREEZE** → select objects → those entities' layers are frozen in the current viewport
    only; **VPTHAW** → select objects → those layers are thawed; Esc / empty pick changes nothing;
  - a `.gs` save/load round-trips per-viewport frozen layers **and** color overrides;
  - a PDF plot of a viewport shows frozen layers **absent** and VP-Color layers in their
    **override color**, matching the screen;
  - existing global layer freeze/color and model-space rendering are unchanged.
- Owner-layer: UI / Commands / Domain / Renderer / IO
- Status: accepted
- Revisions: 2026-07-13 — initial (ADR-021; amends ADR-007 for per-viewport plot color;
  supersedes the REQ-028 "Frozen Layers" panel UI).

### REQ-047 — ORTHO mode: optional H/V drawing constraint, off by default, reliably toggleable
- Purpose: draw commands must be able to place points at any angle — the user could only draw
  orthogonal lines because ORTHO was forced on and could not be reliably turned off
- Priority: should
- Type: functional
- Statement: **ORTHO** is an optional drawing constraint that, when **on**, snaps a draft/committed
  point onto the horizontal or vertical line through the current anchor (whichever axis the cursor is
  farther along), matching AutoCAD. ORTHO is **off by default** — with ORTHO off, LINE and the other
  draft commands commit to the **actual** cursor/typed point at **any angle**. ORTHO is toggled by
  **F8** and by the status-bar **ORTHO** button; **F8 works even while the command bar has keyboard
  focus** (it is a mode key, not text). **Object snap overrides ORTHO** (a snapped point wins). This is
  a UI/interaction constraint over the existing draw path; it changes no geometry, coordinate, or
  storage behavior (REQ-101 fidelity untouched). (The same mode-key rule applies to **F3** object-snap.)
- Acceptance:
  - a fresh drawing has ORTHO **off**; drawing a LINE between two non-aligned points produces a segment
    at the true angle (not snapped to H/V);
  - turning ORTHO **on** (F8 or the status button) constrains the next LINE segment to horizontal or
    vertical from the anchor; turning it off again restores free-angle drawing;
  - **F8 toggles ORTHO even while the command bar is focused** (typing a command does not disable F8);
  - when a point is object-snapped, ORTHO does not override the snap.
- ORTHO also governs **direct-distance entry** and **grip editing**:
  - **Direct-distance entry.** With ORTHO on and a draft anchor set, typing a bare distance places the
    next point that far from the anchor **along the axis the crosshair indicates** — left, right, up or
    down. The direction comes from the crosshair, so the anchor and the crosshair must be compared in the
    **same coordinate frame**: storage is local (world = local + document origin), so a world-space
    crosshair is converted to local first.
  - **Grip editing.** Dragging a grip is a draw operation and obeys ORTHO the same way: the dragged point
    is constrained to the horizontal or vertical line through **the grip's position when it was armed**,
    an object snap still wins, and typing a bare distance places the grip that far along that axis and
    ends the stretch (AutoCAD's grip behaviour). One undo returns the entity to its pre-drag state
    whether it was placed by dragging or by typing. While a grip is armed the **cursor dynamic-input box**
    (REQ-024) is shown even though no command is active: it displays the **live stretch distance**,
    following the cursor and updating as the grip moves, with its text **selected** so a keystroke replaces
    it. Enter commits the shown or typed distance.
- Acceptance (direct-distance and grips):
  - with ORTHO on, a first LINE point placed and the crosshair held to the **left** of it, typing `50`
    draws a 50-unit segment to the **left** — and likewise right, up and down;
  - the same holds on a drawing whose document origin is a state-plane coordinate, not only on a fresh
    drawing at the origin;
  - with ORTHO on, dragging a line's endpoint grip moves it only horizontally or vertically from where
    the grip started; turning ORTHO off restores free dragging;
  - with a grip armed, the dynamic-input box appears at the cursor showing the live distance with its text
    selected; typing `25` replaces it, moves the grip 25 units along the crosshair's axis and completes
    the stretch; one undo restores the original geometry; Esc cancels the drag with the box focused.
- Owner-layer: UI (default + key/status toggles, the grip drag) / Commands (the pure ORTHO constraint and
  axis-direction helpers, the typed-distance paths)
- Status: accepted
- Revisions: 2026-07-13 — initial. Root cause: ORTHO defaulted on (`main.cpp` orthoEnabled=true) and
  F8 was gated behind text-input focus, so it could not be reliably turned off.
  2026-08-11 — extended to direct-distance entry and grip editing. Two defects fixed: (a) the LINE and
  POLYLINE typed-distance paths compared a **world**-space crosshair against a **local** anchor, so any
  drawing with a non-zero document origin had the origin added to dx alone and every typed distance drew
  to the **right**; (b) grip dragging ignored ORTHO entirely and had no typed-distance entry.

### REQ-048 — True entity/layer colors in paper space (on screen and in the plot)
- Purpose: paper space should show a drawing in its real colors like model space and AutoCAD — the
  user could only see a flat neutral color (color appeared only for REQ-046 VP Color overrides)
- Priority: should
- Type: functional
- Statement: In a paper layout, both **model geometry shown through viewports** (every viewport,
  always — including the floating viewport) and **native paper-space sheet geometry** (lines, text,
  circles, arcs, ellipses, polylines, filled regions) render in their **true color** — the entity's
  own color, or its layer's color when the entity is ByLayer — resolved by the existing
  `ResolveEntityRgbaForViewport` path, instead of the flat neutral `kVpModelCol` / `kPaperGeomCol`.
  Precedence: a REQ-046 **VP Color override** wins over the entity/layer color; **selection and hover
  highlight** colors still win over the base color; per-viewport **frozen** layers (REQ-028) and
  **off / non-plottable** layers remain hidden/excluded (only color resolution changes, not
  visibility). Colors are resolved at **render and plot time** from existing attributes — no geometry,
  coordinate, or storage change (REQ-101 untouched). The **PDF plot** prints these true colors
  (**amends ADR-007**: the plot is full-color, not monochrome; the REQ-046 per-color path grouping is
  the mechanism). Delivered incrementally: (A) on-screen colors — viewport model + native sheet;
  (B) plot colors for model-through-viewport geometry; (C) plot colors for native sheet geometry
  (depends on REQ-049 adding sheet geometry to the plot).
- Acceptance:
  - model geometry in every viewport shows its true entity/layer color on screen; a VP Color override
    still wins; selected/hovered objects still show the selection/hover color;
  - native sheet geometry shows its true entity/layer color on screen;
  - the PDF plot prints model-through-viewport geometry (and, with REQ-049, native sheet geometry) in
    true colors, VP Color override still winning;
  - per-viewport frozen and off/non-plottable layers remain hidden/excluded exactly as before;
  - stored geometry/coordinates are unchanged and model-space rendering is unchanged.
- Owner-layer: UI / Renderer / IO
- Status: accepted
- Revisions: 2026-07-13 — initial (ASSUMPTION-1 follow-up from REQ-046; amends ADR-007 to full color).
  2026-07-13 — **background-adaptive white/black** (AutoCAD color-7 behavior): a resolved color that is
  near-white renders **black** on a light background (the paper sheet / plot page), and a near-black
  color renders **white** on a dark background, so linework and text stay legible. This adaptation
  applies to the paper-space resolve sites (model-through-viewport, native sheet geometry, native sheet
  text) on screen and in the plot; other colors are unchanged. Acceptance: with a layer/entity/VP-Color
  set to white, its geometry and text are **visible (black)** on the white sheet and in the plotted PDF;
  non-white colors are unaffected.

### REQ-049 — Plot native paper-space sheet geometry and text
- Purpose: title blocks and sheet annotations must appear in the plotted PDF — today the plot renders
  only model-through-viewport stroked geometry, so native sheet lines/text and all text are omitted
- Priority: should
- Type: functional
- Statement: The PDF plot renders **native paper-space sheet geometry** (lines, circles, arcs,
  ellipses, polylines, filled regions — already in paper inches) and **text** (single-line TEXT and
  MTEXT), so sheets plot as composed. **Stroke (SHX) fonts** plot as their actual stroke geometry (the
  same strokes drawn on screen), which is faithful and reuses the existing SHX renderer; **TTF text**
  is plotted best-effort (approach chosen during implementation; if faithful TTF outline emission is
  not tractable in this increment it is documented as debt, not silently dropped — REQ-201). Plotted
  sheet geometry and text honor layer on/frozen/plottable state and are colored per REQ-048.
- Acceptance:
  - a layout with native sheet lines/geometry plots them at their sheet position;
  - single-line TEXT and SHX MTEXT on the sheet appear in the plot at the correct position/size;
  - plotted sheet geometry/text is colored per REQ-048 and excluded when its layer is off/non-plottable;
  - any TTF-text limitation is recorded as documented technical debt, not a silent omission.
- Owner-layer: IO / Renderer
- Status: accepted
- Revisions: 2026-07-13 — initial (enables REQ-048 increment C; PDF text rendering is a new capability).
    2026-07-15 — TTF-text debt resolved: TrueType sheet text is now plotted by embedding the real font
    (Windows Fonts-dir resolution → `FPDFText_LoadFont` → real PDF text objects, sized/positioned/colored
    per REQ-048), degrading to a base-14 standard font (logged, REQ-201) when the font file can't be
    resolved. `.ttc` collections and filled regions (ADR-011) remain the only recorded plot-text/geometry gaps.

---

### REQ-050 — MTEXT is sized by the viewport scale (constant plotted height per viewport)
- Purpose: MTEXT is a plotted annotation — its size on the final sheet must be governed by the scale of
  the viewport it is shown through, not by a single drawing-wide scale, so the same MTEXT reads at its
  intended plotted height through viewports at different scales
- Priority: should
- Type: functional
- Statement: A plain MTEXT entity stores a **plotted height (inches)**; its **model (world) height is
  derived at render time from the scale of the viewport it is drawn through** — the viewport being edited
  in place (floating model space) when one is active, otherwise the drawing's model plot scale
  (`modelUnitsPerPlottedInch`). Consequently the MTEXT's **plotted height stays constant on the sheet**
  regardless of that viewport's scale, and (like any model entity) it still scales on screen with zoom.
  Single-line **TEXT keeps the drawing plot scale** (plotted-inch × `modelUnitsPerPlottedInch`) — a
  specified plotted text height — and is unchanged. **Survey-point label MTEXT is unchanged** (its own
  layout owns its size, and it keeps its readability floor). No stored coordinates or heights change.
- Acceptance:
  - editing model MTEXT through a viewport whose scale differs from the drawing plot scale sizes the MTEXT
    off the viewport's scale (constant plotted height), not the drawing scale;
  - in the plain model view (no active viewport) MTEXT is sized off the drawing plot scale, unchanged;
  - single-line TEXT sizing is unchanged; survey-point labels are unchanged.
- Owner-layer: Renderer (viewport MTEXT sizing)
- Status: accepted
- Revisions: 2026-07-29 — initial. On-screen viewport render only; matching the PDF plot's MTEXT sizing to
  the per-viewport scale is a noted consistency follow-up if the two diverge.

---

### REQ-051 — MTEXT edits through an AutoCAD-style "Text Formatting" panel
- Purpose: the MTEXT editor should read and behave like AutoCAD's/nanoCAD's — a floating "Text
  Formatting" toolbar with the style, font, height, and colour controls where a surveyor reaches
  for them, over an in-place editing box — instead of a bare multiline box whose only formatting
  affordance is hand-typed rich-text tags (the user asked for this redesign from a reference
  screenshot)
- Priority: should
- Type: functional
- Statement: Editing an MTEXT in place (REQ-039's shared editor — **model** MTEXT, including the MTEXT
  placement command's text entry, and **native paper-space** MTEXT) presents a **floating panel titled
  "Text Formatting"**, **draggable** by its title bar, with its on-screen position and its ruler/expanded
  state **persisted** across edits and sessions (UserPrefs, the REQ-040 `cmdBar*` pattern — no new global).
  The panel carries **two toolbar rows** laid out in the reference order: row 1 = text style, font,
  annotative, height, bold, italic, strikethrough, underline, overline, background mask, undo, redo,
  stacking, entity colour, ruler toggle, **OK**, and an **expand control** that collapses row 2; row 2 =
  columns, MTEXT justification, paragraph, five paragraph-alignment buttons, line spacing, lists, insert
  field, uppercase, lowercase, superscript, subscript, symbol, oblique, tracking, width factor.
  The panel **sizes itself to its content**: every control is reachable without scrolling the panel.
  A **column ruler** sits above the in-place box; the ruler toggle shows and hides it, and its **right
  marker drags to set the MTEXT's column width** (an undoable edit of the annotation's box). The
  in-place box **edits WYSIWYG** (ADR-023): text **wraps at the MTEXT's column width**, the box is **one
  line tall and grows as the text wraps** or breaks, formatting **renders as formatting** (bold, italic,
  underline, uppercase, per-run font and colour), and the `[[…]]` wire tags are **never shown**. The box
  shows **no corner resize handle**: dragging the box corner is not implemented, and an affordance that
  invites a drag it cannot perform would misrepresent the editor.
  **Controls that the stored text model already supports are functional**: text style (applies per
  REQ-044/ADR-020), **font per selected characters**, **colour per selected characters**, bold, italic,
  underline, uppercase, symbol insertion, MTEXT justification (the 9-way attachment point), and —
  **whole-object** — text height, oblique angle, and entity colour. **Every remaining control is present
  but disabled and names itself in a tooltip**; each is a separate follow-up requirement, so the panel
  never implies a capability the drawing cannot store.
  **Single-line TEXT keeps its existing bare in-place box** (AutoCAD-faithful — no toolbar). The
  **rich-text wire format and every stored `CadAnnotation` field are unchanged**, so `.gs`, DXF, and PDF
  round-trips of MTEXT behave exactly as before.
- Acceptance:
  - double-clicking a model MTEXT opens a panel titled "Text Formatting" with two toolbar rows and a
    ruler above the in-place box;
  - dragging the panel by its title bar moves it, and it reopens at that position on a later edit and
    after an application restart;
  - selecting part of the text and choosing a font changes **only** those characters, the rest keeping
    theirs; the same holds for the per-selection colour control;
  - changing height changes the whole MTEXT's plotted height; changing oblique slants the whole MTEXT;
    the entity-colour control changes the object's colour (ByLayer honoured);
  - the text-style dropdown lists the drawing's styles and applying one re-bakes the MTEXT per REQ-044;
  - bold/italic/underline/uppercase and symbol insertion behave as they did before the redesign;
  - the justification dropdown sets the MTEXT attachment point and the text re-lays out in its box;
  - every disabled control does nothing and shows a tooltip naming it; the ruler toggle hides and
    shows the ruler; the expand control collapses and restores row 2;
  - paper-space MTEXT opens the same panel; **single-line TEXT still opens the bare in-place box**;
  - OK commits and Esc cancels as before, and a `.gs` save/reload plus a DXF export of edited MTEXT are
    unchanged.
- Owner-layer: UI (panel + in-place editor) / IO (UserPrefs persistence)
- Status: accepted
- Revisions: 2026-07-30 — initial. Scope deliberately bounded to controls the existing rich-text wire
  format and `CadAnnotation` already support, so no data-format change is implied; the disabled controls
  (paragraph properties, columns, fields, stacking, super/subscript, tracking, width factor, annotative,
  background mask, strikethrough, overline, in-panel undo/redo) and a drag-to-resize box corner are
  recorded follow-ups.
    2026-07-30 (same day, after the first user review) — the panel now sizes itself to its content
    (it clipped its second row); the ruler's width drag is **un-deferred** and implemented; the in-place
    box opens one line tall and grows per line of text. The word-wrap gap this exposed (ImGui's
    `InputTextMultiline` has none) was escalated and resolved as **ADR-023**: the box now edits WYSIWYG
    through the in-tree `ui/RichTextEdit` widget — text wraps at the column, the box grows with it, and
    the wire tags are hidden. Delivered under TASK-024.

---

### REQ-052 — Open and save DWG drawings
- Purpose: DWG is the format surveying clients, engineers and consultants actually exchange. A CAD
  product for survey work that can only read DXF cannot be handed a client's drawing, and the user
  has asked for DWG to eventually become GoSurvey's **native** format in place of `.gs`
  (see the open follow-ups below and `docs/dwg-plan.txt`).
- Priority: must
- Type: functional
- Statement: GoSurvey **opens** a DWG drawing and **saves** one, through File menu entries that sit
  alongside the DXF entries and use the same import log. DWG is Autodesk's proprietary format and
  AC1032 (R2018) has no public specification, so this requirement is satisfied in **phases**
  (ADR-024): Phase 1 converts DWG ↔ DXF out of process using a converter already installed on the
  machine and reuses `DxfIo`; later phases replace that with an in-tree codec. Whatever the phase,
  the behaviour below holds.
  On **open**: the file's format tag is read and reported by release name; a file that is not a DWG
  is refused with that reason rather than mis-parsed; when no converter is available the failure
  states exactly what to install and how to point GoSurvey at it; and every limitation the import
  route imposes is written to the log, not left for the user to discover.
  On **save**: because Phase 1's payload is the DXF export, the save is **lossy** — it cannot carry
  block definitions, extra layouts, elevations, attributes, or the Civil 3D objects and proxies a
  client drawing contains. A save therefore **states what it will drop before writing anything**,
  and never overwrites the destination unless a good converted file exists, so a failed save cannot
  destroy the previous drawing.
- Acceptance:
  - File ▸ Import DWG opens a real R2018 drawing and its geometry appears in model space;
  - a non-DWG file, a missing file, and a machine with no converter each produce a specific, actionable
    message and leave the drawing untouched;
  - File ▸ Export DWG shows what the export drops, names the destination when it would be overwritten,
    and writes nothing if cancelled;
  - the DWG that GoSurvey writes **opens in AutoCAD with no recovery prompt** and contains the
    entities, layers and text that were exported;
  - a failed conversion leaves no temporary directories behind and does not modify the destination.
- Status: accepted
- Notes:
  2026-07-30 — Phase 1 delivered under TASK-030 (ADR-024, converter route). Verified against
  `26-084 - Master.dwg` (AC1032, Civil 3D lineage) and by writing a DWG that AutoCAD 2026 reopens.
  Building Phase 1 exposed a **pre-existing DXF conformance defect**: the TEXT emitter wrote group
  73 without the second `AcDbText` subclass marker, so AutoCAD rejected GoSurvey's DXF outright
  ("Unexpected DXF group code: 73 — drawing discarded"). Fixed in the same task; it had been
  silently breaking DXF export to AutoCAD, not only DWG.
  2026-07-30 — Phase 1b delivered under TASK-031: the DXF TEXT record layout and the DWG probe
  (version detection + converter discovery) were extracted into units the test target can link, and
  11 test cases committed over them — including the group-73 regression above. Proven by mutation
  test (re-introducing the defect fails 4 assertions) and by byte-identical export output before and
  after the refactor. Suite: 698 assertions / 109 cases. Remaining emitters are still untested
  (TASK-031 DEBT-4).
  Open follow-ups, each its own requirement: a native in-tree codec; preservation of objects
  GoSurvey does not model (the user's decision is that a save **must** preserve them, which Phase 1
  cannot do); first-class blocks; multiple layouts; elevations; and the migration of `.gs` to DWG
  as the native format. All are itemised in `docs/dwg-plan.txt`.

### REQ-053 — RECT command, and polylines survive a DXF/DWG save
- Purpose: rectangles are the most-drawn shape in survey deliverables (parcels, structures, title-block
  panels, detail frames) and the user had to draw four separate lines for each. Building it also exposed
  that **no polyline of any kind was written to DXF** — every polyline was dropped from an export in
  silence, so a rectangle would have been unshareable even once it could be drawn.
- Priority: must
- Type: functional
- Statement: **RECT** (aliases `RECTANG`, `RECTANGLE`) draws an axis-aligned rectangle from **two
  opposite corners**, picked in the viewport or typed on the command line; the second corner also accepts
  `@dx,dy`, which is how a rectangle of an exact width and height is drawn. Between the corners the
  rectangle rubber-bands to the cursor. RECT is a **first-class draw command**: it has a ribbon button,
  a cursor dynamic-input prompt at each corner (REQ-024), right-click repeat, and it draws into **model,
  paper and floating-model space** like the other draw commands (REQ-036/REQ-037). ORTHO deliberately
  does **not** constrain the second corner — the shape is already axis-aligned and constraining it would
  collapse the rectangle to a line. A rectangle is stored as a **4-vertex closed polyline** — the same
  representation AutoCAD's RECTANG produces (an LWPOLYLINE) — so it is not a new entity type and it
  inherits selection, grips, snaps, MOVE/COPY/ROTATE/SCALE, `.gs` persistence and layer/colour handling
  from the existing polyline. **Every polyline, rectangle or not, is written to DXF as an `LWPOLYLINE`**
  carrying its true vertex count, its closed flag, and its layer/colour/linetype/lineweight/transparency;
  DWG save inherits this because it converts from the DXF (REQ-052). Degenerate corners (zero width or
  height) are rejected with a message rather than stored (REQ-201).
- Acceptance:
  - `RECT` + two viewport picks creates one rectangle; it selects, highlights and reports as a single
    object, not four lines;
  - the ribbon's Rectangle button starts it, each corner shows its dynamic-input prompt, and right-click
    repeat re-runs it;
  - drawing it on a paper layout puts it in that layout's paper store, not the model;
  - the second corner typed as `@100,50` produces a rectangle exactly 100 wide and 50 tall;
  - two coincident (or axis-collinear) corners are refused with a message and the command restarts;
  - the rectangle's four corners snap as endpoints, its edges as midpoints, and its interior offers a
    **geometric centre** (REQ-047's snap set);
  - exporting a drawing containing a rectangle writes an `LWPOLYLINE` with group 90 = 4, group 70 = 1,
    and four 10/20 vertex pairs; re-opening that DXF shows the rectangle;
  - the export log states how many `LWPOLYLINE`s were written (REQ-201).
- Owner-layer: Commands (the command + closed-polyline commit) / IO (`DxfIo` + the pure `DxfEntityEmit`
  record) / Viewport (rubber preview)
- Status: accepted
- Revisions: 2026-08-11 — initial. Found while implementing: `ExportDxfFile_Impl` had no polyline branch
  at all, so this requirement also covers the export gap it uncovered.

### REQ-054 — Right-click selection menu, and Select similar matches type + layer + colour
- Purpose: on a survey plan the **layer** is the classification — parcel lines, contours, utilities and
  text all coexist as the same geometric primitive. "Select every line in the drawing" is never the
  selection a surveyor wants; "select every line on this layer, in this colour" is.
- Priority: should
- Type: functional
- Statement: Right-clicking in the drawing **with a selection** opens the selection shortcut menu
  (MOVE/COPY/ROTATE/SCALE/DELETE, Select similar, Selection…, Clear selection) — this is the shipped
  AutoCAD default for Right-Click Customization's *Edit Mode*, and it remains user-configurable in
  Settings. **Select similar** replaces the selection with every object that matches the lead object on
  **all three** of: object type (annotations narrow further by annotation kind — TEXT is not similar to a
  dimension), **layer**, and **colour**. Layer and colour compare case-insensitively, an unset layer means
  layer `0` and an unset colour means `ByLayer`, so entities differing only in spelling still match. The
  command reports the count together with the layer and colour it matched on (REQ-201).
- Acceptance:
  - right-clicking with objects selected opens the shortcut menu rather than repeating the last command,
    on an existing profile as well as a fresh one;
  - with one line on layer `PARCEL` selected, Select similar picks up the other `PARCEL` lines and leaves
    lines on other layers, and lines of a different colour, unselected;
  - selecting a TEXT and running it does not sweep in dimensions;
  - the command line states the count, the layer and the colour.
- Owner-layer: Commands (`SelectSimilarToCurrentSelection`) / UI (the shortcut menu) / IO (the preference
  default and its one-time migration)
- Status: accepted
- Revisions: 2026-08-11 — initial. Recorded as a SPEC GAP: Select similar and the right-click menu were
  already implemented with no governing requirement, and Select similar matched on object type alone.
  The menu was also unreachable, because `rightClickEditMode` shipped defaulted to RepeatLastCommand.

### REQ-055 — A newly opened drawing is the focused tab, and a drawing reopens at the view it was saved at
- Purpose: two interruptions to the basic open/save loop. Creating or opening a drawing left the user on
  the *previous* tab, so every File > New and File > Open needed a manual tab click to reach the drawing
  just asked for. And a saved drawing reopened at the default view rather than where the user left it,
  so every reopen started with a zoom/pan hunt to get back to the work.
- Priority: should
- Type: functional
- Statement: **Tab focus.** Creating a drawing (File > New, the tab bar's "+") or opening one
  (File > Open) makes that drawing's tab the **active, focused** tab in the same action — no second click.
  Closing a tab likewise focuses the tab that takes its place.
  **Saved view.** Saving a drawing records the drawing viewport's **pan and zoom**, and opening it restores
  them, so the drawing reopens looking at what the user left on screen. The pan is stored in **world**
  coordinates: loading may rebase the document origin (large-coordinate rebase), and a local pan would
  silently point somewhere else in the drawing after that. The saved view is an **additive** `.gs` key —
  older files still load, and fall back to framing the drawing.
- Acceptance:
  - File > New shows the new empty drawing immediately, with its tab selected;
  - File > Open shows the opened drawing immediately, with its tab selected, with two or more tabs open;
  - pan and zoom somewhere specific, save, close, reopen — the drawing is at that same pan and zoom;
  - the same holds for a drawing on state-plane coordinates, where opening rebases the document origin;
  - a `.gs` saved before this requirement still opens, framed to its drawing rather than at a stale view.
- Owner-layer: UI (tab bar selection) / IO (`GsIo` view key)
- Status: accepted
- Revisions: 2026-08-11 — initial. Root causes: the tab loop assigned `activeDrawingIdx` and consumed
  `pendingDrawingTabSwitch` from whichever tab ImGui reported selected, and tabs are submitted in index
  order — so the still-selected old tab always won before the new tab was reached. The `.gs` writer had
  no view state at all.

### REQ-056 — TRIM defaults to smart line trim, controlled by the TRIMSTATE system variable
- Purpose: the common trim is "get rid of that bit" — the user knows what should go, not which object is
  doing the cutting. Making cutting-edge selection mandatory put a bookkeeping step in front of every
  trim. The drawn-line trim was already implemented but hidden behind an `L` option nobody would find.
- Priority: should
- Type: functional
- Statement: **TRIM** starts in the mode named by the **TRIMSTATE** system variable:
  - **TRIMSTATE 0 (default)** — *smart trim*: two clicks draw a line across the drawing, and the pieces
    that line crosses are trimmed. No cutting edges are picked.
  - **TRIMSTATE 1** — *classic*: pick cutting edges, Enter, then click the pieces to trim.
  Within a run, **T** switches to picking cutting edges and **L** back to the drawn line, so either mode
  stays reachable whatever TRIMSTATE is set to. `TRIMSTATE` typed bare prompts
  `Enter new value for TRIMSTATE <n>:` and a blank Enter keeps the current value; `TRIMSTATE 1` sets it in
  one line. Only 0 and 1 are accepted — anything else is refused with a message (REQ-201). The value
  **persists in user preferences**, so it is a setting rather than a per-session mode.
  While picking cutting edges (and trim targets), entity picking uses the **existing hover highlight and
  selection highlight**: the object under the cursor pre-highlights, and picked cutting edges stay
  highlighted as a selection. TRIM is the only command that relaxes the "no entity hover during a
  command" rule, because its clicks name objects rather than coordinates.
- Acceptance:
  - on a fresh profile, TRIM prompts for the first point of a trim line — no cutting-edge step;
  - two clicks across a segment trim it, and the command ends;
  - `TRIMSTATE 1` then TRIM prompts for cutting edges; the mode survives a restart;
  - a bare `TRIMSTATE` shows the current value and blank Enter leaves it unchanged; `TRIMSTATE 2` is
    refused with a message and the value is unchanged;
  - `T` during a line trim switches to cutting edges, `L` during edge picking switches back;
  - hovering an object while picking cutting edges highlights it; picked edges stay highlighted; hovering
    an already-picked edge does not double-highlight.
- Owner-layer: Commands (mode + the TRIMSTATE command) / UI (hover gate) / Viewport (highlight) /
  IO (preference persistence)
- Status: accepted
- Revisions: 2026-08-11 — initial. The drawn-line trim already existed as the `L` option; this makes it
  the default and gives the choice a name.

---

## 3D model space requirements

> These cover the move from a plan-view 2D drawing surface to a true 3D model space
> (ADR-025). They are deliberately split into five independently shippable requirements:
> REQ-057 puts Z into the data, REQ-058 puts a camera in front of it, REQ-059/060 are the
> navigation and manipulation surfaces, and REQ-061 carries the camera into paper space.
> Paper-space *sheet* geometry stays 2D throughout — a sheet is 2D by definition
> (ADR-009/013 stores are unchanged).

### REQ-057 — 3D coordinates through the model, IO, and Properties
- Purpose: surveyors work in three dimensions; today elevation is captured but never drawn,
  so terrain, layered utilities and vertical relationships are invisible and uncheckable
- Priority: must
- Type: functional
- Statement: Every model-space entity carries a Z coordinate — lines, polylines, circles,
  arcs, ellipses, filled regions, text/MTEXT, dimensions and survey points. Z is stored
  **interleaved** with X and Y in every flat geometry store, one uniform convention across all
  geometry (ADR-025 (a), amended 2026-08-11), and **absolutely**, with no `worldDocumentOriginZ`
  (ADR-025 D2 — the local-origin invariant stays X/Y-only). `SurveyPoint::elevation` **is**
  the point's Z: no duplicate field and no conversion step, so existing drawings gain true
  relief with no re-import. Z survives a round-trip through DXF (group 30), DWG and `.gs`.
  The Properties panel displays and edits Z per entity type, and that edit is undoable.
- Acceptance:
  - importing a DXF fixture with non-zero Z and re-exporting reproduces every group-30 value
    within REQ-101 tolerance;
  - a `.gs` saved with 3D geometry reloads with every Z bit-identical;
  - a legacy `.gs` carrying no Z loads with all Z = 0 and renders identically to pre-change;
  - editing Z in Properties moves the entity, and Ctrl+Z restores the previous value;
  - a survey point reports its stored elevation as its Z with no import or conversion step;
  - a circle or filled region survives insert, erase-from-the-middle and undo with its Z still
    attached to the right entity (asserted in tests — the stride-widening regression).
- Owner-layer: Domain (storage), IO (persistence), UI (Properties)
- Status: accepted
- Revisions: 2026-08-11 — initial. 2026-08-11 — **amended**: the statement originally specified
  additive parallel Z arrays per ADR-025 D1. That design rested on an incorrect reading of the
  existing strides (`userLinesFlat` and `userPolylineVerts` already carry Z inline). Corrected to
  interleaved XYZ throughout; see the ADR-025 correction note and the decision log.

### REQ-058 — Orbitable 3D camera with ray picking and a UCS work plane
- Purpose: make the third dimension inspectable and drawable-in
- Priority: must
- Type: functional
- Statement: The model viewport is driven by a camera (eye / target / up → view matrix) with
  selectable orthographic or perspective projection. The user can orbit freely. **Plan view
  with orthographic projection is the startup default and reproduces the previous 2D
  behaviour.** Picking becomes a screen-ray → world-ray test and object snapping resolves in
  3D. Drawing input resolves as ray × the **active work plane (UCS)**, which defaults to the
  world XY plane. Every existing 2D command continues to work unchanged while the camera is
  in plan view.
- Acceptance:
  - plan view renders pixel-comparable to the pre-change build on a reference drawing;
  - endpoint / midpoint / center / intersection snaps resolve correctly from an orbited
    camera, verified against hand-computed coordinates within REQ-101;
  - LINE, ARC, CIRCLE and TEXT drawn on a non-default UCS land on that plane within REQ-101;
  - the existing test suite stays green;
  - the REQ-100 frame budget is met while orbiting;
  - **every entity type** — not only lines — snaps, previews and commits at the correct elevation
    under an orbited view;
  - **snap glyphs face the viewer** at any orientation rather than lying in the work plane, where
    they foreshorten to an unreadable edge near a horizontal view. They are UI markers, not geometry.
- Owner-layer: Renderer (matrices, draw), UI / Commands (input, picking, snap)
- Status: accepted — **SIGNED OFF 2026-08-12.** Every acceptance condition is met:
  plan-view parity (asserted by `CameraTests`, not assumed); snaps from an orbited camera;
  intersection snaps (REQ-062 / TASK-038 — the snap named here did not exist until then);
  **every entity type** snapping, previewing and committing at the correct elevation (TASK-036
  closed GAP-1/GAP-3, fourteen defects across six pipeline stages); screen-facing snap glyphs
  (TASK-037 closed GAP-2); the suite green; and the REQ-100 frame budget measured at p95 8.93 ms
  against 16 ms (TASK-039) — **re-measured under MSVC 2026-08-15 at p95 9.27 ms, still met**
  (TASK-052).
- Revisions: 2026-08-11 — initial. 2026-08-11 — acceptance extended with the two conditions above
  once 3D drawing was actually exercised: "it works for lines" did not generalise, and a snap glyph
  built as flat world geometry is unreadable in a near-horizontal view.
  2026-08-12 — signed off. The prior status ("only LINE is carried through; CIRCLE is known broken")
  had been stale since TASK-036 and is superseded.

### REQ-059 — ViewCube (view navigation widget)
- Purpose: direct, discoverable view control and continuous orientation feedback
- Priority: should
- Type: functional
- Statement: A **labelled ViewCube surrounded by a W/N/S/E compass ring** occupies the model
  viewport's top-right corner and tracks the camera orientation continuously. Appearance follows
  the mockup supplied with the original feature request. The widget carries:
  - **six labelled faces** — TOP, BOTTOM, FRONT, BACK, LEFT, RIGHT — each label drawn on its face
    whenever that face is visible, shrunk to fit rather than omitted;
  - **two rotation arrows** that square the view up with the next compass direction (N/E/S/W)
    clockwise or counter-clockwise, **relative to the active coordinate system** — under a rotated
    UCS they square to the UCS's north, not the world's;
  - a **home button** that sets the SW isometric view (azimuth 45° from the active coordinate
    system, elevation atan(1/√2) ≈ 35.264°).
  Clicking a face sets the camera to that standard view. **Every orientation change the widget
  initiates is animated**, not snapped — a hard jump makes it easy to lose track of which way the
  model turned. A manual orbit cancels an animation in flight.
- Acceptance:
  - clicking TOP, FRONT and a side face each set the camera to the corresponding standard view;
  - every visible face shows its label, including the long ones (BOTTOM, FRONT, RIGHT);
  - each rotation arrow moves the view to the next quarter turn **even when already square**, and
    preserves the current elevation — it is a rotation, not a view preset;
  - the home button reaches SW isometric from any starting orientation;
  - after any orbit the cube's displayed orientation matches the camera;
  - **a click anywhere on the widget — face, arrow or home — never reaches the viewport behind it**
    and in particular never begins a selection; geometry outside the widget still picks normally;
  - orientation changes ease to the target and settle within 0.5 s, taking the short way around the
    compass; a manual orbit during one takes over immediately;
  - **in plan view (the startup default) the widget is legible** — this is the condition the first
    implementation failed.
- Owner-layer: UI
- Status: accepted
- Revisions: 2026-08-11 — initial. 2026-08-11 — amended to ImOGuizmo's stock axis-ball after
  FINDING-2 (the adopted dependency could not render the mockup unmodified). 2026-08-11 —
  **amended back**: shipped and observed, the axis-ball collapses to a point in plan view and
  conveyed no orientation, so the cube is now built in-tree and the mockup is the target again.
  The legibility clause above was added so this cannot regress silently.

### REQ-060 — 3D manipulation gizmo for the current selection
- Purpose: direct manipulation instead of coordinate entry
- Priority: should
- Type: functional
- Statement: With a selection active, a translate / rotate / scale gizmo operates on it in 3D,
  driven by the REQ-058 camera matrices. Each gizmo drag is a single undoable operation and
  produces the same result as the equivalent MOVE / ROTATE / SCALE command.
- Acceptance:
  - translate, rotate and scale each move the selection as displayed, and one Ctrl+Z restores
    the prior state in a single step;
  - a gizmo drag and the equivalent typed command produce coordinates agreeing within REQ-101;
  - no gizmo is drawn when the selection is empty.
- Owner-layer: UI (widget), Commands (apply + undo)
- Status: accepted
- Revisions: 2026-08-11 — initial.

### REQ-061 — Per-viewport camera in paper space
- Purpose: put a plan view and an isometric on the same sheet
- Priority: should
- Type: functional
- Statement: `PaperLayout` geometry remains 2D paper inches (ADR-009/013 stores unchanged).
  Each `Viewport` gains a stored camera direction, up vector and projection, persisted
  additively in `.gs`. Screen rendering and the PDF plot each render a viewport's model
  content from that viewport's own camera.
- Acceptance:
  - a layout with two viewports — one plan, one isometric — renders both correctly on screen
    and plots both correctly to PDF;
  - a legacy `.gs` loads with every viewport in plan view and renders identically to
    pre-change.
- Owner-layer: Domain (data), Renderer (draw), IO (`.gs` + plot)
- Status: accepted
- Revisions: 2026-08-11 — initial.

### REQ-062 — Intersection and apparent-intersection object snaps
- Purpose: snap to where objects meet — and, in a 3D view, to where they only *look* like they meet
- Priority: must
- Type: functional
- Statement: Two new object snaps join the existing set.
  **Intersection** returns a point where two objects genuinely meet in 3D: their XY paths cross
  *and* their elevations agree at that crossing within REQ-101. **Apparent intersection** returns a
  point where two objects cross **as projected into the current view** but need not meet in space —
  the case a plan view cannot distinguish and an orbited one makes obvious. Where the two candidate
  3D points differ, apparent intersection returns the one **nearer the camera**: the object the user
  is visually pointing at. Both are per-type toggles alongside Endpoint / Midpoint / Center, persist
  in user preferences and `.gs`, and appear in the Shift+right-click snap-override menu.
  Coverage is every drawable pair of line segments, polyline edges, arcs, circles and ellipses.
  Intersections are computed analytically wherever a closed form exists and refined numerically
  otherwise; a tessellated approximation does not satisfy REQ-101 and is not acceptable.
- Acceptance:
  - two segments that cross in XY at the same elevation report an Intersection snap at the crossing,
    verified against hand-computed coordinates within REQ-101;
  - the same two segments at elevations differing by more than REQ-101 report **no** Intersection —
    and do report an Apparent intersection whenever the view projects them across each other;
  - a line crossing a circle reports both intersection points at the exact analytic coordinates, not
    the chord approximations a tessellated circle would give (a 24-chord arc is off by ~0.86 ft at
    r = 100, which is 86× REQ-101);
  - an intersection outside an arc's sweep, or beyond a segment's ends, is not reported;
  - apparent intersection follows the view: two skew objects that cross on screen stop reporting a
    snap once the camera orbits so their projections separate;
  - in plan view an Apparent intersection at equal elevations coincides with the Intersection.
- Owner-layer: util (pure intersection math), viewport (snap), UI (toggles, menu, glyph)
- Status: accepted
- Revisions: 2026-08-12 — initial.

### REQ-063 — Triangle mesh entity
- Purpose: hold imported 3D model geometry that GoSurvey does not author
- Priority: must
- Type: functional
- Statement: A new entity type stores a triangle mesh: interleaved XYZ positions (architecture
  §11.8), a vertex normal per position, triangle indices, and a per-mesh material colour. Meshes are
  grouped into named **parts** so one imported model keeps its object structure — a pipe run remains
  distinguishable from a valve. Meshes are **reference geometry, not draftable**: GoSurvey does not
  create or edit them, they carry no linetype or lineweight, and no command modifies their vertices.
  They participate in layers, visibility, selection, delete, and view extents; they are excluded from
  object snapping (REQ-064 covers what snapping, if anything, they get) and from DXF/DWG export,
  which has no lossless representation for them.
- Acceptance:
  - a mesh of N triangles round-trips through `.gs` with vertex positions bit-identical on reload;
  - a legacy `.gs` with no mesh section loads unchanged;
  - meshes are included in zoom-extents and in the drawing's bounding box;
  - erasing a mesh is undoable in one step;
  - a mesh on a frozen or off layer is not drawn, and one on a non-plottable layer is not plotted;
  - memory: a 2-million-triangle model loads and reports its triangle count without exhausting a
    32-bit index space or silently truncating.
- Owner-layer: Domain (store), IO (`.gs`), Renderer (draw)
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial draft.

### REQ-064 — Shaded visual styles
- Purpose: make 3D models readable as solids rather than as a thicket of edges
- Priority: must
- Type: functional
- Statement: The model viewport gains a **visual style** selector with at least
  **2D Wireframe** (today's behaviour, and the default), **Hidden** (wireframe with depth testing,
  so near geometry occludes far), and **Shaded** (filled triangles with diffuse lighting from a
  headlight, plus optional edges). Depth testing is enabled for every style except 2D Wireframe.
  Entity colour resolution is unchanged (REQ-048); shading multiplies it. The style is per-viewport
  state, persisted in `.gs` and in user preferences, and each paper-space viewport (REQ-061) carries
  its own.
- Acceptance:
  - **2D Wireframe renders pixel-identical to the pre-change build on a reference drawing** — the
    existing behaviour is preserved exactly, not approximately;
  - in Hidden and Shaded, a near object occludes a far one, and the draw-order artefacts that a
    depth-less renderer shows under orbit are gone;
  - in Shaded, a curved surface shows a lighting gradient rather than a flat fill, and the lighting
    follows the camera when orbiting;
  - switching styles does not alter geometry, selection, snapping results, or the plot;
  - the REQ-100 frame budget is met in Shaded at the REQ-063 mesh density chosen for the bench.
- Owner-layer: Renderer (draw), UI (selector), IO (persistence)
- Status: accepted (2026-08-12) — **fully delivered 2026-08-15.** The last unverified condition, the
  frame budget in Shaded, now has a measurement behind it: REQ-100's profile (b) exists (TASK-053)
  and reports **p95 1.97 ms** at 2,000,000 triangles in Shaded on the RTX 5060, against a 16 ms
  budget. On the integrated GPU the same scene is 21.40 ms and fails — see BUG-013, which decides
  which of those two the budget is judged on.
- Revisions: 2026-08-12 — initial draft. Supersedes ADR-025 ASSUMPTION-1, which deliberately left
  depth testing off pending a visual-style requirement; this is that requirement.

### REQ-065 — glTF / GLB model import
- Purpose: get real 3D models — plant, structural, scanned-and-modelled — into the drawing
- Priority: must
- Type: functional
- Statement: GoSurvey imports 3D models into REQ-063 meshes from **glTF 2.0** (`.gltf` + external
  buffers, and self-contained `.glb`), **STL** (binary and ASCII), and **DWG** — the last by driving
  an installed AutoCAD to explode and tessellate its 3D solids, because a DWG's 3D content may be
  vendor custom objects that only the vendor's own enabler can decode (ADR-026 Context, amended
  2026-08-12). One command accepts all three; the user picks a file and does not have to know which
  route it takes. STL carries no colour or object names, and the DWG route goes through STL, so both
  produce a single unnamed part — **reported at import**, not left to be discovered.
  Node hierarchy is flattened to world space with each node's transform applied,
  and node names are kept as part names. Base-colour factors from PBR materials become per-mesh
  colours; textures, animation, cameras, lights, skins and morph targets are **out of scope and
  reported as skipped**, never dropped silently (REQ-201). The import prompts for a unit scale and an
  insertion point, defaulting to the file's declared units where present, because model authoring
  units (commonly inches or millimetres) rarely match a survey drawing's feet. Imported coordinates
  are converted to the local storage frame in double before being stored (the local-storage
  invariant), so a model placed at state-plane coordinates keeps sub-hundredth precision.
- Acceptance:
  - a `.glb` of known triangle count imports with that exact count, and its bounding box matches the
    source dimensions within REQ-101 after the unit scale;
  - a nested node hierarchy with non-identity transforms lands in the right place — verified against
    hand-computed coordinates for at least one doubly-nested node;
  - per-node names and base colours survive, so an imported model is not one undifferentiated blob;
  - a file containing textures/animation imports its geometry and **states in the log what it
    skipped**;
  - a malformed or truncated file is rejected with a specific message and leaves the drawing
    unchanged — no partial import;
  - importing at state-plane coordinates keeps vertex precision within REQ-101;
  - a binary and an ASCII STL of the same solid import to the same triangle count and bounds, and a
    binary STL whose header begins "solid" is not misread as ASCII;
  - selecting a `.dwg` imports its 3D solids without any pre-conversion step by the user, and states
    which converter was used and what the route dropped;
  - a `.dwg` is never modified by the import — the conversion explodes a copy;
  - when no capable converter is installed, the import says so specifically (a DWG→DXF-only
    converter is not sufficient and must be named as such), and changes nothing.
- Owner-layer: IO (parsers + conversion), Domain (store), UI (prompt), Platform (process, dialog)
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial draft. Route chosen over OBJ/FBX/STL — see ADR-026 and the
  decision log.

### REQ-066 — Raw description on survey points
- Purpose: let a point group match the field code even after the description has been edited
- Priority: must
- Type: functional
- Statement: `SurveyPoint` gains a **`rawDescription`** field holding the description as collected in
  the field. It is written once at import and is **never rewritten** by description expansion or by
  a user edit of `description`; the two are independent. It is persisted additively in `.gs` and in
  the `GOSURVEY` DXF XDATA schema (ADR-005), so neither format gains a version bump. A record with no
  raw description — every point in every drawing written before this requirement — loads with the
  field empty, and any consumer that matches on it falls back to `description`.
- Acceptance:
  - a point imported with a field code keeps that code in `rawDescription` after `description` is
    edited to something else;
  - a legacy `.gs`, and a legacy DXF point carrying the pre-REQ-066 XDATA, both load with
    `rawDescription` empty and are matched on `description` instead — not skipped, not defaulted to
    the description's text;
  - `rawDescription` round-trips `.gs` and DXF unchanged, including when empty.
- Owner-layer: Domain (field), IO (`.gs`, DXF XDATA)
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial. Raised while specifying REQ-067: raw-description matching was
  requested and no raw description was stored anywhere.

### REQ-067 — Point groups
- Purpose: name a set of survey points once and reuse it — chiefly as a surface's data source
- Priority: must
- Type: functional
- Statement: A **point group** is a named, persisted, drawing-owned object whose membership is a
  **rule**, not a frozen list. A rule combines any of: **point-id ranges** (`1-500, 1200,
  1400-1450`), a **description** wildcard, a **raw-description** wildcard (REQ-066), and an
  **explicit id list** picked in the drawing. **Criteria combine as a union (OR)**: a point joins the
  group if it matches *any* filled-in criterion, and an empty criterion contributes nothing rather
  than matching everything. So `ids 1-500` + `desc EG*` resolves to every point numbered 1–500 plus
  every `EG` point, and a hand-picked point is always in its own group regardless of the other
  criteria. Narrowing a group by exclusion is **not** in this release. Membership is evaluated on
  demand from the current point set, so points imported after the group was defined join it without
  the group being edited; the explicit-id part is by definition unaffected by new points. A group is **not an entity**: it
  has no geometry, no layer, no colour, is not drawn, is not selectable in the viewport, and is not
  exported. Groups are owned by the drawing and are undoable, so creating or editing one can be
  undone in a single step. A point that is deleted leaves no trace in any group.
- Acceptance:
  - a group defined as `EG*` resolves to exactly the points whose description matches and to no
    others; the same rule against `rawDescription` resolves independently of an edited description;
  - importing further `EG` points and re-resolving includes them with no edit to the group;
  - an id-range rule `1-10, 20-30` excludes 11–19, and includes both endpoints;
  - an explicit-id group is unchanged by newly imported points;
  - deleting a point removes it from every group's resolved membership and leaves no dangling id
    behind in the stored rule;
  - a rule that matches nothing resolves to an empty group and says so — it is not an error, and it
    is not silently treated as "all points" (REQ-201);
  - a group with **no** criterion filled resolves to **empty**, not to every point — the difference
    between "no filter" and "match everything" is exactly the mistake that would silently put a whole
    drawing into a surface;
  - a rule with two criteria filled resolves to the **union** of their matches, and a hand-picked id
    stays in the group even when it matches neither wildcard nor any id range;
  - groups round-trip `.gs`, and a legacy `.gs` with no group section loads unchanged.
- Owner-layer: Domain (rule + resolution), IO (`.gs`), UI (editor)
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial.
  2026-08-15 — **criteria combine as a union (OR), and exclusion is out of scope.** "Combines any of"
  was ambiguous enough to change what the feature does, and it was resolved by the user before any
  code rather than guessed at (workflow §5). OR is what makes the hand-pick list meaningful: under
  AND, a manually picked point outside the id range would be dropped from its own group. Also
  pinned down: an all-empty rule resolves to **empty**, never to "all points".

### REQ-068 — TIN surface entity
- Purpose: hold a triangulated terrain model — the object every other surface requirement acts on
- Priority: must
- Type: functional
- Statement: A **surface** is a named, drawing-owned object holding a triangulation: interleaved XYZ
  vertices (architecture §11.8), triangle indices, and the per-triangle adjacency that contouring and
  analysis need. The triangulation is **immutable once built and replaced wholesale on rebuild**, and
  is therefore held as `shared_ptr<const>` by both the live state and every undo snapshot
  (architecture §11.5, as amended 2026-08-12) — a surface must not be deep-copied by unrelated edits.
  Surfaces participate in layers, visibility, selection, erase, undo and view extents. They are
  **excluded from DXF and DWG export**, which has no representation GoSurvey can write losslessly,
  and the exclusion is **stated in the export log** (REQ-201), never silent. A surface is persisted in
  `.gs` in an additive section.
- Acceptance:
  - a surface round-trips `.gs` with vertex positions bit-identical on reload;
  - a legacy `.gs` with no surface section loads unchanged;
  - surfaces are included in zoom-extents and in the drawing's bounding box;
  - erasing a surface is undoable in one step, and the restored surface is the same triangulation;
  - a surface on a frozen or off layer is not drawn, and one on a non-plottable layer is not plotted;
  - **an edit unrelated to the surface — drawing a line — does not copy the triangulation**: the
    undo snapshot shares the payload, asserted on the shared pointer rather than by inspection;
  - exporting a drawing containing a surface names the surface as excluded in the log.
- Owner-layer: Domain (store), IO (`.gs`), Renderer (draw)
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial.

### REQ-069 — Surface definition: point groups, breaklines, boundaries, dynamic rebuild
- Purpose: make a surface a live model of its inputs rather than a one-time snapshot
- Priority: must
- Type: functional
- Statement: A surface stores an **ordered, editable definition** whose items are **point groups**
  (REQ-067), **breaklines** (existing 3D lines and polylines designated as such), and **boundaries**
  (closed polylines typed **outer**, **hide** or **show**). Breaklines and boundaries are referenced
  by **stable entity id** (REQ-076), never by array index. Triangulation is **constrained**: no
  triangle edge crosses a breakline. Boundaries apply in definition order — an outer boundary clips
  the surface to itself, a hide boundary removes surface inside it, and a show boundary restores
  surface inside a hide. Standard breaklines only; proximity, wall and non-destructive breaklines are
  out of scope.

  The surface is **dynamic**: when a definition source changes — a consumed point moves or is
  deleted, a breakline or boundary polyline is edited, a group's membership changes — the surface is
  marked out of date and **retriangulates**, with no user action. Rebuild is **coalesced to at most
  one per command / undo boundary**, so an edit touching many sources rebuilds once, not once per
  source. The rebuild runs **off the UI thread** (architecture §8): the edit completes immediately,
  the surface is visibly marked stale until the result arrives, and **a result whose definition is no
  longer current — because of an undo or a further edit — is discarded, not applied**. Deleting a
  referenced entity removes that item from the definition; it never leaves a dangling reference.

  Inputs that have no correct answer are **reported, not absorbed** (REQ-001, REQ-201): breaklines
  that cross in plan at different elevations, duplicate points at the same plan location with
  different elevations, and a definition that yields fewer than three non-collinear points each
  produce a specific message stating what the build did.
- Acceptance:
  - a breakline across a saddle produces triangle edges along it, and **no triangle crosses it**,
    verified against hand-computed expected edges on a committed dataset;
  - an outer boundary clips the surface to itself; a hide boundary leaves a void; a show boundary
    inside a hide restores surface there;
  - moving a survey point the surface consumes changes the surface with no manual rebuild;
  - a single MOVE of N consumed points triggers **one** rebuild, not N;
  - undo issued while a rebuild is in flight leaves the surface consistent with the undone state —
    the in-flight result is discarded;
  - deleting a polyline used as a breakline removes it from the definition, and the surface rebuilds
    without it, with no dangling id;
  - crossing breaklines at different elevations produce a named diagnostic and a stated outcome;
  - a definition of fewer than three non-collinear points fails with a specific message and leaves no
    partial surface;
  - the definition round-trips `.gs`, ids intact.
- Owner-layer: Domain (definition, rebuild), util (triangulation), Commands (designate/edit)
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial.

### REQ-070 — Surface styles
- Purpose: control what a surface looks like without changing what it is
- Priority: must
- Type: functional
- Statement: A **surface style** is a named, reusable, drawing-owned object referenced by surfaces —
  the ADR-020 text-style pattern, a document-owned table rather than a per-surface copy, so editing a
  style changes every surface using it. A style controls: **minor and major contour interval**, each
  with colour and lineweight; **triangle** display; **surface border**; **point** display; and the
  REQ-072 band and arrow settings. **Contours are display geometry, not entities**: they are
  regenerated from the triangulation and the style, are never stored in `.gs`, never appear in
  selection, and never appear in the drawing's entity counts. Changing a style property must not
  re-triangulate the surface.
- Acceptance:
  - changing the contour interval updates the display **without rebuilding the triangulation** and
    adds no entity to the drawing or to the saved `.gs`;
  - two surfaces sharing a style both change when the style is edited;
  - a style with triangles off and contours on draws only contours; with both off and border on,
    only the border;
  - a major interval that is not a whole multiple of the minor interval is rejected with a specific
    message rather than producing mis-labelled contours;
  - styles round-trip `.gs`; a legacy `.gs` loads unchanged; a surface whose style was deleted falls
    back to a default style rather than failing to draw.
- Owner-layer: Domain (table), Renderer (draw), UI (editor), IO (`.gs`)
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial.

### REQ-071 — Contour extraction
- Purpose: get contours out as real geometry when they must be edited, labelled or handed over
- Priority: should
- Type: functional
- Statement: A command bakes a surface's **currently displayed** contours into ordinary polyline
  entities on a chosen layer. The result is normal drawing geometry — editable, snappable, exportable
  — and is **deliberately not linked to the surface**: a later rebuild does not change it, and it is
  not removed when the surface is erased. The command reports how many polylines it created at which
  interval (REQ-201).
- Acceptance:
  - extraction produces polylines at exactly the displayed contour elevations, each vertex within
    REQ-101 of the linear interpolation along the triangle edge it came from;
  - extracting twice produces two independent sets, neither affecting the other;
  - rebuilding the surface afterwards leaves already-extracted polylines untouched;
  - the created count and interval are reported;
  - extracting from a surface whose style has contours disabled creates nothing and says so, rather
    than silently extracting a hidden interval.
- Owner-layer: Commands, Domain
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial.

### REQ-072 — Elevation banding, slope banding, and slope arrows
- Purpose: read grade and drainage off the surface directly — the reason the surface exists
- Priority: must
- Type: functional
- Statement: A surface style carries an editable **range table** — band count, breakpoints, and a
  colour per band — driving per-triangle colouring by **elevation** or by **slope**, with an on-screen
  **legend** whose ranges are the table's. Separately, **slope arrows** draw per triangle in the
  downhill direction of that triangle's plane, coloured by grade. Banding, arrows and the plain style
  display are independent toggles.
- Acceptance:
  - a triangle of known elevation and of known slope each take the colour their band prescribes,
    including at an exact breakpoint, where the band a value falls into is defined and tested rather
    than left to float comparison;
  - the legend's displayed ranges equal the table's, and change with it;
  - on a planar tilted surface every arrow points the same direction, and that direction matches the
    hand-computed downhill vector within REQ-101;
  - a perfectly flat triangle produces no arrow direction and is drawn as flat rather than as an
    arbitrary direction;
  - turning banding off restores the style's plain display unchanged.
- Owner-layer: Domain (band assignment), Renderer (draw), UI (table + legend)
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial.

### REQ-073 — Surface-to-surface volumes
- Purpose: earthwork — the number a grading design is judged by
- Priority: must
- Type: functional
- Statement: Given two surfaces, GoSurvey reports **cut**, **fill** and **net** volume over the area
  the two have **in common**, together with that common area, and offers a cut/fill colour map over
  the same region. The comparison region is stated explicitly in the result, because a volume quoted
  without the area it covers is not a result.
- Acceptance:
  - two planar surfaces offset by a known constant over a known common area report cut, fill and net
    within a stated tolerance of the hand-computed value;
  - two surfaces that do not overlap report zero volume and say so, rather than reporting a number
    derived from no common area;
  - partial overlap reports volumes over the overlap only, and states the common area used;
  - the cut/fill map colours cut and fill distinctly and shows nothing outside the common area;
  - comparing a surface with itself reports zero net within tolerance.
- Owner-layer: Domain (compute), UI (report + map)
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial.

### REQ-074 — Spot elevation and grade readout
- Purpose: the constant, small question while grading — how high is it here, and what is the grade
- Priority: should
- Type: functional
- Statement: Picking a location on a surface reports the **interpolated elevation** at that point.
  Picking two reports **grade**, **slope percentage**, and the horizontal and vertical distance
  between them. A pick outside the surface reports that it is outside; it never extrapolates.
- Acceptance:
  - elevation at a point inside a triangle of known plane equals the planar interpolation within
    REQ-101;
  - a pick outside the surface, or inside a hide-boundary void, reports "outside surface" and no
    elevation;
  - grade between two points on a known plane matches the hand-computed value within REQ-101;
  - two picks at the same location report zero distance rather than dividing by zero.
- Owner-layer: Commands, UI
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial.

### REQ-075 — Surface Manager
- Purpose: one place to see and edit every surface in the drawing
- Priority: should
- Type: functional
- Statement: A panel lists the drawing's surfaces and supports create, rename, delete, **edit the
  definition** (add, remove and reorder point groups, breaklines and boundaries — REQ-069), assign a
  style (REQ-070), and force a rebuild. For each surface it shows point count, triangle count,
  elevation range, and whether the surface is currently out of date or rebuilding.
- Acceptance:
  - every REQ-069 definition operation is reachable from the panel;
  - a rebuild is reflected in the displayed counts and elevation range;
  - a surface that is out of date or rebuilding is shown as such, and the state clears when the
    rebuild lands;
  - deleting a surface from the panel is undoable in one step;
  - renaming to a name already in use is refused with a specific message.
- Owner-layer: UI, Commands
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial.

### REQ-076 — Stable entity identity
- Purpose: let one object reference another and survive an erase
- Priority: must
- Type: functional
- Statement: Every drawing entity carries a **stable identity** — a per-drawing monotonically
  increasing id, assigned at creation, **persisted in `.gs`, and never reused** within a drawing, so
  a reference to a deleted entity resolves to nothing rather than to whatever later took its array
  slot. Cross-object references (a surface's breaklines and boundaries, a survey point's label) are
  stored **by id, never by array index**. Entities loaded from a drawing written before this
  requirement are assigned ids on load, in a deterministic order, so a legacy file is not a special
  case anywhere above IO. Resolving an id to an entity is by an index built on demand — no
  per-entity map is stored, and no reference-fixup pass runs at erase.
- Acceptance:
  - an entity's id is unchanged by erasing a different entity, by undo/redo, by copy/paste, and by a
    `.gs` save/load round trip;
  - a reference to an erased entity resolves to **nothing**, and specifically not to the entity that
    moved into its former index;
  - a pasted copy of an entity receives a **new** id, distinct from its source's;
  - a legacy `.gs` loads with ids assigned deterministically — loading the same file twice yields the
    same ids;
  - ids are not reused after an erase within a session, and are still not reused after a save/load;
  - `SurveyPoint`'s annotation-label reference is migrated to an id, and the index-fixup loop in
    `EraseCadAnnotationAtIndex` is deleted rather than duplicated.
- Owner-layer: Domain (id allocation + resolution), IO (`.gs`)
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial. Raised as a blocking Verification finding against REQ-069: the
  codebase addresses entities by array index and compacts on erase, so a stored reference silently
  re-points to a different entity. See ADR-027 and the decision log.

---

## Distribution requirements

> How a build reaches a user, and how a running install learns that a newer one
> exists. These are the first requirements in the project whose acceptance
> depends on a machine other than the user's.

### REQ-077 — The application knows its version and checks its channel for a newer one
- Purpose: a user runs a current build without having to go looking for one
- Priority: should
- Type: functional
- Statement: The application carries its own version, derived from a **single source** — the CMake
  `project(VERSION)` — so that the binary, the installer, the git tag and the release title cannot
  disagree. The version is displayed in the UI. On startup the application asks its configured
  **release channel** (`stable` or `beta`) whether a newer version exists, by fetching a small JSON
  manifest over HTTPS.

  The check runs **on every launch** and **gates the session**: until it finishes, the application
  shows a modal "Checking for updates" dialog with a progress indicator and accepts no other input.
  The user therefore always knows the state of their install before doing any work, and can never
  begin a drawing on a build that is about to ask to replace itself.

  Two properties keep that from becoming a hang. The fetch still runs **off the UI thread** — a
  blocked UI thread cannot repaint, so the progress indicator could not animate and Windows would
  mark the app "Not Responding"; the thread is what makes the modal honest rather than frozen. And
  the check is **hard-bounded by a short timeout**, after which it gives up and the session starts.

  On any failure — no network, DNS failure, timeout, malformed JSON, HTTP error — the dialog closes
  and the application proceeds exactly as if no update existed, with nothing shown to the user. A
  setting disables the check outright, in which case no dialog appears and no request is made.
- Acceptance:
  - the version shown in the UI, the version embedded in the executable's Windows version resource,
    the installer's `AppVersion`, and the git tag all derive from the one CMake value — changing that
    value changes all of them and no other edit is required;
  - **every** launch with the check enabled performs the check — two launches a minute apart both
    issue a request;
  - the checking dialog blocks all other interaction until it resolves, and the application remains
    responsive and repainting throughout (never "Not Responding");
  - with the network unreachable, the session starts within the stated timeout and shows no error;
  - each of no-network, timeout, HTTP 404/500, and malformed JSON leaves the application running
    normally with no error shown to the user — the failure is logged, not surfaced (this is the
    deliberate, recorded exception to REQ-201; see the decision log);
  - a `stable` install is never offered a prerelease;
  - with the setting disabled, no dialog appears and no network request is made at any time;
  - version ordering is correct across the prerelease boundary — `0.5.0-beta.2` < `0.5.0-beta.10` <
    `0.5.0`, and an equal or older remote version produces no prompt.
- Owner-layer: util (version compare + manifest parse, pure), Platform (HTTPS transport), UI
  (version display + checking dialog + setting), IO (`UserPrefs` channel)
- Status: accepted (2026-08-15)
- Revisions: 2026-08-15 — initial. See ADR-029 and the decision log.
  2026-08-15 — **amended: the check now gates startup instead of hiding behind it.** The 24-hour
  throttle is removed (it made "checks when the app opens" false roughly half the time), and the
  non-blocking, invisible check is replaced by a modal with a progress indicator. This reverses the
  original "a user must not be able to tell the feature is present" in favour of "a user always
  knows what build they are about to work on" — a deliberate user decision, recorded in the
  decision log, not a drift. The off-thread fetch survives the change for a technical reason rather
  than a policy one: it is what allows the modal to animate and stay responsive.

### REQ-078 — An update is applied only after the user chooses it
- Purpose: keep the user in control of when their CAD session ends
- Priority: should
- Type: functional
- Statement: When REQ-077 finds a newer version, the application **presents it and waits**. It
  displays the new version, the current version, and the release notes carried in the manifest,
  offering exactly three outcomes: install now, be reminded at the next launch, or skip this version
  permanently. There is no silent download and no silent install — this application holds unsaved
  drawings, and an unannounced restart destroys work.

  On the user's choice to install, the application downloads the installer, **verifies it against the
  SHA-256 recorded in the manifest**, and refuses to execute it on any mismatch. Before handing over,
  it routes through the existing unsaved-changes path, so a user with a dirty drawing is asked to
  save rather than losing it to the restart. The installer is then run non-interactively and the
  application exits; the installer replaces the files and relaunches the application.

  The hash is an **integrity** check, not an authenticity one: it and the installer come from the same
  host over the same TLS connection, so it detects corruption and a truncated download, not a
  compromised publisher. Authenticity requires Authenticode signing, which is recorded as technical
  debt rather than claimed (see the decision log).
  **Compatibility is stated before the user accepts, not discovered afterwards.** The manifest
  carries the `.gs` format version the offered build writes, and the dialog compares it against
  this build's. Two distinct outcomes are shown differently, because collapsing them would train
  users to ignore both:
  - **forward-only** (the offered build writes a newer format) — routine, stated plainly: existing
    drawings still open, but drawings saved afterwards will not open in the installed version;
  - **breaking** (existing drawings will not open) — prominent, and **declared by the release
    author**, because a semantic break need not move the format version and so cannot be detected
    automatically.

  A warning informs the choice; it never removes it. The three buttons are unchanged.
- Acceptance:
  - no download begins, and no installer runs, without an explicit user click;
  - an offered build writing a newer `.gs` format shows the forward-only notice; one at the same
    format shows nothing; a declared break shows the prominent warning and the author's text;
  - a manifest with no compatibility information (one published before the field existed) warns
    about nothing rather than treating absence as risk;
  - the compatibility notice appears **above** the release notes, not below them;
  - "Skip this version" suppresses that version permanently but a *later* version still prompts;
  - "Remind me later" prompts again on the next launch;
  - a deliberately corrupted download fails the hash check, is deleted, does not execute, and reports
    the failure to the user (REQ-201 applies here — unlike the REQ-077 check, this path was
    user-initiated, so silence would be wrong);
  - a drawing with unsaved changes triggers the existing unsaved-changes modal before the application
    exits, and cancelling there cancels the update;
  - after the installer runs, the previous versioned executables (`GoSurvey-0.*.exe`) are gone from
    the install directory, one `GoSurvey.exe` remains, and desktop/Start-menu shortcuts and the `.gs`
    file association still resolve;
  - a partially downloaded file left by a killed process does not block or corrupt the next attempt.
- Owner-layer: UI (dialog), Platform (download, hash, process launch), IO (`UserPrefs` skip state)
- Status: accepted (2026-08-15)
- Revisions: 2026-08-15 — initial. See ADR-029 and the decision log.

### REQ-079 — `.gs` files carry a format version and are migrated forward on load
- Purpose: an old drawing opens in a new build, always, and the rare case where it cannot is stated
  rather than discovered
- Priority: must
- Type: functional
- Statement: Every `.gs` file records the format version that wrote it. A build opens **any file at
  or below its own version**, migrating older ones forward on load; the user is not asked, and the
  file on disk is unchanged until they save.

  Migration runs on the **parsed JSON**, before the typed loader sees it, as a chain of
  single-version steps (v1→v2→v3). Each step is a pure function of the document tree, so a drawing
  five versions old is carried forward by composing steps that were each written and tested against
  one change.

  A file written by a **newer** build is refused — not guessed at — with a message naming the
  version that wrote it and the version this build understands. That is the only case where a
  drawing legitimately does not open, and it is a downgrade, never a loss.

  **Normalization is distinct from migration, and is permitted.** A load may put a drawing into a
  canonical *storage* form without changing what the drawing means. Today there is exactly one such
  step: a file whose coordinates are of state-plane magnitude and whose `worldDocumentOrigin` is
  `(0,0)` has its origin moved to the extents midpoint and its local coordinates rebased, because
  geometry is stored `local` with `world = local + worldDocumentOrigin` and float precision depends on
  the locals being small. This *is* a change to the bytes and is reported to the user, so a normalized
  load is not byte-identical on resave and is not required to be. It must be **idempotent** — the
  origin is then established, so a second load does nothing — and it must not be lossy beyond the
  precision the file already had: the rebase's rounding is bounded by the float spacing of the
  rebased coordinate, which is never coarser than the spacing of the value it replaced.

  Normalization is deliberately *not* open-ended. Adding a second normalization step is a recorded
  decision, not an implementation detail, because every one of them costs a load that rewrites the
  user's file.

  **A change that cannot be expressed as a migration is a breaking change**, and is treated as one:
  it is declared deliberately, surfaced in the update dialog before the user accepts the update
  (REQ-078), and is expected to be rare. Backward compatibility is the default and the burden of
  proof is on breaking it.
- Acceptance:
  - a file at the current version loads with no migration **and no normalization** and is
    byte-identical on resave;
  - **normalization is idempotent**: where a load does normalize (today the only case is the
    large-coordinate origin rebase — see the Statement), the *second* resave is byte-identical to the
    first. One transformation, never a drift that compounds per open/save cycle;
  - a file at an older version loads, and the resulting drawing is equivalent to the same content
    saved at the current version;
  - migrations compose — a file two or more versions old is carried forward through every
    intermediate step in order;
  - a file written by a newer build is refused with a message naming both versions, and the
    application is left in its prior state, not a half-loaded one;
  - a file with a missing, non-integer, or zero/negative version is refused as malformed;
  - a migration that fails reports which step failed and loads nothing;
  - the current build opens every `.gs` in `samples/` (the regression corpus).
- Owner-layer: IO (`GsIo` reader), util/IO (`GsMigrate`, pure)
- Status: accepted (2026-08-15)
- Revisions: 2026-08-15 — initial. Raised on discovering that `.gs` has carried a `version` field
  since the beginning while the reader compared it with `!=`: bumping it would have made **every
  existing drawing unopenable**, so the field was unusable and eleven changes across REQ-044…076
  were forced through a "tolerant key, no version bump" workaround instead. See ADR-030.

  2026-08-17 — **normalization carved out of the byte-identity condition**, and the idempotence
  condition added in its place. Raised by issue #61: the fuzzer's `gs-roundtrip` oracle failed on
  roughly a third of all seeds because loading a drawing with state-plane-magnitude coordinates
  rebases the document origin, so the first resave differed. The requirement as written called that a
  defect; investigation found it is the local-storage design working correctly — for a 5,000 ft survey
  at easting 2e6 the rebase takes float quantization from ~0.25 ft to ~0.0002 ft. The original
  condition was therefore asking the format to promise something the precision design contradicts.
  Amended rather than the code changed, and the weaker promise replaced with a **stronger, testable
  one** (idempotence) so the amendment is not simply an exemption. Decision D-2026-08-17-a.

### REQ-080 — Anonymous install and active-usage telemetry
- Purpose: inform pricing and understand adoption without user accounts or subscriptions
- Priority: should
- Type: functional
- Statement: The application generates a random 128-bit anonymous install ID on first run and
  persists it in the user preferences file. It sends two fire-and-forget telemetry events:
  - `install` — exactly once, when the install ID is generated
  - `active` — at most once per rolling 24-hour period, reporting current usage

  The events are sent via HTTPS POST to a configurable endpoint (the `TelemetryEndpoint`
  constant) as a minimal JSON payload: `installId`, `event`, `version`, `channel`, `os`.
  **No personally identifiable information is included.** No username, hostname, email, or
  hardware fingerprint is sent; the install ID is the only identifier.

  The telemetry fires in a detached one-shot worker thread at startup, independent of any
  other background tasks. It must never block the UI, gate a session, or fail the application
  if the network is unavailable. Any network error (timeout, DNS failure, unreachable host) is
  dropped silently. This is the same sanctioned silent-failure exception as REQ-077's update
  check, for the same reason: a background reporting call has no actionable user recourse for
  its own failure.

  Distinction: a ping measures first *run*, not raw downloads. GitHub Releases download counts
  (available freely on the asset page) complement this and measure downloads; this requirement
  measures installs that have executed once.
- Acceptance:
  - on first run, an `install` event is sent exactly once; subsequent runs do not resend it;
  - on any run, an `active` event is sent at most once per rolling 24-hour period, even if the
    application is restarted multiple times in the same window;
  - the payload JSON is well-formed and contains exactly the five fields (installId, event,
    version, channel, os);
  - no PII is included in any payload (absence of username, hostname, path, email, hardware ID);
  - network failures (timeout, DNS, unreachable host, TLS error) do not raise an exception, log
    a message, or otherwise fail the application;
  - killing network access does not hang or freeze the startup;
  - a privacy disclosure is present in the UNITS dialog or settings panel explaining what is sent;
  - the current build sends pings to the configured endpoint and an inspector tool confirms the
    payload shape and timing.
- Owner-layer: Platform (PostJson), Telemetry (ping logic + rate limiting), IO (persistence)
- Status: accepted (2026-08-16)
- Revisions: 2026-08-16 — initial. Resolved as a SPEC GAP (no prior requirement existed for
  telemetry). User answered three key questions: (1) tracking only, no license-key enforcement
  for now (licensing is deferred); (2) self-hosted endpoint (not third-party analytics vendor);
  (3) no opt-out toggle, always-on anonymous pings (PII-free by design). See ADR-032.

### REQ-081 — The Dark theme reads as a coherent, separated UI
- Purpose: the shell's panels must be tellable apart at a glance; a uniformly flat
  surface hides where one panel ends and the next begins
- Priority: should
- Type: non-functional (appearance)
- Statement: The **Dark** color theme presents a coherent dark UI in which docked
  panels are distinguishable from each other and from the application ground
  without reading their titles. Concretely:
  - a panel surface is **lighter** than the dockspace ground behind it, and every
    panel/dock node is delimited by a 1 px border **darker** than both — the
    light-surface/dark-gap pairing is what produces the separation;
  - input and property-value fields are **recessed** (darker than the panel
    surface they sit on);
  - a section header inside a panel is a full-width bar distinct from the panel
    surface, with its disclosure triangle at the leading edge;
  - one accent colour marks selection and active state across tabs, headers,
    check marks and slider grabs;
  - Properties coordinate rows carry a fixed-colour **axis badge** — X red,
    Y green, Z blue;
  - chrome painted directly through `ImDrawList` rather than through
    `ImGuiCol_*` (toolbar band, ribbon panels, ribbon buttons, status bar,
    autocomplete popup, property grid) follows the **active** theme instead of
    fixed colours.

  This requirement governs the **shell chrome only**. The drawing viewport is out
  of its scope.
- Acceptance:
  - with Dark active, no chrome element renders in the classic theme's palette
    (the `#464646` / `#3A3A3A` grays or the steel-blue `#3C5575` family);
  - two adjacent docked panels are separated by a visible border line, and the
    panel surface differs from the dockspace ground by a visible value step;
  - switching Options → Display → *Color theme* Dark → Light → Dark leaves each
    theme rendering its own palette, with no colour left over from the other on
    the frame after the switch;
  - the **Light** (nanoCAD classic) theme renders exactly as it does today —
    this work does not change it;
  - viewport contents — crosshair, grips, entity/layer colours, selection
    highlight, snap markers, paper-space sheet — are unchanged;
  - a Properties geometry row whose label ends in X, Y or Z shows the axis badge
    in red, green or blue respectively; a non-axis row (e.g. Radius) shows none.
  - **(added 2026-08-16, revision 2; clause 1 and the accent clause amended by
    revision 3)** the palette is derived, not picked:
    - every neutral is **achromatic** (R = G = B), so no surface carries a colour
      cast and all chroma in the UI belongs to the accent and the semantic
      triad — anything coloured is therefore meaningful;
    - the neutral ladder steps on roughly even **CIE L\*** intervals, and each
      structural relationship (panel over ground, seam under ground, field under
      panel, header over panel, panel over tab strip) is a stated L\* distance
      rather than an eyeballed one;
    - primary text meets **WCAG AA at 7:1** on the panel surface and secondary
      text meets **4.5:1** — `TextDisabled` carries real secondary content here
      (hints, derived readouts, command hints), so it is held to the text bar,
      not to the disabled-text exemption;
    - the accent is one hue used at several lightnesses/alphas, warm against the
      neutral ground so accented marks advance;
    - the semantic triad (axis X/Y/Z, and any future danger/success/info) is
      **equiluminant within ~2 L\***, so no member visually outranks the others,
      and each carries its label at ≥ 4.5:1.
  - **(added 2026-08-16, revision 4)** the shell states **elevation**, not just
    separation: the ribbon and the docked panels read as plates above the drawing
    canvas. A flat palette has no bevels to lean on, so this is carried by two
    paired marks — a lit edge along the top of a raised plate, and a soft shadow
    that plate casts onto the surface below it, landing **on the receiving
    surface** (the drawing canvas), not in the gap between them. Light comes from
    the top-left, matching the direction the classic theme's 3D bevels already
    imply, so the two themes never disagree about where the light is.
  - **(added 2026-08-16, revision 6)** inside a window, a **boxed or scrolling
    region sits on its own tone**, one step below the window it is cut into, so a
    scroll box reads as a well rather than as more window; and a **tab bar has a
    strip behind it**, so unselected tabs sit on that strip and the selected one
    stands on the body it belongs to. A child used purely to group layout is
    exempt and stays on the window tone — nesting two inset tones defeats both.
  - **(added 2026-08-16, revision 5)** a **floating window** — dialog, modal or
    popup — reads as lifted off the shell rather than pasted onto it. It carries
    a soft drop shadow on all sides, a lit top edge, and a title bar that is
    visibly live when the window holds focus. This applies to **every** floating
    window without each one opting in, so a dialog added later is covered the day
    it is written; a theme opts out by setting no window shadow.
- Owner-layer: UI (`src/ui/CadUi.cpp`)
- Status: accepted (2026-08-16)
- Revisions: 2026-08-16 — initial. Resolved as a SPEC GAP: both shipped themes
  (`ApplyCadDarkTheme`, `ApplyCadLightTheme`) were written with no governing
  requirement, and the `ImDrawList` chrome was hard-coded to the classic theme's
  colours regardless of which theme was active. User supplied the Hazel editor as
  the visual reference and chose (1) the **Dark** theme as the one to restyle,
  leaving the classic theme intact, and (2) full parity including the
  property-grid widgets. See ADR-033.
  2026-08-16 (revision 2) — after seeing revision 1 running, the user asked that
  the palette be put on a proper footing rather than left as hand-picked values.
  Measuring the shipped ramp found three defects the eye had registered but not
  named: the border (L\* 6.3) and the tab strip (L\* 6.8) were **0.5 L\* apart**,
  so panel outlines were invisible where they mattered most; the four darkest
  tones spanned 5 L\* while the three lightest spanned 16, which is what read as
  flat in places and abrupt in others; `TextDisabled` sat at **3.93:1**, below
  AA, while carrying real secondary content; and the axis triad spanned **13.6
  L\*** (green at 56.4 vs red at 42.8), so the Y badge visually outranked the
  others and its letter contrast was only 3.0:1. The added acceptance conditions
  above state the rules those defects broke. No ADR — values only; the mechanism
  is unchanged from ADR-033.
  2026-08-16 (revision 3) — the user reviewed revision 2 and asked for **true
  neutral** rather than its slight cool cast, resolving TASK-059's ASSUMPTION-1
  against it. Clause 1 is amended from "one hue at low saturation" to
  "achromatic", and the accent clause drops "near the neutrals' complement"
  (a complement is undefined against a hueless ground). Each neutral was replaced
  by the achromatic gray of **identical luminance**, so the L\* ladder, every
  structural distance and every contrast ratio carry over unchanged — maximum
  drift 0.14 L\*. Recorded because it makes the palette's one remaining chromatic
  claim stronger, not weaker: with no cast on any surface, colour anywhere in the
  shell now means something.
  2026-08-16 (revision 4) — the user reported that the ribbon and the Properties
  panel did not read as *above* the drawing, only as differently coloured. Value
  contrast alone turned out not to carry elevation once the palette was neutral;
  it needs the directional pair (lit top edge + cast shadow). Added as an
  acceptance condition rather than as an implementation note because "which
  surface receives the shadow" is the part that is easy to get wrong and looks
  like nothing when it is — see TASK-060. Delivered alongside three layout
  corrections that are not colour and are logged there.
  2026-08-16 (revision 5) — the user asked that dialogs (settings, import points,
  attach PDF, edit points, the traverse editor, the save-before-close prompt)
  stand out. The cause was structural rather than per-dialog: a floating window's
  fill is the *same* tone as the docked panel it covers, so nothing marked where
  one ended and the other began. Stated as a property of floating windows in
  general — not of the named dialogs — because a per-dialog fix would have to be
  repeated for every dialog written afterwards and would be forgotten. See
  TASK-061.

### REQ-082 — Tabular data windows behave like a spreadsheet
- Purpose: the Viewpoints and Layer Manager windows are the two places a surveyor
  reads and edits many rows at once; a form that happens to be laid out in
  columns is not usable at that scale
- Priority: should
- Type: functional
- Statement: A window whose content is a table of records — today the **survey
  points grid** (VIEWPOINTS) and the **Layer Manager** — behaves as a data grid,
  not as a stack of form controls:
  - **column sort**, ascending/descending by clicking a header, on every column
    whose value has an order (multi-column sort where the table supports it).
    Sorting reorders the **view only**; the underlying record order is unchanged;
  - **resizable, reorderable and hideable** columns;
  - the **header row stays visible** while the rows scroll;
  - a cell's editor **fills its cell** and carries no frame of its own at rest —
    the grid's own rules and row banding supply the structure — while remaining
    fully editable, with the frame appearing on hover and while editing;
  - **row height is uniform** and set by one line of text;
  - a toggle cell (checkbox, radio) is **centred and visible in both states**.
- Acceptance:
  - clicking a sortable header reorders the displayed rows and marks that column;
    clicking again reverses it;
  - rows with equal keys keep a stable, non-flickering order between frames;
  - after sorting, editing a row edits the record shown in that row, and deleting
    a row deletes the record shown in that row — i.e. the view order never
    rewires which record a control acts on;
  - scrolling the rows leaves the header in place;
  - an unchecked checkbox is visible;
  - the record order saved to file is unaffected by any display sort.
- Owner-layer: UI (`src/ui/CadUi.cpp`)
- Status: accepted (2026-08-16)
- Revisions: 2026-08-16 — initial. Raised by the user asking that these two
  windows "behave more like a spreadsheet, like Google Sheets". Recorded as its
  own requirement rather than as another REQ-081 revision because sorting and
  column state are **behaviour a user relies on**, not appearance — and because
  the third acceptance condition (view order must not rewire which record a
  control acts on) is the one that makes this safe to build and belongs in the
  spec rather than in a comment. See TASK-062.

---

## Performance requirements

> Performance is a requirement, not an afterthought — but always paired with a
> *measurement method*. A performance requirement with no defined benchmark is
> not verifiable.

### REQ-100 — Frame budget
- Purpose: interactive responsiveness (desktop/OpenGL)
- Priority: should
- Type: performance
- Statement: The viewport holds a **16 ms frame (60 FPS) at the 95th-percentile
  frame while continuously orbiting a 250,000-line-segment scene** on the
  reference machine. 250k segments is the density of a real topo with contours;
  continuous orbit is the worst case, because orbiting defeats any plan-view
  culling.

  The budget has **three cost profiles**, not one, and the bench carries a case for each:
  (a) **line segments** — 250,000, the original case; (b) **shaded meshes** — the REQ-063 density
  chosen for the bench (ADR-026); and (c) **a surface** — **100,000 points / ~200,000 triangles,
  contoured and orbited**, which is a large but ordinary topo survey. A surface is its own profile
  because contours are regenerated display geometry (REQ-070) rather than stored vertices, so its
  per-frame cost does not follow from either of the other two. **Triangulation time is not part of
  this budget** — a rebuild runs off the UI thread (REQ-069) and is measured separately.
- Acceptance: a committed benchmark scene profiled on the reference machine stays
  within budget at the 95th-percentile frame during a scripted orbit, **in each of the three
  profiles above**, **built with the toolchain named in `project.md` §7**. A frame budget is a
  property of a binary, not of source code: the compiler chooses the vectorisation, inlining and
  layout that decide it, so a figure measured with a different compiler is a different result.
- Owner-layer: Renderer
- Status: accepted — **MET. All three profiles measured 2026-08-15** (TASK-052, TASK-053), and every
  figure recorded before 22:42 that day was measured on the **wrong GPU** (TASK-053 FINDING-3).
  GoSurvey exported neither `NvOptimusEnablement` nor `AmdPowerXpressRequestHighPerformance`, so on
  this hybrid laptop it rendered on the integrated Radeon 610M while `project.md` §7 names an
  RTX 5060. The machine was named and the compiler was named; the *device inside the machine* was
  not. Fixed by TASK-054 (BUG-013) — the application now asks for the discrete GPU.

  On the **RTX 5060** (forced via the per-application GPU preference), MSVC build:

  | profile | scene | p95 | verdict |
  |---|---|---|---|
  | (a) line segments | 250,000 | **1.38 ms** | MET |
  | (b) shaded meshes | 2,000,000 triangles, Shaded | **1.97 ms** | MET — the case exists as of TASK-053 |
  | (c) surface | 100,000 points / 199,966 triangles | **10.28 ms** | MET |

  **The budget is judged on the RTX 5060** (user decision, 2026-08-15), and the integrated-GPU
  figures are kept beside it as a **documented floor** rather than discarded — both are recorded,
  neither is quietly preferred. The floor is what a user sees on a machine whose discrete GPU is
  disabled or absent:

  | profile | RTX 5060 — judged | Radeon 610M — floor |
  |---|---|---|
  | (a) 250,000 segments | 1.38 ms | 9.27 ms |
  | (b) 2,000,000 triangles, Shaded | 1.97 ms | 21.40 ms — over budget |
  | (c) 100k-point surface | 10.28 ms | 9.32 ms |

  Judging on the discrete GPU is only coherent because the application now **asks** for it
  (BUG-013 / TASK-054, fixed 2026-08-15); before that fix the named reference device and the device
  actually used were different things, which is what made every earlier figure misleading. The floor
  row is why the fix matters: on the integrated GPU the mesh profile does not hold the budget.

  Note on (c): the surface profile is **CPU-bound**. It moved 9.32 → 10.28 ms between the two GPUs —
  slightly *worse* on the faster one — while (b), with ten times the triangles, runs at 1.97 ms. Its
  cost is per-frame regeneration of triangle edges, not rasterisation, and it is the only profile
  anywhere near the budget. Relevant before REQ-069/070/071 add contour regeneration on top of it.

  Superseded figures, all **integrated-GPU** measurements: 8.93 ms (clang, TASK-039, 2026-08-12);
  9.27 ms segments / 9.32 ms surface (MSVC, TASK-052, 2026-08-15); and the headroom sweep that put
  the segment ceiling between 500k and 750k. On the RTX 5060, 1,000,000 segments runs at 2.30 ms.

  Previously recorded here (retained for the audit trail):
  - (a) **line segments — MET**, p95 **9.27 ms** at 250,000 segments against the 16 ms budget;
  - (b) **shaded meshes — NOT MEASURED. `BENCH` has no mesh scene**, so this profile has no case to
    run. Predicted by TASK-041 §7 and still open; REQ-064's "budget met in Shaded" condition rests
    on the same gap. Until a mesh case exists, REQ-100 cannot be claimed in full;
  - (c) **surface — MET**, p95 **9.32 ms** at 100,000 points / 199,966 triangles. First measurement
    of this profile; it had no clang predecessor.

  Run it with the `BENCH` command (`BENCH`, `BENCH <segments>`, `BENCH SURFACE`, `BENCH MESH`); the
  scene generators and statistics are `src/util/benchscene.*`, and every run appends to
  `%APPDATA%\GoSurvey\bench-req100.txt`, **naming the profile it measured** (TASK-053).
- Revisions: 2026-08-11 — placeholder `<60 FPS / 16 ms>` / `<N>` replaced with a
  measurable budget, because REQ-058 makes framerate user-visible for the first
  time and R5 could not otherwise have a testable acceptance condition.
  2026-08-12 — reference machine named (it was undefined, which made the budget unreproducible);
  first measurement recorded.
  2026-08-12 — split into three cost profiles (segments / shaded meshes / contoured surface). ADR-026
  had already noted the budget "gains a second dimension" for meshes without writing it down; the
  surface case (REQ-068…072) is a third, and a single-number budget cannot be claimed by a feature
  whose cost profile it never measured.
  2026-08-15 — acceptance now names the toolchain, and the recorded measurement is marked invalid.
  The project pinned MSVC after discovering it had been building with clang against a spec that
  said MSVC (decision log). This is the same class of gap the 2026-08-12 revision closed for
  hardware: a performance number is meaningless without stating the machine, and equally
  meaningless without stating the compiler. Re-measure with `BENCH` and record the MSVC figure.
  2026-08-15 — re-measured under MSVC (TASK-052). Profiles (a) and (c) pass; (b) is recorded as
  **not measured** rather than assumed, because no mesh bench scene exists. The headroom claim is
  corrected from 3–4× to ~2× the required density: that number described a clang binary the project
  no longer ships, and leaving it in place would have understated the risk on weaker hardware.
  2026-08-15 (later) — profile (b) built and measured (TASK-053), and in the course of that the
  **device** question surfaced: every figure above had been measured on the integrated GPU. The
  acceptance conditions now need to name the GPU as they already name the machine and the compiler.
  A budget is a property of a binary on a device; this requirement has now been wrong about all
  three in turn, which is an argument for stating them, not for trusting the number.
  **Resolved the same day:** the budget is judged on the RTX 5060 with the integrated figures kept
  as a documented floor (decision log), and BUG-013 was fixed so the application actually requests
  the device the budget names — the requirement and the binary now agree about the hardware.

### REQ-101 — Numerical tolerance
- Purpose: domain correctness (CAD/survey)
- Priority: must
- Type: performance/quality
- Statement: A coordinate is **stored** and **computed** within **±0.01 ft** of the value the user
  supplied or the reference dataset states.

  "Stored" is not a redundant word here. Geometry is held `local` in `float`, with
  `world = local + worldDocumentOrigin`, so the error in a stored coordinate depends on the magnitude
  of the value at the moment it is narrowed to `float` — not on the arithmetic that follows. Narrowing
  a typed easting *before* the document origin is subtracted quantizes it at world magnitude: at
  easting 2e6 the `float` spacing is 0.25 ft, so `2000000.10` was stored as `2000000.125`, an error of
  0.025 ft that no later computation can undo. **The document origin is therefore established before a
  coordinate of large magnitude is narrowed**, so the narrowing happens at local magnitude and the
  same input stores within ~1e-4 ft.

  Establishment is bounded at both ends, and both bounds are load-bearing. Below
  `kLargeCoordinateRebaseThreshold` no frame is needed. Above
  `kMaxEstablishableOriginMagnitude` a value is not a coordinate, and building a frame around it would
  make garbage *representable* instead of refused — so it is left to the finiteness guards and
  reported (REQ-201).
- Acceptance:
  - the regression dataset passes at the stated tolerance (assert against tolerance, never exact
    float equality);
  - **a coordinate typed at state-plane magnitude is STORED within tolerance**, not merely computed
    within it — checked on a drawing whose document origin starts at `(0,0)`, which is the case that
    fails if the origin is established too late;
  - establishment is **one-time**: a second large coordinate does not move the frame again, since
    re-centring would round every stored coordinate through `float` on each move (the compounding
    drift REQ-079's idempotence condition forbids);
  - a magnitude too large to be a coordinate does not become storable by acquiring a frame — the
    refusal still happens and is still reported.
- Owner-layer: Commands (`ParseWorldPointD`, the entry-time establishment), util/Commands
  (`CadCoordinateFrame`)
- Status: **accepted (2026-08-17)**
- Revisions: 2026-08-17 — accepted, and the template placeholders replaced with the measured rule.
  Promoted from `proposed` by decision **D-2026-08-17-b**, on evidence rather than on principle: a
  coordinate typed at easting 2e6 was being stored 0.025 ft off, which is 2.5x this requirement's own
  tolerance, before any commit, save or load. The requirement had stated the number since it was
  drafted but was unusable as authority while it stayed `proposed` — so the defect it describes could
  not be fixed without accepting it first. Scoped to **stored** as well as computed coordinates in the
  same change, because storage was where the violation actually was.

---

## Quality requirements

### REQ-200 — Deterministic, reproducible build
- Purpose: maintainability
- Priority: must
- Type: quality
- Statement: A clean build from a fixed commit produces identical artifacts and
  emits them to the build directory, never the source tree.
- Acceptance: two clean builds of the same commit yield matching binaries
  (modulo timestamps).
- Owner-layer: Build/Platform
- Status: accepted
- Revisions: `<date>` — initial.

### REQ-201 — No silent failures
- Purpose: debuggability
- Priority: must
- Type: quality
- Statement: Runtime failures are surfaced (returned status or logged error);
  programmer errors trip an assertion. No failure path is empty.
- Acceptance: code review confirms every error branch logs/returns; assertions
  guard invariants.
- Owner-layer: all
- Status: accepted
- Revisions: `<date>` — initial.

### REQ-202 — Releases are produced by the pipeline, not by hand
- Purpose: make a release an act of pushing, not a procedure to remember
- Priority: should
- Type: quality
- Statement: Building the installer is done by CI from a clean checkout, never from a developer
  workstation. Pushing to the repository builds, runs the test suite, and — depending on where it
  was pushed — packages and publishes:

  | Push target | Result |
  |---|---|
  | any other branch | build + test; installer kept as a workflow artifact; nothing published |
  | `beta` | installer published to a **single rolling prerelease** tagged `channel-beta`, whose assets are replaced each time |
  | `master` | version-gated stable release: tagged `v<version>` and published, **only if** that tag does not already exist |

  The version gate is what makes "push to master" safe to do repeatedly: the release step is a no-op
  when `project(VERSION)` still matches the newest release, so a documentation push to master does
  not republish, retag, or re-notify users. Bumping the version *is* the act of releasing.

  Every published release carries the installer, a `latest.json` manifest (version, download URL,
  SHA-256, size, release notes), and nothing that the machine could not regenerate from the tagged
  commit. A failing test suite blocks publication.

  This is REQ-200 extended one step: REQ-200 says a clean build of a fixed commit is reproducible;
  this says the artifact users actually receive **is** that build, rather than whatever happened to
  be in a developer's `build/` directory.
- Acceptance:
  - a push to a feature branch produces a downloadable installer artifact and creates no release
    and no tag;
  - a push to `beta` leaves exactly one `channel-beta` prerelease in the releases list regardless of
    how many times it is pushed, carrying the newest installer;
  - a push to `master` with an unchanged version publishes nothing and fails nothing;
  - a push to `master` with a bumped version creates tag `v<version>` and a stable release;
  - a failing `ctest` run publishes no release;
  - the installer's `AppVersion`, the release tag, and the manifest's `version` field are equal on
    every published release;
  - the manifest's SHA-256 matches the published installer.
- Owner-layer: Build/Platform
- Status: accepted (2026-08-15)
- Revisions: 2026-08-15 — initial. See ADR-029 and the decision log.

### REQ-203 — The command layer is drivable without a window
- Purpose: debuggability, maintainability — the interactive surface is the largest part of the
  system with no automated coverage, and it is where users actually meet the bugs
- Priority: should
- Type: quality
- Statement: The Commands layer runs to completion with **no window, no GL context, and no ImGui
  context**. A headless driver executes a **transcript** — a line-oriented text file of command-line
  submissions and viewport picks — against a real `AppCommandState`, and reports what the drawing
  became.

  Two consequences follow, and both are the point of the requirement rather than side effects:

  - **The Commands layer names nothing above it.** Architecture §2 says this already; today nothing
    enforces it, and one violation has accumulated (`LoadApplicationFont` in `CadCommands.cpp`
    reaches into ImGui). A headless target that must link makes the linker the enforcer, so the next
    violation is a build break instead of a review finding nobody happened to make.
  - **A transcript is a regression test.** A bug reproduced by hand once becomes a file that runs on
    every build, in the same form whether a human or a generator wrote it.

  The driver reads a transcript, writes a machine-readable result (entity counts, emitted log lines,
  invariant status), and exits non-zero on any failure. Reaching a native file dialog must not open
  one: the platform dialog functions are answered from the transcript.

  This requirement is about **drivability**, not about what is checked — the checks are REQ-204.
- Acceptance:
  - the headless target links with **no GLFW, no GLEW, no `gl*` symbol, and no ImGui backend** on
    its link line, and its binary imports no `opengl32.dll` — proven by the link line and by
    `dumpbin /DEPENDENTS`, not by inspection. *(Amended 2026-08-16: this condition originally said
    "no imgui". ImGui **core** is on the headless link line deliberately — loading a `.gs` measures
    label text through the current font and stores the result as geometry, so headless must measure
    it with the same font the GUI uses or the diff condition below is unmeetable. See the ADR-031
    amendment; the boundary that matters is no window and no GPU.)*
  - a transcript drawing a line, a circle, and a polyline yields exactly what a user performing the
    same steps yields, compared by saving `.gs` and diffing;
  - a transcript step that reaches a file dialog is answered from the transcript and never blocks;
  - a failing run exits non-zero naming the failure, the step index, and the transcript line;
  - the same transcript run twice produces byte-identical output;
  - the transcript corpus runs in CI on every push and a non-zero exit fails the build (REQ-202).
- Owner-layer: Build/Platform (the target), Commands (`ProcessCommandLineSubmit` /
  `SubmitViewportPick` as the driven entry points), Platform (the dialog seam)
- Status: accepted (2026-08-16)
- Revisions: 2026-08-16 — initial. See ADR-031 and the decision log.

### REQ-204 — Randomized command sequences are checked against document invariants
- Purpose: debuggability — find the state corruptions nobody thought to write a test for, and make
  each one arrive as a reproducer rather than as a user's description of a crash
- Priority: may
- Type: quality
- Statement: A generator produces REQ-203 transcripts from the command registry under a **seed**,
  interleaving commands, picks, cancels, undo/redo, and space switches, with coordinates drawn from
  a deliberately hostile distribution (NaN, infinity, 1e12, denormals, exact duplicates, collinear
  and zero-length geometry). After **every** step the driver evaluates a fixed set of invariants.

  The invariant set is the substance of this requirement. A fuzzer without oracles finds only
  crashes, and crashes are the shallow half of the problem:

  | Invariant | What a violation means |
  |---|---|
  | Undo then redo restores an identical document | The classic CAD defect class — an edit not fully captured by the snapshot |
  | `.gs` save → load → save is byte-identical | A field written but not read, or read but not written (REQ-079) |
  | DXF export → import → export is stable | An entity type silently dropped by an exporter with no branch for it |
  | No coordinate is NaN or infinite | Degenerate input propagating into stored geometry |
  | Local storage holds: `world = local + worldDocumentOrigin` | A world-coordinate value stored without subtracting the origin |
  | Flat-store strides hold (§11.8) | A 3D-widening regression, silently misreading every subsequent vertex |
  | Entity ids are unique and `nextEntityId` exceeds all of them | REQ-076 identity broken |
  | Every selection index is in range for its store | A stale index surviving a compacting erase (§11.9) |
  | Every submitted command emits at least one log line | REQ-201, checked rather than reviewed |

  A run is reproducible from its seed alone. A failing run is **automatically minimized** to the
  shortest transcript that still fails, and that minimized transcript — not the seed — is the
  artifact a bug report carries, because it survives changes to the generator.

  Fuzzing the **file parsers** (`DxfIo`, `GsIo`, glTF, STL, CSV) is the same requirement pointed at
  a different input: there the mutated thing is bytes of a seed file rather than a command sequence,
  and the oracle is "no crash, no hang, and a refusal is reported" (REQ-201).
- Acceptance:
  - the same `--seed N` twice produces an identical transcript and an identical result;
  - **each listed invariant has a fixture that deliberately breaks it and proves the check fires** —
    a check that has never failed is not known to be a check;
  - a failing run emits a minimized transcript that reproduces the failure standalone under the
    REQ-203 driver;
  - minimization terminates, is bounded in attempts, and reports its reduction ratio;
  - a clean run over a seed range exits zero and prints nothing but a summary;
  - the generator is TEST-ONLY: the shipped `GoSurvey.exe` neither links nor contains it (REQ-300).
- Owner-layer: Build/Platform (the target), Commands (the invariants' subject), util (the invariant
  checks themselves, pure)
- Status: accepted (2026-08-16)
- Revisions: 2026-08-16 — initial. See ADR-031 and the decision log. Delivery is staged
  (`docs/fuzz-harness.md` §8) and begins with the file parsers rather than the command driver.

---

## Constraint requirements

> Restate the hard limits from `project.md` §7 as verifiable requirements so the
> review can fail a change that crosses one.

### REQ-300 — Dependency discipline
- Priority: must
- Statement: A new third-party dependency enters the build only after the
  three-question policy in `project.md` is answered affirmatively and recorded
  in the decision log.
- Acceptance: each dependency maps to a decision-log entry.
- Status: accepted

### REQ-301 — Minimal abstraction
- Priority: must
- Statement: A new interface/trait/template/generic is introduced only with two
  or more present-day concrete uses.
- Acceptance: review names the two call sites, or the abstraction is removed.
- Status: accepted

---

## Traceability matrix

> Keep this table current. It is the at-a-glance health check: a requirement with
> no test is unverified; a test with no requirement is untethered work.

| Requirement | Layer | Test(s) | Status |
|-------------|-------|---------|--------|
| REQ-001 | IO | `<TEST-001>` | accepted |
| REQ-100 | Renderer | `BenchSceneTests` (exact segment count; byte-identical regeneration; segment count changes density not extent; iso-elevation contours; nearest-rank percentile) + the `BENCH` / `BENCH SURFACE` / `BENCH MESH` commands on the reference machine (`project.md` §7), MSVC, RTX 5060 — segments 1.38 ms, meshes 1.97 ms, surface 10.28 ms vs 16 ms, 2026-08-15 (TASK-052, TASK-053) | accepted (device pending BUG-013) |
| REQ-101 | Commands/compute | `headless.regression-req101-origin-at-entry` (a typed easting at 2e6 is stored within tolerance — measured 2000000.10 → origin 2000000 + local 0.10000000149, ~1.5e-9 ft, was 0.025 ft; establishment is one-time; an over-large magnitude is still refused; first resave byte-identical) + `headless.regression-59-circle-infinite-radius` / `-59b` (which double as the upper bound's guard). Reference-dataset half still `<regression set>` — pending, see below | **accepted** (typed-storage half verified; reference dataset outstanding) |
| REQ-010 | UI | manual (FBK import shows raw rows) | implemented |
| REQ-011 | compute | `TraverseTests` "ComputeStats" | implemented |
| REQ-012 | compute | `TraverseTests` "Complementary distance" | implemented |
| REQ-013 | UI | review (raw rows read-only) | implemented |
| REQ-014 | UI | manual (closure window, side-by-side) | implemented |
| REQ-015 | compute | `TraverseTests` "adjustment drives misclosure to zero" | implemented |
| REQ-016 | compute | `TraverseTests` "perfect loop yields zero residuals" | implemented |
| REQ-017 | compute | `TraverseTests` "insufficient/invalid input is surfaced" | implemented |
| REQ-018 | Domain/UI | `TraverseTests` "ReduceLegFromSets re-derives leg" | implemented |
| REQ-020 | UI/IO | manual (UNITS opens dialog; precision drives readouts; persists) | accepted |
| REQ-021 | Domain/UI | `AngleFormatTests` (DD/DMS/Surveyor's, direction/base, default parity) | accepted |
| REQ-022 | UI/IO | manual (insertion units stored + sampled; survey precision independent) | accepted |
| REQ-023 | IO | runtime DXF round-trip (survey points reconstructed via XDATA; existing points preserved + merged, id conflict → overwrite/offset prompt; foreign POINT → cross-lines) | accepted |
| REQ-024 | UI | manual (LINE shows one live coord box tracking x,y; type locks it; @dx,dy / bearing accepted; Enter/click commits; non-point prompt single field) | accepted |
| REQ-025 | UI/Domain | manual (Model + Paper layout tabs; add/rename/delete; MODEL/PAPER status button toggles) | accepted |
| REQ-026 | UI/Domain | manual (paper size + orientation render the sheet outline at physical size) | accepted |
| REQ-027 | UI/Domain/Renderer | manual (≥2 viewports at different scales; create/move/resize/scale) | accepted |
| REQ-028 | UI/Domain/Renderer | manual (layer frozen in one viewport hidden there only) | accepted |
| REQ-029 | IO/Renderer | manual + measured (single layout → 1-page PDF at true scale within REQ-101) | accepted |
| REQ-030 | IO/Renderer | manual (≥2 layouts → one multi-page PDF, per-page size/scale) | accepted |
| REQ-031 | IO | manual (layouts/viewports/scales/paper/frozen-layers round-trip through .gs) | accepted |
| REQ-032 | UI | manual (Layout ribbon shows in paper space; normal ribbon in model) | accepted |
| REQ-033 | UI/Commands | manual (two-click rectangular viewport with rubber-band preview; Esc cancels) | accepted |
| REQ-034 | UI/Commands/Renderer | ~~manual (polygonal viewport clips model to the polygon) — Inc 3d~~ | withdrawn (2026-07-13) |
| REQ-035 | UI/Commands | manual (viewport click/window select + grips; MOVE/COPY/DELETE act on viewports) | accepted |
| REQ-036 | UI/Commands/Renderer | manual (double-click into viewport edits model through it; leave returns to paper) | accepted |
| REQ-037 | Domain/UI/Commands/Renderer/IO | manual (draw lines+text on a sheet; move/copy/rotate/delete/snap; not in model or other layouts; .gs round-trip) | accepted |
| REQ-038 | Commands/UI/Domain/IO | `ClipboardTests` (paste offset per type; paper snap; .gs round-trip of new paper types) + manual (model↔paper, same-space; title block to sheet; pasted = selection; props preserved; empty no-op; 1:1) | accepted |
| REQ-039 | UI/Commands/Domain/IO | `PaperSpaceTests` (paper text bounds top-left; box-select hit per type) + manual (box-select; double-click text edit model+paper; Properties show/edit; grips; draw/modify parity; no model change; .gs round-trip) | accepted |
| REQ-040 | UI | `CommandLineTests` (fade-alpha vs elapsed time; recent-tail line selection) + manual (floating bar draggable + position/visibility persist; last ~3 lines fade after idle; F2 toggles console; ESC always cancels; select-to-copy; ×/Ctrl+9 hide/restore; autocomplete + dynamic input + [A]/[2P] preserved; dock still available) | accepted |
| REQ-041 | UI/IO | `SurveyCsvValidateTests` (file-state classification; duplicate-ID detection within file + vs session) + manual (distinct not-found/empty/locked messages; Import disabled on file-level; row-only prompts to confirm skip; overall status string) | accepted |
| REQ-042 | Commands/Domain/UI/Renderer/IO | `HatchTests` (point-in-polygon-with-holes pick; box-select hit; move translates loops; .gs round-trip) + manual (click inside selects; outside/in-hole does not; delete/move/copy + undo; hover; box-select) | accepted |
| REQ-043 | Commands/Renderer/UI/Domain/IO | `HatchTests` (boundary trace: closed rect → loop, gap → none, nested → smallest; pattern/angle/scale stored) + manual (prompt internal point; inside→preview, outside→none; click fills region; no-region message; ribbon color/transparency/layer/angle/scale honored; selectable) | accepted |
| REQ-044 | Domain/UI/IO | `TextStyleTests` (resolve/bake from style; override keeps per-text value while non-overridden props re-bake on style edit; legacy empty-style text unchanged; dimensions ignored) + manual (active-style dropdown; new text adopts active style; `.gs` round-trip of table + per-annotation style; old `.gs` unchanged; Phase 2 STYLE dialog create/rename/delete/edit ripple; Phase 3 Properties overrides + oblique; DXF import registers STYLE table + links imported TEXT/MTEXT so editing an imported style's font updates them, heights preserved) | accepted |
| REQ-045 | UI | manual (PAN/P enters pan; hand cursor; left-drag pans 1:1; Esc/Enter/right-click exits + restores cursor/tool; middle-drag unchanged; model/paper/floating) | accepted |
| REQ-046 | UI/Commands/Domain/Renderer/IO | `PaperSpaceTests` (VP color override set/get/clear; per-viewport independence; VPFREEZE adds / VPTHAW removes a layer in the vp's frozen set) + manual (panel gone; Layer Manager VP Freeze/VP Color columns gated on current viewport; freeze/color affect current vp only; VPFREEZE/VPTHAW pick; `.gs` round-trip of frozen + color; PDF plot shows frozen absent + override colored) | accepted |
| REQ-047 | UI/Commands | `OrthoConstrainTests` (constraint off = no-op at any angle; on = snaps to nearer H/V axis; snap-independent math; direct-distance direction resolves +X/-X/+Y/-Y and refuses a crosshair on the anchor; frame sensitivity — a local crosshair keeps -X where a world-space one is forced to +X) + manual (fresh drawing draws free-angle; F8/status toggles ORTHO incl. while the command bar is focused; object snap overrides ORTHO; typed distance draws left/up/down on a state-plane drawing; grip drag constrained to H/V from the armed grip; typed grip distance completes the stretch and one undo restores it) | accepted |
| REQ-048 | UI/Renderer/IO | manual (viewport model + native sheet show true entity/layer colors on screen; VP Color override + selection/hover still win; frozen/off/non-plottable unchanged; PDF plot prints true colors) | accepted |
| REQ-049 | IO/Renderer | manual (native sheet geometry + TEXT/MTEXT appear in the plotted PDF at correct position/size, colored per REQ-048; off/non-plottable excluded; any TTF-text limit recorded as debt) | accepted |
| REQ-050 | Renderer | manual (MTEXT edited through a viewport at a non-drawing scale sizes off the viewport scale = constant plotted height; plain model view unchanged; single-line TEXT unchanged; survey labels unchanged) | accepted |
| REQ-052 | IO/UI/Platform | `DxfEntityEmitTests` (TEXT declares AcDbText twice with group 73 in the second subclass — the shipped regression; group 7 an AcDbText property; entity groups inside AcDbEntity; 440 omitted when opaque + 0x02000000 packing) + `DwgProbeTests` (all ten release tags; "not a DWG" vs "unknown DWG"; short/empty/missing/null files; converter override honoured, classified case-insensitively, bogus path never trusted, cache holds until rescan) + manual/harness (real R2018 DWG imports; export states what it drops; the written DWG reopens in AutoCAD 2026 with its entities, layers and text; no temp dirs leaked) | accepted (Phase 1 + 1b) |
| REQ-057 | Domain/IO/UI | planned — DXF group-30 round-trip within REQ-101; `.gs` Z bit-identical on reload; legacy `.gs` loads all-zero Z; Properties Z edit undoable; survey elevation reads back as Z; parallel Z arrays stay length-locked across insert/erase/undo | accepted |
| REQ-058 | Renderer/UI/Commands | `CameraTests` (plan-view parity, anchor-before-rotation composition, billboard basis) + `Ray3dTests` + `LinetypeTessellationTests` (per-vertex Z) + `CurveIntersectTests` + `BenchSceneTests`; manual/scripted in-app before/after for the render, overlay and glyph stages that no test target can link (TASK-036/037/039) | accepted — signed off 2026-08-12 |
| REQ-059 | UI | planned — manual (+Z / −Y / an off-axis handle animate correctly and settle < 0.5 s; gizmo tracks the camera after orbit; clicks outside the gizmo still pick geometry). Appearance is ImOGuizmo stock — the mockup is not the target (amended 2026-08-11) | accepted |
| REQ-060 | UI/Commands | planned — manual (translate/rotate/scale each apply and undo in one step; gizmo result matches the typed command within REQ-101; no gizmo with an empty selection) | accepted |
| REQ-061 | Domain/Renderer/IO | planned — manual (two viewports, one plan one isometric, correct on screen and in the PDF plot; legacy `.gs` opens all-plan and renders unchanged) | accepted |
| REQ-063 | Domain/IO/Renderer | planned — `.gs` round-trip bit-identical; legacy `.gs` loads; extents include meshes; erase undoable in one step; layer freeze/off/non-plottable honoured; 2M-triangle model loads without index overflow | accepted |
| REQ-064 | Renderer/UI/IO | planned — 2D Wireframe **pixel-identical** to pre-change (the parity gate, as REQ-058 had); occlusion correct in Hidden/Shaded; lighting follows the camera; style change does not alter geometry/selection/snap/plot; REQ-100 met in Shaded | accepted |
| REQ-065 | IO/Domain/UI | planned — exact triangle count; bbox within REQ-101 after unit scale; doubly-nested node transform hand-verified; names + base colours survive; skipped features reported not silent (REQ-201); malformed file leaves drawing unchanged; state-plane precision within REQ-101 | accepted |
| REQ-062 | util/Viewport/UI | `CurveIntersectTests` (seg×seg incl. parallel/collinear/endpoint-touch; seg×circle two-root/tangent/miss; arc sweep and segment-range filtering; circle×circle incl. concentric and tangent; ellipse×curve refined to REQ-101 against hand-computed roots; projection into a view basis) + manual (elevation-separated segments give APPINT but not INT; orbiting until projections separate drops the APPINT) | accepted |
| REQ-056 | Commands/UI/Viewport/IO | manual (fresh profile: TRIM prompts for a trim line and two clicks trim + end; `TRIMSTATE 1` restores cutting-edge picking and survives a restart; bare `TRIMSTATE` shows the value, blank Enter keeps it, `TRIMSTATE 2` refused; T/L switch mid-run; hover pre-highlights a candidate edge, picked edges stay highlighted, an already-picked edge does not double-highlight) | accepted |
| REQ-055 | UI/IO | manual (File > New and File > Open land on the new tab with 2+ tabs open; "+" likewise; closing a tab focuses its replacement; pan/zoom survives save → close → reopen, including on a state-plane drawing that rebases on load; a pre-REQ-055 `.gs` opens framed to its drawing) | accepted |
| REQ-054 | Commands/UI/IO | manual (right-click with a selection opens the shortcut menu on an existing profile and a fresh one; Select similar on a `PARCEL` line picks up only `PARCEL` lines of that colour; a TEXT does not sweep in dimensions; the log states count + layer + colour) | accepted |
| REQ-053 | Commands/IO/Viewport | `DxfEntityEmitTests` (LWPOLYLINE group 90 = true vertex count and precedes 70, both before the first vertex; closed flag 1/0; every vertex emitted in order as a 10/20 pair; AcDbPolyline marker exactly once; vertex-less record emits nothing rather than a 90-of-zero; 440 omitted when opaque and placed inside AcDbEntity) + manual (RECT by two picks is one selectable object; `@dx,dy` gives an exact width x height; degenerate corners refused; corners/midpoints/geometric centre snap; export log counts LWPOLYLINEs; the DXF reopens with the rectangle; DWG save carries it) | accepted |
| REQ-066 | Domain/IO | planned — raw desc survives a description edit; legacy `.gs` + legacy XDATA load empty and fall back to `description`; empty value round-trips | accepted |
| REQ-067 | Domain/IO/UI | planned — `PointGroupTests` (id-range endpoints + gaps; description vs raw-description wildcard independence; explicit-id group unaffected by new points; deleted point leaves no dangling id; empty match reported not silent) + `.gs` round-trip; legacy `.gs` unchanged | accepted |
| REQ-068 | Domain/IO/Renderer | planned — `.gs` round-trip bit-identical; legacy `.gs` loads; extents include surfaces; erase undoable in one step; layer freeze/off/non-plottable honoured; **unrelated edit does not copy the TIN** (asserted on the shared pointer); DXF/DWG export names the exclusion | accepted |
| REQ-069 | Domain/util/Commands | planned — `TinBuildTests` (breakline appears as an edge and nothing crosses it, vs hand-computed edges; outer clip / hide void / show restore; <3 non-collinear points fails with no partial surface; crossing breaklines at different Z diagnosed) + manual (point move rebuilds with no user action; one MOVE of N points = one rebuild; undo during an in-flight rebuild discards it; deleting a breakline entity removes the definition item) | accepted |
| REQ-070 | Domain/Renderer/UI/IO | planned — interval change does not re-triangulate and adds no entity to drawing or `.gs`; shared style edits both surfaces; major-not-a-multiple-of-minor refused; deleted style falls back to default; `.gs` round-trip | accepted |
| REQ-071 | Commands/Domain | planned — extracted vertices within REQ-101 of edge interpolation; two extractions independent; rebuild leaves extracted polylines untouched; count + interval reported; contours-disabled extraction creates nothing and says so | accepted |
| REQ-072 | Domain/Renderer/UI | planned — `SurfaceAnalysisTests` (band assignment incl. exact breakpoints; downhill vector on a tilted plane vs hand-computed; flat triangle yields no direction) + manual (legend matches table; banding off restores plain display) | accepted |
| REQ-073 | Domain/UI | planned — `SurfaceVolumeTests` (two planes offset by a known constant over a known area vs hand-computed; no overlap = zero + stated; partial overlap uses overlap only and reports the common area; self-comparison = zero) | accepted |
| REQ-074 | Commands/UI | planned — elevation vs planar interpolation within REQ-101; pick outside surface / inside a void reports outside and no elevation; grade on a known plane hand-verified; coincident picks report zero distance not a divide-by-zero | accepted |
| REQ-075 | UI/Commands | planned — manual (every REQ-069 definition op reachable; counts + elevation range update on rebuild; stale/rebuilding shown and cleared; delete undoable in one step; duplicate rename refused) | accepted |
| REQ-076 | Domain/IO | planned — `EntityIdTests` (id survives erase of another entity, undo/redo, copy/paste, `.gs` round-trip; reference to an erased entity resolves to nothing, not to its index successor; paste yields a new id; legacy load is deterministic across two loads; no reuse within a session or across save/load) + the `EraseCadAnnotationAtIndex` fixup loop deleted, not duplicated | accepted |
| REQ-077 | util/Platform/UI/IO | `UpdateCheckTests` (17 cases / 101 assertions, green 2026-08-15: ordering incl. `0.5.0-beta.2` < `0.5.0-beta.10` < `0.5.0`; release outranks its own prereleases but not the next version's; malformed versions refused not coerced; manifest parse of good/malformed/missing-field documents; channel → URL) — remaining conditions (no delay offline, 24 h throttle, disabled = no request) written but **not yet exercised**; needs a published manifest. Was: planned — `UpdateCheckTests` (version ordering across the prerelease boundary incl. `0.5.0-beta.2` < `0.5.0-beta.10` < `0.5.0`; equal/older yields no update; manifest parse of a good document, a malformed one, and one missing required fields; channel → URL selection; stable never selects a prerelease) + manual (network unplugged = no dialog, no delay, no error; second launch inside 24 h issues no request; setting off issues no request) | accepted |
| REQ-078 | UI/Platform/IO | `UpdateCheckTests` (skip suppresses that version but not a later one — green 2026-08-15); the download / hash / unsaved-guard / install paths are implemented but **unexercised — no manifest has been published yet**, and no real upgrade has been performed (TASK-050 ASSUMPTION-1). Was: planned — `UpdateCheckTests` (skip-state suppresses that version but not a later one) + manual (nothing downloads without a click; corrupted download fails the hash, is deleted, and is reported; dirty drawing hits the unsaved-changes modal and cancel aborts the update; after install one `GoSurvey.exe` remains, old `GoSurvey-0.*.exe` gone, shortcuts + `.gs` association still resolve; killed mid-download then retried succeeds) | accepted |
| REQ-202 | Build/Platform | planned — observed pipeline behaviour (feature branch → artifact only, no tag; repeated `beta` pushes → exactly one `channel-beta` prerelease; unchanged version on master → no publish, no failure; bumped version → `v<version>` tag + release; failing ctest → no release; tag == AppVersion == manifest version; manifest SHA-256 matches the asset) | accepted |
| REQ-051 | UI/IO | `MtextToolbarTests` (panel-anchor clamp in-bounds/off-screen/oversized; font+colour run-tag composition incl. empty family = no tag; ruler tick spacing + zero-width = no ticks; attach label 1–9 + out-of-range fallback) + manual (panel titled "Text Formatting" with two rows + ruler; drag persists across edits and restart; font/colour apply to the selection only; height/oblique/entity colour whole-object; style dropdown re-bakes per REQ-044; B/I/U/caps/symbol unchanged; justification re-lays out; disabled controls inert with naming tooltips; ruler + expand toggles; paper MTEXT same panel; single-line TEXT still bare box; OK/Esc + `.gs`/DXF round-trip unchanged) | accepted |
| REQ-203 | Build/Platform/Commands | planned — the `gosurvey_headless` link line carries no imgui/glfw/GLEW/`gl*` symbol; a hand-written transcript (line + circle + polyline) saves a `.gs` identical to the same steps performed in the GUI; a queued `DIALOG` answer satisfies a file-dialog call with no block; a deliberately-broken transcript exits non-zero naming invariant + step + line; the same transcript twice is byte-identical; CI runs the corpus per push | accepted |
| REQ-082 | UI | planned — manual (header click sorts + marks the column, second click reverses; equal keys stable; after sorting, edit/delete act on the record shown; header frozen while scrolling; unchecked checkbox visible; saved file order unaffected by display sort) | accepted |
| REQ-081 | UI | planned — manual, side-by-side against the Hazel reference shots (adjacent docked panels separated by a visible border; panel surface lighter than the dockspace ground; recessed fields; Dark shows no `#464646`/steel-blue chrome; Dark→Light→Dark leaves no colour behind; Light pixel-unchanged; viewport contents unchanged; X/Y/Z badges present, Radius has none) | accepted |
| REQ-204 | Build/Platform/Commands/util | planned — `--seed N` twice is identical; **one deliberately-broken fixture per invariant proving each check fires**; a failing run's minimized transcript reproduces standalone under the REQ-203 driver; minimization terminates within its bound and reports its ratio; a clean seed range prints only a summary; `GoSurvey.exe`'s link line contains no generator symbol | accepted |

---

## Anti-requirements

> Optional but valuable: things the project deliberately will **not** require.
> Documenting them stops well-meaning contributors from "fixing" non-problems.

- "We do **not** require pluggable rendering backends — OpenGL only until a
  second backend is a real requirement (avoids speculative abstraction)."
- We do **not** require automated testing of the rendered GUI — no UI-automation driver, no
  screenshot diffing, no golden images. REQ-203 tests the Commands layer beneath the UI instead.
  Pixel-level tests need an interactive desktop session, are flaky by construction, and mostly
  exercise ImGui rather than GoSurvey. *(accepted 2026-08-16 alongside REQ-203; ADR-031 alt. (1).)*
- `<…>`
