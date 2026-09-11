# Command Reference

Every command GoSurvey recognises at the command line, with its aliases and where it is documented
in full.

Command names are **not case-sensitive**. Typing is **fuzzy** — a partial or slightly wrong name
still resolves, and an unrecognised entry answers `Unknown command. Did you mean: …?`. Type `HELP`
for a compact list inside the program.

---


## By category

### Draw

| Command | Aliases | Description | Details |
|---|---|---|---|
| `3DPOLY` | `3DP`, `3DPOLYLINE` | Draw a polyline whose vertices each carry their own elevation | [[Commands/3dpoly]] |
| `ARC` | — | Draw an arc | [[Commands/arc]] |
| `CIRCLE` | `C` | Draw a circle | [[Commands/circle]] |
| `ELLIPSE` | `EL` | Draw an ellipse | [[Commands/ellipse]] |
| `FEATURELINE` | `FL` | Draw a feature line: named 3D linework with per-vertex elevations (REQ-087) | [[Commands/featureline]] |
| `FEATURELINELIST` | `FLLIST` | List every feature line and its vertices | [[Commands/featurelinelist]] |
| `HATCH` | `H`, `BHATCH` | Fill a closed area (pick an internal point) | [[Commands/hatch]] |
| `LINE` | `L` | Draw line segments | [[Commands/line]] |
| `POLYLINE` | `PL` | Draw a connected polyline | [[Commands/polyline]] |
| `RECT` | `RECTANG`, `RECTANGLE` | Draw a rectangle (two opposite corners) | [[Commands/rect]] |

### Solids and 3D

| Command | Aliases | Description | Details |
|---|---|---|---|
| `BOX` | — | Create a box solid: BOX <X,Y[,Z]> <length> <width> <height> | [[Commands/box]] |
| `CONE` | — | Create a cone solid: CONE <X,Y[,Z]> <base radius> <top radius> <height> | [[Commands/cone]] |
| `CYLINDER` | `CYL` | Create a cylinder solid: CYLINDER <X,Y[,Z]> <radius> <height> | [[Commands/cylinder]] |
| `EXTRUDE` | `EXT` | Extrude a selected closed polyline or circle into a solid: EXTRUDE <height> | [[Commands/extrude]] |
| `INTERSECT` | `IN` | Keep only the volume two selected solids share | [[Commands/intersect]] |
| `ISOLINES` | — | Curves drawn around a curved solid face: ISOLINES [0-256], or bare to report | [[Commands/isolines]] |
| `LOFT` | `LFT` | Loft a solid through two or more selected closed polylines or circles, in pick order | [[Commands/loft]] |
| `POLYSOLID` | `PSOLID` | Sweep a wall along a path: POLYSOLID, then points (A arc, C close, H/W/J, O object) | [[Commands/polysolid]] |
| `PRESSPULL` | `PP` | Move a solid FACE, or turn a closed shape into a solid: PRESSPULL, select a target, then a distance | [[Commands/presspull]] |
| `PYRAMID` | `PYR` | Create a pyramid solid: PYRAMID <X,Y[,Z]> <sides> <base r> <top r> <height> | [[Commands/pyramid]] |
| `REVOLVE` | `REV` | Revolve a selected closed polyline or circle about an axis into a solid | [[Commands/revolve]] |
| `SECTION` | — | Cross-section of the selected solids by the active UCS plane, as a closed polyline | [[Commands/section]] |
| `SLICE` | `SL` | Cut selected solids with a plane (three points), keeping one side or both | [[Commands/slice]] |
| `SOLIDCHECK` | `SCHECK` | Check every solid (or the selection): closed, manifold, oriented, self-intersecting | [[Commands/solidcheck]] |
| `SOLIDLIST` | `SOLIDS` | List every solid: kind, layer, volume, surface area, topology counts | [[Commands/solidlist]] |
| `SPHERE` | `SPH` | Create a sphere solid: SPHERE <X,Y[,Z]> <radius> | [[Commands/sphere]] |
| `SUBTRACT` | `SU` | Subtract the second selected solid from the first | [[Commands/subtract]] |
| `SWEEP` | `SWP` | Sweep a closed profile along a line, arc or open polyline path (T twist, A path-alignment) | [[Commands/sweep]] |
| `TORUS` | `TOR` | Create a torus solid: TORUS <X,Y[,Z]> <radius> <tube radius> | [[Commands/torus]] |
| `UNION` | `UNI` | Combine two selected solids into one | [[Commands/union]] |
| `WEDGE` | `WE` | Create a wedge solid: WEDGE <X,Y[,Z]> <length> <width> <height> | [[Commands/wedge]] |

### Annotation

