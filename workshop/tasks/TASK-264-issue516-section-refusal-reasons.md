# TASK-264 — issue #516: SECTION and SLICE name the limit a curved cut actually hit

- Type:    fix
- Status:  review
- Opened:  2026-09-17
- Owner:   Workshop
- GitHub:  #516

## Requirement authority

REQ-201 (a refusal names its reason), REQ-314 (`Slice`) and REQ-335 (`SECTION`), both given a
revision of this date. No decision entry: this changes which existing reason is reported, not what is
accepted or refused.

## Why

Found by the 2026-09-16 section-coverage probe. For cylinders and cones:

| cut | message shown before | what was true |
|---|---|---|
| tilted, crossing an end cap | "The cut would split the solid into disjoint pieces…" | nothing splits; the ellipse runs off the side |
| tilted, between the caps | "This release slices solids with flat faces only…" | `SLICE` makes this cut; `SECTION`'s outline cannot hold an ellipse |
| parallel to the axis | "flat faces only" | a cut direction not supported yet |

## What changed

`src/util/brep.hpp` / `brep.cpp`, appended to `Problem` with their own `ProblemText`:

| reason | returned from |
|---|---|
| `SliceCutCrossesCurvedEnd` | `SliceCylinderOblique` and `SliceConeOblique` when the ellipse would clip a cap; `SliceConeObliqueOpenNotch` for a degenerate notch sweep |
| `SliceCutAlongCurvedAxis` | `Slice`'s curved fall-through, for a Cylinder / Cone recipe with the plane normal perpendicular to the axis |
| `SliceCutTooSteepForCone` | the same fall-through for a cone cut steeper than its side (every tilted cylinder cut is taken earlier) |
| `SliceCutSeveralOutlines` | the planar cutter when a face is crossed more than twice or the cross-section is more than one loop, and `SectionLoop` for more than one face on the plane |
| `SliceResultInvalid` | the four curved builders when their pieces fail validation, and `SectionLoop`'s degenerate loop |
| `SectionEllipse` / `SectionCurve` | `SectionLoop` for an `Ellipse` / `Intersection` edge |
| `SectionHasHole` | `SectionLoop` for a cut face with more than one loop |

`SliceResultComplex` is left only on the weld of a kept side into separate pieces, where "disjoint
pieces" is true. `SliceCurvedFace` is left for curved solids that are not a cylinder or cone (sphere,
torus, filleted box), whose wording is #520's to improve. No command changed: `SECTION` and `SLICE`
print `ProblemText` verbatim.

## Tests

- `BrepTests [issue516]`:
  - Every row of the table through both `SectionLoop` and `Slice`: a 45° cut crossing the caps; a
    45° cut between the caps (sliced, section refused as an ellipse); an axis-parallel cut of a
    cylinder and of a cone, through and off the axis.
  - A perpendicular cut still sections, and a sphere still says "flat faces only".
  - A square tube cut across reports `SliceCutSeveralOutlines`, with no "disjoint" in the text.
- Updated:
  - Three existing refusals now expect their named reason: the cylinder cap clip, the cone cap clip,
    and the steep cone cut.
  - The every-reason-has-its-own-text case lists all ten slice and section reasons.
  - `req314-slice` expects the end-cap wording.
- `headless.issue516-section-refusal-reasons`: the issue's table through `SECTION` and `SLICE`.
- **Proven to bite:** against `beta`'s `brep.cpp`, 4 of the new sections fail.
- Full suite 1610/1617; the 7 failures are `beta`'s own.

## Not in scope

Supporting these cuts is #517; sphere, torus and hole sections are #520.
