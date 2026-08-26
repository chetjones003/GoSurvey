---
name: project_dxf_survey_point_xdata
description: Survey points round-trip through DXF via a GOSURVEY XDATA schema (REQ-023, ADR-005)
metadata:
  type: project
---

Survey points survive a DXF export→import round-trip (REQ-023, ADR-005; PR #38,
issue #37). Before this, export wrote each survey point as a `POINT` but import
expanded *every* `POINT` into 4 cross-line segments — losing all survey points.

**XDATA schema** (all in `src/io/DxfIo.cpp`, app id `GOSURVEY` registered in the
APPID table — its handle is appended at the END of the symbol-handle range so no
existing handle shifts):
- Survey `POINT`: `1001 GOSURVEY`, `1071 id`, `1070 labelStyle`, `1000 description`.
  Coordinates stay in `10/20/30`, layer in `8`.
- Survey-label `MTEXT`: `1001 GOSURVEY` marker only.

**Import:** a `POINT` with the GOSURVEY XDATA → rebuilt `SurveyPoint` (apply
`xf.apply` + subtract `worldDocumentOrigin`, like geometry, so it lands 1:1); a
`POINT` without it keeps the cross-line behavior. Survey-label MTEXTs are skipped;
after the entity loop, each reconstructed point regenerates its own linked label
via `EnsureSurveyPointLabelMtext` (so no duplicate/orphan). Import clears
`st.surveyPoints` first ("replace" semantics; `ClearCadGeometry` does NOT clear
survey points).

Note: import now calls `EnsureSurveyPointLabelMtext`, which uses
`ImGui::GetFont()` — fine in the app (import runs mid-frame); a headless harness
must stand up a minimal ImGui context. DXF round-trips can't be unit-tested
(ADR-002 keeps the test target Domain-only); verify with a harness linked against
the compiled objects. See [[project_dxf_stateplane_precision]].