| Command | Aliases | Description | Details |
|---|---|---|---|
| `DIMALIGNED` | `DAL` | Aligned dimension | [[Commands/dimaligned]] |
| `DIMANGULAR` | `DAN` | Angular dimension | [[Commands/dimangular]] |
| `DIMLINEAR` | `DLI` | Linear dimension | [[Commands/dimlinear]] |
| `DIMSTY` | `DIMSTYLE`, `DSTY` | Dimension style editor | [[Commands/dimsty]] |
| `MTEXT` | `MT` | Place multiline text | [[Commands/mtext]] |
| `STYLE` | `ST`, `DDSTYLE` | Text style manager: create / edit named text styles | [[Commands/style]] |
| `TEXT` | — | Place single-line text | [[Commands/text]] |

### Modify

| Command | Aliases | Description | Details |
|---|---|---|---|
| `ALIGN` | `AL` | Align objects to others | [[Commands/align]] |
| `ARRAY` | `AR` | Rectangular or polar array of copies | [[Commands/array]] |
| `BREAK` | `BR` | Split an object at one or two picked points | [[Commands/break]] |
| `CHAMFER` | `CHA` | Connect two curves with a straight bevel (Distance/Angle/Trim) | [[Commands/chamfer]] |
| `COPY` | `CP` | Copy objects | [[Commands/copy]] |
| `DELETE` | `DEL` | Erase objects | [[Commands/delete]] |
| `EXPLODE` | `X` | Explode selected block references | [[Commands/explode]] |
| `EXTEND` | `EX` | Extend objects to a boundary edge | [[Commands/extend]] |
| `FILLET` | `F` | Round a corner between two curves with a tangent arc (Radius/Trim) | [[Commands/fillet]] |
| `JOIN` | `J` | Join collinear objects | [[Commands/join]] |
| `LENGTHEN` | `LEN` | Change an object's length (DElta/Percent/Total/DYnamic) | [[Commands/lengthen]] |
| `MIRROR` | `MI` | Mirror objects across a line | [[Commands/mirror]] |
| `MOVE` | `M` | Move objects | [[Commands/move]] |
| `OFFSET` | `O` | Offset at a distance | [[Commands/offset]] |
| `OVERKILL` | `OK` | Remove duplicate geometry | [[Commands/overkill]] |
| `PASTE` | — | Paste from clipboard | [[Commands/paste]] |
| `PASTEORIG` | `PO` | Paste at original coordinates | [[Commands/pasteorig]] |
| `ROTATE` | `RO` | Rotate objects | [[Commands/rotate]] |
| `SCALE` | `SC` | Scale objects | [[Commands/scale]] |
| `STRETCH` | `S` | Crossing/window-select, then move only the vertices inside the box | [[Commands/stretch]] |
| `TRIM` | `TR` | Trim objects to an edge | [[Commands/trim]] |
| `TRIMSTATE` | — | TRIM mode: 0 = draw a line to trim (default), 1 = pick cutting edges | [[Commands/trimstate]] |

### Selection and visibility

| Command | Aliases | Description | Details |
|---|---|---|---|
| `HIDEOBJECTS` | — | Hide the selected objects | [[Commands/hideobjects]] |
| `ISOLATEOBJECTS` | `ISOLATE` | Hide everything except the selection | [[Commands/isolateobjects]] |
| `QUICKSELECT` | `QS` | Select by object properties | [[Commands/quickselect]] |
| `SELECT` | — | Build a selection set | [[Commands/select]] |
| `SELECTSIMILAR` | `SESIM` | Select entities matching the current selection's type, layer and colour | [[Commands/selectsimilar]] |
| `UNISOLATEOBJECTS` | `UNISOLATE` | Show every object hidden by isolation | [[Commands/unisolateobjects]] |

### View and navigation

| Command | Aliases | Description | Details |
|---|---|---|---|
| `CROSSHAIR3D` | `CURSOR3D`, `XHAIR3D` | 3D crosshair cursor showing the UCS axes: ON / OFF | [[Commands/crosshair3d]] |
| `FOV` | `LENS` | Perspective field of view, in degrees | [[Commands/fov]] |
| `GIZMO` | — | What the 3D gizmo does: GIZMO MOVE \| ROTATE \| SCALE | [[Commands/gizmo]] |
| `ORBIT` | `3DORBIT`, `3DO` | Free orbit the model view (drag with the left mouse button) | [[Commands/orbit]] |
| `PAN` | `P` | Pan the view (drag with the left mouse button) | [[Commands/pan]] |
| `PERSPECTIVE` | `PROJECTION`, `PERSP` | View projection: ON (perspective) / OFF (orthographic) | [[Commands/perspective]] |
| `PLAN` | — | View the XY plane of a coordinate system (does not change the UCS) | [[Commands/plan]] |
| `REGEN` | `RE` | Regenerate the drawing | [[Commands/regen]] |
| `UCS` | — | Define the User Coordinate System (N names one; restore/delete in the View Manager) | [[Commands/ucs]] |
| `UCSFOLLOW` | — | 0 = changing the UCS leaves the view alone; 1 = it switches to a plan view | [[Commands/ucsfollow]] |
| `VIEW` | `V`, `DDVIEW` | Named views: VIEW [Save/Restore/Delete/?] <name>, or VIEW alone for the View Manager | [[Commands/view]] |
| `VISUALSTYLE` | `VS`, `VSCURRENT` | Viewport visual style: 2D / HIDDEN / SHADED | [[Commands/visualstyle]] |
| `ZOOMEXTENTS` | `ZE` | Zoom to drawing extents | [[Commands/zoomextents]] |
| `ZOOMWINDOW` | `ZW` | Zoom to a window | [[Commands/zoomwindow]] |

