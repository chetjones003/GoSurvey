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
  - **(added 2026-08-17, revision 3)** once an import has **run**, the summary reports
    **what that import did** — "Imported N point(s) — M row(s) skipped." — and the
    panel does **not** re-validate the file it just imported. Import is disabled,
    because the file's rows are now in the drawing, and the summary says so rather
    than presenting it as a failure: a completed import never renders as an error
    colour and never shows the "Cannot import" wording. The outcome stands until the
    user changes the path, the column order, or the header setting — any of which
    resumes normal pre-import validation;
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
  2026-08-17 (revision 3) — **what the panel shows *after* an import was never
  specified, and the unspecified behaviour was wrong.** The importer marked the
  preview dirty on completion, so the panel immediately re-validated the same file
  against a session that now contained the points that import had just created —
  and reported a successful import of 5 points as "Cannot import — no valid data
  rows", in red, with a duplicate-ID error for every row. Every statement in it was
  literally true of a *second* import and every one of them was misleading about the
  first. Recorded as a requirement revision rather than a quiet fix because the gap
  was in the spec: REQ-041 defined the pre-import states exhaustively and said
  nothing about the state after, so the code was not violating it. See BUG-014,
  TASK-069, D-2026-08-17-d.

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
  2026-08-29 — **Native codec path decided: LibreDWG (REQ-170, ADR-041, D-2026-08-29-g).** Phase 1
  converter remains until REQ-170 is verified, then leaves the user-facing path. **This epic does
  not close DM-08** (unknown-object preservation / R2018 write). DWG write is R2000/R2004 only.

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

### REQ-302 — Tabbed, responsive application ribbon (GitHub issue #83)
- Purpose: `DrawRibbonBar` (`CadUi.cpp:2363`) lays every section — Edit, Draw, Modify, Annotate,
  Inquiry, Survey, View, plus the contextual Layout/PDF Underlay/Hatch sections — out in a single
  horizontal row inside a child window opened with `ImGuiWindowFlags_HorizontalScrollbar`
  (`CadUi.cpp:2429-2430`). At anything narrower than a wide desktop window this scrolls, hiding
  tools behind a scrollbar the user must know to operate. The ribbon should instead read as a
  native CAD ribbon (tabbed, like AutoCAD/Civil 3D/nanoCAD) and never require scrolling.
