# TASK-217 — Issue #400 (increment 4): polar ARRAY interactive fill-angle + live preview honour the active UCS plane

- Type:    bug fix (3D integration gap left by increments 1–3)
- Status:  COMPLETE — PR #418 (feat/issue400-polar-angle-preview → beta), manual GUI pass done
- Opened:  2026-09-08
- Owner:   Workshop
- GitHub:  #400

## 1. Authority

- REQ-305 acceptance 4, 5, 10, 11 (accepted). No SPEC change needed — the criteria already
  require UCS-plane picks (10), polar rotation about the active UCS Z axis "for ANY UCS" (11),
  and a preview that reflects every parameter (5). The implementation from increments 1–3 does
  not meet 4/5/11 for the *interactive* (click-driven) fill-angle pick or for the live preview.
- Reuses: `ucs::WorldToPlane` / `ucs::PlaneToWorld` (`CadUcs`), `CadWorkPlaneAnchoredAt` /
  `CadActiveUcsStorage` (`CadCommands.hpp`), `ray3d::RotatePointAboutAxis` /
  `ray3d::RotateVectorAboutAxis` (REQ-328, `ray3d.hpp`), and the already-shipped
  `RotateSelectionAboutAxis` (`CadCommands.cpp`) as the reference for the preview math.

## 2. Observed defect (user report, 2026-09-08, FRONT UCS)

Draw a large circle, draw a small circle, `ARRAY` → `P` → pick centre on the large circle →
4 items → drag/click the fill angle → the 4 copies all land exactly on top of the original.

Root cause: the interactive fill-angle pick computes the sweep angle in **world X/Y**:

```
CadCommands.cpp:12726
deg = atan2(py - st.arrayCenterY, px - st.arrayCenterX)   // world-Y term
```

Under FRONT/BACK/LEFT/RIGHT/orbited UCS the drawing plane's in-plane "vertical" axis is world Z,
not world Y, so `py - arrayCenterY` ≈ 0 for every pick and the angle collapses to ~0° (or 180°).
A 0° fill angle ⇒ 0° step ⇒ all N instances coincide with the source.

Second defect (same class): the entire `K::Array` branch of `BuildTransformPreview`
(`TransformPreview.cpp` ~535–758) is world-XY: rectangular offsets add to world x/y, polar
rotates about world Z via `rotatePreviewPt` / `RotateNormalAboutZ`, and the preview's own
fill-angle read (`:740`) has the same `atan2` collapse. So the ghost shown while the user drags
is also wrong under a tilted UCS (acceptance 5 + 11).

The **typed** angle path (`HandleArrayText`, type `360`) and the **commit** loop
(`CommitArrayPolar`, already on `RotateSelectionAboutAxis` from REQ-328) are correct — this is
purely the interactive pick + the preview.

## 3. Scope (this increment)

In:
1. `CadCommands.cpp` interactive fill-angle pick (`st.arrayPhase == Polar_WaitAngle`,
   ~:12719–12733): resolve the picked point and the centre into the active UCS plane's local 2D
   frame (anchored at the centre) and take `atan2(local.y, local.x)` there — mirroring how the
   rectangular spacing phases at ~:12684–12699 already convert their picks with
   `CadWorkPlaneAnchoredAt` + `ucs::WorldToPlane`.
2. `TransformPreview.cpp` `K::Array` branch: rebuild the per-instance rectangular and polar
   walks on the UCS plane —
   - reconstruct the 3D cursor as `(curX, curY, cmd.uiCursorWorldZ)` (CadUi already resolves the
     cursor onto the active work plane every frame and publishes Z there — verified
     `CadUi.cpp:13564–13609`), so no change to `BuildTransformPreview`'s signature;
   - rectangular: cell offset `(c·col, r·row, lv·level)` in UCS-local → world delta via the
     anchored frame, same arithmetic as `ArrayCellWorldDelta`;
   - polar: rotate each instance with `ray3d::RotatePointAboutAxis` about the UCS Z axis through
     the centre, and rotate Circle/Arc plane normals with `ray3d::RotateVectorAboutAxis` — a
     direct port of `RotateSelectionAboutAxis`'s body into the ghost walk; the interactive
     fill-angle read at `:740` gets the same UCS-plane `atan2` fix as item 1.
   - Ellipse/Annotation/FeatureLine keep the world-Z-only preview path (they are refused at
     commit under a tilted axis anyway — REQ-328 item 2 / acceptance 11).