### Layers and properties

| Command | Aliases | Description | Details |
|---|---|---|---|
| `LAYER` | `LA` | Open the Layer manager | [[Commands/layer]] |
| `VPFREEZE` | `VPF` | Freeze the picked entities' layers in the current viewport | [[Commands/vpfreeze]] |
| `VPTHAW` | `VPT` | Thaw the picked entities' layers in the current viewport | [[Commands/vpthaw]] |

### Survey and COGO

| Command | Aliases | Description | Details |
|---|---|---|---|
| `CREATEPOINTS` | `CRTPTS` | Create survey points | [[Commands/createpoints]] |
| `DIST` | `DI` | 3D distance between two points: dX, dY, dZ, slope distance | [[Commands/dist]] |
| `EXPORTPOINTS` | `EXPPTS` | Export survey points | [[Commands/exportpoints]] |
| `ID` | — | Identify point coordinates | [[Commands/id]] |
| `IMPORTPOINTS` | `IMPPTS` | Import survey points | [[Commands/importpoints]] |
| `INVERSE` | `INV` | Inverse between two points | [[Commands/inverse]] |
| `SURFELEV` | `SE` | Surface elevation at a point; grade between two | [[Commands/surfelev]] |
| `TRAVERSE` | `TRAV`, `TRAVERSEEDITOR` | Open the Traverse Editor | [[Commands/traverse]] |
| `VIEWPOINTS` | `VWPTS` | View / edit survey points | [[Commands/viewpoints]] |

### Surfaces and volumes

