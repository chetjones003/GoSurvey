# Workflows

Start-to-finish tasks, using only what GoSurvey actually does.

---

## 1. Set up a new project drawing

**Goal:** a drawing configured the way you work, saved as a template.

**Prerequisites:** none.

1. **File → New.**
2. Type `UNITS`. Set **Length → Precision** (3 decimals suits feet), **Angle → Type** to
   `Deg/Min/Sec` or `Surveyor's Units`, and **Insertion scale** to `Feet` or `Meters`. Press
   **Apply**.
3. Type `LAYER`. Add the layers you use — `BOUNDARY`, `TOPO`, `TEXT`, `VPORTS`, and so on. Set each
   one's colour and linetype. Untick **Plot** for `VPORTS`, so viewport borders never plot.
4. Type `STYLE`. Create the text styles you use and **Set Current** on the default one.
5. Set the **annotation scale** on the status bar to the scale you usually plot at.
6. In **Properties → General** with nothing selected, set **Default text height (in)** — 0.10 is a
   common choice.
7. **File → Save As…** and save it somewhere permanent, e.g. `Templates\project-template.gs`.
8. **View → Settings… → Files → Startup template (.gs)**, browse to that file, and press
   **Save startup preferences**.

**Result:** every new drawing starts with your layers, styles, and scale.

---

## 2. Import survey points from the field

**Goal:** get a day's shots into a drawing.

**Prerequisites:** a CSV from the data collector.

1. Type `IMPORTPOINTS` (or `IMPPTS`).
2. **Browse…** to the CSV.
3. Set **Column order** to match — `P,N,E,Z,D` for PNEZD, `P,E,N,Z,D` for PENZD.
4. Tick **First row is header (skip)** if there is a header.
5. Read the **File preview**. Confirm the columns line up with the preset you chose.
6. Read the **Validation summary**. Press **Refresh preview** after any change.
7. Press **Import**. If some rows are bad, confirm
   *Import N valid row(s) and skip M bad row(s)?*
8. Read the command log for per-row problems — bad numbers and duplicate IDs are named with their
   line number.
9. Type `ZE`.
10. Type `VIEWPOINTS` and spot-check a few coordinates against the field notes.

**Troubleshooting:** if nothing imported, the column order is almost certainly wrong. Look at the
preview — northing and easting swapped is the usual cause, and it puts points in a different county.

---

## 3. Reduce a traverse and commit the stations

**Goal:** coordinates for a closed traverse, with closure documented.

**Prerequisites:** field observations, or an FBK file.

1. Ribbon **Survey → Traverse.**
2. **Starting Station** — enter the **Start ID**, northing, easting, elevation, and the
   **Ref Bearing°** of the backsight.
3. Tick **Closed Loop**.
4. **+ Add Leg** for each leg. Expand it and enter the **F1** and **F2** horizontal and vertical
   angles and the distance. Set the **Zenith°** checkbox to match your instrument.
   *(Or press **Import .fbk…** and skip to step 6 — this replaces the current traverse.)*
5. Check the computed bearings and coordinates in the summary rows.
6. **Calculate Closure…** → **Unadjusted** tab. Read **Linear misclosure** and **Precision: 1:N**.
7. **Least Squares** tab. Set **Angle σ (sec)**, **Dist σ (ft)**, and **Dist ppm** to your
   instrument's real figures. Press **Recompute**.
8. Check the **Std dev of unit weight** — near 1.0 means your stated errors match the observations.
   Scan the residuals; one much larger than the rest is a blunder to go and check.
9. **Accept Least-Squares Result.**
10. **Commit to Drawing.** The stations become survey points.
11. Open **Reports** in the left panel and confirm the traverse record.

---

## 4. Draw a boundary from points

**Goal:** boundary linework snapped exactly to the corner monuments.

**Prerequisites:** the corner points are in the drawing.

1. Press `F3` so **OSNAP** is on. Right-click **OSNAP** and make sure **Survey point** and
   **Endpoint** are ticked.
