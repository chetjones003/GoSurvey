# GoSurvey User Manual

GoSurvey is a 2D/3D CAD and COGO platform for land surveyors. It draws linework, holds a
survey-point database, reduces raw field observations, builds TIN surfaces, and plots sheets —
and it exchanges drawings with AutoCAD and Civil 3D through DXF, DWG, and CSV.

This wiki is the **end-user manual**. It documents what the program actually does today, including
the places where a feature is present but not finished.

> **Current version:** 0.5.3 · Windows x64 · [Releases](https://github.com/chetjones003/GoSurvey/releases)

---

## Start here

| Page | What it covers |
|---|---|
| **[[Getting Started]]** | Install, first launch, new/open/save, your first drawing and first survey |
| **[[User Interface]]** | Every panel, bar, tab, and button on screen |
| **[[Command Reference]]** | The master index of every typed command and its aliases |
| **[[Keyboard Shortcuts]]** | Every key the program listens for |

## Drawing and editing

- **[[Drawing Tools]]** — LINE, POLYLINE, ARC, CIRCLE, ELLIPSE, RECT, HATCH
- **[[Modify Tools]]** — MOVE, COPY, ROTATE, SCALE, OFFSET, TRIM, JOIN, DELETE, OVERKILL, ALIGN
- **[[Object Selection]]** — picking, windows, cycling, grips, QUICKSELECT, isolation
- **[[Object Snaps]]** — the eight snap types, the OSNAP toggle, and the one-shot override
- **[[Coordinate Input]]** — what you may type at a point prompt, and what you may not
- **[[Drafting Aids]]** — Ortho, grid, dynamic input, elevation and the work plane
- **[[Annotation]]** — TEXT, MTEXT, the rich-text editor, dimensions, text styles
- **[[Layers]]** — the Layer Manager, per-viewport freeze, and layer properties
- **[[Properties]]** — the Properties panel, per-object-type fields

## Surveying

- **[[Survey Points]]** — creating, editing, importing, and exporting the point database
- **[[Point Groups and Surfaces]]** — rule-based groups and TIN surfaces
- **[[Traverse Editor]]** — raw observations, Face 1/Face 2, least-squares closure, FBK import
- **[[Coordinate Alignment]]** — ALIGN, the 2D Helmert fit, control-point tagging
- **[[Inquiry Commands]]** — ID, INVERSE, SURFELEV

## Views, sheets, and output

- **[[Views and Navigation]]** — zoom, pan, orbit, the ViewCube, visual styles
- **[[Paper Space and Layouts]]** — layouts, viewports, floating model space
- **[[Plotting]]** — Page Setup Manager, Plot, Batch Plot
- **[[PDF Underlays]]** — attaching a PDF and snapping to it
- **[[Import and Export]]** — DXF, DWG, CSV, FBK, glTF/GLB, STL, JSON
- **[[Files and Drawings]]** — the `.gs` file, drawing tabs, templates, updates

## Reference

- **[[Settings and Options]]** — every tab of the Options dialog
- **[[Command Line]]** — the command bar, autocomplete, history, dynamic input
- **[[Workflows]]** — start-to-finish tutorials
- **[[Troubleshooting]]** — problem → cause → solution
- **[[FAQ]]**
- **[[Known Limitations]]** — what is present but unfinished

---

## Conventions used in this manual

- `LINE` — a command you type in the command line. Case does not matter.
- `L` — a command alias.
- **Bold** — something you click, or a label printed on screen.
- `Specify first point:` — a prompt that appears in the command log.
- **North is 0°, angles increase clockwise.** This is the survey convention and it never changes,
  no matter how angles are *displayed*. See [[Coordinate Input]].
- **World X = Easting, World Y = Northing, World Z = Elevation.**