| Command | Aliases | Description | Details |
|---|---|---|---|
| `CATCHMENT` | `CATCH` | Catchment at an outlet: CATCHMENT <name>[, <x>, <y>] \| CATCHMENT EXTRACT | [[Commands/catchment]] |
| `DESIGNATEBOUNDARY` | `DBD` | Add a picked closed polyline as a surface boundary (outer/hide/show/clip) | [[Commands/designateboundary]] |
| `DESIGNATEBREAKLINE` | `DBL` | Add a picked line/polyline as a surface breakline | [[Commands/designatebreakline]] |
| `DESIGNATECONTOUR` | `DCON` | Add a picked line/polyline as a surface contour source | [[Commands/designatecontour]] |
| `EXTRACT` | — | Bake a surface's displayed contours into polylines: EXTRACT <surface>[, <layer>] | [[Commands/extract]] |
| `FLELEV` | — | Feature line elevations: FLELEV <n> [SET\|GRADEAHEAD\|GRADEBACK\|RAISE\|INSERT\|DELETE …] | [[Commands/flelev]] |
| `FLELEVEDIT` | — | Open the feature line elevation editor: FLELEVEDIT [<n>] | [[Commands/flelevedit]] |
| `QUICKPROFILE` | `QPROF` | Quick profile along two points: QUICKPROFILE <surface>[, <x1>, <y1>, <x2>, <y2>] | [[Commands/quickprofile]] |
| `SURFACEADDFILE` | `SFADDFILE` | Link a point file into a surface: SURFACEADDFILE <surface>, <path>[, <layout>[, HEADER]] | [[Commands/surfaceaddfile]] |
| `SURFACEADDPOINT` | `SFADDPT` | Add a TIN definition point: SURFACEADDPOINT <surface>[, <x>, <y>, <z>] | [[Commands/surfaceaddpoint]] |
| `SURFACECREATE` | `SFCREATE` | Create a surface from point groups: SURFACECREATE <name>, <group>[, <group>…] | [[Commands/surfacecreate]] |
| `SURFACECREATECORR` | `SFCORR` | Create a corridor surface: SURFACECREATECORR <name> | [[Commands/surfacecreatecorr]] |
| `SURFACECREATEGRID` | `SFGRID` | Create a grid surface: SURFACECREATEGRID <name>, ox, oy, sx, sy, cols, rows[, z…] | [[Commands/surfacecreategrid]] |
| `SURFACECREATEVOLGRID` | `SFVOLGRID` | Grid volume surface: SURFACECREATEVOLGRID <name>, <base>, <comparison> | [[Commands/surfacecreatevolgrid]] |
| `SURFACEDELETE` | `SFDELETE` | Delete a surface: SURFACEDELETE <name> | [[Commands/surfacedelete]] |
| `SURFACEDELPOINT` | `SFDELPT` | Delete nearest TIN definition point: SURFACEDELPOINT <surface>[, <x>, <y>] | [[Commands/surfacedelpoint]] |
| `SURFACEIMPORTFILE` | `SFIMPORTFILE` | Import a linked point file into the drawing and break the link | [[Commands/surfaceimportfile]] |
| `SURFACELIST` | `SFLIST` | List every surface and its full definition | [[Commands/surfacelist]] |
| `SURFACEMOVEPOINT` | `SFMOVEPT` | Move a TIN definition point: SURFACEMOVEPOINT <surface>[, <x1>, <y1>, <x2>, <y2>, <z2>] | [[Commands/surfacemovepoint]] |
| `SURFACEREBUILD` | `SFREBUILD` | Rebuild a surface now (all surfaces if no name): SURFACEREBUILD [<name>] | [[Commands/surfacerebuild]] |
| `SURFACERENAME` | `SFRENAME` | Rename a surface: SURFACERENAME <old>, <new> | [[Commands/surfacerename]] |
| `SURFACESTATS` | `SFSTATS` | Surface statistics: SURFACESTATS [<name>] | [[Commands/surfacestats]] |
| `SURFDELLINE` | `SFDELLINE` | Delete an interior TIN edge: SURFDELLINE <surface>[, <x>, <y>] | [[Commands/surfdelline]] |
| `SURFSTYLE` | `SS` | Surface style editor: contours, triangles, border (REQ-070) | [[Commands/surfstyle]] |
| `SURFSWAPEDGE` | `SFSWAP` | Swap a TIN interior edge: SURFSWAPEDGE <surface>[, <x>, <y>] | [[Commands/surfswapedge]] |
| `UNDESIGNATE` | `UNDES` | Remove one definition item: UNDESIGNATE <surface>, <BREAKLINE\|BOUNDARY\|POINTFILE\|CONTOUR>, <n> | [[Commands/undesignate]] |
| `VOLCSV` | — | Write CSV of volume results: VOLCSV [<path>] | [[Commands/volcsv]] |
| `VOLDASH` | — | Volume Dashboard: live cut/fill/net panel between two surfaces (REQ-073) | [[Commands/voldash]] |
| `VOLREPORT` | — | Insert MTEXT of the last volume report: VOLREPORT [TABLE] | [[Commands/volreport]] |
| `VOLTABLE` | — | Insert a TABLE of the last volume report: VOLTABLE | [[Commands/voltable]] |
| `VOLUMES` | `VOL` | Cut/fill/net volume between two surfaces: VOLUMES <base>, <comparison>[, <clip id>] | [[Commands/volumes]] |
| `VOLUMESURFACE` | `VOLSURF` | Create a TIN volume surface: VOLUMESURFACE <name>, <base>, <comparison> | [[Commands/volumesurface]] |
| `WATERDROP` | `WDROP` | Water-drop path: WATERDROP <name>[, <x>, <y>] \| WATERDROP EXTRACT | [[Commands/waterdrop]] |
| `WATERSHED` | `WSHED` | Watershed basins: WATERSHED [<name>] | [[Commands/watershed]] |

### Paper space and plotting

| Command | Aliases | Description | Details |
|---|---|---|---|
| `MSPACE` | `MS` | Edit the model through the selected viewport (floating model space) | [[Commands/mspace]] |
| `MVIEW` | `RECTVIEWPORT`, `RECTVP` | Rectangular paper-space viewport (two clicks) | [[Commands/mview]] |
| `PLOTSCALE` | `PSCALE` | Set the plot scale | [[Commands/plotscale]] |
| `PSPACE` | `PS` | Return to paper space from a floating viewport | [[Commands/pspace]] |

### Blocks and attributes