2. Set the current layer to `BOUNDARY` on the ribbon.
3. Type `PL` (POLYLINE).
4. Click each corner in order, snapping to the survey point markers.
5. Type `CLOSE` to close back to the first corner.
6. Type `ZE`.

**Alternative — from bearings and distances:**

```
PL
(snap to the first corner)
A 132d15m30s
248.55
A 222d15m30s
150.00
...
CLOSE
```

**Troubleshooting:** if the polyline will not close cleanly, `INVERSE` between the first and last
corners to see the gap. A gap means an entered bearing or distance is wrong, not that the tool
failed.

---

## 5. Bring in a recorded plat as a PDF and trace it

**Goal:** get an old plat into the drawing at real coordinates.

**Prerequisites:** the plat as a PDF; state-plane coordinates for at least two monuments.

1. Type `PA` (PDFATTACH).
2. **Browse…** to the PDF, pick the page from the thumbnails, set **Raster DPI** to 200, and
   **Attach**. Place it roughly.
3. Select the underlay and set **Fade** so you can see your own linework over it.
4. Press `F3` for OSNAP. Check the underlay's **Lines** snap toggle.
5. Either draw over the boundary with `PL`, snapping to the PDF corners, or press **Vectorize** to
   convert the detected lines in bulk onto the current layer.
6. Run `OVERKILL` to remove duplicates and merge collinear segments.
7. Run `JOIN` on chains that should be single objects.
8. Now fit it to control — see workflow 6.

---

## 6. Move a local drawing onto state plane

**Goal:** transform an assumed-coordinate drawing to real-world coordinates.

**Prerequisites:** at least two points whose real coordinates you know.

1. Select the geometry to transform, **or** select nothing to transform everything.
2. Type `AL` (ALIGN).
3. If nothing was selected, window-select and press **Enter** (or press Enter with an empty
   selection to move everything).
4. **Source 1** — snap to the first known point in the drawing.
   **Destination 1** — type its real coordinates, e.g. `1543268.25,483112.90`
5. **Source 2** / **Destination 2** — repeat for the second point.
6. Add more pairs if you have them. Press **Enter** to solve.
7. In the results window, read the **Resid** column and the **Point error (RMS)**. Press **`-`** on
   any outlier and watch the RMS update.
8. **Untick Apply Scale** if the drawing is already at true scale, then press **Apply**. Tick it if
   the drawing was traced or digitised.
9. Type `ZE`. The status readout now shows real-world coordinates.
10. Check the **Reports** tab for the transformation record. Source points are now tagged ` ADJ`
    and destinations ` CON`.

---

## 7. Build a ground surface

**Goal:** a TIN from field shots, kept up to date as more shots arrive.

**Prerequisites:** points in the drawing with field codes.

1. Ribbon **Survey → Groups → New point group.** Name it `EG`.
2. Set **Raw description matches** to `EG*`. Watch the live `Matches N of M points.` count.
   *(Use **Raw description**, not Description — office edits to descriptions will not break it.)*
3. Ribbon **Survey → Surfaces → New from group…** Name it `Existing Ground`.
4. Tick the `EG` group. Confirm the point count is at least 3. Press **Create**.
5. Read the reported point count, triangle count, and elevation range. An elevation range that is
   obviously wrong means a bad shot is in the group.
6. Type `ORBIT` and drag to tilt the view. Elevation problems are obvious from a 3/4 angle and
   invisible in plan. (Visual style does not shade a TIN — see [[Known Limitations]].)
7. Type `SE` (SURFELEV) and spot-check elevations against known shots.

**Later, after importing more shots coded `EG`:**

8. **Survey → Surfaces**, select the surface, press **Rebuild**. The new points are already in the
   group — nothing needs re-selecting.

---

## 8. Annotate a survey

**Goal:** labelled points and sheet notes.

1. Set the **annotation scale** on the status bar to the scale you will plot at. Do this **first** —
   it decides the size everything is created at.
