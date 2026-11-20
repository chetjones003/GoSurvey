# GoSurvey Feature Showcase

Open `GoSurvey Feature Showcase.gsproj` in GoSurvey. Every DWG in this project was generated from newly authored geometry for this showcase; none of the repository's sample DWGs were copied or used as a base.

| Drawing | Contents |
| --- | --- |
| `01-Geometry-and-Civil-Objects.dwg` | Items 1–8 together: line box with endpoint markers, closed and rectangular polylines, arcs and bulged arc, elevated polylines, and survey-control marker geometry. |
| `09-Mountainous-Surface.dwg` | A native GoSurvey TIN surface built from a new 25 × 20 grid (500 survey points) over a synthetic ridge-and-basin terrain, with interpolated minor and major contours displayed from its surface style. |
| `10-Grading.dwg` | A new pad and four-step grading study with toe/daylight, pad and slope lines. |
| `11-Alignments.dwg` | A new access-route centerline with station ticks, offsets and profile markers. |
| `12-Primitive-3D-Models.dwg` | Original native GoSurvey solids for box, wedge, pyramid, cylinder, cone, sphere and torus, alongside illustrated wireframe equipment. |
| `13-Complex-Piping-System.dwg` | An original piping layout with four native swept pipe runs, twin headers, risers, branches, vessel, pumps and equipment skids. |
| `14-Additional-Features.dwg` | An original site plan with building/parking areas, storm and sanitary routes, and structure symbols. |

The project is intended as editable sample geometry and workflow practice. Terrain is a GoSurvey TIN derived from the included survey-point observations in `Points/mountain-points.csv`, which the surface keeps as its registered source. The native solids and pipe runs exercise GoSurvey's own 3D model types. Grading and alignment drawings remain authored geometry studies rather than Civil 3D grading-group and alignment objects.

## Regenerate

After building GoSurvey, run `python samples/GoSurvey-Feature-Showcase/generate_showcase.py` from the repository root. The script writes the terrain observations to `Points/mountain-points.csv`, creates temporary authored DXF sources, imports them through GoSurvey's headless document path and saves GoSurvey DWGs into `Drawings/`.