| Command | Aliases | Description | Details |
|---|---|---|---|
| `ATTDEF` | — | Attribute definition (in BEDIT) | [[Commands/attdef]] |
| `ATTEDIT` | — | Edit attribute values on selected inserts | [[Commands/attedit]] |
| `ATTEXT` | — | Extract attributes to the command log / file | [[Commands/attext]] |
| `ATTSYNC` | — | Synchronize attributes from definitions | [[Commands/attsync]] |
| `BCLOSE` | — | Close the block editor | [[Commands/bclose]] |
| `BEDIT` | — | Open the block editor: BEDIT <name> | [[Commands/bedit]] |
| `BLOCK` | — | Define a block from the selection: BLOCK <name>, <x>, <y>[, CONVERT\|DELETE\|RETAIN] | [[Commands/block]] |
| `BLOCKFAV` | — | Favorite blocks | [[Commands/blockfav]] |
| `BLOCKIMPORT` | — | Import block definitions (.dxf/.dwg/.sat); omit the path to browse | [[Commands/blockimport]] |
| `BLOCKLIB` | `BLOCKBROWSER` | List the drawing block library with previews | [[Commands/blocklib]] |
| `BLOCKLIST` | — | List block definitions | [[Commands/blocklist]] |
| `BLOCKMODEL` | — | Switch to model space | [[Commands/blockmodel]] |
| `BLOCKPAPER` | — | Switch to the first paper layout | [[Commands/blockpaper]] |
| `BLOCKRECENT` | — | Recently used blocks | [[Commands/blockrecent]] |
| `BLOCKSEARCH` | — | Search block names | [[Commands/blocksearch]] |
| `BLOCKSTATS` | — | Definition statistics | [[Commands/blockstats]] |
| `BSAVE` | — | Save the block being edited | [[Commands/bsave]] |
| `BSAVEAS` | — | Save the edited block under a new name | [[Commands/bsaveas]] |
| `COPYCLIP` | — | Copy the selection to the CAD clipboard | [[Commands/copyclip]] |
| `COPYSEL` | — | Copy selected inserts: COPYSEL dx, dy | [[Commands/copysel]] |
| `INSERT` | `I` | Insert a block (dialog); or INSERT <name>, <x>, <y>[, sx, sy, rotDeg[, sz, z]] | [[Commands/insert]] |
| `MAKEBLOCK` | — | Create an empty block definition: MAKEBLOCK <name> | [[Commands/makeblock]] |
| `MIRRORSEL` | — | Mirror selected inserts: MIRRORSEL x0, y0, x1, y1 | [[Commands/mirrorsel]] |
| `MKLINE` | — | Add a line: MKLINE x0, y0, x1, y1 | [[Commands/mkline]] |
| `MOVESEL` | — | Move selected inserts: MOVESEL dx, dy | [[Commands/movesel]] |
| `PASTEBLOCK` | — | Paste clipboard block references: PASTEBLOCK dx, dy | [[Commands/pasteblock]] |
| `PURGE` | `-PURGE` | Purge unused block definitions | [[Commands/purge]] |
| `ROTATESEL` | — | Rotate selected inserts: ROTATESEL x, y, deg | [[Commands/rotatesel]] |
| `SCALESEL` | — | Scale selected inserts: SCALESEL x, y, factor | [[Commands/scalesel]] |
| `SELBLOCK` | — | Select the last block reference | [[Commands/selblock]] |
| `SELLINE` | — | Select the last line | [[Commands/selline]] |
| `WBLOCK` | — | Write a block definition to its own .dwg file | [[Commands/wblock]] |

### Settings and utilities

| Command | Aliases | Description | Details |
|---|---|---|---|
| `ELEV` | `UCS` | Elevation new geometry is drawn at (W = world Z 0) | [[Commands/elev]] |
| `HELP` | — | Show command help | — |
| `OPTIONS` | `OP`, `SETTINGS` | Open the Options dialog | [[Commands/options]] |
| `UNITS` | `UN`, `DDUNITS` | Drawing units: display precision & angle format | [[Commands/units]] |

### Import and reference geometry

| Command | Aliases | Description | Details |
|---|---|---|---|
| `IMPORTMODEL` | `GLTF`, `IMPORT3D` | Import a glTF/GLB 3D model as reference geometry | [[Commands/importmodel]] |
| `PDFATTACH` | `PA` | Attach a PDF underlay | [[Commands/pdfattach]] |

### Diagnostics

| Command | Aliases | Description |
|---|---|---|
| `BENCH` | — | REQ-100 frame-budget benchmark: BENCH [segments] \| BENCH SURFACE [points] \| BENCH MESH [triangles] \| BENCH SOLID [count] |

---

## Alphabetical index