2. Select the points to label and set their **Label style** in the Properties panel.
3. **Settings → Drafting** — adjust **Label center east/north of point (plotted in)** so labels sit
   where you want them, and set **Coordinate display precision**.
4. If a label lands on top of linework, drag it away. A leader arrow appears automatically.
5. For notes, set the current layer to `TEXT`, then type `MT` (MTEXT), drag a frame, and type. Use
   the Text Formatting toolbar for fonts, sizes, and colours.
6. For dimensions, use `DAL` (aligned) or `DLI` (linear). At the linear dimension-line prompt, `H`
   and `V` lock the orientation.

---

## 9. Build a plot sheet and produce a PDF

**Goal:** an 11" × 17" sheet at 1" = 50', plotted to PDF.

1. Press **+** on the status bar to add a layout. Right-click its tab ▸ **Rename** and give it a
   name.
2. Right-click the tab ▸ **Page Setup Manager…** Set **Paper size** to `ANSI B (11" x 17")` and
   **Drawing orientation** to `Landscape`. Confirm the reported plot size.
3. Right-click the tab ▸ **Viewports… → + Add viewport.** Set **X**, **Y**, **W**, **H** in paper
   inches, **Center X / Center Y** to the model point you want centred, and **Scale (model/in)** to
   `50`.
4. Set the viewport's **Layer** to a **non-plottable** layer so its border does not print.
5. Set the status-bar annotation scale to `1" = 50'` so text and labels are sized to match.
6. Turn **VPLOCK** on so panning cannot rescale the viewport.
7. In paper space, draw the border and titleblock with `RECT`, `LINE`, and `MTEXT` — all in paper
   inches.
8. To fine-tune what the viewport shows, double-click into it (**FLOAT**), pan, then press `Esc`.
   Or set **Center X / Center Y** numerically, which is repeatable.
9. Use `VPFREEZE` to hide layers you do not want in this view.
10. Ribbon **Layout → Plot**. Choose an output path. The log reports
    `PLOT — wrote 1 page(s) to <path>`.

**For a multi-page set:** repeat for each sheet, then ribbon **Layout → Batch**, tick the layouts,
and **Plot to PDF…**. Pages come out in **layout order** — reorder with **Move or Copy…** first if
that matters.

---

## 10. Exchange with AutoCAD or Civil 3D

**Sending work out:**

1. **File → Export DXF…** for the linework.
2. `EXPORTPOINTS` for the survey points as a CSV — the reliable route into Civil 3D's point
   database.
3. Tell the recipient the DXF is ASCII AC1032 and that dimensions are exploded.

**Bringing work in:**

1. **File → Import DXF…** for the linework. Read the log — it names the paper-space entities and
   unsupported records it skipped.
2. `IMPORTPOINTS` for the points as CSV. **Do this in either order** — importing a DXF keeps the
   survey points already in the session.
3. If IDs collide at the DXF merge, choose **overwrite** or **offset**.
4. `ZE`, then `OVERKILL` to clean up stacked duplicates from the import.

**Civil 3D COGO points will not come through a DXF** — they are custom objects. CSV is the route.

---

## 11. A complete small project

1. New drawing from your template (workflow 1).
2. `IMPORTPOINTS` — the field CSV (workflow 2).
3. Traverse Editor — reduce and commit the control (workflow 3).
4. `ALIGN` — onto state plane if the work was local (workflow 6).
5. `PL` — boundary and linework from the points (workflow 4).
6. Survey → Groups and Surfaces — a ground surface (workflow 7).
7. Label points, add notes and dimensions (workflow 8).
8. `OVERKILL` — final cleanup.
9. Build the layout and plot the PDF (workflow 9).
10. `EXPORTPOINTS` and **Export DXF…** for the client (workflow 10).
11. `Ctrl+S`.

---

## Related

[[Getting Started]] · [[Survey Points]] · [[Traverse Editor]] · [[Coordinate Alignment]] · [[Plotting]] · [[Troubleshooting]]
