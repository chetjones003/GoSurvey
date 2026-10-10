# TASK-238 — SECTION: the cross-section of a solid, without cutting it (REQ-335)

- Type:    feat (new requirement)
- Status:  review
- Opened:  2026-09-09
- Owner:   Workshop
- GitHub:  #149 acceptance 5 (3D Phase 6 — Analysis), step 4 of 6.

## 1. Authority

- **REQ-335** — drafted and accepted here (D-2026-09-09-i).
- **REQ-314 / ADR-046** — `Slice`, which this is built on and whose accepted set it inherits.
- **REQ-311 / D-2026-08-31-e** — `ucs::Ucs` is the plane abstraction; the section plane is one.
- **REQ-316 / ADR-047** — polyline bulges, which is how an arc reaches the drawing intact.
- **ADR-048 (a)** — the kernel does not know what a document entity is; `brep::Path` is why it does
  not have to learn.
- **REQ-201** — refuse with a stated reason. Five refusal paths do.
- Constraints: CON-06 smallest change; no new type, no new dependency.

## 2. Problem

#149 acceptance 5: *"Sectioning produces correct section geometry and does not modify the model."*
The issue describes it as *"Related to Phase 4's slice, but non-destructive — it inspects rather than
cuts."*

Probe **P3** confirmed it can be exactly that, and probe **P5** (written for this step) settled the
scope by answering the question the plan flagged as needing a decision: *what is a section loop
actually made of?*

| cut | boundary |
|---|---|
| box / wedge / pyramid, perpendicular **and oblique** | all `Line` — one closed polyline |
| cylinder, cone — **perpendicular** | two `Arc` — one closed polyline, arcs as bulges |
| cylinder — **oblique** | two `Ellipse` — **not** bulge-representable |
| cone — oblique, and both Boolean results tried | `Slice` itself refuses |

That table is the scope. Everything a `Path` of lines and arcs can carry is produced; everything
else is refused by name.

## 3. What was built

**Kernel:** `brep::SectionLoop(solid, planePoint, planeNormal, outPlane, outLoop, outWhy)` — the
section's own frame plus its boundary as a closed `brep::Path` in that frame's 2D coordinates.

**Command:** `SECTION` — one closed polyline per selected solid, on the current layer, in one undo
step, by the active UCS plane.

Three decisions, recorded as D-2026-09-09-i:

**(a) The accepted set is `Slice`'s, inherited rather than restated.** Sectioning asks the same
geometric question and keeps a different answer, so a solid `Slice` declines is declined here with
**`Slice`'s own `Problem`** — asserted by a test that reads the reason off `Slice` and compares,
rather than merely checking that both fail. If `Slice` grows, this grows with it.

Worth noting for whoever reads the header next: `Problem::SliceCurvedFace`'s comment still says
"planar-faced solids only", and it has been **stale since Phase 4 grew the cylinder and cone cases**.
That is precisely why the accepted set here is read from behaviour and not from prose.

**(b) The plane is the active UCS, not SLICE's three points.** `ucs::Ucs` is this project's plane
abstraction, and since REQ-329 every modify command already resolves into the active UCS plane — so
"set the work plane, then ask what the section looks like" is the gesture the codebase teaches.
REQ-335 records a three-point form as increment 2, so this reads as a choice rather than a narrow
implementation.

**(c) The section is a closed `brep::Path`.** Already the kernel's "chain of straight and circular
segments in the XY plane of a frame", so no new type is introduced and the kernel stays ignorant of
`CadPolyline` (ADR-048 (a)). Arcs reach the drawing as **bulges**, so a cylinder's circular section
is a circle rather than a polygon.

Non-destructiveness is **structural**: the kernel takes the solid by const reference and the two
pieces are local and discarded. There is no path by which it could modify its input — and the tests
assert it byte-for-byte anyway, because the command layer could still have got it wrong.

The loop is reversed where needed so it always winds **counter-clockwise about the caller's normal**.
The `above` piece's cut face looks *down* — anti-parallel to the section normal — so a section taken
straight off it would come back wound the wrong way.

## 4. Tests

**`BrepTests [req335]`, 9 cases.** Every figure is a closed form:

| case | what it pins |
|---|---|
| box, perpendicular | the rectangle's area, four straight segments, **and the solid byte-identical afterwards** |
| box, oblique | `A/cos θ` — the check that the loop is measured **in its own plane**. A projection back to horizontal returns 600 here and would pass every other assertion |
| cylinder, perpendicular | two arcs, sweeps summing to `2π`, area `πr²` — nothing flattened |
| cone, perpendicular | the circle at that height: cutting at half height gives a **quarter** of the base area |
| the returned frame | origin and normal are the **caller's**, and every section vertex lies on the plane |
| oblique cylinder | refused by name — it meets the wall along an `Ellipse` |
| a sphere | refused with **`Slice`'s own reason**, compared against `Slice` directly |
| plane misses / degenerate normal | refused by name |
| survey magnitudes | the same rectangle 2.2e6 ft east |

**`headless.req335-section`** drives the command end to end and carries the half a unit test can only
argue: the document's own counts before and after, undo/redo returning exactly, and a refusal leaving
the drawing untouched. It sections through a **rotated UCS** rather than a translated one — typed
coordinate entry is 2D outside 3DPOLY, so a `0,0,6` origin would silently drop its Z, and the rotated
plane is the more interesting case anyway.

## 5. Assumptions

None. The scope was measured by P5 rather than assumed, and every accepted case is asserted against a
closed form.

## 6. Verification result

- **build-project** — clean release build, MSVC/Ninja, no new warnings.
- **architecture-review** — no new type (the section reuses `brep::Path`), no new layer, no new
  dependency. The kernel gains one function beside `Slice`; the command layer gains one function and
  one registry entry.
- **code-review** — the refusal style, the `Problem` vocabulary and the all-or-nothing commit shape
  are `Slice`'s and `CommitSlice`'s, reused rather than re-invented.
- **dependency-audit** — none added.
- **performance-review** — one `Slice` per selected solid, on a command the user invokes by hand.
- **testing** — full suite **1415/1415 green**, 9 new unit cases and 1 new transcript.

COMPLETION REPORT — TASK-238 — 2026-09-09
- Requirements satisfied:  REQ-335 (new, accepted); GitHub #149 acceptance 5
- Summary:                 SECTION inspects rather than cuts, and refuses rather than approximates
- Tests:                   9 unit cases + 1 transcript; 1415/1415
- Verification verdict:    PASS for increment 1
- Assumptions:             none
- Architectural decisions: D-2026-09-09-i (a)-(c)
- Dependencies:            none added
- Technical debt noted:    increment 2 — three-point plane form, elliptical boundaries, holes.
                           Also: `Problem::SliceCurvedFace`'s doc comment is stale (§3(a)) and is
                           left for whoever owns REQ-314 to correct, rather than edited from here
- Build:                   reproducible, clean on Windows/MSVC
- Docs updated:            REQ-335, a traceability row, D-2026-09-09-i, this task log