| Command | Aliases | Page |
|---|---|---|
| `3DPOLY` | `3DP`, `3DPOLYLINE` | [[Commands/3dpoly]] |
| `ALIGN` | `AL` | [[Commands/align]] |
| `ARC` | — | [[Commands/arc]] |
| `ARRAY` | `AR` | [[Commands/array]] |
| `ATTDEF` | — | [[Commands/attdef]] |
| `ATTEDIT` | — | [[Commands/attedit]] |
| `ATTEXT` | — | [[Commands/attext]] |
| `ATTSYNC` | — | [[Commands/attsync]] |
| `BCLOSE` | — | [[Commands/bclose]] |
| `BEDIT` | — | [[Commands/bedit]] |
| `BENCH` | — | [[Commands/bench]] |
| `BLOCK` | — | [[Commands/block]] |
| `BLOCKFAV` | — | [[Commands/blockfav]] |
| `BLOCKIMPORT` | — | [[Commands/blockimport]] |
| `BLOCKLIB` | `BLOCKBROWSER` | [[Commands/blocklib]] |
| `BLOCKLIST` | — | [[Commands/blocklist]] |
| `BLOCKMODEL` | — | [[Commands/blockmodel]] |
| `BLOCKPAPER` | — | [[Commands/blockpaper]] |
| `BLOCKRECENT` | — | [[Commands/blockrecent]] |
| `BLOCKSEARCH` | — | [[Commands/blocksearch]] |
| `BLOCKSTATS` | — | [[Commands/blockstats]] |
| `BOX` | — | [[Commands/box]] |
| `BREAK` | `BR` | [[Commands/break]] |
| `BSAVE` | — | [[Commands/bsave]] |
| `BSAVEAS` | — | [[Commands/bsaveas]] |
| `CATCHMENT` | `CATCH` | [[Commands/catchment]] |
| `CHAMFER` | `CHA` | [[Commands/chamfer]] |
| `CIRCLE` | `C` | [[Commands/circle]] |
| `CONE` | — | [[Commands/cone]] |
| `COPY` | `CP` | [[Commands/copy]] |
| `COPYCLIP` | — | [[Commands/copyclip]] |
| `COPYSEL` | — | [[Commands/copysel]] |
| `CREATEPOINTS` | `CRTPTS` | [[Commands/createpoints]] |
| `CROSSHAIR3D` | `CURSOR3D`, `XHAIR3D` | [[Commands/crosshair3d]] |
| `CYLINDER` | `CYL` | [[Commands/cylinder]] |
| `DELETE` | `DEL` | [[Commands/delete]] |
| `DESIGNATEBOUNDARY` | `DBD` | [[Commands/designateboundary]] |
| `DESIGNATEBREAKLINE` | `DBL` | [[Commands/designatebreakline]] |
| `DESIGNATECONTOUR` | `DCON` | [[Commands/designatecontour]] |
| `DIMALIGNED` | `DAL` | [[Commands/dimaligned]] |
| `DIMANGULAR` | `DAN` | [[Commands/dimangular]] |
| `DIMLINEAR` | `DLI` | [[Commands/dimlinear]] |
| `DIMSTY` | `DIMSTYLE`, `DSTY` | [[Commands/dimsty]] |
| `DIST` | `DI` | [[Commands/dist]] |
| `ELEV` | `UCS` | [[Commands/elev]] |
| `ELLIPSE` | `EL` | [[Commands/ellipse]] |
| `EXPLODE` | `X` | [[Commands/explode]] |
| `EXPORTPOINTS` | `EXPPTS` | [[Commands/exportpoints]] |
| `EXTEND` | `EX` | [[Commands/extend]] |
| `EXTRACT` | — | [[Commands/extract]] |
| `EXTRUDE` | `EXT` | [[Commands/extrude]] |
| `FEATURELINE` | `FL` | [[Commands/featureline]] |
| `FEATURELINELIST` | `FLLIST` | [[Commands/featurelinelist]] |
| `FILLET` | `F` | [[Commands/fillet]] |
| `FLELEV` | — | [[Commands/flelev]] |
| `FLELEVEDIT` | — | [[Commands/flelevedit]] |
| `FOV` | `LENS` | [[Commands/fov]] |
| `GIZMO` | — | [[Commands/gizmo]] |
| `HATCH` | `H`, `BHATCH` | [[Commands/hatch]] |
| `HELP` | — | [[Commands/help]] |
| `HIDEOBJECTS` | — | [[Commands/hideobjects]] |
| `ID` | — | [[Commands/id]] |
| `IMPORTMODEL` | `GLTF`, `IMPORT3D` | [[Commands/importmodel]] |
| `IMPORTPOINTS` | `IMPPTS` | [[Commands/importpoints]] |
| `INSERT` | `I` | [[Commands/insert]] |
| `INTERSECT` | `IN` | [[Commands/intersect]] |
| `INVERSE` | `INV` | [[Commands/inverse]] |
| `ISOLATEOBJECTS` | `ISOLATE` | [[Commands/isolateobjects]] |
| `ISOLINES` | — | [[Commands/isolines]] |
| `JOIN` | `J` | [[Commands/join]] |
| `LAYER` | `LA` | [[Commands/layer]] |
| `LENGTHEN` | `LEN` | [[Commands/lengthen]] |
| `LINE` | `L` | [[Commands/line]] |
| `LOFT` | `LFT` | [[Commands/loft]] |
| `MAKEBLOCK` | — | [[Commands/makeblock]] |
| `MIRROR` | `MI` | [[Commands/mirror]] |
| `MIRRORSEL` | — | [[Commands/mirrorsel]] |
| `MKLINE` | — | [[Commands/mkline]] |
| `MOVE` | `M` | [[Commands/move]] |
| `MOVESEL` | — | [[Commands/movesel]] |
| `MSPACE` | `MS` | [[Commands/mspace]] |
| `MTEXT` | `MT` | [[Commands/mtext]] |
| `MVIEW` | `RECTVIEWPORT`, `RECTVP` | [[Commands/mview]] |
| `OFFSET` | `O` | [[Commands/offset]] |
| `OPTIONS` | `OP`, `SETTINGS` | [[Commands/options]] |
| `ORBIT` | `3DORBIT`, `3DO` | [[Commands/orbit]] |
| `OVERKILL` | `OK` | [[Commands/overkill]] |
| `PAN` | `P` | [[Commands/pan]] |
| `PASTE` | — | [[Commands/paste]] |
| `PASTEBLOCK` | — | [[Commands/pasteblock]] |
| `PASTEORIG` | `PO` | [[Commands/pasteorig]] |
| `PDFATTACH` | `PA` | [[Commands/pdfattach]] |
| `PERSPECTIVE` | `PROJECTION`, `PERSP` | [[Commands/perspective]] |
| `PLAN` | — | [[Commands/plan]] |
| `PLOTSCALE` | `PSCALE` | [[Commands/plotscale]] |
| `POLYLINE` | `PL` | [[Commands/polyline]] |
| `POLYSOLID` | `PSOLID` | [[Commands/polysolid]] |
| `PRESSPULL` | `PP` | [[Commands/presspull]] |
| `PSPACE` | `PS` | [[Commands/pspace]] |
| `PURGE` | `-PURGE` | [[Commands/purge]] |
| `PYRAMID` | `PYR` | [[Commands/pyramid]] |
| `QUICKPROFILE` | `QPROF` | [[Commands/quickprofile]] |
| `QUICKSELECT` | `QS` | [[Commands/quickselect]] |
| `RECT` | `RECTANG`, `RECTANGLE` | [[Commands/rect]] |
| `REGEN` | `RE` | [[Commands/regen]] |
| `REVOLVE` | `REV` | [[Commands/revolve]] |
| `ROTATE` | `RO` | [[Commands/rotate]] |
| `ROTATESEL` | — | [[Commands/rotatesel]] |
| `SCALE` | `SC` | [[Commands/scale]] |
| `SCALESEL` | — | [[Commands/scalesel]] |
| `SECTION` | — | [[Commands/section]] |
| `SELBLOCK` | — | [[Commands/selblock]] |
| `SELECT` | — | [[Commands/select]] |
| `SELECTSIMILAR` | `SESIM` | [[Commands/selectsimilar]] |
| `SELLINE` | — | [[Commands/selline]] |
| `SLICE` | `SL` | [[Commands/slice]] |
| `SOLIDCHECK` | `SCHECK` | [[Commands/solidcheck]] |
| `SOLIDLIST` | `SOLIDS` | [[Commands/solidlist]] |
| `SPHERE` | `SPH` | [[Commands/sphere]] |
| `STRETCH` | `S` | [[Commands/stretch]] |
| `STYLE` | `ST`, `DDSTYLE` | [[Commands/style]] |
| `SUBTRACT` | `SU` | [[Commands/subtract]] |
| `SURFACEADDFILE` | `SFADDFILE` | [[Commands/surfaceaddfile]] |
| `SURFACEADDPOINT` | `SFADDPT` | [[Commands/surfaceaddpoint]] |
| `SURFACECREATE` | `SFCREATE` | [[Commands/surfacecreate]] |
| `SURFACECREATECORR` | `SFCORR` | [[Commands/surfacecreatecorr]] |
| `SURFACECREATEGRID` | `SFGRID` | [[Commands/surfacecreategrid]] |
| `SURFACECREATEVOLGRID` | `SFVOLGRID` | [[Commands/surfacecreatevolgrid]] |
| `SURFACEDELETE` | `SFDELETE` | [[Commands/surfacedelete]] |
| `SURFACEDELPOINT` | `SFDELPT` | [[Commands/surfacedelpoint]] |
| `SURFACEIMPORTFILE` | `SFIMPORTFILE` | [[Commands/surfaceimportfile]] |
| `SURFACELIST` | `SFLIST` | [[Commands/surfacelist]] |
| `SURFACEMOVEPOINT` | `SFMOVEPT` | [[Commands/surfacemovepoint]] |
| `SURFACEREBUILD` | `SFREBUILD` | [[Commands/surfacerebuild]] |
| `SURFACERENAME` | `SFRENAME` | [[Commands/surfacerename]] |
| `SURFACESTATS` | `SFSTATS` | [[Commands/surfacestats]] |
| `SURFDELLINE` | `SFDELLINE` | [[Commands/surfdelline]] |
| `SURFELEV` | `SE` | [[Commands/surfelev]] |
| `SURFSTYLE` | `SS` | [[Commands/surfstyle]] |
| `SURFSWAPEDGE` | `SFSWAP` | [[Commands/surfswapedge]] |
| `SWEEP` | `SWP` | [[Commands/sweep]] |
| `TEXT` | — | [[Commands/text]] |
| `TORUS` | `TOR` | [[Commands/torus]] |
| `TRAVERSE` | `TRAV`, `TRAVERSEEDITOR` | [[Commands/traverse]] |
| `TRIM` | `TR` | [[Commands/trim]] |
| `TRIMSTATE` | — | [[Commands/trimstate]] |
| `UCS` | — | [[Commands/ucs]] |
| `UCSFOLLOW` | — | [[Commands/ucsfollow]] |
| `UNDESIGNATE` | `UNDES` | [[Commands/undesignate]] |
| `UNION` | `UNI` | [[Commands/union]] |
| `UNISOLATEOBJECTS` | `UNISOLATE` | [[Commands/unisolateobjects]] |
| `UNITS` | `UN`, `DDUNITS` | [[Commands/units]] |
| `VIEW` | `V`, `DDVIEW` | [[Commands/view]] |
| `VIEWPOINTS` | `VWPTS` | [[Commands/viewpoints]] |
| `VISUALSTYLE` | `VS`, `VSCURRENT` | [[Commands/visualstyle]] |
| `VOLCSV` | — | [[Commands/volcsv]] |
| `VOLDASH` | — | [[Commands/voldash]] |
| `VOLREPORT` | — | [[Commands/volreport]] |
| `VOLTABLE` | — | [[Commands/voltable]] |
| `VOLUMES` | `VOL` | [[Commands/volumes]] |
| `VOLUMESURFACE` | `VOLSURF` | [[Commands/volumesurface]] |
| `VPFREEZE` | `VPF` | [[Commands/vpfreeze]] |
| `VPTHAW` | `VPT` | [[Commands/vpthaw]] |
| `WATERDROP` | `WDROP` | [[Commands/waterdrop]] |
| `WATERSHED` | `WSHED` | [[Commands/watershed]] |
| `WBLOCK` | — | [[Commands/wblock]] |
| `WEDGE` | `WE` | [[Commands/wedge]] |
| `ZOOMEXTENTS` | `ZE` | [[Commands/zoomextents]] |
| `ZOOMWINDOW` | `ZW` | [[Commands/zoomwindow]] |

