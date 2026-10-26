# TASK: Qt ViewCube conforms to REQ-059 (issue #1034)

## Requirement authority
REQ-059 (accepted): labelled cube + W/N/S/E compass ring, six labelled faces, two square-up arrows relative to the active coordinate system, SW-isometric home, animated orientation changes (settle < 0.5 s, short way round, manual orbit cancels), legible in plan view, every click on the widget consumed. Related: REQ-154 (UCS), #153, #372.

## Acceptance criteria (issue #1034)
The nine criteria in the issue, verbatim: compass-ringed cube top-right; tracks the camera continuously; six labels shrunk not omitted; face click = standard view; arrows square to the next compass direction relative to the UCS and keep elevation; Home = SW isometric relative to the UCS; widget changes animate < 0.5 s and manual orbit takes over; legible in plan view; widget clicks never reach the viewport.

## Files / subsystems
- `src/viewport/ViewCube.hpp` (new, header-only, Qt/GL-free): scene geometry, hit-testing, click targets. Ported from the pre-#971 ImGui `ViewCube.cpp`, with the LEFT/RIGHT azimuths corrected (see assumptions).
- `src/qt/QtViewCube.hpp/.cpp` (new): paints a `Scene`.
- `src/qt/EditorViewport.cpp/.hpp`: draws the cube from the live camera, consumes presses/double-clicks/releases over the widget, animates widget orientation changes with the existing `CadStartViewAnimation`/`CadTickViewAnimation` (ticked by a 16 ms timer, which also ticks named-view restores that previously never advanced), cancels the animation on hand orbit.
- Tests: `tests/ViewCubeTests.cpp` (pure), `tests/QtEditorTests.cpp` (`issue1034...`, plus the old cube framebuffer test now samples the real TOP face).

## Approach
Reuse the existing domain animation and `CadUcsViewAzimuthOffsetDeg`; keep geometry pure so every criterion except pixels is unit-tested; Qt layer only paints and forwards.

## Architectural-boundary check
UI layer only; no new dependency, no global state. `ViewCube.hpp` depends on `Camera.hpp` alone.

## Assumptions / technical debt
- Camera azimuth places the eye at (-sin az, -cos az). So the LEFT face (normal -X) is looked at from az 90 and RIGHT from az 270, matching `ucs::OrthographicPresets`. The old ImGui table (and the ribbon/menu "Left"=270 / "Right"=90 entries) had them swapped, so clicking RIGHT showed the LEFT face. The cube now follows the geometry; the ribbon Named Views combo and View > Orientation menu are unchanged and still disagree (follow-up).
- Middle-button presses over the widget still pan/orbit; left and right presses are consumed.
- Ribbon/menu orientation changes still snap (`SetOrientation`); only widget-initiated changes (and named-view restores) animate.
