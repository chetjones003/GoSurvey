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
  cursor dynamic-input shows a prompt label plus **two coordinate fields** (X and
  Y) that continuously display the crosshair's current **world** coordinates at
  the configured display precision (REQ-020 `displayLinearPrecision`). The active
  field is highlighted; typing overrides (locks) that field to the typed value;
  Tab moves between the fields; Enter — or a viewport click — commits the point.
  Prompts that do not expect a point (bearing/angle/distance/option/command-name
  entry) keep a single input field. There is no Send button; commit is by Enter
  or click.
- Acceptance: starting LINE shows the "first point" prompt with two boxes that
  track the cursor's easting/northing; typing locks the X field; Tab focuses Y;
  Enter commits the shown/typed X,Y and a viewport click still places the point;
  a non-point prompt (e.g. circle radius, bearing entry) shows a single field.
- Owner-layer: UI
- Status: accepted
- Revisions: 2026-06-12 — initial.

---

## Performance requirements

> Performance is a requirement, not an afterthought — but always paired with a
> *measurement method*. A performance requirement with no defined benchmark is
> not verifiable.

### REQ-100 — Frame budget
- Purpose: interactive responsiveness (desktop/OpenGL)
- Priority: should
- Type: performance
- Statement: The viewport sustains `<60 FPS / 16 ms>` while displaying a scene
  of `<N>` primitives on the reference hardware.
- Acceptance: the benchmark scene profiled on the reference machine stays within
  budget at the 95th-percentile frame.
- Owner-layer: Renderer
- Status: proposed
- Revisions: `<date>` — initial.

### REQ-101 — Numerical tolerance
- Purpose: domain correctness (CAD/survey)
- Priority: must
- Type: performance/quality
- Statement: Computed `<coordinates>` match the reference dataset within
  `<±0.01 ft>`.
- Acceptance: the regression dataset passes at the stated tolerance (assert
  against tolerance, never exact float equality).
- Owner-layer: `<domain/compute>`
- Status: proposed
- Revisions: `<date>` — initial.

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
| REQ-100 | Renderer | `<bench-frame>` | proposed |
| REQ-101 | compute | `<regression set>` | proposed |
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
| REQ-024 | UI | manual (LINE shows two live coord boxes; type locks X; Tab→Y; Enter/click commits; non-point prompt single field) | accepted |

---

## Anti-requirements

> Optional but valuable: things the project deliberately will **not** require.
> Documenting them stops well-meaning contributors from "fixing" non-problems.

- "We do **not** require pluggable rendering backends — OpenGL only until a
  second backend is a real requirement (avoids speculative abstraction)."
- `<…>`