---

## Commands that accept arguments on one line

Most commands prompt step by step. These also accept everything on a single line:

| Form | Effect |
|---|---|
| `TRIMSTATE 1` | Sets the value without prompting |
| `ELEV 125.4` | Sets the work-plane elevation |
| `ELEV W` / `UCS W` | Returns to the world XY plane |
| `VS SHADED` | Sets the visual style (`2D`, `HIDDEN`, `SHADED`) |
| `PSCALE 50` | Sets the plot scale to 1 plotted inch = 50 model units |
| `IMPORTMODEL "C:\path\model.glb" 0.0833 100 200 0` | Path, unit scale, then insertion X Y Z |
| `BENCH 250000 900` | Segment count and frame count |

Typing the bare command instead opens the prompt or a file dialog.

---

## Features with no command

Some things are only reachable from the interface:

| Feature | Where |
|---|---|
| New / Open / Save / Save As | **File** menu, `Ctrl+S` for Save |
| Import and Export DXF / DWG | **File** menu |
| Undo / Redo | `Ctrl+Z` / `Ctrl+Shift+Z`, Edit menu, ribbon |
| Copy / Paste to clipboard | `Ctrl+C` / `Ctrl+V`, Edit menu, ribbon |
| Cut | Right-click ▸ Clipboard ▸ Cut, `Ctrl+X` |
| Options | **View → Settings…** |
| Plot and Batch Plot | **Layout ribbon** in paper space |
| Page Setup Manager, Viewports, Move or Copy layout | Right-click a layout tab |
| Traverse Editor | Ribbon **Survey → Traverse** |
| Point Groups, Surfaces | Ribbon **Survey → Groups / Surfaces** |
| Selection panel | Status bar **SEL** |
| Select similar, Clear selection | Right-click in the viewport with a selection |
