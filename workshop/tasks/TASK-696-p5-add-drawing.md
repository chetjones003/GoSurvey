# TASK — Projects P5: Add Drawing to Project, with Convert (issue #696)

- Branch: `feat/projects-p5-add-drawing`
- Authority: **REQ-378** (accepted, clauses 5/5a amended by **D-2026-10-05-g**); D-2026-10-05-d (decision 13);
  builds on REQ-374/375/376/377; GitHub #696 (P5)
- Boundary check: the point merge is pure in `src/io/ProjectAddDrawing.{hpp,cpp}`; the similarity plan is
  pure in `src/geo/DrawingConversion.{hpp,cpp}` (the only CS-MAP callers stay in `geo::`); applying it to a
  drawing is `src/commands/ConvertDrawing.{hpp,cpp}`; the three-step flow (prepare / convert / commit) is
  `src/commands/ProjectAddFlow.{hpp,cpp}` with no window; the dialog is `DrawAddDrawingModal` in
  `CadUi_Projects.cpp`; File menu item and typed command `ADDDRAWING`. No new dependency.

## How it works

1. **Prepare** loads the chosen DWG into a private `AppCommandState`, never the user's tabs, and works out the
   preview: counts (new / identical / differing), the conflicts, the settings that will become overrides, and
   whether the drawing's coordinate system or unit differs from the project's. Reads only.
2. **Convert** (optional, only when there is a mismatch): `geo::PlanConversion` measures ONE similarity
   (scale, rotation about the vertical axis, shift) with CS-MAP at the drawing's centre and measures the
   leftover error at the extents; refused above 0.02 m. `ApplyDrawingConversion` applies it to the private
   copy (stored coordinates `sR·local`, document origin `A + sR·origin`, so nothing loses precision).
3. **Commit** works on a copy of the database first, writes the DWG into `Drawings/` (never over an
   existing file: `Name (2).dwg`), and only then replaces the database. A failed write changes nothing.
   Unconverted: byte copy with only the GoSurvey trailer replaced. Converted: saved as a GoSurvey drawing so
   the DWG body matches the new coordinates. The trailer carries no points (project-owned), the rules, the
   overrides, and the project's enforced zone/unit. The caller opens the copy as a tab of the project.

## Decisions / assumptions (recorded)

- Convert is **full** (units + coordinate system) as one similarity, in this PR (user choice 2026-10-05;
  D-2026-10-05-g). A drawing with no coordinate system / unitless, or a project that fixes none, is **not** a
  mismatch (nothing to compare); EnforceProjectSettings still assigns the project's values.
- A drawing holding surfaces, meshes, point clouds, solids, pipe runs, position markers, multileaders, PDF
  underlays or paper-space viewports cannot be converted; it stays blocked and the kinds are named. Arcs and
  ellipses in a tilted plane block only a convert that turns the drawing.
- Convert clears saved views, named UCSs, the active UCS, the geographic marker, the transformation and
  captured map areas of the copy (they refer to the old coordinates), and says so (REQ-201).
- A unit change also scales `modelUnitsPerPlottedInch` so text keeps its size on paper; that makes the plot
  scale an override of the project's default when they differ.
- A drawing with no points keeps default rules (an empty filter shows all). A number the drawing holds twice:
  the first is used. A conflict the user has not decided is skipped (the safe outcome).
- Heights are never scaled by a grid factor: only a unit change scales Z.

## Technical debt

- Selection-style transform routines (MOVE/ROTATE/SCALE) refuse several kinds, so Convert has its own
  per-store transform in `ConvertDrawing.cpp`; a new geometry store must be added there (and to
  `UnconvertibleKinds` until it is).
- Convert works on float-stored annotation / block coordinates, which carry ~1e-7 relative precision.
- Preparing a very large DWG reads it on the UI thread (a one-shot wait, like File > Open).
- The dialog is not covered by automated GUI tests (hover/click is not automatable here).

## Verification

- `GoSurveyTests "[req378]"` (`ProjectAddDrawingTests.cpp`): counts, each choice, default-skip, renumber
  collisions, rules show exactly the added set, empty drawing keeps default rules.
- `GoSurveySnapTests "[req378]"` (`ProjectAddFlowTests.cpp`): preview writes nothing; copy-in with the
  original byte-identical; tagging; overrides captured; conflicts end to end; cancel changes nothing;
  read-only refuses; second add gets `(2)` and shares identical points; units mismatch blocks then converts
  (points, heights and the DWG body); zone mismatch converts within tolerance against CS-MAP's own answer;
  blockers named; each store moves by the one transform; an out-of-tolerance conversion is refused.
- Full ctest: 9 failures (offset / surfaces / solids / feature-line headless transcripts) — identical to the
  P3/P4 baseline, none involve projects.
