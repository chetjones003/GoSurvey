# FAQ

---

### How do I start a drawing?

**File → New.** It opens in a new tab. To start from your own layers and styles, set a startup
template in **Settings → Files** — see [[Files and Drawings]].

### How do I draw a line?

Type `L` and press Enter, then click or type points. Press `Esc` to finish. See
[[Drawing Tools]].

### How do I enter coordinates?

Type `X,Y` or `X Y` for absolute, `@dx,dy` for relative to the last point. **Polar entry
(`@100<45`) is not supported** — lock a bearing instead with `A 45`, then type the distance. See
[[Coordinate Input]].

### How do I draw a line by bearing and distance?

After the first point:

```
A 132d15m30s
248.55
```

`A <bearing>` locks the direction, then a number is the distance along it. `2P` lets you pick the
direction from two points instead, and `+90` turns off it. See [[Coordinate Input]].

### Which way do bearings go?

**North is 0°, and angles increase clockwise.** East is 90°, south 180°, west 270°. That is how you
*type* them, always. How they are *displayed* — decimal, DMS, or surveyor's — is set in `UNITS`.

### How do I snap to an endpoint?

Press `F3` to turn OSNAP on. Right-click the status-bar **OSNAP** button and tick **Endpoint**. See
[[Object Snaps]].

### How do I snap to something buried under other geometry?

**Shift + right-click** in the viewport. Choose a snap type, and you get a list of *every* snap of
that type in the model, sorted by distance from your click.

### How do I change layers?

Set the current layer in the dropdown at the right end of the ribbon before you draw. To change an
existing object, select it and use **Layer** in the Properties panel. `LAYER` (`LA`) opens the Layer
Manager. See [[Layers]].

### I turned a layer off and nothing happened. Why?

Layer **On / Freeze / Lock are stored but not enforced yet** — the Layer Manager says so. To hide
things today, use `HIDEOBJECTS` / `ISOLATEOBJECTS`, or **VP Freeze** in a paper layout.

### How do I undo?

`Ctrl+Z`. Redo is `Ctrl+Shift+Z` or `Ctrl+Y`. The **Edit** menu names the step it will reverse.
Undo history is **per drawing tab** and is cleared when the tab is closed.

### How do I import survey points?

`IMPORTPOINTS` (`IMPPTS`), pick the CSV, set **Column order** to match — `P,N,E,Z,D` is PNEZD.
Check the preview before importing. See [[Survey Points]].

### How do I get Civil 3D points into GoSurvey?

**Export a PNEZD or PENZD CSV from Civil 3D and import that.** Civil 3D COGO points are custom
objects and are not written into a DXF at all, so no third-party program can read them from one.
Use DXF for the linework.

### Will importing a DXF wipe my survey points?

No. **DXF import replaces the CAD geometry and keeps the survey points already in the session.** So
CSV-then-DXF and DXF-then-CSV both work. Colliding IDs prompt you to overwrite or offset.

### How do I calculate an inverse?

`INVERSE` (`INV`), then two picks. It reports ΔE, ΔN, horizontal distance, and bearing. Turn OSNAP
on first so you land exactly on the points. See [[Inquiry Commands]].

### How do I compute an area?

**You cannot, inside GoSurvey.** The only area it reports is a circle's, in the Properties panel.
Export the boundary or the points and compute it elsewhere.

### How do I change units or precision?

`UNITS` (`UN` / `DDUNITS`). Set **Length → Precision**, **Angle → Type**, and **Insertion scale**.
Note that insertion scale is a **label only** — it never scales coordinates. See
[[Settings and Options]].

### How do I set text size?

Two things multiply: **Default text height (in)** in Properties → General, and the **annotation
scale** on the status bar. At `1" = 50'` with 0.10 in, new text is 5 model units tall and plots at
0.10 in. Set the scale *before* placing text. See [[Annotation]].

### How do I plot?

Switch to a paper layout, then press **Plot** on the Layout ribbon. **There is no typed `PLOT`
command.** Output is a vector PDF — GoSurvey does not print to a Windows printer. See [[Plotting]].

### How do I plot several sheets into one PDF?

Layout ribbon → **Batch**, tick the layouts, **Plot to PDF…**. Pages come out in layout order.

### How do I export my drawing?

**File → Export DXF…** for ASCII DXF (AC1032). **File → Export DWG…** goes through an external
converter and is lossy — the dialog lists exactly what it drops. `EXPORTPOINTS` writes the points as
CSV. See [[Import and Export]].

### Why are the DWG menu items greyed out?

DWG needs an external converter. Install the free **ODA File Converter**, or set
`GOSURVEY_DWG_CONVERTER` to `ODAFileConverter.exe` or `accoreconsole.exe`.

### How do I trace a PDF?

`PA` (PDFATTACH). Attach the page, then snap directly to it — snap targets are detected from the
rendered image, so they land on what you can see. **Vectorize** converts the detected lines to real
geometry in bulk. See [[PDF Underlays]].

### How do I move a local drawing to state-plane coordinates?

`ALIGN` (`AL`) with source→destination control pairs. One pair gives translation; two or more give a
full Helmert fit. Review the residuals before applying, and untick **Apply Scale** if the drawing is
already at true scale. See [[Coordinate Alignment]].

### How do I build a surface?

Create a **point group** first (**Survey → Groups**), then **Survey → Surfaces → New from group…**.
Key the group on **raw description** so office edits do not break it. **Rebuild** picks up points
imported later. See [[Point Groups and Surfaces]].

### Can I see elevations?

Yes. Run `ORBIT` (`3DO`) and drag to tilt the view. `SURFELEV` (`SE`) reads elevations and
grades off a surface. `ELEV` sets the elevation new geometry is drawn at, shown on the status bar as
`UCS: Elev …`.

### Does GoSurvey have blocks?

No. There is no BLOCK, INSERT, or attribute support. DWG export writes geometry exploded.

### Does GoSurvey have MIRROR?

The ribbon button exists but is **disabled** — it is not implemented. See [[Known Limitations]].

### What does the POLAR button do?

Nothing yet. It toggles and lights up, but polar tracking is not implemented — its own tooltip says
*"Polar tracking (UI only for now)"*.

### Can I customise keyboard shortcuts?

No. Aliases and shortcuts are fixed. The **right-click** behaviour *is* configurable —
**Settings → User Preferences → Right-click Customization…**

### Is there an autosave?

**No.** There is no autosave, no backup file, and no drawing recovery. `Ctrl+S` often.

### Can I open several drawings at once?

Yes — each is a tab, and each is completely independent: geometry, layers, points, layouts, plot
scale, and undo history. The clipboard moves objects between them.

### Will a new version open my old drawings?

Yes. GoSurvey opens drawings from any older version, converting as it loads, and does not modify the
file until you save. If an update changes the file format, the update dialog tells you before you
accept it.

### How do I turn off update checks?

**Settings → System → Updates → Check for updates on startup.** With it off, GoSurvey makes no
network requests at all.

### Where are my settings stored?

`gosurvey-user.json`, beside the executable. Plot scale and insertion units are stored in the `.gs`
drawing instead. The undo history log is at `%APPDATA%\GoSurvey\history.log`.

### The program says something I do not understand. Where do I look?

Press `F2` for the full command console and scroll back. GoSurvey does not fail silently — the
reason is in the log. If it still does not help, **right-click the log ▸ Copy log to clipboard** and
[open an issue](https://github.com/chetjones003/GoSurvey/issues).

---

## Related

[[Troubleshooting]] · [[Known Limitations]] · [[Command Reference]] · [[Workflows]]