Out:
- Rectangular preview under a tilted UCS for entity types the commit already handles is included
  (translation only, no rotation gap), but no new entity coverage beyond what the preview draws
  today (LineSeg/Circle/Arc/Ellipse/Polyline/FeatureLine — acceptance 5's stated set).
- Typed centre-point Z (`arrayCenterZ = CadCommitElevation`, `HandleArrayText:10961`): left as
  is. For a centre typed as UCS `x,y` it resolves to the UCS origin's elevation, which is correct
  when the centre is on the work plane; a full 3D typed-centre-off-plane case is not in this
  report and not required by acceptance 4. Noted as a known limitation in the completion report.
- Solids in polar ARRAY stay refused (acceptance 13, unchanged).

## 4. Test approach

- Extend `tests/headless/transcripts/issue400-array-ucs-plane.txt`: a tilted UCS (`UCS` `X` `90`),
  a circle on the work plane, `ARRAY` `P`, centre via `CLICKUCS`, **fill angle via a viewport
  pick** (the path that is broken today — the existing cases all type `360`), `EXPECT CIRCLEXYZ`
  on a hand-computed Rodrigues position to prove the copies actually spread around the tilted
  axis. Add a partial-arc (e.g. 180°) picked-angle case.
- Regression: the existing World-UCS and in-plane-rotated-UCS cases in that transcript and
  `regression-87-array.txt` must still pass byte-identical.
- Preview: no headless harness (`BuildTransformPreview` needs a view). Manual GUI check on the
  user's FRONT-UCS repro, screenshotted, plus a Debug-build hand pass per
  `[[project_startup_splash_screen]]`-style manual protocol.
- `Ray3dTests` already covers `RotatePointAboutAxis`; no new unit tests there.

## 5. Verification

- `build-project`, `code-review`, `testing`.
- Architectural-boundary check: no new cross-layer names — `TransformPreview.cpp` already
  includes `ray3d` and reads `cmd` fields; `CadCommands.cpp` change is local to one phase block.

## 6. Open questions

None identified — SPEC is sufficient (acceptance 4/5/10/11). If the manual pass shows the typed
centre-Z limitation actually bites a common workflow, raise it then as its own item.

## 7. Implementation notes

- `CadCommands.cpp` `SubmitViewportPick` `Polar_WaitAngle` branch: the resolved pick and the
  centre are converted into the centre-anchored UCS-plane frame via
  `CadWorkPlaneAnchoredAt` + `ucs::WorldToPlane`, and `atan2` runs on the local X/Y. Reduces to
  the old world-XY arithmetic under the World UCS.
- `TransformPreview.cpp` `K::Array` block:
  - `appendTranslatedInstance` gained a `dz` argument (Line/Circle/Arc/Ellipse/Polyline Z all
    shift; FeatureLine's shared (x,y)-only helper leaves Z unshifted — noted gap, FeatureLine is
    marginal in ARRAY preview and refused by tilted polar anyway).
  - `appendRotatedInstance` re-signed to `(axisPoint, axisUnit, ang)` and its body ported from
    `RotateSelectionAboutAxis` — `ray3d::RotatePointAboutAxis` for points,
    `ray3d::RotateVectorAboutAxis` for Circle/Arc plane normals. Ellipse + FeatureLine keep the
    world-Z-only path and are skipped when the axis is not world-Z-parallel (they are refused at
    commit there).
  - Rectangular driver: spacings resolved as UCS-local distances from
    `(curX, curY, cmd.uiCursorWorldZ)` via the anchor frame; per-cell world delta via
    `ucs::UcsVectorToWorld`, same as `ArrayCellWorldDelta`.
  - Polar driver: axis = `CadWorkPlaneAnchoredAt(centre).zAxis` (normalised); live fill angle from
    `ucs::WorldToPlane(centreFrame, cursor)`; rotate-items via `appendRotatedInstance`, no-rotate
    via a rotated-anchor 3D delta.
- Tests: `tests/headless/transcripts/issue400-array-ucs-plane.txt` extended with two picked-angle
  polar cases (World UCS regression + tilted `UCS X 90`), asserting hand-computed Rodrigues
  centres/normals. Full `dev/test` + direct transcript run: PASS.
- Known limitation (unchanged): typed polar centre `arrayCenterZ` from `CadCommitElevation`
  (UCS-origin elevation). Correct for a centre on the work plane; a 3D-off-plane typed centre is
  not covered and not in the report.

## 8. Completion report

- **Outcome:** PASS. Issue #400 increment 4 delivered on PR #418.
- **Commits (feat/issue400-polar-angle-preview):**
  1. fill-angle pick + live preview resolve in the active UCS plane
  2. code-review follow-ups (inverted-Z UCS ghost sense; FeatureLine translate/rotate gaps)
  3. polar no-rotate anchor Z from the selection's 3D bounds centre (`CadGizmoAnchorWorld`),
     not `CadCommitElevation` — the "copies fly outward" defect found in the first manual pass
  4. REQ-305 revision note
- **Tests:** `tests/headless/transcripts/issue400-array-ucs-plane.txt` +3 cases (World-UCS picked
  angle regression; tilted-UCS picked angle; tilted-UCS rotate-items=No with the selection off the
  work-plane elevation). Each new case verified to fail without its fix. Full `dev/test`: pass
  except the pre-existing unrelated `RecentDrawingsTests` "missing or corrupt store" failure on
  `beta`.
- **Manual GUI pass (FRONT UCS, user):** preview and committed result now match for both
  rotate-items = Yes and No; the "Rotate items?" prompt semantics confirmed as intended
  (AutoCAD parity — not a defect).
- **SPEC:** no criteria change; REQ-305 revision note added (2026-09-08).
- **Assumptions / debt:** the typed-centre-Z limitation above; the FeatureLine preview omits
  copies when the instance delta has a Z component (tilted-plane rectangular array) rather than
  drawing them misplaced.
