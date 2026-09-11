# PDF Underlays

**Ribbon:** Draw → PDF Attach **Command:** `PDFATTACH`, `PA`

Attaches a PDF page as a raster underlay you can **snap to**. This is how you trace a recorded
plat, a utility as-built, or a GIS export without redrawing it first.

---

## Attaching a PDF

```
PDFATTACH — select PDF file and options in the dialog, then place the underlay.
```

The **PDF Attach** dialog:

| Section | Controls |
|---|---|
| **PDF File** | Path and **Browse…** |
| **Page Selection** | Thumbnails of every page — `%d page(s)`. Click the page you want |
| **Placement** | **Specify on screen**, or type **Insertion X:**, **Insertion Y:**, **Scale:**, **Rotation °:** |
| **Snap Recognition** | Which features to detect for snapping |
| **Raster DPI:** | Rasterisation resolution, 72–300 |

Then press **Attach**.

- **Specify on screen** — `PDFATTACH — click in viewport to set insertion point. ESC to cancel.`
  Click to place, then set the scale.
- **Direct insert** — enter the insertion point, scale, and rotation numerically and it is placed
  without a click.

While the page is being converted the dialog shows `Rasterizing... ` and the log says
`PDFATTACH — rasterizing, please wait...`. On success: `PDFATTACH — underlay placed.`

### Raster DPI

Higher DPI means sharper geometry and more snap targets, at the cost of memory and rasterisation
time. 150 is a reasonable default for a plan sheet; go to 300 for a dense GIS export where the
lines are thin.

---

## Background handling

Both light-background PDFs (white paper) and dark-background PDFs (CAD exports) work. The
background is detected automatically and made transparent, so the underlay floats cleanly over your
drawing at any opacity.

The **Background** toggle on the PDF Underlay ribbon section switches this:

| State | Tooltip |
|---|---|
| **Background ON** | *lines visible, paper transparent. Click to show paper.* |
| **Background OFF** | *full raster image visible. Click to hide paper.* |

---

## The PDF Underlay ribbon section

Appears when a PDF attachment is selected.

| Control | What it does |
|---|---|
| **Background** | Toggles paper transparency, as above |
| **Vectorize** | *Vectorize Lines — add PDF snap-line geometry as drawing entities on the current layer* |
| **Fade** | 0–100% opacity slider |
| **Snap: L / C / T** | Per-underlay snap toggles for **L**ines, **C**ircles, **T**ext |

### Vectorize

**Vectorize** converts the snap-lines GoSurvey detected in the raster into **real drawing entities**
on the current layer. It is a bulk trace: instead of drawing over every line by hand, you get
geometry you can then trim, join, and clean up with `OVERKILL`.

Set the current layer before you press it — everything lands there.

---

## Snapping to a PDF

Snap targets are found by analysing the **rendered raster image**, not the internal PDF path
structure. This matters: GIS exports, scanned drawings, and dense CAD PDFs routinely contain path
objects that do not correspond to any visible line. Reading the image is what makes snap land on
what you can actually see.

| Target | How it is recognised |
|---|---|
| **Endpoints** | Line ends and stroke terminals — one foreground neighbour in the raster topology |
| **Corners** | Bends and junctions where two lines meet at an angle — two non-opposite neighbours |
| **Junctions** | T-intersections and crossings — three or more neighbours |
| **Midpoints** | On detected PDF line segments |
| **Perpendicular** | On detected PDF line segments |

Candidates are filtered through a visibility mask so only points in areas with visible content are
offered, and a spatial grid keeps lookup fast even on PDFs with thousands of targets.

Object snap must be on (`F3`) and the underlay's own **Lines / Circles / Text** toggles decide
what it offers. See [[Object Snaps]].

---

## Multiple underlays

Several PDFs can be attached at once. Each has independent position, scale, rotation, opacity, and
snap toggles.

Selecting several at once gives a bulk **Fade** control in the Properties panel:
*"N PDF underlays selected."*

---

## Properties

Select an underlay and the Properties panel shows:

| Field |
|---|
| File name and page number |
| Insert X · Insert Y |
| Scale · Rotation (deg) |
| Fade |
| Object Snap — Lines · Circles |

---

## A tracing workflow

**Goal:** get a recorded plat into a drawing at real-world coordinates.

1. `PA` and attach the plat PDF at, say, 200 DPI. Place it roughly.
2. Set **Fade** so you can see your own linework against it.
3. Turn **OSNAP** on (`F3`) and check the underlay's **Lines** snap toggle.
4. Draw the boundary with `LINE` or `POLYLINE`, snapping to the PDF corners. Or press
   **Vectorize** to convert the detected lines in bulk, then clean up.
5. Run `OVERKILL` to remove duplicates and merge collinear segments.
6. If you have control coordinates for two monuments, run `ALIGN` to move the traced geometry onto
   state plane — see [[Coordinate Alignment]].
7. Delete or fade the underlay once you are done.

---

## Common problems

| Problem | Cause | Solution |
|---|---|---|
| `Unable to open PDF.` | The file is corrupt, encrypted, or not a PDF | Re-export it, or open and re-save it from a PDF reader |
| `PDFATTACH — failed to rasterize page.` | The page could not be rendered | Try a lower **Raster DPI**, or a different page |
| Nothing to snap to | The underlay's **Lines** toggle is off, or OSNAP is off | Check both; press `F3` |
| Snap targets are sparse or blobby | The raster DPI is too low for the line weight | Re-attach at a higher DPI |
| The underlay hides the drawing | Fade is at 0% and Background is on | Raise **Fade**, or turn **Background** off |
| The traced geometry is the wrong size | The insertion scale was a guess | `SCALE` with **R** (reference length) across a known dimension, or `ALIGN` with control pairs |
| Vectorize produced a mess of short segments | Raster tracing always does, on a busy sheet | `OVERKILL`, then `JOIN` |

---

## Related

[[Object Snaps]] · [[Coordinate Alignment]] · [[Modify Tools]] · [[Import and Export]]
