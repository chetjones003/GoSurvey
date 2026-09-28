# TASK-284 — the Modify commands work on solids and pipe runs

- Type:    feat
- Status:  review
- Opened:  2026-09-28
- Owner:   Workshop
- GitHub:  #564 section 4 (fourth increment; §3 = #566, §1 = #568, §2 = #569)

## Requirement authority

REQ-351 (new, D-2026-09-28-f). REQ-322 item 6b and REQ-332 item 6 amended by it. REQ-201 (refuse by
name), REQ-313 / ADR-045 (the kernel), REQ-345 (pipe runs), REQ-329 (3D / UCS modify).

## Decisions (D-2026-09-28-f)

- **Q2 (from the issue): real mirror, uniform scale.** `brep::Mirror` produces a correctly oriented
  solid; non-uniform scale stays out of scope (no command can ask for it — every SCALE takes one
  factor).
- **Pipe-run SCALE (found in the code): scale the route, keep the size.** The path vertices scale
  about the base point in 3D; `nominalSize` and wall are untouched.
- Recorded without a question: a COPY of a pipe run joins no piping network (a network is a named
  set the user built; a copy is a new run).

## Starting state (measured)

Already done by earlier requirements: MOVE (REQ-322), in-place ROTATE and SCALE (REQ-332),
Rectangular ARRAY (D-2026-09-07-c), ERASE. Still missing:

| Command | Solid | Pipe run |
|---|---|---|
| MOVE / 3DMOVE | works | silently left behind |
| COPY | silently skipped | silently skipped |
| ROTATE / 3DROTATE | works | silently left behind |
| ROTATE Copy | refused (tilted) / skipped (plan) | skipped |
| SCALE / 3DSCALE | works | silently left behind |
| MIRROR | refused | skipped |
| ARRAY Rectangular | works | skipped |
| ARRAY Polar | refused | skipped |
| STRETCH | refused | silently left behind |

## Plan / architectural-boundary check

- **Kernel (Domain, `src/util`)**: `brep::Mirror(s, planePoint, planeUnit, out, why)` and
  `nurbs::Mirror`. Every frame is reflected and then made right-handed again by negating its Y axis
  (X' = R·X, Y' = −R·Y, Z' = R·Z, so a plane's Z stays its outward normal). Negating Y maps an
  angle about Z to its negative, so a curved face's longitude span becomes `[−uEnd, −uStart]`
  (renormalised into `[0, 2π)`), an arc / ellipse edge's `sweep` negates, and a `paramLoops`
  polygon negates u (v for a plane) and is re-wound. A NURBS patch has its control net reflected
  and its U direction reversed, so `Su × Sv` still points outward. Every loop is reversed (order and
  each use's direction) because a reflection reverses orientation. The recipe is kept: every
  primitive is symmetric about its frame's XZ plane (checked in the builders), and a polysolid path
  negates y and sweep and swaps Left/Right justification. Validates or refuses.
- **Commands**: one helper for solids that either replaces or appends (duplicates), and one for
  pipe runs that maps every path vertex through a point function. Wired into every transform path:
  `ApplyTranslationToSelection`, `ApplyRotationToSelection`, `RotateSelectionInPlaceAboutAxis`,
  `ApplyScaleToSelection`, `DuplicateCadSelectionTranslated` (COPY, rect ARRAY, polar ARRAY without
  rotation), `DuplicateCadSelectionRotated` (plan ROTATE Copy), `RotateSelectionAboutAxis` (polar
  ARRAY with rotation, tilted ROTATE Copy), and both MIRROR paths. STRETCH keeps refusing solids and
  now refuses pipe runs by name too.
- Fittings are block references and already follow MOVE/COPY/ROTATE/SCALE/MIRROR/ARRAY (and are
  refused by name under a tilted-axis rotate or mirror, REQ-328's existing boundary).

## Tests

- Kernel (`tests/BrepMirrorTests.cpp`): volume / area preserved for box, wedge, cylinder, sphere,
  torus, cone, a bored box (inward face) and a loft (NURBS); mirroring twice restores every vertex;
  every frame right-handed orthonormal; a wedge's vertices are the reflected points; the mirrored
  solid survives a Boolean UNION with its original; recipe kept and agrees; degenerate plane refused.
- Commands: transcript `req351-modify-solids` (COPY / MIRROR / ROTATE Copy on a solid; one undo;
  mixed selection) and `tests/ModifyPipeRunTests.cpp` (pipe run MOVE / ROTATE / SCALE (size kept) /
  MIRROR / COPY / polar ARRAY; attributes carried; STRETCH refuses a pipe run by name).

## Found while testing

- **MIRROR's erase-source has its own eraser** (`EraseMirroredSourceNoUndo`), which knew nothing of
  solids or pipe runs — so "mirror and erase the original" would have left the original behind. The
  solid / pipe-run removal was lifted out of `ExecuteDeleteSelection` into
  `EraseSelectedSolidsAndPipeRuns`, now called by both. The same eraser also skipped block
  references and tables, which the flat MIRROR has always copied — so a mirrored pipe FITTING left
  its original behind. Fixed in the same place (a fitting is a block reference).
- Typed ROTATE angles are clockwise-positive (survey convention); pipe runs turn the same way lines
  and solids do.

## Results

- Kernel: `tests/MirrorSolidTests.cpp` — 6 cases, 397 assertions, including the tessellation's
  signed volume and winding on every mirrored kind.
- Commands: `tests/ModifyPipeRunTests.cpp` — 9 cases; `req351-modify-solids` transcript; updated
  `issue400-array-solids` (Polar now arrays a solid), `req322-move-3d` and `req332-rotate-scale-solid`
  (the still-refusing command is now STRETCH).
- Full suite: 1851/1858. The 7 failures (issue233, issue402-offset-ucs, regression-58, req068,
  req087, req313-solid-isolines, req313-solid-primitives) fail identically on unmodified `beta`.

## Not in scope / technical debt

- Tilted-axis ROTATE / MIRROR still refuse block references by name (REQ-328), so a fitting is
  refused there while its pipe run turns.
- No live drag preview is drawn for a pipe run during MOVE / ROTATE (the commit is correct).
- STRETCH of a pipe run by its path vertices is not built; it is refused by name.