- Priority: should
- Type: functional
- Statement: The ribbon gains a top-level tab strip (Home, Insert, Annotate, View, Manage, Output,
  Survey — only the active tab's sections render) and a responsive layout that adapts to available
  window width without ever falling back to a scrollbar. Delivered incrementally — each increment
  independently shippable and independently verifiable, the same pattern REQ-103/REQ-070 used:
  1. **Tab infrastructure** — the tab strip, persisted active tab, and re-homing of every *existing*
     ribbon section under the tab that owns it. No command's behavior, availability, or contextual
     trigger changes — only where it is found. This closes the "no tab structure" and "organization"
     acceptance groups of issue #83 for the sections that exist today; it does **not** yet remove the
     scrollbar at narrow widths (that is increment 2) and does not yet give Insert/Manage/Output real
     content (that is increment 3, since no ribbon commands for those categories exist yet — see
     Acceptance below).
  2. **Responsive layout engine** — wide/medium/narrow breakpoints per tab (reduced spacing/padding,
     compact icon-only buttons, row reflow, an overflow menu for a group that still doesn't fit).
     Removes `ImGuiWindowFlags_HorizontalScrollbar` entirely. Likely warrants its own ADR before
     implementation, since it is a genuinely new, reusable UI abstraction (one responsive-layout
     mechanism used by all 7 tabs) — to be scoped when this increment opens.
  3. **Content audit** — Insert/Manage/Output get real command sets by relocating existing
     menu-bar-only commands (import, plot/export/publish, settings/standards) into their tabs;
     full audit of every ribbon command against its assigned tab/group, no duplicate or unclear
     placement.
- Acceptance — Increment 1, Tab infrastructure (this increment):
  - a tab strip renders at the top of the ribbon band, above the existing panel/section row, with
    exactly 7 tabs in this order: Home, Insert, Annotate, View, Manage, Output, Survey;
  - the tab strip reuses the existing Model/Layout tab toggle styling
    (`PushModeToggleButtonColors`/`PopModeToggleButtonColors`, `CadUi.cpp:6308-6313`, REQ-025/026
    precedent) rather than inventing a new tab-button style — the active tab is visually distinct
    the same way the active space tab already is;
  - clicking a tab sets `cmd.activeRibbonTab` and only that tab's re-homed sections render below it;
    a tab with no sections assigned yet (Insert, Manage, Output — see mapping below) shows an empty
    panel row, not an error or placeholder text;
  - the active tab persists across restart: `activeRibbonTab` is a plain `AppCommandState` field,
    loaded/saved in `UserPrefs.cpp` with the same one-line shape as `trimState`
    (`UserPrefs.cpp:166-167,387`) — app-level, not per-drawing (D-2026-08-24-g precedent). A fresh
    profile defaults to Home;
  - switching tabs never touches `cmd.active` (an in-progress command), the current selection, the
    undo stack, or drawing geometry — starting a command or making a selection, switching tabs, then
    switching back leaves the command still active and the selection still intact;
  - **section-to-tab mapping** (amended 2026-08-25, D-2026-08-25-d, from the user's own GUI-pass
    feedback — every section otherwise keeps its existing content, condition, and command wiring
    unchanged, this increment only changes which tab shows it):
    - Home: Edit (Undo/Redo/Copy/Paste), then Draw+Modify (model space) or Layout (paper space,
      `ribbonPaperSpace` — unchanged trigger)
    - Annotate: **Text** section (Text/Mtext + style dropdown — the section formerly labeled
      "Annotate", renamed) and **Dimensions** section (Aligned/Linear, moved here from Survey's
      Inquiry section) — model space only, matching today (both sections are nested inside the same
      `if (!ribbonPaperSpace)` block Draw/Modify already used and stay that way)
    - View: View (Extents/Window + visual-style combo)
    - Survey: Inquiry (ID Point + Elev/Grade only — Aligned/Linear moved to Annotate's Dimensions
      section above), Survey — model space only, same existing nesting as Annotate
    - Insert, Manage, Output: no section maps here yet (empty tab) — populated in increment 3
  - **the right-hand current-layer strip (`RibbonLayerStrip`) and every contextual section keep
    today's trigger conditions and render on every tab, unchanged** — Layers (always), Layout
    (paper space, gated on `ribbonPaperSpace`, itself folded into the Home-tab condition above since
    it already lived in that same `if`/`else`), PDF Underlay (a `PdfUnderlay` selected), Hatch (hatch
    command active or a `FilledRegion` selected). Scoping Layers/PDF/Hatch to a single tab would
    hide them whenever a different tab happens to be active while the user is mid-edit on that
    object, a real usability regression this increment deliberately avoids; revisiting whether they
    should instead force-select their owning tab is left to increment 3;
  - the ribbon's total height grows by exactly the tab strip's height plus its gap row (the existing
    `139.f` constant in `main.cpp:405` becomes `139.f + kRibbonTabStripH + kRibbonTabStripGapY`) —
    panel content height inside each section is unchanged, so no existing section's button sizing
    regresses;
  - **no ribbon scrollbar at the window sizes exercised in the user's GUI pass** (amended
    2026-08-25, D-2026-08-25-d): `RibbonToolsLeft` is sized to the ACTIVE tab's own precomputed
    content width, not a blanket "window width minus the 500px layer strip" cap, with
    `ImGuiWindowFlags_HorizontalScrollbar` replaced by `NoScrollbar`/`NoScrollWithMouse`. This is a
    **partial, disclosed** answer to "no scrollbars anywhere" — a tab whose content is wider than the
    actual app window still clips rather than scrolling; the full guarantee across all window widths
    (compact buttons, row-wrap/overflow) is increment 2's job, not this one's;
  - build is clean; the full regression suite stays green; a manual GUI pass confirms tab switching,
    persistence across restart, and that every command reachable today (by ribbon) is still
    reachable today, now under its mapped tab.
- Acceptance — Increment 2, Responsive layout engine (opened 2026-08-25, D-2026-08-25-e, ADR-038):
  - a `RibbonBreakpoint` (Wide/Medium/Narrow) is computed per active tab, per frame, from available
    width vs. that tab's own `wideW`/`mediumW` (ADR-038 (a)) — no persisted state, no user-facing
    setting;
  - Wide renders byte-for-byte as increment 1 does today — no regression to any existing section's
    button size, spacing, or label;
  - Medium renders every section from the active tab with compact metrics (smaller button width,
    icon-only small buttons, tighter spacing) — same sections, same commands, no section dropped;
  - Narrow renders sections left-to-right at Medium metrics until the next section would not fit,
    then collapses everything remaining for that tab into one "More ▼" button whose popup renders
    the overflowed sections' full Wide-metric content unchanged (ADR-038 (b)) — every command
    reachable today stays reachable at every width, none hidden or removed;
  - `RibbonToolsLeft` is sized to `min(fittedW, available)` — never wider than the actual available
    width, so no ribbon control is ever clipped, at any tested window width;
  - no horizontal or vertical scrollbar anywhere in the ribbon at any tested window width (this is
    the full guarantee increment 1 explicitly left partial);
  - switching breakpoints (by resizing the window) never touches `cmd.active`, selection, undo
    stack, or drawing geometry — same invariant increment 1's tab switching already holds;
  - build is clean; the full regression suite stays green; a manual GUI pass at multiple window
    widths (including at least 2 narrower than any width increment 1 was tested at) confirms Wide/
    Medium/Narrow read correctly on screen, the "More" popup opens and every command in it works,
    and no scrollbar or clipping appears at any tested width.
- Acceptance — Increment 3, Content audit (opened 2026-08-25, D-2026-08-25-h):
  - **Correction to this requirement's own Statement, found while opening this increment:** the
    "relocating existing menu-bar-only commands (import, plot/export/publish, settings/standards)"
    language above was written before the codebase was checked against it. `DrawMainMenuBar`
    (`CadUi.cpp:1181-1347`) is File/Edit/View only — 168 lines total. There is no Blocks/INSERT
    mechanism (REQ-107, status `proposed`, not built), no Xref, no image attach, no point-cloud
    import, and no Publish/Standards/Purge/Audit/Units command anywhere in the codebase (confirmed
    by grep across `src/`). The only genuine relocation candidates that exist today are: Import
    DXF, Import DWG (File menu), Export DXF, Export DWG (File menu), and Settings (View menu) —
    Plot and Batch Plot already have a ribbon home (Home tab, paper space, from increment 1).
    Increment 3 relocates exactly these; it does not invent Blocks/Xref/point-cloud/Publish/
    Standards content, since none of that exists to relocate — building any of it is out of scope
    (REQ-107 or a future requirement, not this one).
  - Insert tab gets one "Import" section: Import DXF, Import DWG (DWG gated on
    `FindDwgConverter().available()`, identical tooltip/disabled behavior to the existing File-menu
    item) — same underlying `ImportDxfFile`/`ImportDwgFile` calls, no new import logic;
  - View tab gets a new "Settings" section (user's explicit placement decision, 2026-08-25 —
    overrides this requirement's original Manage-tab assumption): one button opens the same
    Settings window `cmd.showSettingsWindow = true` already opens from the View menu;
  - Output tab gets two sections: "Export" (Export DXF, Export DWG, same DWG gating/behavior as the
    File menu) and "Plot" (Plot, Batch Plot) — Plot/Batch Plot **move** from Home tab's paper-space
    Layout section to Output (user's explicit decision, 2026-08-25) rather than appearing in both
    places, per issue #83's "avoid duplicate or unclear placement of commands"; Home tab's Layout
    section keeps only Rect VP / Poly VP (viewport-authoring tools, not output);
  - Manage tab is **not** populated in this increment — the only candidate found (Settings) was
    placed on View instead per the user's decision above, and nothing else exists to relocate
    there today; it stays an empty panel row, same as it's been since increment 1, until a future
    requirement gives it real content;
  - File/Edit/View menu items are unchanged — Import/Export/Settings remain reachable from the menu
    bar too; this is a second entry point, not a move, matching increment 1's own precedent (Edit
    menu's Copy/Paste/Undo/Redo already duplicate the Home tab's Edit section);
  - no command's underlying behavior changes — only where each is reachable from the ribbon;
  - switching to Insert/View/Output tabs never touches `cmd.active`, selection, undo stack, or
    drawing geometry, same invariant every earlier increment holds;
  - build is clean; the full regression suite stays green; a manual GUI pass confirms Import DXF/
    DWG, Settings, Export DXF/DWG, and Plot/Batch Plot all work correctly from their new ribbon
    locations, Home's paper-space Layout section still renders correctly with only Rect VP/Poly VP,
    and no scrollbar/clipping is introduced at any width already covered by increment 2.
- Owner-layer: UI (`src/ui/CadUi.cpp`, `src/ui/CadUiSettings.cpp`) / IO (`src/io/UserPrefs.cpp`,
  preference persistence)
- Status: accepted
- Revisions: 2026-08-25 — initial (GitHub issue #83, D-2026-08-25-c). Sequenced into 3 increments;
  increment 1's acceptance fully specified, increments 2-3 deferred until reached (REQ-103 precedent).
  2026-08-25 (D-2026-08-25-d) — amended from the user's own manual GUI pass: tab-strip padding, the
  View tab's visual-style combo width, the section-to-tab mapping (Dimensions/Text split under
  Annotate), and the scrollbar-removal mechanism (content-sized `RibbonToolsLeft`, partial —
  see Acceptance above).
  2026-08-25 (D-2026-08-25-e) — increment 2 opened: Acceptance written above, ADR-038 recorded
  (measure-then-decide breakpoints + shared overflow popup); increment 3 remains deferred.
  2026-08-25 (D-2026-08-25-g) — increment 2 done: user confirmed the manual GUI pass, no findings.
  2026-08-25 (D-2026-08-25-h) — increment 3 opened: this requirement's own Statement corrected
  (no blocks/xrefs/point clouds/standards exist to relocate — see Acceptance above); Acceptance
  written from what actually exists plus the user's explicit placement decisions (Settings → View,
  not Manage; Plot/Batch Plot → Output, moved off Home, not duplicated).
  2026-08-25 (D-2026-08-25-i) — increment 3 done, user confirmed with no findings; REQ-302 fully
  delivered, all 3 increments complete, issue #83 closed.

### REQ-303 — POLYLINE finishes without typing CLOSE or END (GitHub issue #80)
- **Duplication note (found merging `master` into `beta` for REQ-304/issue #82, D-2026-08-25-l):**
  `origin/beta` independently implemented this exact same GitHub issue (#80) as **REQ-118** (below,
  `spec/requirements.md`), in a separate, parallel session — same feature, same acceptance intent,
  different task number (TASK-109 on beta vs. TASK-108 here) and a real, small implementation
  difference: beta's blank-Enter handler explicitly clears an active bearing lock
  (`CancelSegmentAnglePick`/`ResetSegmentAngleLock`) before committing, which this requirement's own
  implementation did not. The merge kept **this** requirement's structure and specific refusal
  message (matching this requirement's own tested Acceptance text below) and folded in beta's extra
  state-cleanup call as a correctness improvement — see D-2026-08-25-l for the full reconciliation.
  Both REQ-118 and REQ-303 are left `accepted` as an honest historical record of the duplicate work;
  neither describes the merged implementation's exact code in full, cosmetic (message-text/
  comment) detail any more, only in substance.
- Purpose: POLYLINE currently requires the user to type `CLOSE`/`CL` or `END` to finish, which is
  not how the rest of the application's drawing commands read — LINE already finishes segments with
  a bare Enter, and every CAD application the user is used to closes a polygon by clicking back on
  its own start point. The typed keywords stay (nothing here removes them); this adds the two
  interactions a CAD-native user reaches for first.
- Priority: should
- Type: functional
- Statement: while POLYLINE (or 3DPOLY, which shares the same state machine — `polylineDraft3d` only
  changes the vertex label and Z handling) is actively drawing (`polylinePhase == NeedNextPoint`):
  (a) the draft's own start vertex is offered as an ordinary Endpoint object snap — same glyph,
  priority, and OSNAP-Endpoint toggle as any other endpoint, not a new snap kind — and a viewport
  click that lands on it closes the polyline exactly as typed `CLOSE` does, including `CLOSE`'s own
  minimum-vertex refusal when attempted too early; (b) a blank Enter (an empty command-line submit)
  finishes the polyline OPEN exactly as typed `END` does, including `END`'s own minimum-segment
  refusal when attempted too early, and — unlike LINE's own blank-Enter, which restarts a new
  chain — exits the command, matching what `END` already does. Both interactions call the same
  `CommitPolylineDraft` the typed keywords call, so the paper-space parity TASK-107 (REQ-039) gave
  that function applies to both with no separate implementation.
- Acceptance:
  1. with at least two segments drawn (≥3 vertices, `CLOSE`'s existing minimum), clicking the
     viewport at the polyline's own start point closes it — same log line typed `CLOSE` produces
     ("POLYLINE closed."), and the command exits;
  2. clicking the start point with fewer than two segments refuses without closing or adding a
     vertex there, using the same messages `CLOSE` already gives at that vertex count, and the
     command stays active;
  3. with at least one segment drawn (≥2 vertices, `END`'s existing minimum), a blank Enter finishes
     the polyline open — same log line typed `END` produces ("POLYLINE complete."), and the command
     exits;
  4. a blank Enter with zero segments (only the start point placed) refuses without finishing, using
     the same message typed `END` already gives at that vertex count, and the command stays active;
  5. typed `CLOSE`/`CL` and `END` continue to work exactly as before — neither removed nor changed;
  6. a paper layout drawn onto with either interaction commits to that layout's `paperPoly*` stores,
     not the model store (REQ-039 parity, inherited from TASK-107, not reimplemented here);
  7. the start-point snap only appears while POLYLINE/3DPOLY is drawing, obeys the OSNAP-Endpoint
     toggle like every other endpoint, and disappears once the command ends.
- Owner-layer: Commands / Viewport (the snap candidate)
- Status: accepted
- Revisions: 2026-08-25 — initial (GitHub issue #80, D-2026-08-25-j). Reusing the existing Endpoint
  snap kind (no new glyph) and treating an early click on the start point as a refusal rather than a
  silently-added vertex were both confirmed with the user ahead of implementation.

### REQ-304 — Dynamic cursor text matches the command line for every command state (GitHub issue #82)
- Purpose: LINE already shows a state-specific prompt ("Specify first point:") right next to the
  cursor, but several other commands showed nothing there — the cursor gave no indication of what
  the active command was waiting for, even though the command line (bottom bar) had a real, correct
  prompt. The issue asks for a single source of truth: whatever a command is currently expecting
  should drive both the command line and the cursor prompt identically, and the two must never
  disagree or go stale.
- Priority: should
- Type: functional
- Statement: **The architecture already had a single source of truth before this requirement**:
  `CommandInputHint` (`CadUi.cpp:6111`) and its per-command "FooterHint" delegates
  (`CadCommands.cpp`, declared in `CadCommands.hpp:3721-3732`) are queried fresh every frame, and
  the same return value feeds both `DrawCommandLinePanel`'s live hint line
  (`RenderClickableCommandHint(CommandInputHint(cmd), ...)`, `CadUi.cpp:7251`) and the at-cursor
  dynamic-input palette (`CadUi.cpp:12681-12693`, which shows `CadPointPromptLabel` for point
  entry and falls back to the identical `CommandInputHint` text otherwise). Recomputing from live
  state every frame — nothing is cached across states — is also what already guarantees no staleness
  and no command-line/cursor disagreement (Acceptance items 3-6 and 14-16 below were already true
  for every command that had a branch in this function at all).
  A full audit of every `AppCommandState::Kind` against that if-chain (cross-referencing the enum in
  `CadCommands.hpp:1122` against every branch in `CommandInputHint` and its delegates) found **ten
  Kind values with no branch at all**, which fell through to the generic `"Command:"` placeholder on
  both surfaces: `FeatureLine`, `Fillet`, `Chamfer`, `PdfAttach`, `Hatch`, `Pan`, `VpFreeze`,
  `VpThaw`, `Elev`, `Orbit`.
  - `Pan` and `Orbit` are **not** gaps: both are continuous camera-drag modes with a dedicated
    hand cursor and the point-entry palette deliberately suppressed for `Pan`
    (`CadUi.cpp:12878-12884`, REQ-045/REQ-084 (c)) — a changed cursor icon is the correct, existing
    contextual feedback for a drag gesture with no typed value, and adding a redundant text prompt
    on top of it would contradict that existing design. These two are explicitly out of scope.
  - The remaining eight (`FeatureLine`, `Fillet`, `Chamfer`, `PdfAttach`, `Hatch`, `VpFreeze`,
    `VpThaw`, `Elev`) are real gaps: `FILLET`/`CHAMFER` had informative text but only as one-time
    `log.push_back` scrollback lines at each state transition (`CadCommands.cpp`, `StartFilletCommand`
    / `HandleFilletViewportPick` / `HandleFilletText` and the CHAMFER equivalents) — never a
    queryable "what is the state right now" function — so the scrollback looked reasonable while the
    live command-line hint and the cursor both still showed `"Command:"`. `FeatureLine`, `PdfAttach`,
    `Hatch`, `VpFreeze`, `VpThaw`, and `Elev` had no per-state hint mechanism of any kind.
  - Fix: new branches added to the existing `DrawingExtrasFooterHint` (`CadCommands.cpp`) — the
    function whose own doc comment already states new commands should extend it rather than invent a
    separate mechanism (`CadUi.cpp:6376-6377`) — covering every reachable state of all eight commands
    (FILLET/CHAMFER: `WaitFirstEntity`/`WaitSecondEntity` plus each `*TextAwaiting*` sub-prompt for
    radius/trim/distance/angle; FEATURELINE: first point, next point, and the pending-elevation
    prompt; PDFATTACH: `WaitInsertPoint` plus the two never-reached phases `WaitScaleRef`/
    `WaitRotationPt`, kept for completeness since the enum already declares them; HATCH/ELEV/
    VPFREEZE/VPTHAW: their one real state each). No new abstraction, dependency, or query mechanism
    — the fix is closing gaps in the one that already existed.
- Acceptance (from GitHub issue #82's checklist):
  1. every command has been audited for dynamic cursor prompts — done via the `Kind`-enum
     cross-reference above;
  2. every command input state has an appropriate dynamic cursor prompt where applicable — done for
     all `Kind` values except `Pan`/`Orbit`, which use cursor-icon feedback by design (see above);
  3. dynamic cursor text reflects the current command state — true by construction (fresh function
     call every frame, no cached string);
  4. dynamic cursor text updates immediately on every state transition — same reasoning;
  5. dynamic cursor text does not become stale — same reasoning;
  6. commands that already had dynamic cursor text continue to work correctly — no existing branch
     was modified, only new branches added; full regression suite (593/593 Catch2 + headless
     transcripts) green, unchanged from before this task;
  7. commands missing dynamic cursor text are updated — `FeatureLine`, `Fillet`, `Chamfer`,
     `PdfAttach`, `Hatch`, `VpFreeze`, `VpThaw`, `Elev`;
  8-12. point / coordinate / numeric / angle-azimuth-bearing / entity-selection input all provide
     contextual cursor feedback — already true pre-existing for the commands that had it (LINE,
     ROTATE, DIMANGULAR, TRIM, OFFSET, MOVE/COPY, etc.); newly true for FILLET/CHAMFER's numeric
     radius/distance/angle sub-prompts, ELEV's numeric elevation, and the eight commands' entity-pick
     states;
  13. command variants update the dynamic cursor when selected — already true (SCALE/ROTATE
     reference-mode variants); newly true for FILLET's Radius/Trim and CHAMFER's
     Distance/Angle/Trim variants, whose hint text changes the instant the corresponding
     `*TextAwaiting*` flag flips;
  14. command cancellation removes the dynamic cursor prompt — unchanged, generic to `cmd.active`
     resetting to `Kind::None` (not touched by this task) for every command, including the eight
     fixed here;
  15. command completion removes the dynamic cursor prompt — same reasoning;
  16. the command line and dynamic cursor remain semantically consistent — guaranteed by construction
     (one function, two call sites, `CadUi.cpp:7251` and `CadUi.cpp:12681-12693`);
  17. new commands can provide dynamic cursor prompts without a separate UI system — already true
     (the extension point already existed and is exactly what this task used, not something built
     for it);
  build is clean; the full regression suite (593 Catch2 cases + headless transcripts) stays green,
  unchanged pass count from before this task. Verification for this requirement is necessarily
  manual for the visual/wording quality of the eight new hint strings (same as REQ-024's own
  "manual" verification method) — this session cannot simulate mouse hover to screenshot the cursor
  bubble (`project_gui_hover_not_automatable` precedent); the user's own GUI pass is the outstanding
  step.
- Owner-layer: Commands (`CadCommands.cpp` — `DrawingExtrasFooterHint`) / UI (consumes it unchanged,
  `CadUi.cpp`)
- Status: accepted
- Revisions: 2026-08-25 — initial (GitHub issue #82, D-2026-08-25-k). Full `Kind`-enum audit found
  ten gaps; two (`Pan`/`Orbit`) are by-design exclusions (cursor-icon feedback), eight fixed by
  extending the existing `DrawingExtrasFooterHint` delegate. No architectural decision — the
  single-source-of-truth mechanism the issue asked for already existed; this closes coverage gaps
  in it.

---

### REQ-305 — ARRAY command: rectangular and polar (GitHub issue #87)
> Relabeled from `REQ-304` while merging `master` into `beta` — that number was already taken on
> `beta` by "Dynamic cursor text" (issue #82) above, a real ID collision from two independent
> sessions working the same day.
- Purpose: users need to place regular grids and circular patterns of existing drawing objects
  (survey monument symbols, culvert/utility grids, radial layouts) without manually repeating
  COPY. ARRAY generates the pattern interactively with a live preview, as a single undoable step.
- Priority: should
- Type: functional
- Statement: a new `ARRAY` command follows the shape of the existing MOVE/COPY/ROTATE/SCALE/MIRROR
  modify commands (`AppCommandState::Kind`, `ModifyPhase`-style sub-state, `TransformPreview`'s
  ghost-preview batch, `CommandInputHint`'s per-phase cursor/command-line prompt, one
  `PushUndoSnapshot` for the whole command). Flow: (1) select objects — reusing the existing
  modify-command selection shape (a pre-existing selection is used as-is; otherwise clicking
  individual entities and/or dragging a window/crossing box, both accumulating additively into one
  growing selection — Shift-click or a subtracting crossing box removes — confirmed by pressing
  Enter, "ESC cancels" at any point before that); (2) choose array type via the existing clickable-command-variant mechanism
  (`[R]ectangular` / `[P]olar` tokens, keyboard letter or click, both routed through the same command
  text handler); (3a) **Rectangular** — columns, column spacing, rows, row spacing, each enterable by
  typed number or by an interactive cursor-driven distance, with the grid preview updating live as
  each value changes; the original selection occupies grid cell (0,0); (3b) **Polar** — center point
  (via the normal point-input path: click, typed X,Y, object snap), number of items (**total**
  instances including the original — "8" produces 8 total, not 8 additional), angle to fill (360°
  = full circle, a partial angle = an evenly spaced arc over that sweep, following the existing
  CW-from-north angle convention), and a Rotate-items Yes/No toggle (Yes: each copy's orientation
  turns with its position, reusing `DuplicateCadSelectionRotated`'s rotation; No: each copy keeps the
  source orientation and only its reference point moves to the computed position) — defaulting to
  the same default ROTATE's own copy-mode implies (rotate = Yes), with the polar preview updating
  live as center/count/angle/rotate change. (4) Confirm commits every instance in one
  `PushUndoSnapshot`; ESC at any point before confirmation cancels with no geometry created and the
  original selection untouched. Array instances are independent duplicated entities (not a
  persistent associative array object) — reusing the existing `DuplicateCadSelection{Translated,
  Rotated}`-style per-type duplication, extended to loop per instance instead of producing one
  copy — consistent with REQ-103 MIRROR/ROTATE-copy's existing "duplicate, never mutate the
  source" shape. Path arrays and post-creation associative editing are out of scope (issue #87).
  Survey points are **excluded** from the array selection, filtered the same way `Surface` is
  already dropped from MOVE/ROTATE/SCALE (`DropSurfacesFromSelectionForTransform`'s pattern) — an
  array-sized batch of survey points cannot go through the existing single-offset ID-conflict modal
  (COPY/ROTATE-copy), and building N-way ID resolution is new scope this issue does not require
  (D-2026-08-25, confirmed with the user). Entity types the modify commands already exclude
  (Surface, Mesh, PdfUnderlay) are excluded from ARRAY for the same stated reasons.
- Acceptance:
  1. `ARRAY` is launchable by typed command and offers `[R]ectangular`/`[P]olar` as both a typed
     letter and a clickable command-line token, both invoking the same start-array-type function;
  2. object selection accepts a pre-existing selection, individual entity/annotation/fill/survey-
     point clicks, and/or a window/crossing box — any mix accumulates into one selection until
     Enter confirms it, matching MOVE/COPY/SCALE/ROTATE/MIRROR/ALIGN's own selection step
     (D-2026-08-25-n); Surface/Mesh/PdfUnderlay/survey points are dropped from the array selection
     with a log line naming the count and reason, matching the existing MIRROR/MOVE exclusion
     wording style;
  3. Rectangular: columns, column spacing, rows, row spacing are each settable by typed number or
     interactive cursor distance; confirming produces `columns × rows` total instances positioned
     on a regular grid, cell (0,0) at the original selection's position, with correct spacing along
     both axes for both positive and negative spacing (grows the opposite direction);
  4. Polar: center point accepts click, typed X,Y, and object snap; item count N produces exactly N
     total instances (the original plus N-1 new copies) evenly spaced across the fill angle;
     360° places the last instance at 360°/N before wrapping (no duplicate instance at 0°==360°);
     a partial angle (e.g. 180°) spaces N instances evenly across that arc; Rotate-items = Yes turns
     each copy's orientation with its position, Rotate-items = No keeps every copy at the source
     orientation;
  5. the preview (ghost lines/circles via `TransformPreview`, matching MOVE/COPY's existing preview
     coverage — LineSeg/Circle/Arc/Ellipse/Polyline/FeatureLine) updates immediately as any
     parameter changes and commits no geometry until confirmed;
  6. `CommandInputHint` returns an ARRAY-specific prompt for every phase (select objects, array
     type, columns, column spacing, rows, row spacing, center point, item count, angle to fill,
     rotate behavior), matching the existing per-phase-hint pattern;
  7. ESC at any phase before confirmation cancels with a log line (matching `CancelActiveCommand`'s
     existing per-command messages), discards the preview, and leaves the original geometry and
     selection unchanged — no partial array remains;
  8. confirming an array pushes exactly one undo snapshot for the whole operation; Ctrl+Z after a
     completed array removes every generated instance and leaves the source objects exactly as
     before the command; the source objects are never deleted or modified by ARRAY;
  9. every entity type MOVE/COPY can duplicate (LineSeg, Circle, Arc, Ellipse, Polyline, Annotation,
     FilledRegion, FeatureLine) is duplicated correctly by ARRAY, including types not covered by
     the live preview (Annotation, FilledRegion — consistent with MOVE/COPY's own preview gap).
- Owner-layer: Commands (`CadCommands.cpp`/`.hpp`), Viewport (`TransformPreview.cpp`, cursor hint)
- Status: accepted
- Revisions: 2026-08-25 — initial (GitHub issue #87, D-2026-08-25-m). Survey-point exclusion from
  the array selection was confirmed with the user ahead of implementation (see Statement).
  2026-08-25 — Acceptance 2 amended (D-2026-08-25-n): the user reported ARRAY's opening
  "select objects" step only accepted a two-corner window/crossing box, with no way to click an
  individual object and no way to keep selecting after one box — a real gap against how every other
  CAD selection step in this app already behaves, not a spec-compliant report. Rather than fix ARRAY
  alone (which was accurately matching MOVE/COPY/SCALE/ROTATE/MIRROR/ALIGN's own identical
  limitation at the time), the user chose to fix the shared selection shape across all of them in
  one pass. See REQ-103's own revision note for the shared mechanism; STRETCH is deliberately
  excluded (REQ-103 step 5 — its crossing box is load-bearing geometry, not just an object filter).

### REQ-306 — Dynamic cursor input is content-driven, not a fixed footprint (GitHub issue #104)
- Purpose: the at-cursor dynamic input (REQ-024/REQ-304 — `##ViewportCommandInput` and the grip
  drag's `##ViewportGripInput`, both `CadUi.cpp`) is a small window that already reads its prompt
  and field content fresh every frame, but its input field used a **fixed-width clamp**
  (`std::clamp(240.f * scale, 160.f, 360.f)` for point entry, `360.f`/`200.f` for the single
  non-point field, `200.f`/`140.f`/`320.f` for the grip-stretch field) — a footprint sized for the
  longest string the field could ever hold, shown even when the live content is short (e.g. a
  short bearing or a two-digit distance). The issue asks for the box to size to what it is
  currently showing.
- Priority: should
- Type: functional
- Statement: the width of the dynamic-cursor input field — and the window that contains it — is
  computed from the field's **current text** (`ImGui::CalcTextSize`, plus fixed chrome for caret
  and frame padding) every frame, clamped only to a minimum (so an empty/one-character field stays
  clickable) and a viewport-fraction maximum (so a long paste cannot take over the screen), never
  to a constant tuned for the longest possible value. The non-point field additionally sizes to
  fit its placeholder hint ("Type value or Enter") while empty, since the hint must stay readable.
  The window itself keeps `ImGuiWindowFlags_AlwaysAutoResize` (pre-existing, REQ-024) and its
  padding is tightened from 10x8px to 8x6px (rule: remove padding that isn't earning its space).
  Positioning (offset from the cursor, clamped to the work area near screen edges) is unchanged in
  mechanism but now estimates the pre-layout window size from the same content-driven width instead
  of a constant, so the edge clamp matches the box actually drawn.
- Acceptance:
  1. the field's on-screen width tracks its own text: a one-character value (e.g. typing `5`) draws
     a visibly narrower box than a long typed expression (e.g. `1234567.891,1234567.891`), in the
     same frame the text changes;
  2. the window carries no content beyond the prompt label and its one field — no fixed-size empty
     space is reserved beneath or beside them (`ImGuiWindowFlags_AlwaysAutoResize`, unchanged from
     REQ-024, plus the now-content-driven field width);
  3. this applies identically to all three fields: the point-entry coordinate field, the single
     non-point field (bearing/angle/distance/option/command-name), and the grip-stretch field;
  4. the field never shrinks below a minimum that keeps it clickable and never exceeds roughly half
     the work-area width, so a pathological value cannot obscure the drawing;
  5. REQ-024's existing behavior is unchanged: live tracking until typed, type-to-start seeding,
     select-all-on-refresh for the grip field, Enter/viewport-click commit, and per-state prompt
     text from `CommandInputHint`/`CadPointPromptLabel` (REQ-304) all continue to work exactly as
     before — this requirement touches sizing only, not input behavior;
  6. the box stays fully within the application window near every edge, using the same clamp
     mechanism as before (REQ-024), now driven by the actual (smaller, typically) content width.
- Owner-layer: UI (`CadUi.cpp`)
- Status: accepted
- Revisions: 2026-08-26 — initial (GitHub issue #104, D-2026-08-26-c).

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
  (REQ-067), **breaklines** (existing 3D lines and polylines designated as such), **contour
  polylines** (REQ-129), and **boundaries** (closed polylines typed **outer**, **hide**, **show**, or
  **clip** — REQ-128). Breaklines, contour sources and boundaries are referenced
  by **stable entity id** (REQ-076), never by array index. Triangulation is **constrained**: no
  triangle edge crosses a breakline or contour source. Boundaries apply in definition order — an
  outer boundary clips the surface to itself, a hide boundary removes surface inside it, a show
  boundary restores surface inside a hide, and a **clip** boundary excludes input points outside it
  before triangulation (REQ-128). Standard breaklines only; proximity, wall and non-destructive
  breaklines are out of scope. A named surface with too little data to triangulate is still a
  surface (REQ-124): the definition exists, `tin` is null, and the next source edit rebuilds.

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
    partial TIN — the named surface object remains (REQ-124);
  - the definition round-trips `.gs`, ids intact.
- Owner-layer: Domain (definition, rebuild), util (triangulation), Commands (designate/edit)
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial. 2026-08-27 — D-2026-08-27-a: contour sources (REQ-129), clip
  (REQ-128), and empty named surfaces (REQ-124).

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
  colour per band — driving per-triangle colouring by **elevation**, by **slope**, or by **direction /
  aspect** (REQ-130), with an on-screen **legend** whose ranges are the table's. Separately, **slope
  arrows** draw per triangle in the downhill direction of that triangle's plane, coloured by grade.
  Banding, arrows and the plain style display are independent toggles. One table, one mode at a time
  — a triangle has one colour.
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
- Revisions: 2026-08-12 — initial. 2026-08-27 — D-2026-08-27-a: direction/aspect is REQ-130's third
  mode; this requirement's elevation/slope/arrow conditions are unchanged.

### REQ-073 — Surface-to-surface volumes, and a live Volume Dashboard
- Purpose: earthwork — the number a grading design is judged by, kept current as either surface
  changes rather than re-run by hand
- Priority: must
- Type: functional
- Statement: Given two surfaces, GoSurvey reports **cut**, **fill** and **net** volume over the area
  the two have **in common**, together with that common area, and offers a cut/fill colour map over
  the same region. The comparison region is stated explicitly in the result, because a volume quoted
  without the area it covers is not a result. **Cut, fill and net are reported in cubic yards**
  (computed in cubic feet, displayed as ft³ / 27). Common area remains square feet.

  **Bounded volumes** (REQ-131) use the same comparison, limited to a closed clip region.

  A **Volume Dashboard** panel (2026-08-23 amendment) picks two surfaces from the drawing and holds
  the report on screen: cut, fill, net, the common area, and the cut/fill map toggle, all in one
  place rather than a one-shot command result that scrolls away. The dashboard is **live**: when
  either selected surface's triangulation is replaced — REQ-069's dynamic rebuild, or a fresh
  triangulation from any source — the dashboard recomputes with **no user action**, the same
  dynamic-recompute pattern REQ-069 established for a surface's own triangulation, reusing
  architecture §8's one-shot-worker contract (generation staleness + cooperative cancellation) rather
  than a new mechanism. The panel is visibly marked stale until the new result lands, and a result
  computed against a selection that is no longer current — because the panel's surface pick changed,
  or an undo landed, while the compute was in flight — is **discarded, not applied**, mirroring
  REQ-069's own rule for exactly the same failure shape. Recompute is **coalesced to at most one per
  relevant change**: a rebuild that itself coalesced many edits into one (REQ-069) triggers one
  dashboard recompute, not one per edit it absorbed. The panel is UI/session state — which two
  surfaces are picked, and whether the panel is open — and is **not persisted to `.gs`**, the same
  choice REQ-075's Surface Manager makes for its own selection state.
- Acceptance:
  - two planar surfaces offset by a known constant over a known common area report cut, fill and net
    within a stated tolerance of the hand-computed value;
  - two surfaces that do not overlap report zero volume and say so, rather than reporting a number
    derived from no common area;
  - partial overlap reports volumes over the overlap only, and states the common area used;
  - the cut/fill map colours cut and fill distinctly and shows nothing outside the common area;
  - comparing a surface with itself reports zero net within tolerance;
  - rebuilding either dashboard-selected surface (REQ-069) updates the reported volume with no user
    action, and the dashboard shows a stale/computing state until the new result lands;
  - a single rebuild that coalesces N edits into one (REQ-069) triggers exactly one dashboard
    recompute, not N;
  - undo, or changing the panel's surface pick, while a recompute is in flight leaves the dashboard
    showing a result consistent with the CURRENT selection — the in-flight result is discarded;
  - picking a surface that is itself out of date (mid-rebuild) is reflected as such rather than
    computing a volume against a stale triangulation;
  - closing and reopening the panel, or saving and reloading the drawing, does not change which
    surfaces are selectable or force a recompute that current data already answered.
- Owner-layer: Domain (compute + the async worker), UI (dashboard panel + map)
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial. 2026-08-23 — amended (D-2026-08-23-k) to add the Volume Dashboard
  panel and make it live: recompute-on-rebuild, staleness marking, and discard-of-stale-results,
  mirroring REQ-069's own pattern rather than inventing a second one. 2026-08-27 — cut/fill/net
  display is cubic yards (ft³/27); compute unchanged.

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
- Statement: Toolspace Prospector is the place to **edit the definition** (add, remove point groups,
  breaklines, contour sources, boundaries, point-file links — REQ-069). The **Surfaces** window
  (Survey ribbon / left-click a named surface) edits **style and analysis** (REQ-070 / REQ-072), not
  the definition tree. A surface can still be created empty from Toolspace (REQ-124). Force rebuild
  is on the Surfaces collection menu and on each surface. For each surface the old manager still
  shows point count, triangle count, elevation range, and stale/rebuilding state when that panel is
  opened.
- Acceptance:
  - every REQ-069 definition operation is reachable from Toolspace Prospector on the surface's
    Definition (and Masks) nodes;
  - a rebuild is reflected in displayed counts where those readouts exist;
  - a surface that is out of date or rebuilding is shown as such, and the state clears when the
    rebuild lands;
  - deleting a surface from the panel is undoable in one step;
  - renaming to a name already in use is refused with a specific message.
- Owner-layer: UI, Commands
- Status: accepted (2026-08-12)
- Revisions: 2026-08-12 — initial. 2026-08-27 — D-2026-08-27-a: empty create (REQ-124) and contour
  sources (REQ-129) are reachable from the panel. 2026-08-28 — D-2026-08-28-c: definition editing
  moves to Toolspace; the Surfaces window is style/analysis.

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
  - `active` — **(amended 2026-08-23, D-2026-08-23-f) on every launch**, reporting current usage.
    This used to be throttled to at most once per rolling 24-hour period; that throttle is gone
    by explicit user decision. No `lastActivePingDate` is tracked or persisted any more — there
    is nothing left to throttle against. The server enforces no per-day dedup either
    (`ux_pings_active_daily` was dropped from `tools/telemetry-worker/schema.sql`), so this table
    now measures **launches**, not daily-active-identities; every analytics query in `queries.sql`
    already used `COUNT(DISTINCT install_id)` rather than `COUNT(*)`, so "how many active users"
    numbers are unaffected — only a query counting rows would be.

  The events are sent via HTTPS POST to a configurable endpoint (the `TelemetryEndpoint`
  constant) as a JSON payload: `installId`, `event`, `version`, `channel`, `os`, and
  `email`.

  **Amended 2026-08-23 (D-2026-08-23-e): this requirement no longer guarantees no PII.**
  The original "no personally identifiable information" promise is reversed by explicit,
  informed user decision — not discovered as a defect, not a Workshop judgment call. When
  the user is signed in (REQ-091) at the moment a ping fires, `email` carries their signed-in
  email address; when signed out, `email` is empty and the ping is exactly as anonymous as
  before. **A ping's firing, throttling, and the `install`/`active` decision remain entirely
  independent of sign-in state** — REQ-091's identity system is not a dependency of REQ-080's
  telemetry, only an optional enrichment of it when both happen to be true at once. No
  username, hostname, path, or hardware fingerprint is ever sent; `email` is the one addition,
  and only when known.

  The telemetry fires in a detached one-shot worker thread once per launch, independent of
  any other background task's own success/failure — but its own firing point is no longer
  "immediately at process start": it fires once REQ-091's launch gate resolves (signed in, or
  the offline exception), so that whatever sign-in state is true at that moment is known
  before the payload is built. This costs no perceptible delay in practice, since the gate
  already blocks all other interaction until it resolves. It must never block the UI, gate a
  session on its OWN account, or fail the application if the network is unavailable. Any
  network error (timeout, DNS failure, unreachable host) is dropped silently. This is the same
  sanctioned silent-failure exception as REQ-077's update check, for the same reason: a
  background reporting call has no actionable user recourse for its own failure.

  Distinction: a ping measures first *run*, not raw downloads. GitHub Releases download counts
  (available freely on the asset page) complement this and measure downloads; this requirement
  measures installs that have executed once.
- Acceptance:
  - on first run, an `install` event is sent exactly once; subsequent runs do not resend it;
  - **(amended 2026-08-23)** on every run after the first, an `active` event is sent exactly once
    per launch, with no throttle — opening the application 5 times in a day produces 5 rows, not
    1; this REPLACES the original "at most once per rolling 24-hour period" condition;
  - the payload JSON is well-formed and contains exactly six fields (installId, event, version,
    channel, os, email);
  - **(amended 2026-08-23)** `email` equals the signed-in email when REQ-091's sign-in state is
    true at the moment the ping fires, and is empty otherwise — a ping never blocks on, waits
    for, or is skipped because of sign-in state;
  - **(amended 2026-08-23)** no username, hostname, file path, or hardware fingerprint is ever
    included — `email` is the only field this requirement adds beyond the original five;
  - network failures (timeout, DNS, unreachable host, TLS error) do not raise an exception, log
    a message, or otherwise fail the application;
  - killing network access does not hang or freeze the startup;
  - a privacy disclosure is present in the settings panel accurately describing current
    behavior — including that a signed-in user's email is sent — not the pre-amendment promise;
  - the current build sends pings to the configured endpoint and an inspector tool confirms the
    payload shape and timing.
- Owner-layer: Platform (PostJson), Telemetry (ping logic + rate limiting), Auth (signed-in
  email, read not owned), IO (persistence)
- Status: accepted (2026-08-16)
- Revisions: 2026-08-16 — initial. Resolved as a SPEC GAP (no prior requirement existed for
  telemetry). User answered three key questions: (1) tracking only, no license-key enforcement
  for now (licensing is deferred); (2) self-hosted endpoint (not third-party analytics vendor);
  (3) no opt-out toggle, always-on anonymous pings (PII-free by design). See ADR-032.
  2026-08-23 — added `email` (D-2026-08-23-e), reversing the original no-PII acceptance
  condition by explicit user decision, made after REQ-091 shipped and the user asked for it
  directly. Scope decided in the same conversation: email only when signed in at ping time;
  the ping's firing/throttling stays fully independent of sign-in state.
  2026-08-23 — removed the 24h throttle (D-2026-08-23-f), reversing the original "at most once
  per rolling 24-hour period" acceptance condition by explicit user decision. An `active` event
  now fires every launch; `lastActivePingDate` is no longer tracked client-side and the server's
  per-day unique index was dropped. This is the third REQ-080 acceptance-condition reversal in
  one day (see D-2026-08-23-e above) — each recorded separately because each was its own
  explicit ask, not one bundled decision.

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

### REQ-083 — `.txt` and `.csv` are interchangeable point-file extensions
- Purpose: point files leave data collectors and office packages with either
  extension for identical comma-delimited content; the surveyor should not have
  to defeat a file filter to import their own data (serves the import goal;
  REQ-201 — the tool must not make a file look absent when it is present)
- Priority: should
- Type: functional
- Statement: Import points and Export points treat **`.csv` and `.txt` as two
  spellings of one format** — the comma-delimited point file already defined by
  the layout combo (`P,N,E,Z,D` / `P,E,N,Z,D` / `N,E,Z` / `E,N,Z`). Concretely:
  - the Import points chooser offers `.csv` **and** `.txt` under one default
    filter, with `.csv`-only, `.txt`-only and *All* still selectable;
  - the Export points chooser offers the same list, and a typed name **already
    ending in `.csv` or `.txt` is written as typed** — only a name carrying
    neither gets a default extension appended;
  - **the extension carries no meaning.** Parsing and writing are decided by the
    layout and the header-row setting alone; byte-identical content imports to
    identical points and exports to identical bytes under either extension.
  - **No delimiter is inferred.** A `.txt` delimited by tabs or spaces is *not*
    silently accepted: it fails per row with the existing column diagnostic and
    writes nothing into the model (REQ-001). Sniffing a delimiter would let one
    ambiguous line decide how a whole file is read, which is the failure mode
    REQ-001 exists to prevent; a delimiter *choice* is a separate requirement if
    it is ever wanted.
  - REQ-041's validation — file not found / empty / locked, duplicate IDs within
    the file and against the session, per-row parse errors, the overall status
    line, Import disabled on a file-level problem, and the confirm-skip prompt —
    applies to a `.txt` exactly as it does to a `.csv`, with no second code path.
- Acceptance:
  - the Import points chooser lists `.txt` files with its **default** filter
    selected, and picking one populates the path;
  - the same comma-delimited bytes saved as `points.csv` and as `points.txt`
    import to identical points (ID, northing, easting, elevation, description)
    and produce an identical validation summary;
  - a missing, empty, or locked `.txt` shows its REQ-041 message and Import is
    disabled — the same message the `.csv` of that state shows;
  - a space- or tab-delimited `.txt` reports the per-row column error and adds no
    point to the drawing;
  - in Export points, a typed name with no extension is saved with the chosen
    filter's extension, and a name typed as `points.txt` is saved as
    `points.txt` — not `points.txt.csv`; both files' bytes are identical for the
    same drawing and layout.
- Owner-layer: Platform (the two file choosers), UI (wording), IO (unchanged —
  named here because it is the layer this requirement forbids from branching)
- Status: accepted (2026-08-17)
- Revisions: 2026-08-17 — initial. Raised by the user asking for `.txt` import
  "alongside" `.csv`. Two questions were answered before drafting: parsing stays
  **comma-only** (delimiter auto-detection was offered and declined), and the
  change covers **export as well as import**.

### REQ-084 — Right-click is customizable, and the shortcut menu is the drawing's action menu
- Purpose: right-click is the most-pressed button in a drafting session, and today
  GoSurvey spends it badly. The three context modes exist (REQ-054) but are buried in
  a collapsing header as three unlabelled combo boxes, so nobody finds them; and the
  menu they select is a bare list of five modify commands, so choosing "Shortcut Menu"
  trades a working ENTER for very little. Both halves are fixed together because
  neither is worth much alone: a discoverable dialog that still opens a thin menu has
  nothing to offer, and a rich menu nobody can reach is the state we are already in.
- Priority: should
- Type: functional
- Statement:

  **(a) The Right-Click Customization dialog.** Options → User Preferences carries a
  **Right-click Customization…** button that opens a dialog of that name, laid out as
  AutoCAD's is and owning every right-click preference:
  - a **Turn on time-sensitive right-click** checkbox, with the rule it implies stated
    under it (quick click = ENTER, longer click = shortcut menu) and a **Longer click
    duration** field in **milliseconds**;
  - three labelled groups of **radio buttons** — not combo boxes — each carrying the
    sentence that says when it applies: **Default Mode** ("If no objects are selected,
    right-click means") = Repeat Last Command | Shortcut Menu; **Edit Mode** ("If one
    or more objects are selected") = Repeat Last Command | Shortcut Menu; **Command
    Mode** ("If a command is in progress") = ENTER | Shortcut Menu: always enabled |
    Shortcut Menu: enabled when command options are present;
  - **Apply & Close**, **Cancel** and **Help** buttons. Apply & Close writes the
    preferences to the user profile; **Cancel restores every value the dialog opened
    with**, including the checkbox and the duration.

  **Time-sensitive right-click is off by default**, so an existing profile's
  right-click behaviour does not change on upgrade. While it is **on**, Default Mode
  and Command Mode are **disabled** — the timer, not the preference, decides those two
  contexts — and this is shown by greying them, never by silently ignoring them
  (REQ-201). Edit Mode stays live, because a selection still chooses between repeating
  and the menu.

  **(b) What time-sensitive right-click does.** With it on, a right-click in the
  drawing is classified by **how long the button is held**: released within the
  configured duration it is an **ENTER** (the Command Mode ENTER path, and a repeat of
  the last command when idle); held past the duration it opens the **shortcut menu**,
  at the point of press. The menu therefore opens on release-or-elapse rather than on
  press — that is inherent to the feature, not a defect. With it **off**, right-click
  is classified on press exactly as it is today.

  **(c) The shortcut menu.** With no command running, right-click's shortcut menu is
  the drawing's action menu, in this order:
  - **Repeat LAST** — named for the last command, absent when there is none;
  - **Recent Input** (submenu) — the commands most recently entered, newest first;
    choosing one runs it. This is the same history the command bar's dropdown shows
    (REQ-040), so the two can never disagree;
  - **Isolate Objects** (submenu) — Isolate Objects | Hide Objects | End Object
    Isolation (see (d));
  - **Clipboard** (submenu) — Cut | Copy | Paste, with Cut/Copy disabled on an empty
    selection;
  - **Basic Modify Tools** (submenu) — Move | Copy Selection | Rotate | Scale | Erase |
    Offset | Trim | Join, disabled as a group when nothing is selected;
  - **Pan**, **Zoom**, **Free Orbit** — the view commands;
  - **Quick Select…** and **Options…**.

  **Find… is deliberately absent.** GoSurvey's find/replace searches only the MTEXT buffer being
  edited, and the drawing shortcut menu cannot open while that editor is up; there is no
  drawing-wide FIND. The item would therefore be a control that does nothing, which REQ-201
  forbids. It belongs to a drawing-wide FIND requirement, not to this menu.

  With a **selection**, the modify commands and the REQ-054 selection items (Select
  similar, Selection…, Clear selection) stay reachable as they are today. With a
  **command running**, the menu remains the short Command-Mode menu (Enter / Cancel) —
  a half-finished LINE is not the moment to offer Options.

  **(d) Object isolation.** **Isolate Objects** hides everything except the selection;
  **Hide Objects** hides the selection; **End Object Isolation** restores everything. A
  hidden object is hidden **and unpickable** — it must not be draggable, box-
  selectable, or hoverable while invisible, because an invisible object that still
  answers a click is worse than one that is simply drawn. Isolation is keyed on the
  **stable entity id** (REQ-076), never an array index, so an edit that compacts the
  arrays cannot silently isolate a different object. Isolation is **session state**: it
  is not written to `.gs`, and opening a drawing always shows all of it. It covers the
  entity types that carry `EntityAttributes` — lines, circles, arcs, ellipses,
  polylines, annotations, filled regions and meshes. Survey points and PDF underlays
  are out of scope for this requirement, and the command says so when it skips them.

  **Display Order is deliberately not in this menu.** It needs a persisted per-entity
  ordering key threaded through render, `.gs` and DXF, which is a separate requirement,
  not a menu item.
- Acceptance:
  - Options → User Preferences shows **Right-click Customization…**, and it opens a
    dialog with the checkbox, the millisecond field and the three radio groups;
  - ticking the checkbox greys Default Mode and Command Mode and leaves Edit Mode
    usable; unticking restores all three;
  - changing values and pressing **Cancel** leaves every preference at what it was when
    the dialog opened; pressing **Apply & Close** and restarting the app reproduces the
    chosen values — including the checkbox and the duration;
  - with time-sensitive **on** at 250 ms: a quick right-click during LINE ends the
    command as ENTER does, and a held right-click opens the shortcut menu instead;
  - with time-sensitive **off**, right-click behaves exactly as it did before this
    requirement, for all three modes;
  - the idle shortcut menu shows Repeat / Recent Input / Isolate Objects / Clipboard /
    Basic Modify Tools / Pan / Zoom / Free Orbit / Quick Select / Options; **Recent
    Input** lists the commands just typed, newest first, and picking one runs it;
  - **every** item in the menu does something when chosen — no entry is present that
    cannot act (REQ-201);
  - select two lines, **Isolate Objects** — the rest of the drawing disappears, a
    box-select drag across where it was selects nothing, and hovering there highlights
    nothing; **End Object Isolation** brings it all back;
  - **Hide Objects** on a selection hides exactly that selection;
  - saving a drawing with objects isolated and reopening it shows every object.
- Owner-layer: UI (the dialog, the shortcut menu, the click-timing classification) /
  Commands (isolation state, the isolate/hide/end commands, the pick gates, ORBIT) /
  Render (the draw gates) / IO (`UserPrefs` for the two new preferences)
- Status: accepted (2026-08-18)
- Revisions: 2026-08-18 — initial. Raised by the user with reference screenshots of
  AutoCAD's Right-Click Customization dialog and its drawing shortcut menu. Two
  questions were answered before drafting: **Display Order** was offered and
  deliberately deferred (it is a data-format change, not a menu item), and
  time-sensitive right-click ships **off** by default so no existing profile changes
  behaviour on upgrade. Supersedes REQ-054's Settings surface for these preferences —
  REQ-054's Edit Mode default and its selection menu items are unchanged.
  2026-08-18 — revised during implementation: **Find… dropped from the menu.** The
  reference screenshot carries it, but GoSurvey has no drawing-wide FIND and the
  existing find/replace is reachable only from inside the MTEXT editor, so the item
  would have been inert. Stated above rather than shipped as a dead control (REQ-201),
  and an acceptance condition added that no menu entry may be present that cannot act.

---

### REQ-085 — 3D polyline
- Purpose:     draw a linework string whose vertices each carry their own elevation, which is what a
               breakline or a feature line is made of
- Priority:    should
- Type:        functional
- Statement:   A `3DPOLY` command draws a polyline in which **every vertex has its own elevation**,
               entered per vertex rather than taken from the ELEV work plane. An object snap
               supplies the snapped point's Z (REQ-058, already the rule); with no snap the vertex
               elevation may be typed. The result is an ordinary polyline in the existing store —
               `userPolylineVerts` is already stride-3 XYZ — so it selects, moves, snaps, persists,
               and may be designated a breakline (REQ-069) exactly as any polyline does.
- Acceptance:
  - a `3DPOLY` drawn with three vertices at three different typed elevations stores three different
    Z values, and a `.gs` round trip preserves each;
  - snapping a vertex to a survey point gives that vertex the point's elevation, with ELEV set to
    an unrelated value;
  - the result is accepted by `DESIGNATEBREAKLINE` and the surface honours its per-vertex elevations;
  - the ordinary `POLYLINE` command is unchanged.
- Owner-layer: Commands, UI
- Status:      accepted (2026-08-19)
- Revisions:   2026-08-19 — initial. Raised when the surface workflow was revisited: breaklines were
               being drawn with `POLYLINE`, which commits every vertex at the ELEV plane unless the
               user happens to snap, so a breakline drawn free-hand silently tore the surface down
               to elevation 0 along its length.
               2026-08-19 — accepted and implemented (TASK-075). One acceptance condition is NOT
               covered by an automated test and is recorded as such rather than claimed: snapping a
               vertex to a survey point cannot be driven headlessly, because `viewportSnapPickValid`
               is set by the UI hover path the REQ-203 driver has no equivalent of. The code path is
               shared with POLYLINE (`CadCommitElevation`), which REQ-058 already covers.

### REQ-086 — A point file as a surface data source
- Purpose:     build a surface directly from a delivered point file, without importing thousands of
               points into the drawing first
- Priority:    should
- Type:        functional
- Statement:   A surface's definition may reference **point files** by path alongside its point
               groups (REQ-069). A linked file is re-read on rebuild, so editing the file changes
               the surface, and its points feed the triangulation **without becoming drawing survey
               points**. A linked file may be **imported into the drawing** instead, which reads it
               once through the REQ-083 import path, creates survey points and a point group, and
               breaks the link. A path that no longer resolves is reported and the surface keeps its
               last good triangulation (REQ-001), rather than silently shrinking.
- Acceptance:
  - a surface built from a linked file has the file's points in its triangulation and the drawing's
    survey point count is unchanged;
  - editing the file and rebuilding changes the triangle count;
  - breaking the link creates survey points and a point group, and the surface still builds
    identically afterwards;
  - a missing file is named in the log, the surface is marked not-current, and the previous
    triangulation is retained;
  - a `.gs` round trip preserves the link, and a legacy `.gs` with no such array loads unchanged.
- Owner-layer: Domain, IO, UI
- Status:      accepted (2026-08-19)
- Revisions:   2026-08-19 — initial. The file is read during the UI-thread resolve step
               (`ResolveSurfaceInputs`), never on the rebuild worker, so REQ-069's worker stays pure
               and touches no `AppCommandState` and no filesystem (architecture §8 rule 1).
               2026-08-19 — accepted and implemented (TASK-076). The link stores the column LAYOUT
               alongside the path, which the statement above did not say: a point file does not
               describe its own column order, and a link that re-guessed would swap northing for
               easting on reload. Also added during implementation: breaking the link is REFUSED
               when the import brought in no points (REQ-083 skips rows whose point id already
               exists), because dropping the link would then silently delete the file.s whole
               contribution — the link is the only thing still supplying those points.

### REQ-087 — Feature line entity
- Purpose:     a named 3D linework object that grading is designed with and that a surface can
               consume as a breakline — the object a designer edits, as opposed to survey linework
- Priority:    should
- Type:        functional
- Statement:   A **feature line** is a first-class drawing entity: an ordered chain of points each
               with an elevation, carrying a name and a description. It may be created by drawing it
               (`FEATURELINE`), or converted from existing lines, polylines, arcs or 3D polylines
               (`FEATURELINESFROMOBJECTS`), which may optionally erase the source. Its geometry is
               editable — insert and delete a PI — and it may be **added to a surface as a
               breakline**, after which the surface tracks it dynamically like any other breakline
               (REQ-069). It selects, moves, snaps, hides by layer, persists to `.gs`, and is
               undoable in one step per operation, like every other entity (REQ-076 identity).
- Acceptance:
  - a feature line drawn with per-vertex elevations survives a `.gs` round trip byte-identically;
  - converting a closed polyline yields a closed feature line with the same vertices;
  - inserting a PI adds a vertex without changing the elevation of the existing ones;
  - adding a feature line to a surface forces triangulation edges along it, and moving the feature
    line rebuilds the surface with no user action;
  - deleting the feature line removes it from the surface's definition (REQ-069's rule);
  - a legacy `.gs` with no feature lines loads unchanged.
- Owner-layer: Domain, IO, Renderer, UI, Commands
- Status:      proposed
- Revisions:   2026-08-19 — initial. Reverses ADR-028 alternative (5), which deferred feature lines
               as "a separate milestone once surfaces are trustworthy"; this is that milestone.
               2026-08-20 — TASK-079 landed the "moves" clause: MOVE, COPY, ROTATE and SCALE, with
               previews and a selection highlight, and explicit refusals from TRIM, OFFSET, JOIN
               and COPYCLIP. Status stays `proposed`: "snaps" is not built (stage 2b owes snap,
               grips, DXF and PDF plot), and until it is, the Statement above describes more than
               the code does.
               2026-08-20 — TASK-082: FEATURELINE is drawable with the mouse. A click places X,Y
               and the command prompts for the elevation (default: the previous point's). Fixes
               a defect that made the command mouse-inoperable — K::FeatureLine was absent from
               both viewport-click routing lists, so every click was silently discarded — and
               adds the rubber-band preview the draft never had.

### REQ-088 — Feature line elevation editing
- Purpose:     set and check grade along a feature line, which is the actual work of grading design
- Priority:    should
- Type:        functional
- Statement:   A feature line's elevations are editable through a table showing, per point:
               **station, elevation, length to the next point, grade back and grade ahead**. Editing
               an elevation updates the adjacent grades; editing a grade updates the downstream
               elevations. Points may be raised or lowered as a set by a delta. A feature line
               additionally supports **elevation points** — points that carry an elevation but are
               not geometry vertices, insertable and deletable independently of PIs.
- Acceptance:
  - typing an elevation updates grade back and grade ahead on the neighbouring rows and nowhere else;
  - typing a grade ahead moves the next point's elevation and leaves the current one alone;
  - stations and lengths agree with the feature line's plan geometry to REQ-101's tolerance;
  - an elevation point changes the surface when the feature line is used as a breakline, and does
    not add a plan vertex;
  - every edit is undoable in one step and the surface rebuilds with no user action.
- Owner-layer: UI, Domain
- Status:      accepted
- Revisions:   2026-08-19 — initial.
               2026-08-20 — TASK-080 stage 1: the derived table and the six edits, driven by
               FLELEV. Four of five acceptance conditions verified headlessly; "rebuilds with no
               user action" is verified up to the frame tick the REQ-203 driver does not have.
               Also unblocked REQ-087's breakline clause — ResolveDefinitionChain had no
               FeatureLine branch, so a feature line designated as a breakline resolved as ABSENT
               and was stripped from the surface definition on the next rebuild. Status stays
               `proposed`: the Statement says "editable through a TABLE", and stage 2 is what puts
               one on screen.
               2026-08-20 — TASK-081 stage 2: the Feature Line Elevations panel. Every cell edit
               runs an FLELEV command, so the panel and the REQ-203 driver exercise one code
               path. ACCEPTED. The panel's own rendering has no automated coverage and cannot
               while the driver has no window; that is mitigated by the routing, not solved.

### REQ-161 — Developer Shell (Debug-only chrome tuner, activity log, GUI driver)
- Purpose: let developers tune ImGui chrome live, see what the GUI and command path are doing, and
           drive the **real** ImGui UI from code — without shipping any of that in Release
- Priority: must
- Type: functional
- Statement: A **Developer Shell** exists only when CMake option `GOSURVEY_DEVELOPER_SHELL` is ON.
  That option **defaults ON** if and only if `CMAKE_BUILD_TYPE` is `Debug`, and is **forced OFF**
  for Release (and for any configuration that builds the shipped installer). It is a compile-time
  gate, not a runtime hide.

  When ON, the windowed `GoSurvey` binary:

  - shows a Developer Shell (dockable ImGui window): **chrome tuner** that writes the existing
    ADR-033 `UiChrome` instance and relevant `ImGuiStyle` metrics so padding, sizes, and chrome
    colours change **this frame** with no rebuild;
  - shows an **activity log** of discrete events: ribbon/tool/item activations, Test Engine
    injected mouse/key, viewport **picks/clicks** (not GL draw calls), command-line input and
    output / log lines;
  - links **Dear ImGui Test Engine** (`imgui_test_engine/`, FetchContent, GIT_TAG pinned — REQ-200)
    and compiles ImGui **for this executable only** with `IMGUI_ENABLE_TEST_ENGINE`. Registered
    tests and a Debug-only CLI (`--devshell-run <test_name>`) queue and run those tests against
    the live UI (full GUI driver).

  When OFF (Release): `src/devshell/` is not a source of `GoSurvey`; Test Engine is not fetched
  into that target's link line; `IMGUI_ENABLE_TEST_ENGINE` is not defined on any ImGui objects
  that executable links; there is no Developer Shell menu/window.

  **REQ-203 is unchanged:** `gosurvey_headless` never links Test Engine, never defines
  `IMGUI_ENABLE_TEST_ENGINE`, and never includes `src/devshell/`. Domain/headless keep measuring
  fonts through ImGui **core without** Test Engine hooks.

  Activity logging must not run on the REQ-100 measured hot path as an unbounded per-primitive
  stream. A one-line-per-frame “draw submitted” toggle may exist in the Shell, default **off**.
- Acceptance:
  - `ninja-release` `GoSurvey.exe`: `dumpbin /SYMBOLS` (or `/DEPENDENTS` plus strings) shows **no**
    `ImGuiTestEngine`, **no** `DevShell`, **no** `GOSURVEY_DEVELOPER_SHELL` as a live feature —
    proven by a ctest that fails if those symbols are present;
  - `ninja-debug` with the option ON: Developer Shell is reachable; moving a chrome tuner control
    changes on-screen chrome the same session;
  - a Debug Test Engine script performs a ribbon/tool activation, a viewport click (or item click
    that issues a pick), and command-line in/out; each appears as a **distinct log line**;
  - `--devshell-run` of that script exits 0 with drawing/command state matching the same steps
    done by hand (entity counts / command log), without requiring a human at the mouse;
  - Release behaviour with the Shell absent matches today's app (commands, viewport, chrome).
- Owner-layer: Application (flag, `main` wiring), UI (`src/devshell/`, chrome accessors), Build
- Status: accepted (2026-08-29)
- Revisions: 2026-08-29 — initial. D-2026-08-29-f, ADR-040. Amends the 2026-08-16 GUI-automation
  anti-requirement for **Debug only**.

### REQ-170 — LibreDWG is the DXF and DWG codec
- Purpose: File Format Specs — open and save DWG/DXF in-process with no ODA/AutoCAD converter on
  the customer machine; write DWG only as far as LibreDWG is trustworthy (R2004)
- Priority: must
- Type: functional
- Statement: **GNU LibreDWG** is the codec for `.dwg` and `.dxf` (ADR-041). GoSurvey links it and
  is therefore **GPL-3.0-or-later**.
  **Open:** a DWG or DXF opens without `FindDwgConverter` for the happy path. Version is reported
  by release name (existing probe may remain). A file that is not DWG/DXF is refused with that
  reason (REQ-001). Entities and tables LibreDWG decoded are mapped into the GoSurvey domain;
  every skipped class, exploded INSERT (until REQ-107), extra layout, and proxy is **named in the
  log** (REQ-201). State-plane coordinates obey REQ-101 (origin subtract in double before float).
  **Save DWG:** only **R2000** or **R2004**; default **R2004**. R2007+ is refused. The file AutoCAD
  opens must do so **without a Recover prompt** for the entity set we emit. Before overwrite, the
  UI lists what this down-convert / domain mapping will drop. Failed write leaves the destination
  untouched.
  **Save DXF:** LibreDWG’s DXF writer; binary DXF is included to the extent the library writes it
  (this **subsumes** proposed REQ-112 when implemented). Types with no representation (TIN, mesh,
  cloud, PDF) are logged exclusions, not silent drops.
  Phase 1 converter-based Import/Export remains until these acceptance conditions are met, then is
  removed from the user-facing path (oracle use is allowed).
- Acceptance:
  - an R2018 `.dwg` opens with **no** ODA File Converter and **no** AutoCAD installed in the test
    environment, and model-space LINE/CIRCLE/LWPOLYLINE/TEXT/MTEXT/HATCH that LibreDWG decoded
    appear in the drawing;
  - a non-DWG renamed to `.dwg` is refused and the document is unchanged;
  - File ▸ Export DWG (default) writes R2004; AutoCAD or ODA File Converter (oracle) opens it
    **without Recover** and the emitted entity counts match the log;
  - exporting R2018 is refused by name; the destination file is not created;
  - a failed encode does not truncate an existing destination;
  - `GoSurvey` / installer materials state GPL-3.0-or-later.
- Owner-layer: IO (codec), Domain (mapping), UI (lossy-save list), Build (link LibreDWG, MSVC)
- Status: accepted
- Revisions: 2026-08-29 — File Format Specs (D-2026-08-29-g, ADR-041). Does not replace REQ-052
  Phase 1 until this requirement is verified.

### REQ-171 — Point cloud entity
- Purpose: File Format Specs — hold laser-scan points without pretending they are a TIN (REQ-068)
  or a triangle mesh (REQ-063)
- Priority: must
- Type: functional
- Statement: A drawing may contain **point cloud** objects (ADR-042): reference geometry with a
  `shared_ptr<const>` payload (§11.5), interleaved XYZ, optional RGB and intensity. They are
  visible, selectable, erasable, layer-controlled, and included in extents. They are **not**
  grip-edited, not written to DXF/DWG in this epic, and **not** surface definition sources.
  `.gs` persists them additively. Erase + undo is one step. Legacy `.gs` without the section
  loads unchanged.
- Acceptance:
  - a cloud appears in the viewport, selects as one object, and erase/undo restores it;
  - extents include the cloud; freeze/off/non-plottable on its layer hides it;
  - an unrelated line edit does **not** deep-copy the payload (shared immutable pointer);
  - DXF/DWG export **names** the cloud exclusion in the log;
  - a pre-REQ-171 `.gs` still opens.
- Owner-layer: Domain/Renderer/IO
- Status: accepted
- Revisions: 2026-08-29 — D-2026-08-29-g, ADR-042.

### REQ-172 — E57, LAS, LAZ, PTS, and PTX interchange
- Purpose: File Format Specs — the open scan formats consultants actually send
- Priority: must
- Type: functional
- Statement: GoSurvey **imports and exports** ASTM **E57**, ASPRS **LAS**, **LAZ** (LASzip),
  Cyclone-style **PTS**, and **PTX** (ADR-042, `spec/file-format-specs.md` §3.2). Import creates
  REQ-171 cloud(s). Export writes the selected cloud(s) (or all, when none selected — stated in
  the command). PTS/PTX parsers are in-tree; no delimiter auto-detect. PTX setup transforms are
  applied so points land in world coordinates within REQ-101. Malformed files refuse and leave
  the drawing unchanged (REQ-001).
- Acceptance:
  - a known-good PTS of N points imports N points (count in the log) at coordinates within
    REQ-101 of the file;
  - export of that cloud to PTS, then re-import, preserves count and XYZ within REQ-101;
  - the same for PTX **including** a non-identity setup transform (hand-checked 4×4);
  - a LAS and a LAZ of the same points import equal counts and XYZ within REQ-101;
  - an E57 with XYZ (+ RGB if present) imports; a truncated/malformed E57/LAS/PTS is refused
    with a specific message and no partial cloud;
  - missing file / empty path: no crash, drawing unchanged.
- Owner-layer: IO/Domain/UI
- Status: accepted
- Revisions: 2026-08-29 — D-2026-08-29-g. Delivery order: PTS → PTX → LAS → LAZ → E57
  (`spec/file-format-specs.md` §6).

### REQ-173 — Raster IMAGE underlays (JPEG, PNG, BMP)
- Purpose: File Format Specs — photos and scans on the sheet, like PDF attach, not a viewer app
- Priority: should
- Type: functional
- Statement: JPEG, PNG, and BMP attach as **IMAGE** underlays (ADR-042): file path, insertion
  point, rotation, width/height in drawing units, layer. Decode uses the existing stb_image
  path. They plot if plottable. `.gs` stores the path and placement; a missing image on reload
  unloads that underlay and logs it, and the rest of the drawing still loads. IMAGE may be
  written to DXF/DWG when the LibreDWG mapping exists; until then the export log names the
  exclusion rather than dropping silently.
- Acceptance:
  - attach a PNG, place it, see it in model space; MOVE the underlay; undo restores;
  - freeze its layer hides it; `.gs` round-trip restores path and placement;
  - delete the file on disk, reopen `.gs`: drawing loads, IMAGE is unloaded, log says so;
  - a truncated PNG is refused and no underlay is added.
- Owner-layer: Domain/IO/Renderer/UI
- Status: accepted
- Revisions: 2026-08-29 — D-2026-08-29-g, ADR-042.

### REQ-174 — IFC view import (no write)
- Purpose: File Format Specs — see a building model as reference geometry
- Priority: should
- Type: functional
- Statement: An **IFC** file imports as one or more **REQ-063 meshes** (ADR-042). GoSurvey does
  **not** write IFC, does not store an IFC graph, and does not decode Autodesk vertical objects
  inside DWG (ADR-026). The parser is IfcPlusPlus unless a later decision records a switch.
  Skipped/unsupported IFC products are listed in the log (REQ-201). Malformed IFC leaves the
  drawing unchanged (REQ-001). Meshes remain excluded from DXF/DWG export.
- Acceptance:
  - a small IFC2x3 or IFC4 fixture produces a mesh whose triangle count is logged and is > 0;
  - extents include the mesh; visual style Shaded occludes as REQ-064;
  - File ▸ Export IFC does not exist (or is disabled with “view only”);
  - a truncated IFC is refused; the drawing is unchanged;
  - DXF/DWG export logs the mesh exclusion.
- Owner-layer: IO/Domain
- Status: accepted
- Revisions: 2026-08-29 — D-2026-08-29-g, ADR-042.

### REQ-089 — Surface rollover readout
- Purpose:     the constant "what is this, and how high is it here" while working over a topo,
               answered without a click and without running a command
- Priority:    should
- Type:        functional
- Statement:   When the model-space cursor rests over a TIN surface — the plan position under the
               cursor lies **inside one of its triangles** — and has not moved for a **dwell
               period**, a readout appears beside the cursor naming the surface, its **effective
               style** (REQ-070 resolution), its **layer**, and the **interpolated elevation** at
               that plan position, using REQ-074's interpolation and REQ-101's tolerance. The
               readout covers **every visible surface** over that position — the overlapping
               existing-vs-proposed case REQ-074 already reports on — and is a **readout only**: it
               accepts no input and changes no state. It disappears on cursor movement, and never
               appears while a command is active, while a gesture is in progress, or in paper space.

               **The plan position is the one every other pick consumes** (the REQ-058 input seam),
               so under an orbited camera it is the cursor ray's intersection with the work plane
               rather than with the triangle under the pixel. That is deliberate: SURFELEV reads the
               same seam, so the two always agree about where "here" is. A ray-cast against the
               triangulation would be more faithful under a tilted camera and is a separate
               requirement, not an implementation detail of this one.
- Acceptance:
  - resting the cursor inside a surface for the dwell period shows the readout; moving the cursor
    hides it immediately and re-arms the dwell;
  - the elevation shown equals the planar interpolation at that position within REQ-101 — the same
    condition REQ-074 states, and the same query;
  - a position covered by no triangle — outside the border, in a concave notch, or inside a REQ-069
    hide-boundary void — shows **no readout**, and no elevation is extrapolated;
  - a surface whose style name is empty or no longer in the table reads its REQ-070 fallback style
    name, never blank;
  - a surface on an off or frozen layer, or isolated out under REQ-084 (d), produces no readout;
  - two overlapping visible surfaces produce one block each, both named;
  - **the per-frame cost of moving the cursor over a surface is unchanged**: the covering-surface
    query runs **once, when the dwell elapses**, and its result is latched — never re-run per frame.
    This is an acceptance condition rather than a note because `TinElevationAt` is a linear scan over
    every triangle and REQ-100 profile (c) is the one profile near budget and CPU-bound; a per-frame
    query would roughly double its dominant cost.
- Owner-layer: UI (dwell, gating, draw), Commands (the query)
- Status:      accepted
- Revisions:   2026-08-23 — initial. Requires no ADR: the state is UI-transient on
               `AppCommandState` beside the existing hover fields, the payload latches formatted
               text rather than a surface index (architecture §11.9), the dwell helper is a concrete
               free function in a pure header, and nothing is persisted.
               2026-08-23 — TASK-088 implemented it. The last acceptance condition (the query runs
               once per rest, never per frame) is **pinned by a test** — `HoverDwellTests`, 600
               frames of a held cursor, one query. The elevation, containment and style-fallback
               conditions are met by the calls the readout reuses, each already pinned by
               `TinQueryTests` / `SurfaceStyleTests`. The conditions about what appears **on screen**
               — the readout showing on dwell, hiding on movement, and being suppressed during a
               command — are implemented and reviewed but **not observed**: see TASK-088 FINDING-1,
               where synthetic input could not produce a hovered frame (a ribbon button used as a
               control did not highlight either). Status stays `accepted` rather than advancing to
               `implemented` until someone has watched it work.

### REQ-090 — Survey point rollover readout
- Purpose:     read a point's number and coordinates without clicking it or opening the point list —
               the same question REQ-089 answers for a surface, asked far more often
- Priority:    should
- Type:        functional
- Statement:   When the model-space cursor rests over a survey point's marker and has not moved for
               the REQ-089 dwell period, a readout appears beside the cursor giving the point
               **number**, its **layer**, and its **northing, easting and elevation**.

               **Northing and easting are reported in WORLD coordinates.** Survey points are stored
               local (`world = local + worldDocumentOrigin`, architecture §11 / `CadCoordinateFrame`),
               so the readout resolves through `CadCoord::WorldXFromLocal` / `WorldYFromLocal` at
               `surveyPointDisplayPrecision` — the same conversion and the same precision the
               Properties panel already applies to the same point. Elevation is absolute and is NOT
               rebased, because the local-storage rebase is X/Y-only.

               **A survey point takes precedence over a surface.** Where the cursor rests on a point
               that also lies inside a surface, the point's readout is shown and REQ-089's is not —
               the same priority the pick funnel already applies, and the alternative (stacking both
               blocks) would put a four-row panel between the user and the marker they are pointing
               at. It reuses REQ-089's dwell, suppression rules and panel; there is one readout
               beside the cursor, never two.
- Acceptance:
  - resting on a point marker for the dwell period shows the readout; moving the cursor hides it
    immediately and re-arms the dwell;
  - **northing and easting equal what the Properties panel shows for the same point, in a drawing
    whose `worldDocumentOrigin` is NON-ZERO** — a local/world mix-up is invisible on a test drawing
    near the origin and wrong by hundreds of thousands of feet on a real state-plane one, so the
    condition names the case that can actually fail;
  - elevation equals the point's stored elevation at `surveyPointDisplayPrecision`;
  - the number shown is `SurveyPoint::id`;
  - resting on a point that also lies inside a surface shows the point's readout and only that;
  - no readout appears while a command is active, while a gesture is in progress, or in paper space.
- Owner-layer: UI
- Status:      accepted
- Revisions:   2026-08-23 — initial. Widens the readout REQ-089 introduced, which
               D-2026-08-23-a (4) scoped to surfaces with "a later requirement can widen it if the
               readout proves useful". Requires no ADR for the same reasons REQ-089 did not, and one
               fewer: the hit test already exists and already runs every frame
               (`viewportHoverSurveyPointIndex`), so this adds no query at all.

---

### REQ-091 — User accounts and sign-in (Auth0)
- Purpose: identify a user for license/paid-tier enforcement, which anonymous REQ-080
  telemetry is deliberately unable to do
- Priority: should
- Type: functional
- Statement: The application supports signing in via **Google**, **Microsoft**
  (Outlook/Live), or a self-registered **email + username + password** account, backed
  by Auth0 (a managed identity provider) rather than an in-house auth backend. Sign-in
  uses Auth0's hosted Universal Login page reached via the system browser — the
  application never renders its own password form or OAuth-provider buttons and never
  receives a raw password. The native app authenticates using the system-browser +
  loopback-redirect + PKCE flow (RFC 8252): no embedded webview. The resulting refresh
  token is stored via Windows Credential Manager, never in `gosurvey-user.json` or any
  other plaintext file; access/ID tokens are kept in memory only and are never
  persisted. On subsequent launches the application renews the session silently from
  the stored refresh token, without reopening the browser, unless that token has expired
  or been revoked, in which case interactive sign-in runs again. A signed-in user's
  identity is verified against Auth0's issued token; the application does not trust an
  unverified claim of identity from anywhere else.

  This requirement covers identity and the session-level sign-in gate. It does not gate
  any individual feature or command: the license/tier lookup (below) is a hook other
  requirements will consume once what is gated and under what terms is decided —
  inventing that scope here would be the spec guessing at business decisions it has no
  authority over. The gate below governs SESSION ACCESS, which is a different thing.

  **Launch gate (added 2026-08-23, D-2026-08-23-d, reverses this requirement's original
  "no application feature is gated" condition):** every launch, until the user is signed
  in, a modal window blocks the rest of the application — no drawing can be opened or
  started. It offers only Sign In; there is no dismiss, skip, or close. The one
  exception: when there is no internet connectivity at all, the gate is skipped entirely
  and the application opens normally, signed out — the same offline exception REQ-077's
  update-check gate uses, and for the same reason (a surveyor with no signal must not be
  locked out of a program that has nothing to reach). The gate is resolved once per
  launch and is not re-imposed if the user signs out later in the same session.
- Acceptance:
  - clicking "Sign In" opens the system browser to Auth0 Universal Login showing all
    three configured options: Google, Microsoft, and email/username/password;
  - completing sign-in by any of the three methods returns control to the application
    (the loopback redirect is caught) and the settings panel shows "Signed in as
    `<email>`";
  - closing and reopening the application does not require interactive sign-in again
    while the stored refresh token remains valid;
  - an expired or revoked refresh token causes the next launch to require interactive
    sign-in;
  - the refresh token never appears in `gosurvey-user.json` or any other plaintext file
    — verified by inspecting Windows Credential Manager rather than the prefs file;
  - no individual command or feature is separately gated or blocked by sign-in state or
    tier beyond the launch gate itself (mechanism only, per the scope note above);
  - REQ-080's anonymous telemetry ping is unchanged by this requirement;
  - **(added 2026-08-23)** on launch, with network reachable and no valid stored
    session, a modal blocks all other interaction until Sign In succeeds — no close
    button, no click-away dismissal;
  - **(added 2026-08-23)** on launch, with `HasInternetConnectivity()` reporting no
    route to the internet, the modal does not appear at all and the application opens
    normally;
  - **(added 2026-08-23)** signing out from the Settings panel later in the same
    session does not reopen the launch gate.
- Owner-layer: Platform (loopback listener, Credential Manager, connectivity check), Auth
  (pure logic, mirrors Telemetry's split), UI (sign-in entry point/status, launch gate)
- Status:      accepted
- Revisions:   2026-08-23 — initial (D-2026-08-23-c). See ADR-037 for the Auth0/PKCE/
               Credential-Manager technical shape and the REQ-300 dependency decision.
               2026-08-23 — added the blocking launch gate with an offline exception
               (D-2026-08-23-d), reversing the original "no application feature is
               gated" condition for session access specifically (not for individual
               features/tiers, which remain ungated).

### REQ-092 — License-tier lookup endpoint
- Purpose: give the application a place to learn a signed-in user's entitlement, ahead
  of any requirement naming what that entitlement controls
- Priority: should
- Type: functional
- Statement: A backend endpoint, separate from the REQ-080 telemetry Worker, verifies
  the caller's Auth0-issued JWT and returns that user's license tier. New sign-ups
  default to a single tier (e.g. `"free"`); nothing yet writes any other value — billing,
  an admin tool, or a manual grant are explicitly future work, not part of this
  requirement.
- Acceptance:
  - a request with no JWT, an invalid JWT, or an expired JWT is rejected (401/403) and
    never reaches the tier lookup;
  - a request with a valid JWT for a newly signed-up user returns the default tier;
  - the endpoint's data store is separate from the telemetry Worker's, so a defect in
    one cannot read or corrupt the other's data.
- Owner-layer: Platform/backend (new Cloudflare Worker + D1 database, outside `src/`)
- Status:      accepted
- Revisions:   2026-08-23 — initial (D-2026-08-23-c). See ADR-037.

---

## Backlog — catalogued from Known Limitations (proposed, not accepted)

> REQ-102–REQ-117 were catalogued 2026-08-23 from `docs/WikiDocumentation.md`'s
> "Known Limitations" page (0.5.3) — see D-2026-08-23-i in `spec/project.md`.
> Each is a rough problem statement, not a scoped acceptance contract: scope,
> priority and exact acceptance conditions are settled when a user picks one
> to accept. None of these authorize a `workshop/tasks/` file yet.

### REQ-102 — Layer state enforcement
- Purpose: Layer On/Freeze/Lock are stored and round-tripped but never enforced — every layer always draws, is always selectable, and is always editable
- Priority: should
- Type: functional
- Statement: Toggling a layer Off or Frozen hides its entities from the viewport and any plot; Frozen also excludes it from selection, snapping, and drawing extents; Locked entities stay visible and snappable but reject move/erase/grip/property edits.
- Acceptance (sketch): Off/Frozen layers don't render on screen or in a plot; Frozen layers are excluded from selection, snap and extents; Locked entities are visible/snappable but reject edits; unlocking/thawing restores prior behavior with no data loss; composes correctly with the existing per-viewport layer freeze (REQ-028).
- Owner-layer: Domain/Renderer/Commands/UI
- Status: proposed
- Revisions: 2026-08-23 — catalogued (D-2026-08-23-i)

### REQ-103 — Modify-command completeness
- Purpose: MIRROR, STRETCH, EXTEND, BREAK, FILLET, CHAMFER, ARRAY, EXPLODE, and LENGTHEN are all absent from the Modify toolset
- Priority: should
- Type: functional
- Statement: Add the nine commands above, each operating on the entity types they apply to today, undoable in one step, consistent with the existing MOVE/COPY/ROTATE/SCALE/TRIM/OFFSET pattern. Delivered incrementally, one command per task, in the sequence below — each independently shippable and independently verifiable, the same incremental-epic pattern as REQ-066…075/M-Surfaces.
- Sequence:
  1. **MIRROR** — closest existing precedent (ROTATE/SCALE's transform-funnel shape); no new entity kind; ships first.
  2. **LENGTHEN** — extends/shortens a line/arc/polyline along its own direction; its edge-parameter math is reused by EXTEND and FILLET below.
  3. **EXTEND** — TRIM's direct inverse; reuses TRIM's cutting-edge selection/hover infrastructure (REQ-056) almost entirely.
  4. **BREAK** — splits an entity at one or two picked points; contained, no new entity kind.
  5. **STRETCH** — crossing-window selection that only moves vertices inside the window; the one sub-item likely to need its own short design pass (crossing-vs-window selection doesn't exist yet).
  6. **FILLET / CHAMFER** — corner generation (arc or chamfer line) between two entities, trimming/extending them to meet it; follows EXTEND/TRIM naturally.
  7. **ARRAY** — rectangular + polar; N-copy repetition of the same duplication machinery MIRROR/COPY establish.
  8. **EXPLODE** — decomposes a closed polyline (including rectangles, which are 4-vertex polylines per REQ-053) into line segments; reports what it can't decompose (arcs, ellipses, mesh, surface, fills) rather than doing nothing silently (REQ-201).
- Acceptance — MIRROR (step 1, this increment):
  - a mirror line is specified by two points; text/mtext insertion points reflect across it, but glyphs stay upright and readable — no mirror-image flip (AutoCAD's MIRRTEXT=0 default; no MIRRTEXT-equivalent setting is added, this is fixed behavior for now);
  - after the mirror line, an "Erase source objects? [Yes/No] <N>" prompt appears, defaulting to **No** (source kept, mirrored copy added) — matching AutoCAD's own default;
  - erase=No: the mirrored result is a duplicate with newly assigned stable ids (REQ-076/ADR-027 — never a copied source id), the original selection is untouched; a duplicated survey point with an id conflict is resolved through the existing new-vs-offset modal (the same one COPY/rotate-copy already use), not silently;
  - erase=Yes: the duplicate commits, then the pre-mirror selection is removed — net result is only the reflected geometry;
  - `FilledRegion` (hatches), `Mesh`, `PdfUnderlay`, and `Surface` in the selection are excluded from the mirror with a logged reason (REQ-201) rather than silently dropped or mishandled — `FilledRegion`/`Surface` per the existing rotate/scale exclusion precedent (this file, REQ-042 fills note; `DropSurfacesFromSelectionForTransform`), `Mesh` because it is never edited (REQ-063), `PdfUnderlay` because its scalar rotation/scale fields cannot represent a reflection;
  - one undo step restores the exact pre-mirror state;
  - reachable from the Modify ribbon (button already exists, disabled, at `CadUi.cpp:2322`), by typed `MIRROR`, and by right-click repeat;
  - works with a selection made in model space, in a paper-space layout, and through a floating model-space viewport (parity with ROTATE/SCALE's existing paper-space routing).
- Acceptance — LENGTHEN (step 2):
  - eligible entities are Line, open Polyline, and a non-full-circle Arc; Circle, Ellipse, closed Polyline, a full-circle Arc, and every non-length-bearing entity kind (Annotation, FeatureLine, Surface, Mesh, FilledRegion, PdfUnderlay) are refused with a stated reason (REQ-201), never silently ignored;
  - four sub-modes — DElta (add/subtract a typed length), Percent (scale total length by a typed percentage), Total (set total length directly), DYnamic (interactive drag with a live preview) — each resolve to one target length, then apply it to the end of the picked entity nearest the pick point, holding the other end fixed, within REQ-101 tolerance of the hand-computed target. **The default sub-mode is Total** (amended 2026-08-24, D-2026-08-24-f — AutoCAD defaults to DElta; this deliberately does not), so that with the pick-first entry below the out-of-the-box interaction is: pick a line, read the length it reports, type the length it should be;
  - a target length that would collapse an entity to ~0 length, or push an Arc's sweep past a full circle, is rejected with a message rather than silently clamped or wrongly applied;
  - the active sub-mode persists across repeated picks within one invocation (typing a mode letter again mid-loop switches it and re-prompts for its value); the command loops back to "select object" after each successful apply until Enter/Esc; each individual apply is its own undo step, matching TRIM/OFFSET's per-target granularity, not one step per invocation;
  - **a pick made before the active sub-mode has a value is accepted, not refused** (amended 2026-08-24, D-2026-08-24-e): the picked object is latched, its current length is reported, and that sub-mode's value prompt opens; the value typed next applies to that object immediately rather than only arming the mode. Typing a mode letter at that prompt switches sub-mode and keeps the latched object (DYnamic hands it straight to the drag). A refused length (collapse-to-zero, arc past a full circle) clears the latch, so a later value can never silently apply to a stale object. Without this the Modify ribbon button was a dead end — the opening prompt invited a pick and the pick was rejected — and the command was reachable only by typing a mode letter *and* a number before picking;
  - reachable from the Modify ribbon, typed `LENGTHEN`/`LEN`, and right-click repeat; works on model-space and native paper-space Line/Arc/open-Polyline entities alike. In paper space the value prompt is unavailable (the paper path runs with no active model command to hold it — the same documented simplification MIRROR's paper path makes), so a valueless pick there reports the object's current length and says where to set the value, rather than refusing bare.
- Acceptance — EXTEND (step 3):
  - eligible boundary edges: any entity a user can select except Annotation, FeatureLine, Surface (refused with a stated reason, matching TRIM's existing boundary-edge refusal set); eligible targets: Line, open Polyline, non-full-circle Arc — the same set LENGTHEN established, refused with the same reasons for Circle, Ellipse, closed Polyline, full-circle Arc, and every non-length-bearing kind;
  - boundary intersection is analytic (`curveisect`, REQ-062's already-accepted library), never tessellated — a chord-approximated boundary does not meet REQ-101, the same reasoning that made REQ-062 analytic in the first place;
  - the end of the target nearest the pick extends, along the direction it already points (a Line/open-Polyline's own direction; an Arc's own circle, radius held fixed), to the nearest point where it meets a boundary edge, within REQ-101 tolerance of the hand-computed intersection; the other end stays exactly fixed;
  - a target that does not reach any boundary edge in the extending direction is refused with a stated reason, geometry unchanged — never silently ignored or extended the wrong way;
  - boundary-edge selection is a two-phase pick (edges, Enter, then targets — TRIM's own cutting-edge flow, copy-adapted, not shared code) with boundaries visually read as a selection while being picked, matching TRIM's precedent; the command loops back to "select object" after each successful extend until Enter/Esc; each individual extend is its own undo step, not one per invocation;
  - reachable from the Modify ribbon, typed `EXTEND`/`EX`, and right-click repeat; works in model space, floating model space, AND native paper space (two-phase click flow, no typed value needed so paper space is not simplified away the way MIRROR/LENGTHEN's paper paths are).
  - Acceptance — BREAK (step 4):
    - eligible entities: Line, Arc (any sweep, including a full-circle sweep), Circle, open
      Polyline, and closed Polyline; refused with a stated reason (REQ-201): Ellipse (no
      elliptical-arc entity kind exists in GoSurvey to hold a broken-open ellipse — adding one
      would be a genuinely new entity kind, which this step's own "no new entity kind" framing
      rules out), Annotation, FeatureLine, Surface, Mesh, FilledRegion, PdfUnderlay, Text, Mtext,
      and SurveyPoint;
    - the pick that selects the entity also supplies break point 1 — the closest point ON that
      entity to the pick, not the raw cursor position; a second pick supplies break point 2,
      likewise projected onto the already-selected entity; a break point coinciding with an
      entity's own endpoint (within REQ-101 tolerance) is treated as that endpoint exactly;
    - on an OPEN entity (Line, non-full Arc, open Polyline): the two break points are ordered by
      position along the entity (independent of click order), and the material between them is
      removed. Both points strictly interior → the original entity is shortened in place down to
      start→nearer-point, and a NEW duplicate entity (fresh id, REQ-076/ADR-027) is created for
      farther-point→end. One point coinciding with an existing endpoint → the entity is shortened
      in place from the other point only, no duplicate created. Both points coinciding with the
      two existing endpoints → refused ("would remove the entire entity"), geometry unchanged —
      never silently deleted;
    - on a CLOSED entity (Circle, full-circle-sweep Arc, closed Polyline): click order matters —
      the material swept from break point 1 to break point 2, travelling in the direction of
      increasing parameter (counterclockwise for Circle/full Arc; stored vertex order for closed
      Polyline), is removed, leaving one open result starting at point 2, ending at point 1 —
      matching AutoCAD's own circle-break convention. A Circle converts into a new Arc entity
      (fresh id — Circle and Arc are separate stores; this is a conversion between two entity
      kinds that already exist, not a new kind). A full-circle-sweep Arc is mutated in place (same
      id, no duplicate, no store change). A closed Polyline is mutated in place (same id, `Closed`
      flag cleared, vertex list rewritten to run point2→…→point1). The two break points landing at
      the identical position (a repeated pick) removes nothing and simply opens the closed entity
      at that point — a legitimate "break at point" case, not a no-op refusal;
    - each individual break is its own undo step; the command loops back to "select object" after
      each completed break until Enter/Esc, matching TRIM/LENGTHEN/EXTEND's per-target granularity
      and looping shape;
    - **between the two picks a live preview shows the material that will be removed** (amended
      2026-08-24, D-2026-08-24-e): the span from break point 1 to the cursor — projected onto the
      picked entity by the same `ClosestPointOnEntity` the second pick commits, never the raw
      cursor — is drawn in the preview style, with a marker at each break point. The previewed span
      follows the same ordering rule its commit does: position-ordered on an open entity (click
      order irrelevant), and on a closed entity the complement of the span the commit keeps, so
      reversing the click order visibly previews the other side. A repeated pick ("break at point")
      previews a zero-length span with both markers still shown. It is drawn opaque, in a warning
      colour, at highlight line width, on its own render channel — NOT in the translucent
      transform-preview batch, which is built for a ghost of geometry somewhere it is not yet and
      washes out to nothing when painted over the full-opacity object a removal preview sits on
      top of. **Model space only**: the GL pass that draws it is skipped whenever the active space
      is not model space, so neither paper space nor floating model space shows it — the same
      limit every other GL preview already has, stated rather than implied;
    - reachable from the Modify ribbon, typed `BREAK`/`BR`, and right-click repeat; works in model
      space, floating model space, and native paper space (pure two-click flow, no typed value
      needed, so paper space is not simplified away — same reasoning as EXTEND's step 3).
  - Acceptance — STRETCH (step 5):
    - a crossing/window selection box is picked first (left-to-right = window/fully-inside,
      right-to-left = crossing/overlap — the same rule REQ-039's paper-space parity already
      established), then a base point, then a destination point (or a typed relative
      displacement) — one displacement applies to the whole selection in a single apply, not a
      per-target loop (matching MOVE/ROTATE/SCALE's granularity, not TRIM/LENGTHEN/EXTEND/BREAK's);
    - for every entity in the box-selected set, each of its "definition points" is tested
      independently against the box and only the in-box ones move by the displacement — this is
      the genuine stretch effect for entities straddling the box boundary:
      - Line, Polyline (open or closed), and FeatureLine: every endpoint/vertex is independent (a
        FeatureLine's elevation is never altered by the move, matching MOVE/ROTATE/SCALE's own
        existing plan-only transform of it — REQ-087);
      - Arc: both endpoints are tested. Zero or both in-box → no-op or whole-arc translate,
        unchanged radius/angles. Exactly one in-box → the arc is genuinely reshaped: its center
        and radius are recomputed so it passes through the moved and fixed endpoints while
        preserving the original included angle (the "bulge"), matching AutoCAD; a full-circle-
        sweep Arc is exempt from this and instead follows the Circle rule below (its two
        endpoints coincide, so per-endpoint math is undefined); a stretch that would collapse
        the new chord to ~0 length is refused with a stated reason (REQ-201), the arc left
        unchanged, rather than corrupting it to a zero radius;
      - Circle, Ellipse (center only), Annotation/Text/Mtext/Dim (insertion point; dimension
        extension points are not independently tested), PdfUnderlay (insertion point),
        FilledRegion (one reference point, whole-region translate, no per-vertex boundary
        stretch), SurveyPoint (its own point): each has exactly one definition point and moves
        as a whole only if that point is in-box — matching AutoCAD's own behavior for these
        types (they are not "stretched," only moved-if-selected-and-in-window);
      - `Surface` and `Mesh` are excluded from the selection, consistent with the existing
        transform restrictions (`DropSurfacesFromSelectionForTransform`; Mesh is never edited);
    - a box-selected entity none of whose definition points land in the box is a legitimate
      no-op (still selected, simply unmoved) — not a refusal, matching AutoCAD;
    - one undo step restores the exact pre-stretch state for the whole apply;
    - works in model space, floating model space, and native paper space with true per-point
      partial stretch in both spaces (not simplified to whole-entity-only in paper space); a
      paper-space selection built by a plain click (not a box) degrades to a whole-entity
      translate for every selected entity, since no crossing/window box exists to test points
      against — matching AutoCAD's own degradation for a non-crossing pickfirst set;
    - the box/point-membership test itself operates in plain world-XY (model) or paper-inch XY
      (paper) coordinates, not projected through an orbited 3D camera the way the box-select's
      own entity-candidacy test optionally is — a stated, accepted simplification, recorded as
      technical debt rather than a silent gap;
    - reachable from the Modify ribbon, typed `STRETCH`, and right-click repeat.
  - Acceptance — FILLET (step 6a):
    - eligible curves: Line, non-full-circle Arc (full-circle-sweep Arc and Circle refused — no
      single tangent-side construction distinguishes them from a fillet's corner geometry, matching
      Circle's own exclusion from LENGTHEN/EXTEND for a related reason), and a segment of an open
      OR closed Polyline. A picked Polyline segment is one of two cases, resolved by which second
      pick follows: **(A) the other pick lands on the segment immediately ADJACENT (sharing exactly
      one vertex) on the SAME polyline** — the classic "round this corner" case, well-defined on
      open or closed polylines alike, since the shared vertex needs no near/far disambiguation; **(B)
      the other pick is a different entity (or a non-adjacent segment of a different polyline)** —
      only the polyline's own first or last segment (adjacent to a free/open end) is eligible this
      way, since moving any other segment's endpoint would silently disturb an uninvolved neighboring
      segment sharing that vertex; a closed polyline has no free end, so it is never eligible for
      case (B). An interior segment of an open polyline, picked against a different entity (not its
      own polyline neighbor), and any non-adjacent pair of segments on the same polyline, are refused
      with a stated reason (REQ-201) rather than silently misapplied. Picking the identical segment
      twice is refused ("select two different objects/segments");
    - **radius** is a persisted setting (`filletRadius`, default 0.5, like `TRIMSTATE`'s own
      app-level persistence via `gosurvey-user.json` — not per-drawing) set by typing `R` before a
      pick ("Specify fillet radius <current>:"), applying to this and future invocations until
      changed again;
    - **trim mode** is a persisted setting (`cornerTrimMode`, default Trim) set by typing `T` before
      a pick ("Enter Trim mode option [Trim/No trim] <Trim>:"), **shared with CHAMFER** (matching
      AutoCAD's own shared `TRIMMODE` variable — one toggle governs both commands). Trim mode moves
      each curve's near end (nearest its own pick) to its tangent point, exactly as described below;
      No-trim mode adds the fillet arc at the same computed tangent points but leaves both original
      curves completely unchanged (a real AutoCAD behavior, not a simplification);
    - flow: `FILLET` prompts "Select first object or [Radius/Trim]:"; a pick latches the first curve
      and its pick point; a second pick on an eligible partner computes and applies the fillet
      immediately (no separate confirm step); the command then loops back to "select first object"
      until Enter/Esc — REQ-103's own established per-target looping shape (TRIM/LENGTHEN/EXTEND/
      BREAK), deliberately without AutoCAD's opt-in "Multiple" option, since looping is already this
      epic's default and an opt-out would be the inconsistent choice; each individual fillet is its
      own undo step;
    - **geometry (radius > 0, non-parallel case):** each curve's supporting shape (a Line's infinite
      extension; a non-full Arc's own full circle, `curveisect::MakeCircle` not `MakeArc` — an
      Extend-precedent choice, so a tangent point beyond the current sweep is still found and the
      arc extended to reach it) is offset by exactly the fillet radius on both sides (Line: parallel
      line translated ± radius along its own perpendicular; Arc/Circle-equivalent: concentric circle
      of radius ± the fillet radius); every combination of the two curves' offset shapes is
      intersected (analytically, via the existing `curveisect::IntersectSegSeg`/`IntersectSegConic`/
      `IntersectConicConic` — REQ-062/REQ-101, never tessellated) to produce every candidate fillet
      center; the candidate chosen is the one minimizing the summed squared distance to the two pick
      points — a deterministic, testable tie-break consistent with every other REQ-103 step's
      "nearest to the pick" convention (LENGTHEN/EXTEND/BREAK/TRIM all resolve ambiguity this way);
    - the tangent point on a Line is the foot of the perpendicular from the chosen center onto the
      line's own infinite extension; the tangent point on an Arc is the point on the arc's own
      original circle along the ray from the arc's center through the chosen fillet center (this
      point is guaranteed to already lie exactly on that circle by construction of the offset
      intersection); the fillet arc itself runs from one tangent point to the other around the
      chosen center, taking the smaller of the two possible sweeps (< π always, since a corner-round
      is always the minor arc) — a fresh Arc entity (REQ-076/ADR-027, id 0 until swept), never a
      mutation of either input;
    - **radius = 0** is a valid, well-defined degenerate case, not a refusal: the offset-by-0
      construction reduces to intersecting the two original curves directly (same analytic
      functions, unchanged), no Arc entity is created, and both curves are trimmed/extended (Trim
      mode) directly to that intersection point — matching AutoCAD's own R=0 "sharp corner" behavior;
    - **two parallel, non-collinear Lines** are a special case independent of the current radius
      setting (a real, documented AutoCAD behavior, not a simplification): a semicircular Arc is
      created connecting the two lines' nearest-facing endpoints, with a radius of exactly half the
      perpendicular distance between them; the current `filletRadius` setting is ignored for this
      one case, exactly as AutoCAD ignores it too. Collinear/overlapping Lines, and any radius/
      geometry combination with no real tangent solution (radius too large for the available
      geometry, arcs too far apart, etc.), are refused with a stated reason (REQ-201), geometry
      unchanged — never silently clamped or wrongly applied;
    - in the Case (A) same-polyline-adjacent-vertex flow, the shared vertex is replaced by the two
      tangent points (vertex list grows by one; CSR offsets for every later polyline shift, the same
      bookkeeping BREAK's `ReplacePolylineVerts` already established) and the new Arc entity is
      inserted for the corner — or, at radius 0, the shared vertex simply moves to the single
      intersection point (vertex count unchanged, no Arc). In the Case (B) flow, the standalone
      curve is trimmed/extended to its tangent point via the exact same reused mutation the standard
      curve case uses below;
    - each Line/Arc/open-polyline-end-segment curve's trim/extend to its own tangent point reuses
      LENGTHEN's own mutation functions unchanged — `ApplyLengthenToLine`/`ApplyLengthenToArc`/
      `ApplyLengthenToPolylineEnd` — by converting the known tangent point into the `newLength` those
      functions already accept (a Line's new length is the distance from its fixed end to the
      tangent point; an Arc's follows the identical angle-to-length conversion EXTEND's own
      `FindExtendArcTarget` already established), exactly the reuse chain REQ-103's own sequencing
      note promised ("LENGTHEN's edge-parameter math is reused by EXTEND and FILLET"); the near end
      (the one that moves) is whichever end lies closer to that curve's own pick point, the same
      `NearerToFirstPoint` convention LENGTHEN/EXTEND already use;
    - reachable from the Modify ribbon (new column, no pre-staged stub exists), typed `FILLET`/`F`,
      and right-click repeat; works in model space, floating model space, and native paper space
      (full parity, matching EXTEND/BREAK/STRETCH's precedent, not TRIM's own paper-space gap).
  - Acceptance — CHAMFER (step 6b):
    - eligible curves: Line and a segment of an open or closed Polyline only — Arc is refused with a
      stated reason (REQ-201): a chamfer is a straight connecting line measured by distance/angle
      along each curve from their intersection, which has no standard meaning against a curved Arc
      (matching AutoCAD's own restriction — CHAMFER has never operated on arcs); the same Case (A)
      same-polyline-adjacent-vertex / Case (B) end-segment-only-against-a-different-entity split, and
      the same non-adjacent/interior-segment/identical-segment-twice refusals, apply exactly as
      FILLET's Polyline rules above;
    - **distances/angle** are persisted settings (`chamferDist1`/`chamferDist2`, default 0.5 each,
      for Distance/Distance mode; `chamferAngle`, default 45°, reusing `chamferDist1` as the single
      Distance/Angle-mode distance) — `gosurvey-user.json`-persisted exactly like `filletRadius`;
      **mode** (`chamferMode`: Distance/Distance default, or Distance/Angle) is set by typing `D` or
      `A` before a pick, each prompting for its own value(s); **trim mode is the same persisted
      `cornerTrimMode` setting FILLET uses** (AutoCAD's own shared `TRIMMODE`, not a second toggle);
    - flow mirrors FILLET's exactly: `CHAMFER` prompts "Select first line or [Distance/Angle/Trim]:",
      first pick latches the curve, second pick on an eligible partner computes and applies
      immediately, loops back until Enter/Esc, one undo step per chamfer;
    - **geometry:** `P` = the intersection of the two curves' infinite extensions (`curveisect::
      IntersectSegSeg`, unchanged; parallel/non-intersecting Lines are refused — REQ-201 — since,
      unlike FILLET, CHAMFER has no AutoCAD analogue to FILLET's parallel-lines semicircle special
      case, a real asymmetry between the two commands, not an oversight). Distance/Distance mode:
      Point1 = P + `chamferDist1` along curve 1's own direction, away from P, toward the side the
      curve1 pick landed on; Point2 = the same construction on curve 2 with `chamferDist2`.
      Distance/Angle mode: Point1 = P + `chamferDist1` along curve 1 the same way; the chamfer line's
      direction is curve 1's own direction rotated by `chamferAngle` toward curve 2's side from
      Point1, and Point2 is that ray's intersection with curve 2's infinite extension (refused,
      REQ-201, if parallel to curve 2 — no intersection exists);
    - **both distances (or the single Distance/Angle-mode distance) equal to 0** is a valid,
      well-defined degenerate case mirroring FILLET's radius-0 case: no chamfer Line entity is
      created, and (Trim mode) both curves are trimmed/extended directly to `P`;
    - Trim mode moves each curve's near end (nearest its own pick) to its own computed point (Point1/
      Point2 respectively), reusing `ApplyLengthenToLine`/`ApplyLengthenToPolylineEnd` the identical
      way FILLET's Line/Polyline cases do (CHAMFER never touches `ApplyLengthenToArc` — Arc is not
      an eligible curve); the new chamfer Line entity (fresh id, REQ-076/ADR-027) connects Point1 to
      Point2 regardless of trim mode; No-trim mode adds that Line without altering either curve, the
      same behavior FILLET's No-trim mode has;
    - reachable from the Modify ribbon (same new column as FILLET), typed `CHAMFER`/`CHA`, and
      right-click repeat; works in model space, floating model space, and native paper space (full
      parity, matching FILLET's own paper-space scope above).
  - Acceptance for ARRAY/EXPLODE (steps 7–8) is written when each is accepted for implementation,
    not spec'd in advance of that command's own design pass.
- Owner-layer: Commands/Domain/UI
- Status: accepted
- Revisions: 2026-08-23 — catalogued, proposed (D-2026-08-23-i). 2026-08-23 — accepted; sequenced into 8 increments starting with MIRROR; MIRROR's acceptance conditions written; MIRRTEXT-off and erase-default-No confirmed with the user (D-2026-08-23-j, TASK-094). 2026-08-24 — LENGTHEN's (step 2) acceptance conditions written (D-2026-08-24-a, TASK-095). 2026-08-24 — EXTEND's (step 3) acceptance conditions written; analytic-over-tessellated boundary intersection and paper-space-included both confirmed with the user (D-2026-08-24-b, TASK-096). 2026-08-24 — BREAK's (step 4) acceptance conditions written; Circle/full-circle-Arc target eligibility (converts to Arc) and closed-Polyline target eligibility (splits open) both confirmed with the user (D-2026-08-24-c, TASK-097). 2026-08-24 — STRETCH's (step 5) acceptance conditions written; full AutoCAD-parity arc partial-stretch (center/radius recompute preserving included angle) and full paper-space vertex-level parity both confirmed with the user (D-2026-08-24-d, TASK-098). 2026-08-24 — after the first hand-driven GUI pass: LENGTHEN's valueless first pick amended from a refusal to a latch-and-prompt (the ribbon button was a dead end), and a live removed-span preview added to BREAK's acceptance (D-2026-08-24-e, TASK-100, TASK-101). 2026-08-24 — LENGTHEN's default sub-mode changed from DElta to Total, so pick-then-type-the-new-length is the out-of-the-box flow (D-2026-08-24-f, TASK-100). 2026-08-24 — FILLET's and CHAMFER's (step 6a/6b) acceptance conditions written; full AutoCAD-parity scope (Line/Arc/Polyline-segment eligibility, a Trim/No-trim toggle shared between the two commands, full paper-space parity, and both Distance/Distance and Distance/Angle chamfer input) confirmed with the user (D-2026-08-24-g, TASK-102/TASK-103). 2026-08-25 — the "select objects" shape MOVE/COPY/ROTATE/SCALE/MIRROR established and this REQ's later steps (STRETCH, ARRAY, ALIGN — REQ-039) all reused was two-corner window/crossing box only: no individual-entity click, no accumulating across more than one box, no confirm-on-Enter. A user report against ARRAY (REQ-305) found this the same real gap in every one of them, not ARRAY alone; the shared shape is now click-and/or-box, additive, accumulating until Enter confirms it (D-2026-08-25-n) — applied to MOVE, COPY, SCALE, ROTATE, MIRROR, and ALIGN. STRETCH (step 5) is deliberately excluded: its crossing box is load-bearing geometry (which vertices move), not just an object filter, so it keeps the original box-only shape.

### REQ-104 — Draw-command completeness
- Purpose: SPLINE, XLINE, RAY, DONUT, SOLID, REVCLOUD, WIPEOUT, and MLINE have no command at all
- Priority: could
- Type: functional
- Statement: Add the commands above, each stored, selectable, snappable, undoable, and round-tripping through `.gs` and DXF like existing entities.
- Acceptance (sketch): live preview, commit on the snapped point per the project's preview-vs-commit invariant; each new entity kind is added at every integration site (selection, extents, layer, undo, `.gs`, DXF, render, snap, grips, properties); a spline's chord deviation is within REQ-101 wherever REQ-101 applies.
- Owner-layer: Commands/Domain/IO/UI
- Status: proposed
- Revisions: 2026-08-23 — catalogued (D-2026-08-23-i)

### REQ-105 — Inquiry commands
- Purpose: AREA, DIST, LIST, and MASSPROP are missing outside the Properties panel (which only reports a circle's area)
- Priority: should
- Type: functional
- Statement: Add the commands above, reusing existing geometry/snap infrastructure; read-only, no undo entry.
- Acceptance (sketch): AREA reports area/perimeter for polylines, rectangles and circles within REQ-101; DIST reports distance and delta X/Y/(Z) between two snapped points; LIST prints an entity's stored properties; MASSPROP reports at least area/perimeter/centroid for a closed region.
- Owner-layer: Commands/UI
- Status: proposed
- Revisions: 2026-08-23 — catalogued (D-2026-08-23-i)

### REQ-106 — View-management commands
- Purpose: no view-stack undo, no named views, no isometric presets beyond the ViewCube's standard faces
- Priority: could
- Type: functional
- Statement: Add ZOOM PREVIOUS (pan/zoom/orbit history), named views (save/restore a camera state by name), a VIEW command/dialog to manage them, and one-click NE/NW/SE/SW isometric presets.
- Acceptance (sketch): ZOOM PREVIOUS steps back through recent view changes; a named view restores camera position/target/UCS exactly; isometric presets set the standard 3D-isometric angle in one action. DVIEW and multiple simultaneous model-space viewports are noted as open scope questions, not committed here, given their size.
- Owner-layer: UI/Renderer
- Status: proposed
- Revisions: 2026-08-23 — catalogued (D-2026-08-23-i)

### REQ-107 — Block support (foundational)
- Purpose: GoSurvey has no block/insert mechanism, which blocks title-block reuse, standard symbols, and any future TABLE/annotation work; DWG export always explodes geometry for exactly this reason
- Priority: should
- Type: functional
- Statement: Add BLOCK (define from selection), INSERT (place with position/scale/rotation), WBLOCK (write to its own file), and ATTDEF/block attributes. Dynamic blocks and a block-library browser are explicitly out of scope — see roadmap Someday.
- Acceptance (sketch): a block definition stores its entities once; each INSERT is a lightweight reference, not a geometry copy; editing a definition updates every insert; DWG/DXF export writes real INSERT/BLOCK records; erasing a definition with live inserts is handled per REQ-201, never silently.
- Acceptance (block editor — BEDIT in-place isolated editing, D-2026-08-29-h / ADR-043):
  - BEDIT with a block name from model space enters an **edit session** for that definition; BEDIT
    is refused while a paper layout is active; a second BEDIT for the block already open is a no-op.
  - While a session is open the viewport shows **only that block's geometry** in the block's local
    coordinates; model-space and paper-space entities are not drawn and not pickable/snappable;
    the block's own INSERT overlays are not drawn.
  - Draw commands (LINE, PLINE, CIRCLE, ARC, ELLIPSE, TEXT/MTEXT) and modify commands (MOVE, COPY,
    ROTATE, SCALE, DELETE, TRIM, OFFSET, MIRROR) operate on the block's content; survey-point and
    CSV tools are unavailable in the session.
  - Any content change marks the session dirty.
  - `BCLOSE` (or the ribbon Close Block Editor) with a dirty session raises a modal **Save /
    Don't Save / Cancel**: Save writes the edited geometry into the definition and every INSERT of
    that block re-renders; Don't Save restores the definition as it was at BEDIT; Cancel keeps the
    session open. A clean session closes with no prompt.
  - On close (Save or Don't Save) the ribbon tab and the viewport camera that were active when
    BEDIT was invoked are restored.
  - Nested blocks, meshes, attribute definitions, parameters and actions on the definition are
    preserved unchanged across an edit session.
- Owner-layer: Domain/Commands/IO/UI — architectural; block entity model recorded across the
  issue-#124 work, the in-place editor recorded as ADR-043
- Status: accepted
- Revisions: 2026-08-23 — catalogued (D-2026-08-23-i).
  2026-08-29 — accepted for the block-editor slice (D-2026-08-29-h, ADR-043): in-place isolated
  editing via a model-store swap, with a Save/Don't-Save/Cancel close gate. Dynamic blocks and a
  block-library browser remain out of scope (roadmap Someday).

### REQ-108 — Polar and tracking input aids
- Purpose: the POLAR status-bar toggle lights up with no behavior behind it, there is no object-snap tracking, and there's no typed polar-coordinate entry
- Priority: should
- Type: functional
- Statement: (a) POLAR shows angle guide lines from the last point at a configured increment and snaps the typed/dragged distance to that angle; (b) object-snap tracking lets a hovered snap point become a temporary tracking reference to move along; (c) `@distance<angle` is accepted anywhere a relative point can be typed, alongside the existing bearing-lock (`A <angle>` then distance) workflow.
- Acceptance (sketch): polar guides snap the cursor within a small pixel tolerance at the configured increment; tracking references clear when a command ends; `@100<45` and the equivalent bearing-lock sequence commit the identical point within REQ-101.
- Owner-layer: UI/Commands
- Status: proposed
- Revisions: 2026-08-23 — catalogued (D-2026-08-23-i)

### REQ-109 — Lit shading for TIN surfaces
- Purpose: `SHADED` and `HIDDEN` render a TIN surface as wireframe only, pixel-identical to `2D` — verified by capture — while imported meshes and hatches already shade correctly under REQ-064
- Priority: should
- Type: functional
- Statement: Extend REQ-064's visual-style/lighting pipeline (triangle shader, depth buffer, camera-following light) to TIN surfaces.
- Acceptance (sketch): a TIN surface captured at `2D`/`Hidden`/`Shaded` is no longer pixel-identical across styles; `Hidden` occludes correctly; `Shaded` lighting follows the camera per REQ-064's existing rule; REQ-100's surface frame-budget profile still holds with shading on.
- Owner-layer: Renderer
- Status: proposed
- Revisions: 2026-08-23 — catalogued (D-2026-08-23-i)

### REQ-110 — Annotative rescale of existing text
- Purpose: changing plot/viewport scale repositions survey-point labels but never resizes already-placed TEXT/MTEXT, unlike AutoCAD's annotative objects
- Priority: could
- Type: functional
- Statement: Existing text/MTEXT can optionally be marked annotative so a plot-scale or viewport-scale change resizes it to hold a constant plotted height, matching what REQ-050 already does for MTEXT drawn live through a viewport.
- Acceptance (sketch): a non-annotative object's height is unchanged by a scale change (today's behavior, preserved); an annotative object's on-screen size changes to hold plotted height constant; the flag persists in DXF/`.gs`; REQ-101 fidelity on stored coordinates is untouched.
- Owner-layer: Domain/UI/Renderer
- Status: proposed
- Revisions: 2026-08-23 — catalogued (D-2026-08-23-i)

### REQ-111 — Associative DIMENSION entity
- Purpose: GoSurvey has no dimension object; an "aligned dimension" exports as exploded lines plus text, which is not associative and doesn't round-trip as a dimension
- Priority: could
- Type: functional
- Statement: Add a DIMENSION entity (at minimum linear/aligned) that stores its definition points, updates its displayed measurement when the dimensioned geometry moves, and round-trips as a real DXF `DIMENSION`.
- Acceptance (sketch): dragging dimensioned geometry updates the displayed value and leader within REQ-101; DXF round-trip preserves it as a `DIMENSION`, not exploded geometry; erasing the dimensioned geometry is handled per REQ-201, not silently.
- Owner-layer: Domain/Commands/IO/Renderer
- Status: proposed
- Revisions: 2026-08-23 — catalogued (D-2026-08-23-i)

### REQ-112 — Binary DXF import
- Purpose: only ASCII DXF is read; a binary DXF must be round-tripped through AutoCAD's Save As first
- Priority: could
- Type: functional
- Statement: `DxfIo` detects and reads binary-encoded DXF (the `AutoCAD Binary DXF` sentinel header) alongside the existing ASCII parser.
- Acceptance (sketch): a binary DXF and its ASCII Save-As of the same drawing import to identical GoSurvey state; a malformed/truncated binary DXF is rejected per REQ-001, not partially absorbed.
- Owner-layer: IO
- Status: proposed — **subsumed by REQ-170** when LibreDWG’s DXF path is verified (D-2026-08-29-g);
  do not implement a second binary-DXF parser beside LibreDWG
- Revisions: 2026-08-23 — catalogued (D-2026-08-23-i). 2026-08-29 — File Format Specs: implementation
  belongs to REQ-170, not a parallel `DxfIo` branch.

### REQ-113 — DXF paper-space import
- Purpose: since REQ-037 gave GoSurvey native paper-space geometry, an imported DXF's paper-space entities and title block have somewhere real to go, but import still discards them and only logs a count
- Priority: could
- Type: functional
- Statement: DXF import reconstructs each paper-space layout's entities into GoSurvey's native `PaperLayout` store (REQ-037/ADR-009), the same way model-space entities and REQ-023 survey points already reconstruct.
- Acceptance (sketch): importing a DXF with a title block and paper-space annotations recreates them as editable native paper-space entities on the matching layout tab; entity types with no paper-space import branch yet are named in the log, not silently dropped (REQ-201).
- Owner-layer: IO
- Status: proposed
- Revisions: 2026-08-23 — catalogued (D-2026-08-23-i)

### REQ-114 — Autosave, backup, and crash recovery
- Purpose: there is no safety net between manual `Ctrl+S` saves; a crash or accidental close loses unsaved work
- Priority: should
- Type: functional
- Statement: Periodically autosave the working drawing to a recovery location at a configurable interval, keep the previous save as a `.bak`, and on next launch after an unclean shutdown offer to recover the autosaved state.
- Acceptance (sketch): autosave fires at the configured interval with no modal/perceptible hitch; a normal `Ctrl+S` still writes the real file and rotates the `.bak`; killing the process mid-session and relaunching offers recovery of the newer state; declining recovery leaves the last manual save untouched.
- Owner-layer: IO/UI/Platform
- Status: proposed
- Revisions: 2026-08-23 — catalogued (D-2026-08-23-i)

### REQ-115 — Recent files list
- Purpose: File → Open is the only way back into a recently used drawing
- Priority: could
- Type: functional
- Statement: The File menu lists the N most recently opened/saved `.gs`/DXF/DWG paths, persisted in user preferences.
- Acceptance (sketch): opening or saving a file adds/moves it to the top of the list; the list persists across restarts; a since-moved/deleted path fails gracefully per REQ-201 rather than crashing; clearing empties it.
- Owner-layer: UI/Platform
- Status: proposed
- Revisions: 2026-08-23 — catalogued (D-2026-08-23-i)

### REQ-116 — Customizable keyboard shortcuts and command aliases
- Purpose: keyboard shortcuts and command aliases are fixed; only the right-click shortcut menu is user-customizable today (REQ-084)
- Priority: could
- Type: functional
- Statement: Extend REQ-084's customization precedent to keyboard accelerators and typed-command aliases, persisted in user preferences.
- Acceptance (sketch): a user can rebind a shortcut and define/edit a typed alias; a conflicting rebind is flagged, not silently overwritten (REQ-201); a reset action restores defaults; unrebound shortcuts keep working.
- Owner-layer: UI/Platform
- Status: proposed
- Revisions: 2026-08-23 — catalogued (D-2026-08-23-i)

### REQ-117 — Real snap-to-grid
- Purpose: the grid is a visual reference only; the cursor never snaps to it
- Priority: could
- Type: functional
- Statement: When grid snap is enabled (mirroring AutoCAD's SNAP/GRID pairing), point entry and dragging snap to the nearest grid intersection at the current spacing, composable with object snaps the way ORTHO already composes with them.
- Acceptance (sketch): with grid snap on and no nearby object-snap candidate, a click lands exactly on the nearest grid intersection; an active object snap still takes priority; toggling grid snap off restores today's free-cursor behavior; the setting persists like other display preferences (REQ-020-style).
- Owner-layer: UI/Commands
- Status: proposed
- Revisions: 2026-08-23 — catalogued (D-2026-08-23-i)

### REQ-118 — A polyline closes by clicking its start, and ends on Enter
- Purpose: finishing a polyline requires typing `CLOSE` or `END`. No established CAD requires
  either, so the most common drawing operation in the program is the one that makes the user stop
  and recall command syntax
- Priority: should
- Type: functional
- Statement: While a `POLYLINE` (or `3DPOLY`) draft is active, its **starting vertex is an Endpoint
  object-snap candidate**, so the cursor lands on it exactly and the existing snap marker shows it.
  Committing a point that coincides with that starting vertex **closes** the polyline, writes the
  closing segment, and ends the command — with no `CLOSE` keyword. A bare **Enter** finishes the
  draft as an **open** polyline, adds no closing segment, and ends the command.

  `CLOSE`/`CL` and `END` remain accepted; they are no longer *required*. The minimum-vertex rules
  are unchanged (three to close, two to end open), and below three vertices the starting vertex is
  **not offered as a snap candidate** — so the invalid close cannot be attempted, rather than being
  refused after the fact.

  Close detection is by **coincidence with the stored first vertex**, not by asking the snap system
  what the user meant. The snap makes the point reachable; it does not decide the command. Anything
  that lands on the first vertex — snap, typed coordinate, or an exact click — closes the polyline.

  Enter **ends the command** rather than restarting it. This differs deliberately from `LINE`, whose
  blank Enter starts a fresh chain: a polyline is one object and finishing it is finishing the
  command, where LINE's chain is a run of independent segments.
- Acceptance:
  - four picks, the fourth on the starting vertex, produce **one closed** polyline and the command ends;
  - three picks then Enter produce **one open** polyline, with no closing segment, and the command ends;
  - with only two vertices a pick on the starting vertex does **not** close — it is an ordinary
    vertex, and the starting vertex offers no snap candidate at that point;
  - `CLOSE` and `END` still work and still report through REQ-201;
  - snapping to all other geometry is unaffected while a draft is active;
  - Esc mid-draft leaves the polyline count unchanged in both spaces;
  - `3DPOLY` behaves identically, each vertex keeping its own elevation (REQ-085);
  - all of the above hold in **paper space** as well as model space (REQ-039 (5)/(6)).
- Owner-layer: Commands (the state machine), Viewport (the snap candidate)
- Status: accepted (2026-08-25)
- Revisions: 2026-08-25 — accepted (relabeled D-2026-08-25-l during the master→beta merge for
  REQ-304/issue #82 — `D-2026-08-25-j` collided with master's independent REQ-303, see REQ-303's
  duplication note above); issue #80

### REQ-119 — Command variants are clickable wherever they are prompted (GitHub issue #81)
- Purpose: the command line tells the user which keyword options a command accepts, but only one
  prompt in the entire program renders them as anything the mouse can reach. A user who does not
  already know the shortcut has no way to discover it except by reading prose and guessing what to
  type — which is the opposite of what a prompt is for
- Priority: should
- Type: functional
- Statement: A **command variant** is a keyword option a command accepts at its current prompt
  (`Azimuth`, `3P`, `Reference`, `Copy`, `Close`, `DElta`). Every variant a prompt names is
  **both** typeable and **clickable**, and the two paths are the same path: a click submits the
  variant's shortcut through `ProcessCommandLineSubmit` — the identical entry point Enter uses on
  typed text — so keyboard and mouse cannot drift apart by construction.

  The mechanism is a **text convention, not a data structure**. A variant is declared by writing
  it into the prompt string in the form the codebase already uses, and the renderer derives the
  clickable region and the submitted token from that text. Two forms are recognized:

  - **Inline** — `[A]zimuth`, `[2P]`, `[3P]`: the bracketed run is the shortcut and the link label;
    any trailing lowercase continues as plain text.
  - **Grouped** — `[DElta/Percent/Total/DYnamic]`, `[Y]es/[N]o`: each `/`-separated option becomes
    its **own** link, and each option's shortcut is its **leading run of uppercase letters and
    digits** (`DElta`→`DE`, `Percent`→`P`, `DYnamic`→`DY`, `Yes`→`Y`). Separators and brackets
    render as plain text.

  This convention is chosen deliberately over a declared `{display, shortcut, action}` table: the
  capitalization rule is already how this codebase writes these prompts, so the convention reads
  the intent that is there rather than adding an abstraction with no second present-day use
  (CLAUDE.md rule 2). The cost is accepted and named: the shortcut is *implied* by the prompt text
  rather than declared beside the handler, so a prompt may name a token the command does not
  accept. Acceptance therefore requires that every marked-up token be **verified against the
  command's own text handler**, and that verification is part of the work, not a later audit.

  **One renderer serves every surface.** The floating command bar and the classic docked panel
  render variants through the **same** function; no command implements click handling of its own.
  The renderer is **wrap-aware** — it breaks between segments when the next one will not fit the
  content region — because the docked panel's prompts are long and wrap today.

  **A live prompt is clickable; history is not.** There are **three** places a prompt string can
  come from, and only two of them are prompts:

  | Surface | Role | Rendered |
  |---|---|---|
  | `CommandInputHint` (UI) | the live prompt | **clickable** |
  | `*FooterHint` (Commands) | the live prompt | **clickable** |
  | `log.push_back` | history | **plain text — never clickable** |

  The command log is a record of what already happened. A prompt that has scrolled into it is no
  longer live, and making it clickable would let a user submit a token to a command that has since
  moved on. So a command whose options are announced **only** in the log has no clickable variants
  by construction — the fix is to give that command a **live prompt entry**, not to make the log
  interactive.

  **REQ-304 has already closed the gap this rule was written for, and in doing so made the
  defect worse.** When REQ-119 was first drafted, `FILLET` (`[Radius/Trim]`, `[Trim/No trim]`),
  `CHAMFER` (`[Distance/Angle/Trim]`, `[Trim/No trim]`) and `ELEV` (`W`orld) announced their
  options **only** in the log, and eight commands had no live prompt at all. REQ-304 (issue #82)
  audited the same `Kind` enum, reached the same conclusion about `Pan`/`Orbit`, and gave all
  eight a live prompt through `DrawingExtrasFooterHint`. Authoring those prompts is therefore
  **no longer part of this requirement** — it is done.

  The consequence is that five grouped-variant prompts are now on the **live, clickable** path
  while the renderer still reads only to the first `]`, so each renders as one link submitting a
  token its own command rejects:

  ```
  FILLET: Select first object or [Radius/Trim] …          -> "radius/trim"
  FILLET: Trim mode [Trim/No trim] …                      -> "trim/no trim"
  CHAMFER: Select first object or [Distance/Angle/Trim] … -> "distance/angle/trim"   (two variants)
  CHAMFER: Trim mode [Trim/No trim] …                     -> "trim/no trim"
  ```

  Together with `MIRROR` and `LENGTHEN` that is **seven** dead links, not two — which is why
  increment 1 is the parser and not the markup: fixing the reader repairs all seven at once, and
  every one of them is already written in the notation this rule reads.

  The live-vs-history split stands as the governing rule regardless: a command that announces
  options only in the log still has no clickable variants, and the fix for that is always a live
  prompt, never an interactive log. A command that genuinely has no variants needs no markup, but
  it must be **audited and recorded as having none**, not silently skipped.

  Clickable variants are visually distinct from surrounding prompt text, carry a hover state, and
  do not interfere with coordinate or text entry at the same prompt.
- Sequencing: **two increments.**
  - **Increment 1 — the mechanism.** Grouped-form and shortcut-extraction parsing; wrap-aware
    layout; the docked panel routed through the shared renderer; the hand-rolled LINE-only link
    block deleted; the parsing rule extracted as a **pure function** and unit-tested. LINE's
    `[A]`/`[2P]` and the two currently-defective grouped prompts are correct at the end of this
    increment.
  - **Increment 2 — the coverage audit.** Command-by-command normalization of the remaining prompt
    strings across both hint families, each token verified against its handler, with headless
    transcript coverage per command. Deliberately left unscoped until reached. It is a pure
    markup pass: REQ-304 already authored the live prompts that were missing, so nothing here
    writes a prompt that does not exist — it only teaches existing ones to say `[Radius/Trim]`
    where they currently say `type R (Radius) or T (Trim)`.
- Acceptance:
  - **Increment 1:**
    - clicking `[A]` is indistinguishable from typing `a`, and `[2P]` from typing `2p`, in **both**
      the floating bar and the docked panel — same resulting command state, same log lines;
    - `Erase source objects? [Yes/No] <N>:` renders **two** links; clicking `Yes` erases the
      source objects and clicking `No` does not. Today it renders **one** link that submits
      `yes/no`, which the command rejects;
    - `FILLET: Select first object or [Radius/Trim] …` renders **two** links submitting `r` and
      `t`, and `CHAMFER: Select first object or [Distance/Angle/Trim] …` **three** submitting
      `d`, `a`, `t` — the tokens `HandleFilletText`/`HandleChamferText` accept. These reached the
      clickable path with REQ-304 and are dead links until this increment lands;
    - `LENGTHEN — select object, or [DElta/Percent/Total/DYnamic]:` renders **four** links
      submitting `de`, `p`, `t`, `dy` — each the token `TryLengthenModeToggle` accepts. Today it
      renders one link submitting `delta/percent/total/dynamic`, which the command rejects;
    - a prompt whose text wraps in the docked panel still renders every link on the correct line,
      with no horizontal overflow;
    - no command contains click-handling code of its own;
    - the prompt→variants rule is a pure function covered by `CommandLineTests`, including: inline,
      grouped, mixed-case shortcut extraction, a bracket with no closing `]`, and an empty group.
  - **Increment 2:** every variant a **live prompt** names is clickable; every clickable token is
    accepted by that command's text handler in that state; no variant loses its keyboard path;
    every `AppCommandState::Kind` is either marked up or recorded as having no variants (none is
    silently skipped); and **no log line is clickable**.
- Owner-layer: UI (the renderer and the prompt text); Commands (the `*FooterHint` prompt strings
  and the token handlers the audit verifies against)
- Status: accepted (2026-08-25)
- Revisions: 2026-08-25 — accepted (D-2026-08-25-o, relabeled from this stack's own D-2026-08-25-m
  while merging into `beta` — that letter was already taken by REQ-305/ARRAY); issue #81.
  2026-08-25 — amended (D-2026-08-25-p, relabeled from D-2026-08-25-n for the same reason). Two
  things, one review and one collision. The
  Verification review of TASK-111's plan found a **third prompt surface** the original text did
  not account for — the log — and added the live-vs-history rule. Rebasing onto `beta` then found
  **REQ-304 had already authored** the live prompts for the eight commands that lacked them, so
  that half of the amendment was dropped as done, and increment 2 shrank back to a pure markup
  pass. REQ-304 also moved five grouped-variant prompts onto the clickable path, taking the live
  defect from two dead links to **seven** — recorded here because it is now increment 1's
  strongest motivation, not a footnote.


### REQ-120 — Double-tapping the middle mouse button zooms to extents (GitHub issue #88)
- Purpose: framing the whole drawing is the most-repeated view action there is, and today it costs
  a typed `ZOOMEXTENTS`/`ZE`. Every CAD user already has the muscle memory for AutoCAD's
  wheel double-click; GoSurvey binds that gesture to nothing
- Priority: should
- Type: functional
- Statement: **Double-clicking the middle mouse button over the drawing viewport zooms to
  extents**, matching AutoCAD's binding for the same gesture. It reuses the existing zoom-extents
  path — `ZOOMEXTENTS`/`ZE` are unchanged and remain the typed route to the same result.

  **The gesture is transparent.** Unlike the typed command, which refuses while a command is
  running ("finish or cancel the active command first"), the double-click works **mid-command**:
  a user halfway through a `LINE` can reframe and carry on picking points. This is deliberate and
  matches AutoCAD, where view operations are transparent. It changes the *view* only — the active
  command's phase, its picked points and its draft geometry are untouched, because zooming writes
  the camera and nothing else.

  **Space-aware.** What gets framed depends on where the user is, mirroring where middle-drag pan
  already works (REQ-045):

  | Space | Frames |
  |---|---|
  | Model | the model's entity extents (the existing computation) |
  | Floating model space (inside an activated viewport) | the model's entity extents, framed into the VIEWPORT's own rectangle — **REQ-123** |
  | Paper | the **sheet** — `(0,0)` to `sheetWidthIn() × sheetHeightIn()` |

  Paper space frames the sheet rather than the paper entities on it: the page is the meaningful
  extent of a layout, and a layout with no geometry yet must still frame to something. Paper
  geometry drawn **outside** the sheet is therefore not framed by this gesture — a stated
  limitation, not an oversight.

  **Middle-drag pan is untouched.** REQ-045 guarantees it, and a double-click is not a drag; the
  two gestures do not overlap.
- Acceptance:
  - a middle double-click over the model viewport frames the drawing, identically to `ZOOMEXTENTS`;
  - it works **while a command is active**, and the command's state survives it — a `LINE` with one
    point placed still has that point and still expects the next;
  - the typed route still does **not** zoom mid-command, and its behaviour is unchanged: while a
    command is active, typed text is consumed by that command, so `ZOOMEXTENTS` is read as point
    input and refused by it (`"Could not parse point…"`). `StartZoomExtentsCommand`'s own
    "finish or cancel the active command first" guard is not even reached on that path — it
    applies when the text does reach the dispatcher. Either way the transparency is a property of
    the **gesture** alone, and nothing about the typed command is relaxed;
  - in paper space the gesture frames the sheet;
  - in floating model space it frames the model;
  - middle-drag pan still pans, in every space, unchanged;
  - a double-click with nothing to frame reports it and changes no view (REQ-201).
- Owner-layer: UI (the gesture) — the extents computation and the camera write are existing Commands code
- Status: accepted (2026-08-25). This REQ covers only #88's "Middle Mouse"/"Architecture"
  acceptance sections; #88's "ZOOMEXTENTS" section (margin, aspect-ratio, degenerate/empty extents,
  NaN safety) exercised the pre-existing framing path unmodified and untested here (D-2026-08-26-b)
  and is now **REQ-122**, which closes the issue alongside this one. The camera write named here as
  `ApplyViewportZoomToWorldRect` is `zoomframing::FrameWorldRect` since REQ-122
- Corrected 2026-08-26 (D-2026-08-26-e, REQ-123): the floating-model-space claim above was never
  true. The gesture was raised inside a block guarded by `!routeZoomToViewport`, which is skipped
  whenever a floating viewport owns pan/zoom — so a middle double-click through an activated viewport
  did nothing at all, and the typed command wrote the SHEET camera (GitHub issue #100). REQ-123 owns
  that case now; this requirement keeps the gesture and the model/paper branches.
- Revisions: 2026-08-25 — accepted (D-2026-08-25-o, relabeled D-2026-08-26-a while merging PR #93
  into `beta` — that letter was already taken by REQ-119 above); asked for directly by the user,
  from AutoCAD's wheel double-click.

### REQ-121 — Object selection is a visibly distinct mode: no OSNAP, a pickbox cursor, one prompt (GitHub issue #91)
- Purpose: "pick a point" and "pick an object" are different acts, and today they look and behave
  identically. A user in a selection step sees the same crosshair, sees snap markers that mean
  nothing there, and reads a differently-worded prompt in every command. The snapping is not merely
  useless during selection — it is actively misleading, because the cursor visibly jumps to a snap
  point while the hit-test uses somewhere else
- Priority: should
- Type: functional
- Statement: An **object-selection step** is any command phase whose question is *which objects*
  rather than *which point*: the `PickSelection` phase of MOVE, COPY, SCALE, ROTATE, MIRROR, ALIGN,
  ARRAY and STRETCH, and the entity-picking loops of DELETE, JOIN, TRIM, EXTEND, LENGTHEN, BREAK,
  FILLET and CHAMFER.

  **ZOOM is excluded, and #91 lists it.** Saying so explicitly because dropping it silently would
  look like an oversight: ZOOM WINDOW's box picks a **region of the view** to fit, not objects.
  Nothing is selected by it, so "select objects" would be a prompt that lies, and a pickbox cursor
  would say *click a thing* while the user drags a rectangle. Its corners already come from
  unsnapped coordinates, so rule (1) would change nothing there either. The test is what the click
  is *for*, not whether it happens to drag a box.

  **Idle selection — no command running — is deliberately NOT one**, and is untouched by this
  requirement: it keeps today's crosshair and today's OSNAP behaviour. The reason is that the three
  rules below are a *mode signal*, and a mode signal is only meaningful against a default. Idle is
  that default — it is what the user is looking at most of the time — so making it look like a
  selection step would leave the pickbox meaning nothing, and would change the appearance of normal
  use to fix a problem that only exists inside commands. The distinction being drawn is "a command
  is asking me which objects" versus "nothing is running", which is exactly the line this excludes.

  Three rules hold for the whole duration of such a step, and stop holding the moment the phase
  advances.

  **(1) OSNAP has no effect.** The distinction that matters here is that the *hit-test* is already
  mostly correct and the *cursor* is not:

  | | today | required |
  |---|---|---|
  | what the pick hit-tests against | raw unsnapped cursor, for most steps | raw unsnapped cursor, for **every** step |
  | where the cursor is drawn | snapped — it jumps to snap points | raw — it tracks the mouse |
  | snap markers / tooltips | drawn | not drawn |

  So this is only half a behaviour change. `ViewportUseRawWorldForSelectionRectPick` and the
  `RawEntityPick` route already establish "hit-test raw" for selection rectangles and entity picks
  respectively — the `RawEntityPick` comment already gives this requirement's own reasoning, that
  *"an OSNAP-adjusted point would hit-test somewhere the user is not pointing."* What does not
  exist is any suppression of the snap **cursor adjustment or marker display**, so the user watches
  the crosshair leap to an endpoint while the pick correctly ignores it. Making the rule explicit
  is what stops it from being a per-command accident.

  **The rule also closes a live inconsistency it exposes.** `ALIGN` routes its `PickSelection`
  phase to `SelectionAccumulate` alongside its six siblings, but is **absent** from
  `ViewportUseRawWorldForSelectionRectPick` — so ALIGN's selection-box corners come from *snapped*
  coordinates while MOVE/COPY/SCALE/ROTATE/MIRROR/ARRAY's come from raw. That is a defect, found
  while writing this requirement, and it is precisely the "per-command accident" a stated rule
  prevents. A single predicate answering *"is a selection step active?"* is what all three rules
  below consult, so a command cannot be half-included again.

  **"No effect" includes the one-shot OVERRIDE.** Shift+Right-click opens a "snap once — choose
  type" menu, and picking from it forces a snap on the next pick. That is a way of *asking* for the
  behaviour this rule removes, so the menu does not open during a selection step, and an override
  armed just before one began is not spent inside it either (added 2026-08-26, D-2026-08-26-d: the
  first implementation gated only the automatic snap, leaving the rule true for every snap except a
  deliberately forced one).

  **(2) The cursor is a pickbox.** A square of the size the crosshair configuration already carries
  (`pickbox half-size in px`, an existing setting — this is not a new tunable), replacing the
  crosshair for the duration of the step and reverting when it ends. This is AutoCAD's `PICKBOX`
  convention and the visual signal that rules (1) and (3) are in force.

  **(3) One prompt — for the steps that are nothing but a selection.** The wording is
  **"Select objects, ENTER to continue"**, settled once in one shared string and shown in both the
  command line and the dynamic cursor text (REQ-304's surfaces, and REQ-304's rule that the two
  agree). Today those prompts range from "click two corners to window-select objects" to
  "window-select entities, then press Enter" to no Enter hint at all.

  It applies to the steps whose whole content is *pick objects*: **MOVE, COPY, SCALE, ROTATE,
  MIRROR, ALIGN, ARRAY, DELETE, JOIN**.

  **DELETE and JOIN had to earn that prompt, and the behaviour moved rather than the words**
  (2026-08-26, D-2026-08-26-d). Both were fixed two-click-box commands: the box acted the moment it
  closed, and Enter was answered with *"finish window-select in the viewport (two clicks)"*. The
  shared prompt told the user to press a key the command explicitly refused, which made rule (3) a
  sentence rather than a rule. They now take the click-or-box, accumulate-until-Enter shape
  D-2026-08-25-l gave the seven transform commands — that decision excluded only STRETCH, for a
  stated reason, and simply never included these two. Adding a second, box-only prompt was the
  alternative and was declined: it would have made "one prompt, everywhere" mean "one of two
  prompts", to preserve behaviour nobody had chosen.

  **It does NOT replace a prompt that carries a keyword or a type list**, and that limit is
  load-bearing rather than a concession. TRIM's selection prompt offers `type L — draw the trim
  line`; OFFSET's names what is pickable; STRETCH's says `right-to-left = crossing`, which is
  operative because its box direction is data (REQ-103 step 5). Overwriting those with a generic
  phrase would delete the only place each option is discoverable — and REQ-119 exists precisely to
  make such keywords *more* reachable, so this requirement must not quietly undo it. Those steps
  still get rules (1) and (2); only their prompt text is their own.

  Unifying wording is the goal; erasing information is not. Where a step has nothing to say beyond
  "pick objects", it says exactly the same thing as every other such step.
- Acceptance:
  - no snap marker is drawn, and the cursor does not jump to a snap candidate, at any point during
    any object-selection step listed above;
  - the pick that results is hit-tested against the raw cursor for **every** listed step —
    including ALIGN, whose box corners are snapped today;
  - the cursor renders as a pickbox square for the step's duration and reverts to the crosshair
    when the phase advances or the command is cancelled;
  - MOVE, COPY, SCALE, ROTATE, MIRROR, ALIGN, ARRAY, DELETE and JOIN each show the **identical**
    selection prompt in the command line **and** in the dynamic cursor text, sourced from one shared
    string — byte-for-byte the same, not merely equivalent wording;
  - TRIM, OFFSET and STRETCH keep their own prompts, and every keyword they name (`L`, the pickable
    type list, `right-to-left = crossing`) is still present afterwards — a prompt that lost an option
    to this requirement is a **failure** of it, not a tidy-up;
  - a command left out of the treatment is a **build-time or test-time** failure, not something a
    user finds — the single predicate is exhaustive over the phases, on the precedent of
    `ViewportClickRouteFor`'s `default:`-less switch (REQ-103/TASK-099);
  - the accumulate-until-Enter behaviour of REQ-305 is unchanged — this requirement governs the
    step's *appearance and input treatment*, never which objects it collects;
  - STRETCH keeps its crossing box as load-bearing geometry (REQ-103 step 5): it gets the cursor,
    snap and prompt treatment, and its box semantics are untouched;
  - **with no command running, nothing changes at all** — the crosshair is the crosshair, OSNAP
    behaves exactly as it does today, and snap markers still draw. A user who never starts a command
    cannot tell this requirement was implemented, and that is the intended outcome, not a gap;
  - the Shift+Right-click snap-override menu does not open during an object-selection step, and an
    override armed before the step began is not consumed inside it — verifiable as an A/B against a
    control, since the same gesture at the same pixel must still open the menu under a point step;
  - DELETE and JOIN accumulate objects by click **or** box until Enter, and Enter is what acts on
    the selection — a closing box no longer erases or joins, and Enter with nothing selected is a
    stated refusal that leaves the command running (REQ-201).
- Scope boundary — **model space and floating model space only** (stated 2026-08-26,
  D-2026-08-26-d). In PAPER space the modify commands are pick-first: `StartDeleteCommand` and its
  siblings act on an existing paper selection or answer *"select paper object(s) or viewport(s)
  first"* without ever setting `st.active`, so there is no object-selection **step** for the three
  rules to apply to — the selection itself is made idle, which this requirement excludes by decision.
  Paper space therefore keeps the crosshair and the ordinary OSNAP behaviour throughout. That is a
  consequence of two deliberate choices meeting, not an oversight in either; giving paper space the
  treatment means giving its modify commands a real selection phase, which is a behaviour change
  outside this requirement. Filed separately as GitHub issue #106 rather than absorbed here.
- Owner-layer: UI (cursor rendering, marker suppression, prompt surfaces); Commands (the shared
  prompt string, the selection-step predicate, and DELETE/JOIN's accumulate-until-Enter step);
  Viewport (the existing raw-vs-snapped pick paths, and the DELETE/JOIN click route)
- Status: accepted (2026-08-26)
- Revisions: 2026-08-26 — accepted (D-2026-08-26-a); issue #91. Amended 2026-08-26
  (D-2026-08-26-d, TASK-118) after chetjones003's review of PR #102: the one-shot snap-override
  seam added to rule (1), DELETE/JOIN's behaviour corrected so rule (3) is true for them, and the
  paper-space scope boundary stated rather than left to be discovered.

### REQ-307 — Paper-space MOVE/COPY/DELETE gain a real selection step when nothing is pre-selected (GitHub issue #106)
- Purpose: REQ-121 gave model space a visibly distinct "picking objects" mode — no OSNAP, a pickbox
  cursor, one shared prompt — for every object-selection step, but stated paper space as an explicit
  scope boundary rather than an oversight: `StartDeleteCommand`/`StartPaperMoveCopyViewports` are
  **pick-first** (act on whatever idle click/box-select has already selected, or refuse), so there
  was no selection *step* in paper space for REQ-121's three rules to attach to. This requirement is
  what REQ-121's own scope-boundary text names as the fix — "giving paper-space modify commands a
  real selection phase" — for the one case that actually needed it: starting the command with
  **nothing** selected.
- Priority: should
- Type: functional
- Statement: Paper-space MOVE, COPY and DELETE keep pick-first as the fast path — starting one with
  an existing paper-entity or viewport selection acts immediately, exactly as today. Starting one
  with **nothing** selected no longer answers a flat refusal ("select paper object(s) or viewport(s)
  first." / "select object(s) first."); it opens a real selection step instead, with the identical
  input treatment REQ-121 gives its model-space counterpart:

  1. **A click toggles one object (paper entity or viewport) into the accumulating selection, no
     Shift required, and a window/crossing box merges into it rather than replacing it** — the
     paper-space analog of REQ-305's model-space accumulate-until-Enter shape (D-2026-08-25-l). The
     object universe is the same one idle click/box-select already reaches in paper space (REQ-035
     viewports + REQ-037 native geometry); this adds a second entry point onto it, not a new
     eligibility rule.
  2. **Enter is what advances the step** — to MOVE/COPY's base-point phase, or straight to DELETE's
     erase — and Enter with nothing selected is REQ-201's stated refusal ("Nothing selected — click
     objects or drag a selection window, then press Enter."), leaving the step open rather than
     exiting the command.
  3. **OSNAP has no effect and the cursor is a pickbox for the step's duration**, the same two rules
     REQ-121 states for model space, reusing the same predicates (`ViewportIsObjectSelectionStep` is
     model-space-only by its own doc comment, so this adds a paper-space counterpart,
     `PaperIsObjectSelectionStep`, consulted alongside it everywhere REQ-121's rules are drawn —
     pickbox cursor, the pre-existing paper snap-glyph suppression, and the shared prompt).
  4. **The same shared prompt REQ-121 defines** (`kSelectObjectsPrompt`, "Select objects, ENTER to
     continue | ESC cancel") is shown in both the command line and the dynamic cursor text, exactly
     as its model-space counterparts show it — reusing the string rather than declaring a
     paper-space-specific one.

  ESC cancels the step (clearing the new state, leaving any partial selection intact — the same
  behaviour REQ-121's model-space PickSelection cancellation already has, since `CancelActiveCommand`
  never clears `st.selection` either).
- Acceptance:
  - starting DELETE, MOVE or COPY in paper space with an existing selection is byte-identical to
    today — this requirement adds a second path, it does not touch the first;
  - starting DELETE, MOVE or COPY in paper space with nothing selected opens a selection step and
    logs the same wording model-space's own MOVE/COPY/DELETE use for their PickSelection phase
    ("... click objects or drag a selection window (Enter when done) ..."), not the old refusal;
  - a click during the step toggles one object into the selection without Shift, and Shift+click
    removes it — matching REQ-305's model-space accumulate shape;
  - a window/crossing box during the step MERGES into the accumulating selection rather than
    replacing it;
  - Enter with a non-empty selection advances the step (to base-point for MOVE/COPY, or straight to
    the erase for DELETE) and Enter with an empty selection is REQ-201's refusal, leaving the step
    running;
  - no snap marker is drawn and the cursor does not jump to a snap candidate at any point during the
    step (the pre-existing paper-space snap glyph, which is not gated on any command today, is
    suppressed for this step specifically);
  - the cursor renders as a pickbox square for the step's duration and reverts to the ordinary
    crosshair when the step ends (advances or is cancelled);
  - the command-line prompt and the dynamic cursor text agree, both showing REQ-121's own
    `kSelectObjectsPrompt` string, byte-for-byte;
  - ESC cancels the step without crashing or leaving stale internal state that a later MOVE/COPY/
    DELETE in the same session would trip over.
- Scope boundary — **this requirement covers only DELETE, MOVE and COPY**, the only paper-space
  commands that were pick-first before it (REQ-035 viewports, REQ-037 native geometry). Paper space
  has no ROTATE/SCALE/MIRROR/ALIGN/ARRAY equivalent of MOVE/COPY's own pick-first branch to extend —
  those paper-space commands are invoked only from an existing selection today, unchanged by this
  requirement. Extending the same treatment to a future paper-space command is a new decision, not
  an omission of this one.
- Owner-layer: UI (the ambient paper-space click/box/Enter handling in `CadUi.cpp`, the pickbox
  cursor and snap-glyph suppression, the dynamic-cursor palette's engagement gate); Commands
  (`StartPaperMoveCopyViewports`/`StartDeleteCommand`'s new branch, the shared
  `ProcessPaperMoveWaitingSelectionEnter`/`ProcessPaperDeleteWaitingSelectionEnter` functions,
  `PaperIsObjectSelectionStep`)
- Status: accepted (2026-08-26)
- Revisions: 2026-08-26 — accepted (D-2026-08-26-g), TASK-120; GitHub issue #106, split from #91
  during REQ-121's own review.

### REQ-122 — ZOOMEXTENTS frames the drawing safely: margin, aspect, degenerate extents, no invalid camera (GitHub issue #88)
- Purpose: REQ-120 gave the middle double-click its gesture and reused the existing framing path
  untouched, which left the larger half of issue #88 — everything the framing itself promises —
  asserted but never checked (D-2026-08-26-b). Checking it found one guarantee that does not hold:
  a drawing with no extent (a single point, coincident objects, a hair-length line) frames at a
  zoom around 4.6e6, a view a fifth of a thousandth of a unit tall, which is not a view of anything
- Priority: should
- Type: functional
- Statement: **Framing a world rectangle onto the camera is one shared operation with four
  guarantees.** It is the same operation for `ZOOMEXTENTS`/`ZE`, for REQ-120's middle double-click,
  for `ZOOMWINDOW`/`ZW` and for the post-import fit — issue #88's Architecture section requires that
  the command and the gesture cannot disagree, so there is one implementation and no second copy of
  the arithmetic.

  **(1) It fits, centred, with a margin.** The camera centres on the rectangle's midpoint, and the
  binding axis leaves `8%` of the viewport free — half of it on each of that axis's two sides — so
  geometry never touches an edge. Which axis binds is decided by the **viewport's aspect ratio**,
  which is what keeps the other axis un-clipped rather than assuming a square viewport.

  **(2) A degenerate rectangle still frames to something a user can work in.** Below a **minimum
  framed span of one world unit** on either axis, the rectangle is expanded about its own centre to
  that minimum. One unit is 1% of the view the application opens with (`zoom == 1` shows 100 units),
  so a point, a pair of coincident objects, or a drawing measured in thousandths frames near — but
  still tighter than — the default view, instead of at a magnification where the camera's own float
  precision is the largest thing on screen.

  The floor is **shared by `ZOOMWINDOW`**, deliberately and not as a side effect: the same "never
  zoom to an unusable scale" guarantee applies to a window the user drags to nothing, and splitting
  the rule per caller would be the second copy of the arithmetic this requirement exists to prevent.
  Its cost is stated rather than hidden — no view can be framed tighter than one world unit tall.

  **(3) A rectangle that is not finite frames nothing.** A NaN or infinite bound, or a bound pair
  whose difference overflows, is **refused**: the camera is not written at all, so the previous view
  survives intact and no NaN can reach it. The refusal states its reason (REQ-201). This is the only
  way "invalid camera values are never produced" can be guaranteed — a clamp still writes a wrong
  number.

  **(4) Nothing to frame is not a failure.** An empty drawing produces no extents, and the caller
  says so and changes no view — the behaviour REQ-120 already relies on, now stated.

  **What counts as the drawing's extents is unchanged.** `ComputeRobustWorldExtents` and its
  far-outlier rejection keep deciding that, in model and floating model space; paper space frames
  the sheet (REQ-120). This requirement governs the **camera**, never the entity sweep.
- Acceptance:
  - the camera centres on the extents rectangle, and the whole rectangle is inside the visible
    rectangle at any viewport aspect — wide, square or tall;
  - the binding axis leaves exactly 8% of the viewport free and the other axis at least that much,
    so no geometry touches an edge;
  - a single point, coincident objects, a zero-height row and a hair-length line each produce a view
    at least the minimum framed span across, centred on the content — not a magnification at float
    precision;
  - a drawing larger than the minimum span is framed exactly as before: the floor is invisible above
    it;
  - a non-finite bound, or a span that overflows to infinity, writes **no** camera value and leaves
    the current view untouched, with a stated reason;
  - every accepted rectangle produces finite `pan`/`zoom` values, across spans from `1e-9` to `1e12`
    and aspects from `0.05` to `20`;
  - a rectangle given with its corners in either order frames identically (`ZOOMWINDOW`'s corners
    arrive in drag order);
  - typed `ZOOMEXTENTS` and REQ-120's middle double-click produce the **same** camera, because they
    call the same function;
  - middle-drag pan is unchanged (REQ-045), and two middle drags in succession are two pans, not a
    double-click.
- Owner-layer: Commands (the framing arithmetic and the callers that consume it). No UI change —
  REQ-120 already owns the gesture
- Status: accepted (2026-08-26); closes the remainder of GitHub issue #88 alongside REQ-120
- Revisions: 2026-08-26 — accepted (D-2026-08-26-c); raised by chetjones003 on issue #88 after PR #93
  merged, asking for #88's ZOOMEXTENTS acceptance list to be verified rather than assumed.

### REQ-123 — ZOOM EXTENTS through an activated viewport frames the model into that viewport (GitHub issue #100)
- Purpose: a floating viewport is the model-space window the user is actually working in, and
  zoom-extents was the one navigation operation that did not know it. It wrote the sheet camera and
  left the viewport's framing untouched, so the layout zoomed around a viewport that never moved
- Priority: should
- Type: functional
- Statement: **While a paper-space viewport is activated (floating model space) and the viewport zoom
  lock is OFF, `ZOOMEXTENTS` — typed, or by REQ-120's middle double-click — frames the model into
  that viewport.** It writes the viewport's own `modelCenterX/Y` and `scaleModelPerPaperIn`, and
  writes **no** screen camera: the viewport keeps its size and position on the sheet, and the sheet's
  own pan/zoom is untouched.

  **The viewport's aspect is its rectangle on the sheet**, `paperWIn : paperHIn` — not the
  application window's. That single substitution is the defect: the same drawing framed with the
  window's aspect over-fills one axis of the viewport and leaves the other empty.

  **It is the same framing operation as everywhere else.** REQ-122's `FrameWorldRect` decides the
  centre, the margin, which axis binds, the minimum span and the refusal on a non-finite rectangle;
  this converts that answer into the viewport's units (model units per paper inch). Issue #88's
  Architecture section requires one framing implementation, and this does not add a second.

  **The extents are the model seen THROUGH that viewport.** An entity whose layer is frozen in the
  viewport (REQ-028 / REQ-046) is not part of what the user is looking at, so it does not drag the
  framing out to reach it — the same test the viewport renderer and the plotter already apply. The
  filter is on **visibility**, not on entity kind: the viewport renderer currently draws only lines,
  polylines, circles, arcs and survey points, and that is a renderer limitation, not a statement
  about what a drawing contains. Encoding it here would freeze a gap into the extents math.

  **The zoom lock decides which view is being navigated.** `viewportZoomLocked` already means
  "pan/zoom targets the sheet"; zoom-extents is a zoom, so with the lock **ON** a floating viewport
  frames the **sheet**, exactly as paper space does. Only the lock-OFF case — the default, and the
  one where the wheel and middle-drag already target the viewport — frames into the viewport.

  **REQ-120's floating-model-space claim is corrected here.** It stated that the middle double-click
  frames the model in floating model space. It did not: the gesture was raised inside a block guarded
  by `!routeZoomToViewport`, which is skipped whenever a floating viewport owns pan/zoom, so it never
  fired there at all. The flag is now raised in every space, and this requirement decides what
  "extents" means for each.
- Acceptance:
  - with a viewport activated and the lock off, `ZOOMEXTENTS` centres that viewport on the model
    extents and sets its scale so they fit its rectangle, with REQ-122's margin;
  - the viewport's position and size on the sheet are unchanged, and the sheet's own pan/zoom is
    unchanged — the operation writes nothing outside the viewport;
  - the framing is computed from the viewport's own aspect, so the **same drawing in two viewports of
    different shapes gets two different scales**;
  - the sheet, the viewport border and paper-space geometry are not in the calculation;
  - an entity on a layer frozen in that viewport does not affect the result, and the same entity in a
    viewport where its layer is thawed does;
  - REQ-120's middle double-click produces the same result as the typed command, in a viewport as in
    model space;
  - middle-drag pan continues to move the model within the viewport and nothing else (REQ-045);
  - with **no** viewport activated, paper space frames the sheet exactly as REQ-120 specifies;
  - with the zoom lock ON, a floating viewport frames the sheet;
  - it is covered by a **transcript**, not only by a manual pass — see Owner-layer.
- Owner-layer: Commands (the framing and the extents filter); UI (raising REQ-120's gesture in every
  space). Notably **not** blocked by TASK-113's DEBT-1: the viewport case needs no framebuffer,
  because its aspect comes from paper inches and its framing is stored on the viewport, so it is
  handled ahead of `ProcessPendingViewportZoom`'s `fbW <= 0` guard and is the first zoom behaviour a
  headless transcript can drive end to end
- Status: accepted (2026-08-26); closes GitHub issue #100
- Revisions: 2026-08-26 — accepted (D-2026-08-26-e); reported by chetjones003 as issue #100.

### REQ-124 — Empty named TIN surface (GitHub issue #119)
- Purpose: let the user create the surface object first and add data afterwards, matching Civil 3D
- Priority: must
- Type: functional
- Statement: `SURFACECREATE <name>` with no point groups, and the Surface Manager's New Surface
  action, create a named drawing-owned surface whose triangulation is **null**. Duplicate names are
  refused (REQ-075). Adding sources later rebuilds as REQ-069. A create that *names* groups which
  cannot triangulate still **creates the object** and reports why there is no TIN (REQ-201) — it
  does not leave a bogus triangle set. Hover, SURFELEV, OSNAP and zoom-extents skip a null TIN.
- Acceptance:
  - `SURFACECREATE Empty` adds one surface; `SURFACELIST` reports it as not built; triangle count 0;
  - creating a surface from groups that resolve to fewer than three non-collinear points still adds
    the named surface, logs a specific message, and leaves `tin` null;
  - the empty surface round-trips `.gs` (name, empty definition, no verts/indices);
  - `SURFELEV` on a drawing that contains only an empty surface reports outside / no elevation, and
    does not crash.
- Owner-layer: Domain, Commands, UI, IO
- Status: accepted (2026-08-27)
- Revisions: 2026-08-27 — initial (D-2026-08-27-a).

### REQ-125 — Surface statistics
- Purpose: the numbers a surveyor reads off a surface without running a volume comparison
- Priority: should
- Type: functional
- Statement: A pure `util/surfacestats` module reports, from a triangulation: point count, triangle
  count, plan extents (min/max easting and northing), elevation min/max, 2D area (sum of triangle
  plan areas), 3D area (sum of triangle face areas), and slope min / max / mean (percent grade of
  each triangle's plane, area-weighted for the mean, excluding degenerates). `SURFACESTATS [<name>]`
  prints them; omit the name to list every surface. An empty / null TIN reports zeros and says it is
  not built. Statistics are **not persisted**.
- Acceptance:
  - a 100×100 square planar pad at z=10 reports 2D area 10,000 and 3D area 10,000 within REQ-101;
  - a 100×100 pad at 100% grade (rise=run) reports 3D area 100×100×√2 within REQ-101;
  - a null TIN reports not-built rather than inventing numbers;
  - `SURFACESTATS` names a missing surface rather than printing another surface's figures.
- Owner-layer: util, Commands, UI
- Status: accepted (2026-08-27)
- Revisions: 2026-08-27 — initial (D-2026-08-27-a).

### REQ-126 — Indexed surface elevation queries
- Purpose: SURFELEV, rollover, OSNAP and volumes must not scan every triangle on a REQ-100 surface
- Priority: must
- Type: performance / functional
- Statement: Elevation at XY uses `TinElevationAtIndexed` through a live-only spatial index cached on
  `AppCommandState` (ADR-039 (c)). The index is rebuilt when the TIN pointer changes. Indexed and
  full-scan answers agree, including misses, concave notches, and hide-boundary voids. Large
  coordinates (state-plane magnitude) stay within REQ-101 of the triangle plane.
- Acceptance:
  - for a committed fixture, indexed and scan elevations match within REQ-101 at interior samples
    and both miss the same exterior / notch / void samples;
  - a query against a null TIN is a miss;
  - SURFELEV and REQ-089 rollover use the indexed path (one walk, as today).
- Owner-layer: util (`tinbuild` / `surfacevolume` index), Commands (cache)
- Status: accepted (2026-08-27)
- Revisions: 2026-08-27 — initial (D-2026-08-27-a).

### REQ-127 — Surface elevation object snap
- Purpose: pick a point *on the ground* while drawing, not only on triangle vertices
- Priority: should
- Type: functional
- Statement: A new object-snap kind interpolates the covering visible surface's triangle plane at the
  cursor's plan position and returns XYZ. If several surfaces cover the point, the **topmost in the
  drawing's surface list** wins (last-created if appended) — stated, not guessed. A miss, a null TIN,
  or an invisible surface produces no snap. Running OSNAP has an independent toggle, default **on**.
- Acceptance:
  - on the REQ-074 test plane, a snap at a known interior XY returns that plane's Z within REQ-101;
  - a cursor outside every surface produces no surface snap;
  - with the toggle off, no surface snap is offered.
- Owner-layer: Viewport (CadSnap), Commands, UI
- Status: accepted (2026-08-27)
- Revisions: 2026-08-27 — initial (D-2026-08-27-a).

### REQ-128 — Data-clip surface boundary
- Purpose: keep shots *outside* a site from pulling the TIN, which outer-cull after the fact cannot
- Priority: must
- Type: functional
- Statement: `CadBoundaryKind::Clip` / `TinBoundaryKind::Clip`. If a surface has one or more clip
  rings, an input point is used **only if it lies inside at least one clip** (union). Clip rings are
  constrained edges. After the build, triangles whose centroids fall outside every clip are culled
  (same centroid rule as Outer). Hide/show still apply in definition order among themselves. No clip
  present means "do not filter points". `DESIGNATEBOUNDARY` accepts CLIP. Legacy `.gs` without the
  kind string still loads as outer/hide/show.
- Acceptance:
  - points outside a clip do not appear as TIN vertices; a point inside does;
  - two clips union: a point inside either is used;
  - a clip round-trips `.gs` as `"clip"`;
  - an unclosed polyline is refused as a clip (same as other boundary kinds).
- Owner-layer: util (tinbuild), Domain, Commands, IO
- Status: accepted (2026-08-27)
- Revisions: 2026-08-27 — initial (D-2026-08-27-a).

### REQ-129 — Contour geometry as a surface data source
- Purpose: build or densify a surface from existing contour polylines without treating them as
  ordinary breaklines in the Manager tree
- Priority: should
- Type: functional
- Statement: A surface definition may list **contour sources** by stable entity id (line, polyline,
  3D polyline, feature line). Each vertex and each segment is a triangulation constraint at the
  entity's stored Z. They rebuild dynamically like breaklines (REQ-069). Display contours remain
  style-generated (REQ-070); this is input, not EXTRACT. `DESIGNATECONTOUR` / `UNDESIGNATE … CONTOUR`
  and a Surface Manager Contours node. Additive `.gs` array, omitted when empty.
- Acceptance:
  - a closed 3D polyline at z=100 around a pad forces TIN edges along it at z=100;
  - deleting the polyline drops it from the definition and rebuilds;
  - a drawing without the array loads unchanged.
- Owner-layer: Domain, Commands, UI, IO
- Status: accepted (2026-08-27)
- Revisions: 2026-08-27 — initial (D-2026-08-27-a).

### REQ-130 — Direction / aspect banding
- Purpose: colour triangles by downhill azimuth — drainage aspect, not just grade
- Priority: should
- Type: functional
- Statement: `SurfaceAnalysisMode::Direction` uses the style's existing band table in **degrees**.
  Aspect is downhill azimuth: 0 = +Y (northing), increasing toward +X (easting), in [0, 360). A
  flat or degenerate triangle (REQ-072's flat-grade test) is unbanded, not assigned an arbitrary
  compass. `.gs` stores mode 3. A pre-REQ-130 file with mode 0/1/2 is unchanged.
- Acceptance:
  - a plane that falls due east (+X) bands into the range that contains 90°;
  - a plane that falls due north (+Y) bands into the range that contains 0°;
  - a flat triangle is unbanded;
  - switching mode to None restores the plain style display.
- Owner-layer: util (surfaceanalysis), Renderer, UI, IO
- Status: accepted (2026-08-27)
- Revisions: 2026-08-27 — initial (D-2026-08-27-a).

### REQ-131 — Bounded volumes
- Purpose: earthwork inside a site boundary, not the whole overlapping hull
- Priority: must
- Type: functional
- Statement: Volume comparison (REQ-073) may be limited to a closed polyline in plan. Sample cells
  whose centres fall outside the ring contribute neither volume nor common area. `VOLUMES <base>,
  <comparison>[, <clip entity>]` and a dashboard clip picker. No new surface type. Analytical check:
  two planar surfaces 5 ft apart over a 1-acre clip report 21,780 ft³ (806.67 yd³) cut or fill
  according to which is higher, within a stated relative tolerance of 1%.
- Acceptance:
  - the 5 ft × 1 acre fixture matches 21,780 ft³ within 1%;
  - a clip that misses both surfaces reports zero and says there is no overlap inside the clip;
  - omitting the clip preserves today's full-overlap behaviour.
- Owner-layer: util (surfacevolume), Commands, UI
- Status: accepted (2026-08-27)
- Revisions: 2026-08-27 — initial (D-2026-08-27-a). **Phase 2 — not in the first implementation
  increment.**

### REQ-132 — Watershed analysis
- Purpose: name the drainage basins on a TIN
- Priority: must
- Type: functional
- Statement: A pure `util/watershed` module, given a TIN, produces drain targets (boundary, internal
  depression, or flat) and a per-triangle basin id, plus each basin's plan area. Display is
  style-generated cache geometry, not entities, not stored in `.gs`. `WATERSHED <surface>` reports
  counts; the Surface Manager can inspect a basin. Algorithm and termination rules (flats, pits)
  are specified in the task that implements this REQ, with synthetic fixtures: single basin, two
  basins, ridge, saddle, boundary drain, internal depression.
- Acceptance:
  - the synthetic single-basin fixture yields one basin draining to the designed target;
  - the two-basin / ridge fixture yields two basins that do not cross the ridge;
  - an internal depression is classified as such, not silently merged into a neighbour;
  - a null TIN is refused with a specific message.
- Owner-layer: util, Commands, Renderer, UI
- Status: accepted (2026-08-27)
- Revisions: 2026-08-27 — initial (D-2026-08-27-a). **Phase 3.**

### REQ-133 — Water-drop path
- Purpose: trace where water goes from a picked point
- Priority: must
- Type: functional
- Statement: `WATERDROP` picks a plan position on a surface, finds elevation (REQ-074/126), and
  traces downhill across triangle planes until a REQ-132 drain target. The path is previewed as 3D
  geometry and may be baked to an unlinked 3D polyline (EXTRACT pattern). A start outside the
  surface is refused (no extrapolation).
- Acceptance:
  - on a constant-grade plane the path is a straight downhill line to the designed boundary;
  - a start in a designed pit terminates at that pit;
  - a start outside the TIN reports outside and draws nothing.
- Owner-layer: util, Commands, UI
- Status: accepted (2026-08-27)
- Revisions: 2026-08-27 — initial (D-2026-08-27-a). **Phase 3; depends on REQ-132.**

### REQ-134 — Catchment from an outlet
- Purpose: the contributing area upstream of a structure
- Priority: should
- Type: functional
- Statement: `CATCHMENT` picks an outlet on a surface and reports the upstream triangle set's plan
  area, elevation min/max, and a display boundary (cache geometry, optional EXTRACT bake). Uses the
  REQ-132 drain graph in reverse. An outlet outside the TIN is refused.
- Acceptance:
  - an outlet at a designed basin pour-point reports that basin's area within REQ-101 of the
    synthetic fixture;
  - an outlet on a ridge that drains both ways reports the union of contributing triangles, or a
    stated split rule documented in the implementing task — not a silent half;
  - a null TIN / miss is a named refusal.
- Owner-layer: util, Commands, UI
- Status: accepted (2026-08-27)
- Revisions: 2026-08-27 — initial (D-2026-08-27-a). **Phase 3; depends on REQ-132.**

### REQ-135 — Surfaces in paper-space viewports and PDF plot
- Purpose: a surface that exists in the model must appear where the user looks at the model
- Priority: must
- Type: functional
- Statement: Display-geometry batches already built for model space (contours, border, triangles,
  bands, arrows) are drawn through paper-space viewports subject to the same layer / VP-freeze /
  non-plottable rules as other model entities, and are stroked by `PdfPlot` on plot. No second
  contour engine. A surface on a non-plottable layer is omitted from the PDF and the omission is
  not silent if the export log already names excluded kinds — plot skip follows layer plottable
  the way other entities do (REQ-068).
- Acceptance:
  - a floating viewport whose layer freeze does not hide the surface shows its contours (manual
    GUI; paper overlay path);
  - PLOT of a layout that sees the surface includes contour/border strokes in the PDF;
  - a surface on a non-plottable layer does not appear in the PDF.
- Owner-layer: UI (viewport overlay), IO (PdfPlot), Renderer
- Status: accepted (2026-08-27)
- Revisions: 2026-08-27 — initial (D-2026-08-27-a). Closes the TASK-085 DEBT-1 / roadmap "Surface
  plotting" gap.

### REQ-136 — TIN volume surface from two TINs
- Purpose: a surface whose elevations are the difference between two existing TINs, so cut/fill
  can be contoured, styled, and queried like any other surface
- Priority: must
- Type: functional
- Statement: The user can create a named `CadSurface` whose definition is two other TIN surfaces
  (base and comparison, by name). Its triangulation is derived: at each unique plan vertex of
  either parent that both TINs cover, Z is **comparison minus base**. Those points are
  unconstrained Delaunay (same `BuildTin` as REQ-068). Grid and corridor kinds are REQ-137, not this
  object's job.
  Parents that are themselves volume surfaces are refused. Missing names, identical parents, or
  no overlapping samples are named refusals (REQ-201). The object rebuilds when a parent TIN is
  replaced (REQ-069 dirty). `.gs` stores the two names plus the derived verts/indices like any
  other surface. `VOLUMESURFACE <name>, <base>, <comparison>` and Surface Manager "New volume
  surface…". REQ-073 `VOLUMES` remains the numeric cut/fill report; this requirement does not
  replace it.
- Acceptance:
  - two planar TINs 5 ft apart over the same square produce a volume TIN whose vertex Z values
    are 5 ft within REQ-101;
  - two TINs with no plan overlap refuse with a specific message and add no usable triangulation;
  - a missing parent name is a named refusal;
  - the created object appears in SURFACELIST as a volume surface naming both parents.
- Owner-layer: util (tinvolume), Domain, Commands, UI, IO
- Status: accepted (2026-08-27)
- Revisions: 2026-08-27 — initial (D-2026-08-27-b). 2026-08-28 — D-2026-08-28-a: drop the
  "no ISurface / no grid" sentence; those kinds are REQ-137.

### REQ-137 — Surface kinds and shared query interface (GitHub issue #119)
- Purpose: TIN, grid, grid-volume, and corridor surfaces share elevation / slope / aspect queries
- Priority: must
- Type: functional
- Statement: `CadSurface` carries a `SurfaceKind` (`Tin`, `Grid`, `TinVolume`, `GridVolume`,
  `Corridor`). Query and analysis go through `ISurfaceQuery` in `util/` with **two implementations**
  (TIN triangle interpolation and grid bilinear — REQ-301). Corridor surfaces build a TIN from
  designated feature-line vertices. Grid surfaces store origin, spacing, column/row counts and Z
  samples; they also produce a display TIN (two triangles per cell). Grid-volume Z is comparison
  minus base at shared nodes. `SURFACECREATE` accepts an optional kind; `SURFACECREATEGRID` /
  `SURFACECREATECORR` name the other kinds. Missing data yields a named empty surface (REQ-124).
- Acceptance:
  - a 2×2 grid with known corner Z interpolates the cell centre within REQ-101;
  - a TIN query and `ISurfaceQuery` on the same TIN agree within REQ-101;
  - a corridor surface with no feature lines is named and not built;
  - a grid-volume with no overlapping nodes is a named refusal.
- Owner-layer: util, Domain, Commands, IO
- Status: accepted (2026-08-28)
- Revisions: 2026-08-28 — D-2026-08-28-a.

### REQ-138 — Contour extras, slope angle, and XY aspect (issue #119)
- Purpose: user-defined contour elevations, Chaikin smoothing, contour labels, slope in degrees,
  query slope/aspect at a plan point
- Priority: must
- Type: functional
- Statement: A style may list extra contour elevations, a Chaikin pass count (0–5) on **display**
  contours, and a label spacing in feet along major contours (0 = off). Labels are live overlay
  text, not entities. `SurfaceAnalysisMode::SlopeAngle` bands by `atan(grade/100)` in degrees.
  `SURFELEV` reports elevation, percent grade, slope angle, and aspect degrees (or "outside").
- Acceptance:
  - a user elevation appears in the generated contour level list;
  - smoothing with passes > 0 increases vertex count of an open contour;
  - labels are omitted when spacing is 0;
  - a due-east plane reports aspect 90° and a non-zero slope angle;
  - a miss reports outside and does not invent a slope.
- Owner-layer: util, Commands, UI
- Status: accepted (2026-08-28)

### REQ-139 — Masks and TIN edge swap (issue #119)
- Purpose: mask rings exclude area from calculations; swapped edges survive rebuild
- Priority: must
- Type: functional
- Statement: `CadBoundaryKind::Mask` is a closed polyline that hides triangles (same cull as Hide)
  and is listed under Masks in the Surface Manager. `SURFSWAPEDGE <surface>, <x>, <y>` records an
  interior edge flip in the definition; rebuild reapplies flips to a new `shared_ptr<const CadTin>`.
  A pick that is not on an interior edge is a named refusal.
- Acceptance:
  - a mask removes triangles from area stats versus the unmasked twin;
  - a successful swap changes two triangle index triples and survives SURFACEREBUILD;
  - a miss pick does not mutate the TIN.
- Owner-layer: util, Domain, Commands, UI, IO
- Status: accepted (2026-08-28)

### REQ-140 — Volume MTEXT report and extended statistics (issue #119)
- Purpose: put cut/fill on the sheet; TIN and volume-surface stats match the issue
- Priority: must
- Type: functional
- Statement: `VOLREPORT` inserts an MTEXT of the last successful `VOLUMES` / dashboard cut, fill,
  net (yd³) and common area (ft²). `SURFACESTATS` adds min/max triangle area, unique edge count,
  breakline-edge count, min/max/mean slope in **degrees**, and for a volume surface the integrated
  positive/negative Z (cut/fill) over the difference TIN.
- Acceptance:
  - VOLREPORT with no prior volume result is a named refusal and adds no entity;
  - after VOLUMES, VOLREPORT increases the annotation count by one;
  - stats on a 1-triangle surface report that triangle's area as min and max.
- Owner-layer: Commands, util, UI
- Status: accepted (2026-08-28)

### REQ-141 — Analyze ribbon and water-drop feature line (issue #119)
- Purpose: the issue's Analyze tools are reachable from the Survey ribbon; a drop can be a feature line
- Priority: must
- Type: functional
- Statement: The Survey ribbon tab chrome matches Civil 3D's Survey tab (D-2026-08-28-k):
  Labels & Tables, General Tools (no Object Viewer), Survey (Toolspace), Modify, Analyze, Launch Pad.
  Unimplemented Civil 3D tools are disabled with a **not implemented yet** tooltip (REQ-084).
  Implemented actions: Add Tables (`VOLREPORT` / `VOLREPORT TABLE`), Properties, Isolate Objects,
  Survey Toolspace (`TOOLSPACE`), Survey Point Properties, Edit Elevations (feature-line elevations),
  Quick Profile, Create Surface. Surfaces, volume create, breaklines, elevations, slopes, watershed,
  water drop, catchment, dashboard, VOLREPORT, statistics, and rebuild remain invokable from the
  command line; the TIN Surface contextual tab (REQ-143) also exposes the surface Analyze/Modify set.
  `WATERDROP EXTRACT FL` bakes the last path as a feature line (unlinked).
- Acceptance:
  - each named command remains invokable from the command line;
  - EXTRACT FL with a path adds one feature line; with no path is a named refusal.
- Owner-layer: UI, Commands
- Status: accepted (2026-08-28)
- Revisions: 2026-08-28 — D-2026-08-28-a. 2026-08-28 — D-2026-08-28-k: Survey tab chrome.

### REQ-142 — Toolspace (Prospector and Settings)
- Purpose: a drawing explorer whose chrome matches Civil 3D Toolspace, listing only objects GoSurvey implements
- Priority: must
- Type: functional
- Statement: A dockable **TOOLSPACE** window has a dark title, a view combo, a light
  tree, a right-edge pair of **readable** vertical tabs (**Prospector**, **Settings**), and an empty
  preview strip. There is **no** decorative toolbar (D-2026-08-28-m). Tree labels use Segoe UI when
  installed (else the app UI font), near-black ink on off-white paper, and darker hierarchy lines. Prospector is rooted at the active drawing name and lists Points (light context menu:
  Create, Import, Export, Edit, Select, Zoom to, Pan to), Point Groups, Surfaces, and Feature Lines.
  Left-click on a **collection** folder does nothing; right-click shows that collection's Civil 3D
  command list, with unimplemented items **disabled**. Named point groups, surfaces, and feature lines
  are children of those folders. Left-click does not open editors; right-click menus do (Style and
  Analysis on a named surface, Properties on a named group or feature line). Hierarchy uses thin grey
  tree lines. Definition add/remove is on the expanded surface tree (Masks, Watersheds, Definition: Boundaries,
  Breaklines, Contours, Point Files, Point Groups, Edits). Folders Civil 3D shows that GoSurvey does
  not implement (DEM Files, Drawing Objects, Alignments, …) stay **absent**. Settings
  lists only implemented style tables: General (Text Styles, Layers, Dimension Style) and Surface
  (Surface Styles). The panel reads existing stores; it does not invent document types. `TOOLSPACE`
  opens it; `TOOLSPACE SETTINGS` / `PROSPECTOR` switch tabs; `TOOLSPACE LIST` prints the tree;
  `TOOLSPACE CLOSE` hides it. An unknown verb is a named refusal.
- Acceptance:
  - `TOOLSPACE LIST` on an empty drawing names Points, Point Groups, Surfaces, and Feature Lines and
    does not name Alignments, Pipe Networks, or Parcel;
  - after creating a named surface and a named point group, `LIST` includes those names plus
    Definition, Masks, and Watersheds, and does not name DEM Files;
  - `TOOLSPACE SETTINGS` then `LIST` names Text Styles and Surface Styles and does not name Parcel
    or Grading;
  - `TOOLSPACE NOSUCH` is a named refusal and does not change the tab.
- Owner-layer: UI, Commands
- Status: accepted (2026-08-28)
- Revisions: 2026-08-28 — D-2026-08-28-c: collection left-click, Civil 3D menus, definition from tree.
  2026-08-28 — D-2026-08-28-m: omit dummy toolbar; Segoe UI tree face; stronger text/line contrast.

### REQ-143 — Contextual TIN Surface ribbon tab
- Purpose: Civil 3D-shaped tools appear when a surface is selected, without inventing unimplemented objects
- Priority: must
- Type: functional
- Statement: When at least one `SelectedEntity::Type::Surface` is selected in model space (or floating model space), the ribbon tab strip gains a contextual tab titled `Tin Surface: <surface name>` (the first selected surface). Selecting a surface the first time in a stretch switches to that tab; deselecting restores the previous permanent tab only if the contextual tab is still active. The tab contains the screenshot panels **Labels & Tables**, **General Tools**, **Modify**, **Level of Detail**, **Analyze**, **Surface Tools**, and **Launch Pad**. Implemented actions target that selected surface: Properties (side Properties panel), Inquiry (`SURFELEV`), Isolate Objects (`ISOLATEOBJECTS` / `HIDEOBJECTS` / `UNISOLATEOBJECTS`), Surface Properties, Add Data (breakline / contour / boundary designate), Edit Surface (`SURFACEADDPOINT`, `SURFACEDELPOINT`, `SURFSWAPEDGE`, `SURFACEREBUILD`), Water Drop, Catchment, Volumes Dashboard, Extract (`EXTRACT`, `WATERDROP EXTRACT`, `WATERDROP EXTRACT FL`, `CATCHMENT EXTRACT`). **Object Viewer is omitted** (D-2026-08-28-f): the 3D viewport is the viewer. Other controls with no GoSurvey command are **disabled** and their tooltip includes **not implemented yet**. REQ-084 still forbids a disabled control from acting. The contextual tab index is not a persisted prefs slot (`kRibbonTabCount` stays the permanent tabs).
- Acceptance:
  - with no surface selected, the tab strip does not include a `Tin Surface:` tab;
  - selecting a named surface shows a tab whose label contains that surface's name;
  - `SURFELEV`, `WATERDROP`, `CATCHMENT`, `SURFACEREBUILD`, and `EXTRACT` remain invokable from the command line (this tab does not replace them);
  - unimplemented buttons on the tab cannot be activated (disabled);
  - Object Viewer is not present on the tab.
- Owner-layer: UI, Commands
- Status: accepted (2026-08-28)
- Revisions: 2026-08-28 — D-2026-08-28-d. 2026-08-28 — D-2026-08-28-f: omit Object Viewer.

### REQ-144 — Add and delete TIN definition points (issue #119)
- Purpose: a TIN can gain or lose vertices without mutating the live triangulation pointer
- Priority: must
- Type: functional
- Statement: `CadSurface` stores `addedPointXyz` (local XYZ, stride 3) and `deletedPointPicks`
  (local XY). `SURFACEADDPOINT <surface>[, <x>, <y>, <z>]` appends an add (name-only starts a pick
  at the work-plane elevation). `SURFACEDELPOINT <surface>[, <x>, <y>]` appends a delete pick
  (name-only starts a pick). `ResolveSurfaceInputs` appends added points after groups and files,
  then for each delete pick removes the nearest remaining input point. Rebuild replaces
  `shared_ptr<const CadTin>` (architecture §11.5). Non-TIN kinds and a delete on a surface with no
  assembled points are named refusals (REQ-201). Both lists persist in `.gs`.
- Acceptance:
  - four added corners on an empty TIN rebuild to 4 points;
  - deleting the nearest corner then rebuilding yields 3 points;
  - `SURFACEADDPOINT` on a grid surface is a named refusal and adds no vertex;
  - a missing surface name is a named refusal.
- Owner-layer: Domain, Commands, IO, UI
- Status: accepted (2026-08-28)
- Revisions: 2026-08-28 — D-2026-08-28-e.

### REQ-145 — Quick Profile along two plan points (no alignments)
- Purpose: inspect a surface as a station/elevation graph without Alignment or Profile entities
- Priority: must
- Type: functional
- Statement: `QUICKPROFILE <surface>[, <x1>, <y1>, <x2>, <y2>]` samples the named surface along the
  plan segment using `ISurfaceQuery::elevationAt` (existing TIN and grid implementations). Name-only
  starts a two-point pick. Samples include both endpoints, step 1 ft, at most 4096 points. Hits and
  misses are stored; a line with no on-surface sample is a named refusal (REQ-201). Zero-length and
  missing/unbuilt surfaces are named refusals. The graph is **session UI**, never written to `.gs`
  and not a document entity. Alignments and Civil 3D Profile / Profile View objects are out of scope.
- Acceptance:
  - on a plane Z = X, the sample at the midpoint of (0,0)–(10,0) is 5 within REQ-101;
  - a segment that misses the surface is a named refusal and does not invent elevations;
  - a zero-length segment is a named refusal;
  - `QUICKPROFILE` remains invokable from the command line (the ribbon does not replace it).
- Owner-layer: util, Commands, UI
- Status: accepted (2026-08-28)
- Revisions: 2026-08-28 — D-2026-08-28-g.

### REQ-146 — Cut area and fill area (issue #119 AC-V5)
- Purpose: volume reports name the plan area of cut separately from fill
- Priority: must
- Type: functional
- Statement: `ComputeSurfaceVolume` accumulates `cutAreaFt2` (Base above Comparison) and
  `fillAreaFt2` (Comparison above Base) alongside cut/fill/net volumes and common area.
  `VOLUMES`, the Volume Dashboard, `VOLREPORT`, and `VOLCSV` print both areas.
- Acceptance:
  - two planar TINs 10 ft apart over a 100×100 square report cut area 10,000 ft² and fill area 0
    (or the reverse when the pair is swapped);
  - a report with no prior volume result still refuses `VOLREPORT` without adding an entity.
- Owner-layer: util, Commands, UI
- Status: accepted (2026-08-28)
- Revisions: 2026-08-28 — D-2026-08-28-h.

### REQ-147 — Mixed-sign volume cells split on the zero contour (issue #119 AC-V6)
- Purpose: cut and fill are not collapsed when two surfaces cross inside a sample cell
- Priority: must
- Type: functional
- Statement: Each volume sample cell queries both TINs at its four corners. A cell whose ΔZ does not
  change sign integrates as a prism. A mixed-sign cell splits each half-triangle on the ΔZ = 0
  contour and accumulates cut and fill separately. The 250,000-sample budget remains. Corner misses
  fall back to a centre sample.
- Acceptance:
  - Base Z = X and Comparison Z = 5 over [0,10]×[0,10] report cut volume and fill volume each 125 ft³
    within 5%, and cut/fill areas each 50 ft² within 5%.
- Owner-layer: util
- Status: accepted (2026-08-28)
- Revisions: 2026-08-28 — D-2026-08-28-h.

### REQ-148 — Drawing TABLE for volume summaries
- Purpose: volume numbers can sit in the drawing as an AutoCAD-style table, not only as MTEXT
- Priority: must
- Type: functional
- Statement: A TABLE is a first-class entity (`CadTable` in `cadTables` / `cadTableAttrs`,
  `EntityKind::Table` appended after Surface). It stores insertion (top-left of the unrotated table),
  width, height, rotation, column count, and row-major `cells`. MOVE, COPY, ROTATE, SCALE, MIRROR,
  STRETCH, ARRAY, and ALIGN apply to the whole table. Double-clicking a cell opens an in-place editor
  (Enter commits, Esc cancels). `.gs` round-trips a `"tables"` array. Load migrates leftover
  `CadAnnotation::Kind::Table` into `cadTables`. The viewport and paper overlay stroke the grid and
  cell text.   `VOLREPORT TABLE` (alias `VOLTABLE`) inserts a 2-column table of the last volume result
  (and dashboard rows when present). The grid auto-fits cell text (`CadTableFitToContent`):
  equal columns, height from row count × text height; insert and each committed cell edit
  refit. `VOLREPORT` with no argument still inserts MTEXT (REQ-140). DXF
  export emits cell TEXT (no ACAD_TABLE object).
- Acceptance:
  - `VOLREPORT TABLE` after `VOLUMES` adds one TABLE entity (not an annotation);
  - a TABLE with 2 columns and 4 cells lays out four non-empty rectangles inside its box;
  - `VOLREPORT TABLE` with no volume result is a named refusal;
  - MOVE of a TABLE changes its insertion; a cell hit-test returns the row-major index of a point
    inside that cell;
  - `CadTableFitToContent` makes `width` at least as wide as the longest cell string at the
    table's plotted height, and `height` at least one text-height per row; a longer cell
    string after a fit increases `width`.
- Owner-layer: Domain, Commands, UI, IO
- Status: accepted (2026-08-28)
- Revisions: 2026-08-28 — D-2026-08-28-h; 2026-08-28 — D-2026-08-28-i (entity store + modify + cell edit);
  2026-08-28 — D-2026-08-28-j (auto-fit grid to cell text).

### REQ-149 — Multi-row Volume Dashboard and CSV
- Purpose: several base/comparison/clip analyses share one dashboard and one export
- Priority: must
- Type: functional
- Statement: The Volume Dashboard keeps a list of named analysis rows (`VOLDASH ADD <label>` snapshots
  the current pick and result). The live pickers remain the working row. `VOLCSV <path>` writes a UTF-8
  CSV of every row plus the live result: label, base, comparison, cut, fill, net, cut area, fill area,
  common area. Missing path opens the existing CSV save dialog.
- Acceptance:
  - `VOLDASH ADD` with a landed result increases the row count by one;
  - `VOLCSV` with no volume data is a named refusal;
  - `VOLCSV tests/tmp-vol.csv` after `VOLUMES` writes a file containing `cut`.
- Owner-layer: Commands, UI
- Status: accepted (2026-08-28)
- Revisions: 2026-08-28 — D-2026-08-28-h.

### REQ-150 — Move TIN point and delete TIN line
- Purpose: the issue's remaining TIN edits are definition operations, like REQ-144
- Priority: must
- Type: functional
- Statement: `SURFACEMOVEPOINT <surface>[, <x1>, <y1>, <x2>, <y2>, <z2>]` records a local from-XY and
  to-XYZ; rebuild replaces the nearest assembled point. Name-only is two picks (from, then to; to-Z is
  the work plane). `SURFDELLINE <surface>[, <x>, <y>]` records a pick; rebuild deletes the two
  triangles of the nearest interior edge (`TinDeleteInteriorEdgeNear`). Live `CadTin` is never mutated
  except via pointer swap after a copied mesh edit. Grid/corridor/volume kinds refuse.
- Acceptance:
  - moving the only extra add-point of a three-point TIN relocates that vertex after rebuild;
  - deleting an interior edge of a two-triangle quad leaves fewer than six indices;
  - a miss more than 1 ft from any interior edge is a named refusal.
- Owner-layer: Domain, Commands, IO, util
- Status: accepted (2026-08-28)
- Revisions: 2026-08-28 — D-2026-08-28-h.

### REQ-151 — Arcs as surface breaklines
- Purpose: 3D geometry used as breaklines includes arcs
- Priority: must
- Type: functional
- Statement: `ResolveDefinitionChain` tessellates a `CadArc` into at least 8 chords (≤1 ft or 5°
  whichever is finer, cap 256) at the arc's Z, and treats that chain as an open breakline. Closed
  boundaries still require a polyline or feature line.
- Acceptance:
  - designating an arc as a breakline rebuilds the TIN with a constraint along the chord chain;
  - `DESIGNATEBOUNDARY` on an arc is a named refusal.
- Owner-layer: Commands
- Status: accepted (2026-08-28)
- Revisions: 2026-08-28 — D-2026-08-28-h.

### REQ-152 — Catchment mean elevation
- Purpose: catchment elevation statistics include an average, not only min/max
- Priority: must
- Type: functional
- Statement: `CatchmentResult::meanZ` is the area-weighted mean of triangle vertex elevations in the
  catchment. `CATCHMENT` logs it with min/max.
- Acceptance:
  - a planar catchment reports mean Z equal to that plane within REQ-101;
  - an outside pick still reports outside and does not invent a mean.
- Owner-layer: util, Commands
- Status: accepted (2026-08-28)
- Revisions: 2026-08-28 — D-2026-08-28-h.

### REQ-153 — Contextual SURVEY Point(s) ribbon tab
- Purpose: Civil 3D-shaped tools appear when survey points are selected, without inventing unimplemented COGO objects
- Priority: must
- Type: functional
- Statement: When `selectedSurveyPointIndices` contains at least one valid survey-point index in model space (or floating model space), the ribbon tab strip gains a contextual tab. One valid selected point: title `SURVEY Point: <point id>`. More than one: title `SURVEY Points`. First selection in a stretch switches to that tab **unless** the TIN Surface contextual tab is already active (both tabs stay visible). Deselecting the last point restores the previous permanent tab, or the TIN Surface tab if a surface is still selected. Panels match the screenshots: **Labels & Tables** (single) / **Tables** (multi) with Add Tables; **Edit Label Text** only when one point is selected; **General Tools** (Inquiry, Properties, Isolate Objects — **no Object Viewer**); **Modify**; **Analyze**; **SURVEY Point Tools**; **Launch Pad**. Wired actions: Inquiry (`ID`), Properties, Isolate Objects, Edit/List Points (`VIEWPOINTS`), Point Group Properties (Point Groups window), Import Points, Export Points, Create Points, Create Point Group, Create Surface. Unimplemented Civil 3D leftovers (Add Tables as a point table, Edit Label Text, Renumber, Datum, Elevations from Surface, Lock/Unlock Points, Geodetic Calculator, Transfer Points) are **disabled** and tooltip **not implemented yet**. The tab index is not a persisted prefs slot (`kRibbonTabCount` stays the permanent tabs).
- Acceptance:
  - with no survey point selected, the tab strip does not include a `SURVEY Point` tab;
  - selecting one survey point shows `SURVEY Point:` plus that point's id;
  - selecting more than one survey point shows `SURVEY Points`;
  - Object Viewer is not present on the tab;
  - unimplemented buttons on the tab cannot be activated (disabled);
  - `CREATEPOINTS`, `VIEWPOINTS`, `IMPORTPOINTS`, `EXPORTPOINTS`, and `ID` remain invokable from the command line.
- Owner-layer: UI, Commands
- Status: accepted (2026-08-28)
- Revisions: 2026-08-28 — D-2026-08-28-l.

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

  **Picked points are scoped out of this tolerance, deliberately.** A viewport pick's accuracy is
  bounded by the pixel it came from, so at a usable zoom it is coarser than ±0.01 ft and no arithmetic
  downstream can improve it — the information was never captured. What *is* required is that picking
  add no error of its own: picks are submitted in **local** storage coordinates (not world — see
  `SubmitViewportPick`), and an object snap overrides the cursor with a value read directly out of the
  geometry stores, so **a snapped pick is bit-identical to the vertex it snapped to**. That is the
  property to protect, and it is why the pick path needs no widening to double. Exact values are
  entered by typing, which is what the conditions above govern.
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
| REQ-101 | Commands/compute | `headless.regression-req101-origin-at-entry` (a typed easting at 2e6 is stored within tolerance — measured 2000000.10 → origin 2000000 + local 0.10000000149, ~1.5e-9 ft, was 0.025 ft; establishment is one-time; an over-large magnitude is still refused; first resave byte-identical) + `headless.regression-59-circle-infinite-radius` / `-59b` (which double as the upper bound's guard) + `headless.regression-pick-local-coordinates` (picks are local, so picking adds no error of its own). Reference-dataset half still `<regression set>` — pending, see below | **accepted** (typed-storage half verified; reference dataset outstanding) |
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
| REQ-202 | Build/Platform | **six of seven conditions observed against the live pipeline, 2026-08-20** — evidence per condition in TASK-049 §9, which cites the run ids: feature branch → artifact only, no release, no tag (run `31912058476`); repeated `beta` pushes → exactly one `channel-beta` prerelease across ~20 pushes; unchanged version on master → publishes nothing, fails nothing (run `32049139096`); bumped version → `v<version>` tag + release (`v0.5.0`, `v0.5.1`, `v0.5.2`); tag == AppVersion == manifest version (v0.5.2 checked three ways); manifest SHA-256 matches the asset (re-derived from the downloaded installer, byte-identical, `size` too). **Outstanding: failing ctest → no release has never been observed** — no run has failed at Test; the nearest evidence is run `31910767883`, which failed at Build and published nothing, so the gate is confirmed only in the negative (TASK-049 debt (5)). Status stays `accepted` rather than MET for that reason. Was: planned — observed pipeline behaviour (feature branch → artifact only, no tag; repeated `beta` pushes → exactly one `channel-beta` prerelease; unchanged version on master → no publish, no failure; bumped version → `v<version>` tag + release; failing ctest → no release; tag == AppVersion == manifest version; manifest SHA-256 matches the asset) | accepted |
| REQ-051 | UI/IO | `MtextToolbarTests` (panel-anchor clamp in-bounds/off-screen/oversized; font+colour run-tag composition incl. empty family = no tag; ruler tick spacing + zero-width = no ticks; attach label 1–9 + out-of-range fallback) + manual (panel titled "Text Formatting" with two rows + ruler; drag persists across edits and restart; font/colour apply to the selection only; height/oblique/entity colour whole-object; style dropdown re-bakes per REQ-044; B/I/U/caps/symbol unchanged; justification re-lays out; disabled controls inert with naming tooltips; ruler + expand toggles; paper MTEXT same panel; single-line TEXT still bare box; OK/Esc + `.gs`/DXF round-trip unchanged) | accepted |
| REQ-203 | Build/Platform/Commands | planned — the `gosurvey_headless` link line carries no imgui/glfw/GLEW/`gl*` symbol; a hand-written transcript (line + circle + polyline) saves a `.gs` identical to the same steps performed in the GUI; a queued `DIALOG` answer satisfies a file-dialog call with no block; a deliberately-broken transcript exits non-zero naming invariant + step + line; the same transcript twice is byte-identical; CI runs the corpus per push | accepted |
| REQ-082 | UI | planned — manual (header click sorts + marks the column, second click reverses; equal keys stable; after sorting, edit/delete act on the record shown; header frozen while scrolling; unchecked checkbox visible; saved file order unaffected by display sort) | accepted |
| REQ-081 | UI | planned — manual, side-by-side against the Hazel reference shots (adjacent docked panels separated by a visible border; panel surface lighter than the dockspace ground; recessed fields; Dark shows no `#464646`/steel-blue chrome; Dark→Light→Dark leaves no colour behind; Light pixel-unchanged; viewport contents unchanged; X/Y/Z badges present, Radius has none) | accepted |
| REQ-083 | Platform/UI | `PointFileExtTests` **green 2026-08-17** (5 cases / 26 assertions: a name ending `.csv`/`.txt` in any case gets nothing appended; a bare name gets the chosen filter's extension; a name ending in something else — `points.dat`, `job.2026` — still gets one; empty name; a trailing dot) + manual (Import chooser lists `.txt` under the default filter; the same bytes as `.csv` and as `.txt` import identically and validate identically; a locked `.txt` shows the REQ-041 message with Import disabled; a space-delimited `.txt` reports column errors and adds no point; Export typed as `points.txt` writes `points.txt`) — **the manual half was run and confirmed by the user in the application 2026-08-18**: the Win32 chooser and the REQ-041 file-state path cannot be linked by the test target, and `IMPORTPOINTS` only opens the window (the import is a panel button) so the REQ-203 driver cannot reach it either. Fixtures for the pass: `samples/points-req083.{csv,txt}` (byte-identical) and `samples/points-req083-spaced.txt` | accepted |
| REQ-204 | Build/Platform/Commands/util | planned — `--seed N` twice is identical; **one deliberately-broken fixture per invariant proving each check fires**; a failing run's minimized transcript reproduces standalone under the REQ-203 driver; minimization terminates within its bound and reports its ratio; a clean seed range prints only a summary; `GoSurvey.exe`'s link line contains no generator symbol | accepted |
| REQ-091 | Platform/Auth/UI | `AuthPingTests` **green 2026-08-23** (10 cases / 124 assertions: PKCE verifier charset/length/uniqueness, base64url encode+decode incl. round-trip and RFC 4648 vectors, authorize-URL parameter/percent-encoding correctness including `audience`, silent-refresh-vs-interactive decision) + **live end-to-end, real Auth0 tenant, 2026-08-23**: Google sign-in completed, loopback redirect caught, settings panel showed "Signed in as `<email>`", menu-bar email display confirmed; one real defect found and fixed live (Auth0 rejects a wildcard loopback port — ADR-037 (b) amended to a fixed candidate-port list, D-2026-08-23 amendment); silent-refresh-keeps-user-signed-in path requires "Allow Offline Access" enabled on the Auth0 API (user found this off, turned it on) — re-verification of the full 30-day-persistence path is the user's next manual step | accepted |
| REQ-092 | Platform/backend | `accounts-worker/test.mjs` **green 2026-08-23** (offline, real RSA keypair generated in-process: missing/malformed/tampered/expired/wrong-issuer/wrong-audience/alg-none/missing-subject tokens all rejected 401 before any D1 query; valid token for a new user returns the default tier and issues exactly one insert; valid token for an existing user returns their stored tier with no insert; D1 outage is 503; missing `AUTH0_DOMAIN` binding is 500, not misreported as unauthorized; email upsert/preserve-tier/malformed-dropped cases added when email wiring landed) + **live, 2026-08-23**: deployed Worker confirmed live (401/404 as expected), a real sign-in's `users` row confirmed via direct D1 query (`auth0_sub`, `tier: 'free'`, and — after the email wiring landed — a populated `email`) | accepted |
| REQ-080 (amended) | Telemetry/Auth/UI/Platform | `TelemetryPingTests` **green 2026-08-23** (email-empty/email-present JSON cases; `DecideEventToSend` simplified to install-vs-always-active, throttle tests removed with the throttle) + `telemetry-worker/test.mjs` **green 2026-08-23** (valid/empty/malformed email stored-or-dropped-to-null; column-count assertions 8→9) + **live, 2026-08-23**: deployed Worker smoke-tested with a real POST carrying `email`, confirmed via direct D1 read-back; live migrations applied to the pre-existing deployed table (`ALTER TABLE pings ADD COLUMN email TEXT`, `DROP INDEX ux_pings_active_daily`) | accepted |
| REQ-093 (amended) | UI/Platform | **manual, verified live against the real app across three build-and-look rounds, 2026-08-23 (D-2026-08-23-h):** the splash is its own small window (~440x320), centered on the primary monitor, filled edge-to-edge by the card — the real desktop, not a dimmed backdrop, is visible everywhere outside that small window; a native-resolution 32x32 corner logo (no upscaling — the source art is only that large) plus a large centered "GoSurvey" wordmark; the progress bar visibly animates across a hardcoded 5.0 s regardless of how fast real preload finishes; the main CAD shell is not shown/interactive until the 5 s elapses; user settings/prefs, the startup workspace template, the app font and the app logo are all loaded before the main shell is usable — this was already true pre-splash and REQ-093 does not change *what* loads, only that a splash now covers it; the splash's rotating phase text is cosmetic labeling only, since linetypes have no data table to load and text styles are already resident in memory the instant `AppCommandState` is constructed; closing the window during the 5 s exits cleanly with no hang; **the user's saved dock layout is restored correctly on launch** — this became an explicit acceptance condition only after a regression destroyed it once (D-2026-08-23-h) | accepted |
| REQ-102 | Domain/Renderer/Commands/UI | proposed — not yet scoped; catalogued from Known Limitations 2026-08-23 (D-2026-08-23-i) | proposed |
| REQ-103 | Commands/Domain/UI | planned — sequenced into 8 increments (D-2026-08-23-j); TASK-094 (MIRROR, step 1), TASK-095 (LENGTHEN, step 2), TASK-096 (EXTEND, step 3, model+paper space), TASK-097 (BREAK, step 4, model+paper space), and TASK-098 (STRETCH, step 5, model+paper space, full arc-parity geometry) all self-verified 2026-08-24, transcripts green (565/565 regression, plus 4 new unit tests pinning the arc-stretch formula). **All five then failed in model space**: none was routed in CadUi.cpp's model-space viewport click dispatch, so every click was silently discarded and each command hung on its first prompt (working in floating model space and pure paper space, which route separately). Fixed by TASK-099, which moved the routing decision into the pure `ViewportClickRouteFor` (viewport/ViewportPickPolicy.hpp) as an exhaustive switch with no `default:`, added the headless `CLICK` verb so a transcript exercises the routing the `PICK` verb bypasses, and converted the five REQ-103 transcripts onto it (red before the fix, green after; 571/571 regression). Two further GUI-only defects then surfaced and were fixed: LENGTHEN refused any pick made before its sub-mode had a value, making the ribbon button a dead end (TASK-100 — the pick now latches the object, reports its length and prompts, with Total as the new default sub-mode), and BREAK gained a live preview of the material a break removes, on its own opaque render channel because the shared translucent preview batch is invisible when painted over the object it describes (TASK-101). Both amendments recorded as D-2026-08-24-e / D-2026-08-24-f. **Steps 1-5 are complete**: 573/573 regression green, and the user confirmed the manual GUI pass on 2026-08-24, closing TASK-094..101. **Step 6 (FILLET/CHAMFER) is complete**: TASK-102 (FILLET, step 6a) and TASK-103 (CHAMFER, step 6b) both self-verified 2026-08-24 — full model+paper-space parity for both, tangent-arc/corner-point geometry unit-tested (8 + 3 cases), four headless transcripts (two CLICK-driven), two real bugs found and fixed during TASK-102's self-verification (triple undo-snapshot per apply; pick-based rather than computed-point-based near/far endpoint selection) — both fixes live in shared code, so CHAMFER's own transcripts passed on the first run rather than repeating either mistake. 588/588 regression green; manual GUI pass pending for both (this project's own no-UI-automation constraint). Steps 7-8 (ARRAY/EXPLODE) not started | accepted |
| REQ-104 | Commands/Domain/IO/UI | proposed — not yet scoped; catalogued from Known Limitations 2026-08-23 (D-2026-08-23-i) | proposed |
| REQ-105 | Commands/UI | proposed — not yet scoped; catalogued from Known Limitations 2026-08-23 (D-2026-08-23-i) | proposed |
| REQ-106 | UI/Renderer | proposed — not yet scoped; catalogued from Known Limitations 2026-08-23 (D-2026-08-23-i) | proposed |
| REQ-107 | Domain/Commands/IO/UI | proposed — not yet scoped, likely architectural; catalogued from Known Limitations 2026-08-23 (D-2026-08-23-i) | proposed |
| REQ-108 | UI/Commands | proposed — not yet scoped; catalogued from Known Limitations 2026-08-23 (D-2026-08-23-i) | proposed |
| REQ-109 | Renderer | proposed — not yet scoped; catalogued from Known Limitations 2026-08-23 (D-2026-08-23-i) | proposed |
| REQ-110 | Domain/UI/Renderer | proposed — not yet scoped; catalogued from Known Limitations 2026-08-23 (D-2026-08-23-i) | proposed |
| REQ-111 | Domain/Commands/IO/Renderer | proposed — not yet scoped; catalogued from Known Limitations 2026-08-23 (D-2026-08-23-i) | proposed |
| REQ-112 | IO | proposed — not yet scoped; catalogued from Known Limitations 2026-08-23 (D-2026-08-23-i) | proposed |
| REQ-113 | IO | proposed — not yet scoped; catalogued from Known Limitations 2026-08-23 (D-2026-08-23-i) | proposed |
| REQ-114 | IO/UI/Platform | proposed — not yet scoped; catalogued from Known Limitations 2026-08-23 (D-2026-08-23-i) | proposed |
| REQ-115 | UI/Platform | proposed — not yet scoped; catalogued from Known Limitations 2026-08-23 (D-2026-08-23-i) | proposed |
| REQ-116 | UI/Platform | proposed — not yet scoped; catalogued from Known Limitations 2026-08-23 (D-2026-08-23-i) | proposed |
| REQ-117 | UI/Commands | proposed — not yet scoped; catalogued from Known Limitations 2026-08-23 (D-2026-08-23-i) | proposed |
| REQ-118 | Commands/Viewport | planned — `headless.regression-118-polyline-close-enter` (click the start vertex closes; Enter ends open with no closing segment; two vertices refuse to close; CLOSE/END still work; Esc leaves nothing; model, 3DPOLY and paper space each asserted). Same feature/issue (#80) as REQ-303 below, built independently on `beta` — see REQ-303's duplication note, D-2026-08-25-l | accepted |
| REQ-119 | UI/Commands | **increment 1 done** (TASK-111) + **increment 2 done** (TASK-112) — `CommandLineTests [req119]` (the prompt→variants rule as a pure function: inline, grouped, mixed-case shortcut extraction incl. `No trim`→`N`, unclosed bracket, empty group, and a round-trip so parsing loses no text) + `headless.regression-119-variant-token-accepted` (the mechanism's three prompts) + `headless.regression-119-variant-coverage` (one assertion per marked-up token across CIRCLE/ROTATE/SCALE/TRIM/POLYLINE/FEATURELINE/ELEV, **plus a live refusal assertion for CIRCLE's bare `d`** — a value prefix, not a token, deliberately left unmarked so markup cannot manufacture a dead link) + manual GUI (links render, hover and click in BOTH the floating bar and the classic dock; a wrapping dock prompt keeps its links on the correct line with no horizontal overflow; **no log line is clickable**) | accepted |
| REQ-120 | UI | **manual GUI only, and that is a real limitation, not a shortcut.** The headless driver models no framebuffer and never calls `ProcessPendingViewportZoom` (which early-returns on `fbW <= 0`), so it cannot reach any zoom behaviour — there is no existing zoom transcript in the corpus for the same reason. Covering this by transcript would mean giving the harness a synthetic viewport, which is harness work this requirement did not take on (recorded as TASK-113 DEBT-1). Verified instead by driving the real window: middle double-click frames the drawing in model space; it works MID-COMMAND with the active LINE's placed point surviving; the typed route still does not zoom mid-command (its text is consumed by the active command as point input — unchanged); paper space frames the sheet; middle-DRAG still pans. Leaves GitHub issue #88 open — covers only #88's Middle Mouse/Architecture sections, not its ZOOMEXTENTS acceptance list | accepted |
| REQ-307 | UI/Commands | done (GitHub issue #106, D-2026-08-26-g, TASK-120). Closes REQ-121's own stated paper-space scope boundary for the one case that needed it: `StartPaperMoveCopyViewports`/`StartDeleteCommand`'s paper branch, on an empty selection, now sets `paperMoveWaitingSelection`/`paperDeleteWaitingSelection` and opens a real selection step instead of refusing — pick-first (act on an existing selection) is unchanged, above. `PaperIsObjectSelectionStep` (`ViewportPickPolicy.hpp`) is the paper-space counterpart of REQ-121's own predicate, consulted alongside it at every one of REQ-121's three call sites: the pickbox cursor (`CadUi.cpp`'s crosshair draw), the pre-existing (previously unconditional) paper snap glyph, and `CommandInputHint`'s prompt — all three now return REQ-121's own `kSelectObjectsPrompt` for this step, reusing the string rather than declaring a second one. Enter is handled by two free functions, `ProcessPaperMoveWaitingSelectionEnter`/`ProcessPaperDeleteWaitingSelectionEnter` (`CadCommands.cpp`), called from BOTH the raw viewport `ImGui::IsKeyPressed(Enter)` check (mouse-only entry, the same shape EXTEND's own paper phase already needed since paper commands never set `cmd.active` and so are unreachable from `ProcessCommandLineSubmit`'s Kind-keyed dispatch) AND a new branch at the top of `ProcessCommandLineSubmit`'s blank-line handler — the second call site is what gives this a headless transcript path EXTEND's raw-only precedent does not have, and the two call sites are guarded against double-firing on one keypress with `ImGui::GetActiveID() == 0` (the raw check only fires when no ImGui widget, e.g. the command-line box, currently holds keyboard focus). Click/box accumulation reuses `SelectViewport`/`TogglePaperEntitySelection`'s own pre-existing `additive=true` parameter verbatim — no new toggle logic — and a new `closePaperSelBoxMerge` lambda (a union variant of the pre-existing `closePaperSelBox`) merges a closed box into the accumulating selection rather than replacing it, mirroring REQ-305's model-space `SelectionAccumulate` (D-2026-08-25-l). Tests: `ViewportPickPolicyTests [req307]` (the predicate, pure and header-only); `headless.req307-paper-selection-step`, driven through the real `CMD DELETE`/`CMD MOVE`/`CMD COPY` and blank-`CMD` command dispatch (not CLICK/BOX — paper space's ambient click block is screen-space/ImGui-hover driven with no headless equivalent, the same limitation REQ-121's own paper DELETE/JOIN branch already had), proving the old flat refusal is gone and REQ-201's "Nothing selected" refusal holds on repeated blank Enter. The click-toggle/box-merge accumulation itself, the pickbox rendering, and the snap-glyph suppression are GUI-only verification, same category REQ-121 itself already established for its own three rules — this session cannot simulate mouse hover or screen-space picking. 637/637 ctest green | accepted |
| REQ-121 | UI/Commands/Viewport | done (GitHub issue #91, D-2026-08-26-a + D-2026-08-26-d, TASK-115 + TASK-118). Mechanism: `ViewportIsObjectSelectionStep`, derived from `ViewportClickRouteFor`'s `default:`-less switch, so a command cannot be added and silently omitted — `ViewportPickPolicyTests [req121]` (4 cases: ALIGN's unsnapped corners — red before the fix; every selection step recognised; each exclusion asserted; DELETE/JOIN's route, with ZOOM and STRETCH left on the box route). Review follow-ups closed by TASK-118, re-derived while rebasing onto `beta` after issue #103 landed underneath it: rule (3)'s shared prompt was factually wrong for DELETE/JOIN — fixed by giving them D-2026-08-25-l's accumulate-until-Enter shape, covered by `headless.req121-delete-join-accumulate` (proven red on `beta`: the closing box erased, LINES 3 -> 2). Rule (1)'s reported second seam (the snap-OVERRIDE menu bypassing the gate) had its underlying mechanism replaced by #103 between the original review and this rebase — the "cursor jumps mid-selection" symptom no longer reproduces, because the override's consumption already sits behind the same `!ViewportIsObjectSelectionStep` gate the automatic snap uses; what remained was narrower (the menu could still be *opened*, arming a persistent lock off a selection-step pixel that then silently affected the next ordinary snap), and that is what TASK-118's rebase actually gates. The cursor/OSNAP/prompt rules themselves stay GUI-only — there is no headless equivalent for screen-space picking or for a drawn cursor — and both rounds were verified A/B against a control rather than by absence. Paper space is a STATED scope boundary, not coverage: its modify commands are pick-first, so no selection step exists there (GitHub issue #106 — closed by REQ-307, which gives MOVE/COPY/DELETE a real selection step for the one case that needed it, starting with nothing pre-selected). 634/634 ctest green post-rebase. One `CadSnapTests` case (issue #103, unrelated to this task) carried an em-dash in its Catch2 name that CTest's Windows discovery mangles into a filter matching nothing, reporting a false failure in CI on both this branch and unmodified `beta` (`425afa7`'s own CI run) — fixed here by renaming the test to plain ASCII rather than worked around, since it was blocking CI on every branch built from `beta`, not just this one | accepted |
| REQ-122 | Commands | done (GitHub issue #88, D-2026-08-26-c, TASK-117) — **automated**, which REQ-120 could not be. The framing arithmetic was hoisted into `src/commands/ZoomFraming.hpp` (pure + header-only, the `OrthoConstrain.hpp`/`ViewportPickPolicy.hpp` precedent) so `tests/ZoomFramingTests.cpp` can reach it without a framebuffer: 11 Catch2 cases / 231 assertions covering centring, fit-at-any-aspect, the 8% margin, aspect binding, the one-unit floor on degenerate extents, invariance above the floor, refusal on non-finite input, finiteness across spans 1e-9..1e12, corner order, and null out-params. 3 of the 11 proven red against the old constants before the fix. TASK-113's DEBT-1 is unchanged and still open — `ProcessPendingViewportZoom` itself remains unreachable from the harness — but every guarantee #88 asks for now lives in tested code. The state-dependent halves (empty drawing, live parity with the gesture, middle-drag pan) verified in the GUI, measured off the status-bar readout rather than eyeballed: typed ZOOMEXTENTS and the middle double-click produce identical world coordinates to 4 dp at two screen points. 622/622 ctest green | accepted |
| REQ-123 | Commands/UI | done (GitHub issue #100, D-2026-08-26-e, TASK-119) — **`headless.req123-viewport-zoom-extents`, the first zoom behaviour ever covered by a transcript.** TASK-113's DEBT-1 blocks the others on `ProcessPendingViewportZoom`'s `fbW <= 0` guard; this case needs no framebuffer (its aspect is the viewport's rect in paper inches) so it is handled ahead of that guard. 43 steps: the framing after ZE with hand-computed scales (13.5870 for an 8x4in viewport, 27.1739 for 4x4in — same drawing, different rect, different answer), each viewport independent of the other's zoom, and a layer frozen in the viewport excluded from the extents then restored when thawed. Proven red on `beta`: `expected centre 50, 10 scale 13.587; got 0, 0 scale 50` — the viewport's framing untouched at its creation defaults. Four new driver verbs (VIEWPORT / VPSELECT / CLAYER / VPFREEZE) and `EXPECT VPFRAME`, all REQ-203 gaps of the LAYOUT/CLIPCOPY shape. GUI pass confirmed the numbers against the live status bar (`VP 1" = 40.4'` vs 40.36 computed), the sheet unmoved, REQ-120's gesture working in a viewport for the first time, and middle-drag pan still confined to it. 632/633 ctest (the one failure is `beta`'s own — an em dash in a `CadSnapTests` TEST_CASE name breaks ctest's name round-trip; unrelated and pre-existing) | accepted |
| REQ-124 | Domain/Commands/UI/IO | done (TASK-125) — `headless.req124-empty-surface`; SURFACELIST not-built; SURFELEV outside; SURFACESTATS not-built | accepted |
| REQ-125 | util/Commands | done (TASK-125) — `SurfaceStatsTests`; `SURFACESTATS` / `sfstats` | accepted |
| REQ-126 | util/Commands | done (TASK-125) — live `surfaceQueryCache` on `AppCommandState`; indexed SURFELEV path | accepted |
| REQ-127 | Viewport/Commands | done (TASK-125) — `CadSnapTests [req127]`; OSNAP toggle default on | accepted |
| REQ-128 | util/Domain/IO | done (TASK-125) — `TinConstraintTests [req128]`; DESIGNATEBOUNDARY CLIP; `.gs` `"clip"` | accepted |
| REQ-129 | Domain/Commands/IO | done (TASK-125) — `DESIGNATECONTOUR` / `dcon`; `contourSources` in `.gs` | accepted |
| REQ-130 | util/Renderer/UI/IO | done (TASK-125) — `SurfaceAnalysisTests [req130]`; SURFSTYLE ANALYSIS direction | accepted |
| REQ-131 | util/Commands | done (TASK-126) — `SurfaceVolumeTests [req131]`; VOLUMES optional clip id; VOLDASH CLIP | accepted |
| REQ-132 | util/Commands | done (TASK-127) — WatershedTests; WATERSHED; cache outlines | accepted |
| REQ-133 | util/Commands | done (TASK-127) — water-drop plane/pit/outside; WATERDROP EXTRACT | accepted |
| REQ-134 | util/Commands | done (TASK-127) — catchment pour-point and ridge union; CATCHMENT | accepted |
| REQ-135 | UI/IO | done (TASK-125) — paper overlay + `PdfPlot` stroke of display batches; non-plottable omitted | accepted |
| REQ-136 | util/Domain/Commands/UI/IO | done (TASK-128) — `TinVolumeTests [req136]`; `VOLUMESURFACE`; Surface Manager volume create; `req136-volume-surface` | accepted |
| REQ-137 | util/Domain/Commands/IO | done (TASK-129) — `Issue119SurfaceTests [req137]`; `SURFACECREATEGRID` / `CORR`; `req137-grid-corridor-volreport` | accepted |
| REQ-138 | util/Commands/UI | done (TASK-129) — `[req138]` Chaikin, labels, aspect | accepted |
| REQ-139 | util/Domain/Commands | done (TASK-129) — `[req139]` SURFSWAPEDGE miss/hit | accepted |
| REQ-140 | Commands/util | done (TASK-129) — `[req140]` stats; `VOLREPORT`; `req140-volreport` | accepted |
| REQ-141 | UI/Commands | done (TASK-129) — Survey Analyze ribbon; `WATERDROP EXTRACT FL` | accepted |
| REQ-142 | UI/Commands | done (TASK-130) — Toolspace Prospector + Settings | accepted |
| REQ-143 | UI/Commands | done (TASK-133) — contextual TIN Surface ribbon tab | accepted |
| REQ-144 | Domain/Commands/IO/UI | done (TASK-134) — `req144-surface-add-del-point`; SURFACEADDPOINT / SURFACEDELPOINT | accepted |
| REQ-145 | util/Commands/UI | done (TASK-135) — `SurfaceProfileTests [req145]`; `req145-quick-profile` | accepted |
| REQ-146 | util/Commands | done (TASK-136) — cut/fill areas; `SurfaceVolumeTests [req146]`; VOLUMES/VOLCSV | accepted |
| REQ-147 | util | done (TASK-136) — mixed-sign cell split `[req147]` | accepted |
| REQ-148 | Domain/Commands/UI/IO | TASK-137 entity; TASK-138 auto-fit | accepted |
| REQ-149 | Commands/UI | done (TASK-136) — VOLDASH ADD; VOLCSV; dashboard rows | accepted |
| REQ-150 | Domain/Commands/util | done (TASK-136) — SURFACEMOVEPOINT / SURFDELLINE; `[req150]` | accepted |
| REQ-151 | Commands | done (TASK-136) — arc breaklines; DESIGNATEBOUNDARY refuses arcs | accepted |
| REQ-152 | util/Commands | done (TASK-136) — catchment mean Z; `[req152]` | accepted |
| REQ-153 | UI/Commands | done (TASK-139) — contextual SURVEY Point(s) ribbon tab | accepted |
| REQ-161 | Application/UI/Build | planned — Debug Developer Shell + Test Engine; Release `dumpbin` ctest; `--devshell-run` script | accepted |
| REQ-170 | IO/Domain/UI/Build | planned — LibreDWG DXF/DWG; R2004 default write; no converter on happy-path open; AutoCAD opens emit without Recover; GPL-3 | accepted |
| REQ-171 | Domain/Renderer/IO | planned — point cloud entity; shared immutable payload; logged DXF/DWG exclusion | accepted |
| REQ-172 | IO/Domain/UI | planned — PTS→PTX→LAS→LAZ→E57 read+write; malformed refuse | accepted |
| REQ-173 | Domain/IO/Renderer/UI | planned — JPEG/PNG/BMP IMAGE underlay; missing file unloads image only | accepted |
| REQ-174 | IO/Domain | planned — IFC tessellate to mesh; no IFC write | accepted |
| REQ-302 | UI/IO | done — all 3 increments delivered (GitHub issue #83). Increment 1 (tab infrastructure) done, TASK-104, amended once from GUI-pass feedback (D-2026-08-25-d). Increment 2 (responsive layout engine) done, TASK-105/ADR-038, user confirmed with no findings (D-2026-08-25-g). Increment 3 (content audit) done, TASK-106, D-2026-08-25-h/i — corrected this requirement's own speculative Statement text (no blocks/xrefs/point clouds/standards exist), relocated Import DXF/DWG to Insert, Settings to View, Export DXF/DWG + Plot/Batch Plot to Output (moved off Home); Manage tab intentionally left empty, nothing exists to relocate there. User confirmed the increment 3 manual GUI pass with no findings. 541/541 Catch2 test cases and 591/591 headless transcripts green throughout | accepted |
| REQ-303 | Commands/Viewport | done (GitHub issue #80, D-2026-08-25-j, TASK-108). Click-to-close (start-point Endpoint snap + exact-equality intercept in `SubmitViewportPickImpl`) and blank-Enter-to-end (`ProcessCommandLineSubmit`) both call the existing `CommitPolylineDraft`/typed-keyword gate logic verbatim, plus REQ-118's `CancelSegmentAnglePick`/`ResetSegmentAngleLock` cleanup folded in during the master→beta merge (D-2026-08-25-l). Paper-space parity inherited from TASK-107, not reimplemented. 541/541 Catch2 test cases, 52/52 headless transcripts green (53 registered, 1 pre-existing disabled; 2 new since TASK-107: this task's plus TASK-107's own). New transcript proven red-before/green-after. Manual GUI pass (hover-glyph feedback) pending — this session cannot simulate mouse hover | accepted |
| REQ-304 | Commands/UI | done (GitHub issue #82, D-2026-08-25-k, TASK-110). Full `AppCommandState::Kind` audit against `CommandInputHint`/its FooterHint delegates found 10 uncovered Kinds; `Pan`/`Orbit` are by-design exclusions (dedicated hand cursor, no typed value — REQ-045/REQ-084 (c)); the other 8 (`FeatureLine`, `Fillet`, `Chamfer`, `PdfAttach`, `Hatch`, `VpFreeze`, `VpThaw`, `Elev`) fixed by extending the existing `DrawingExtrasFooterHint` delegate, which already fed both the command-line hint and the cursor prompt from one call — no new mechanism. 593/593 Catch2 + headless regression green, unchanged pass count. Manual GUI pass (visual/wording confirmation of the 8 new hint strings) pending — this session cannot simulate mouse hover | accepted |
| REQ-305 | Commands/Viewport | done (GitHub issue #87, D-2026-08-25-m, TASK-111 — relabeled from REQ-304/TASK-109 while merging `master` into `beta`, see the requirement's own header note). ARRAY (rectangular + polar) follows the MOVE/COPY/ROTATE/SCALE/MIRROR transform-command shape end to end; survey points excluded from the array selection, confirmed with the user (D-2026-08-25-m addendum). Amended once (D-2026-08-25-n, TASK-112): the shared "select objects" step was click-or-box-and-accumulate-until-Enter for MOVE/COPY/SCALE/ROTATE/MIRROR/ALIGN/ARRAY (STRETCH excluded — its crossing box is load-bearing geometry, REQ-103 step 5), replacing the box-only shape all seven originally shared. `GoSurveyTests.exe` 542/542, headless transcript corpus green (1 pre-existing disabled, unrelated) | accepted |

---

## Anti-requirements

> Optional but valuable: things the project deliberately will **not** require.
> Documenting them stops well-meaning contributors from "fixing" non-problems.

- "We do **not** require pluggable rendering backends — OpenGL only until a
  second backend is a real requirement (avoids speculative abstraction)."
- We do **not** require screenshot diffing or golden images of the framebuffer. Pixel-level visual
  tests stay out (flaky; they mostly exercise ImGui/GPU, not GoSurvey).
- We do **not** require a UI-automation driver on **Release** or on `gosurvey_headless`. REQ-203
  transcripts remain the CI-default, windowless command driver. **Debug-only** Dear ImGui Test
  Engine + Developer Shell (REQ-161, ADR-040, D-2026-08-29-f) is the recorded exception: it drives
  the real ImGui tree and is compile-excluded from Release. *(Anti-requirement amended 2026-08-29;
  original “no UI-automation at all” accepted 2026-08-16 with ADR-031 alt. (1).)*
- `<…>`
- We do **not** require native Leica LGS/LGSX/BLK/BLKX/IMP/PTG/BIN or Autodesk RCP/RCS in File
  Format Specs (D-2026-08-29-g). Interchange for those workflows is E57/LAS from the vendor tool.
- We do **not** require IFC write, ODA membership, or DWG write past R2004 in this epic.
- We do **not** require point clouds as TIN data sources (REQ-068 D4 / ADR-042).
