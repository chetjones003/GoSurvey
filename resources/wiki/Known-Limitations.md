# Known Limitations

Things that are visible in the interface but do not work yet, and things a CAD user might reasonably
expect that GoSurvey does not have. This page exists so you find out here rather than halfway
through a deadline.

Current as of **version 0.6.0**.

---

## Controls that respond but do nothing

| Control | Where | State |
|---|---|---|
| **MIRROR** | Modify ribbon | Button is **disabled**. *"Mirror — flip selection across a mirror line (not implemented yet)"* |
| **Poly VP** | Layout ribbon | Button is **disabled**. *"Polygonal viewport — coming in a later increment"* |
| **POLAR** | Status bar | Toggles and lights up; **polar tracking is not implemented**. *"Polar tracking (UI only for now)"* |
| **Layer On / Freeze / Lock** | Layer Manager | Stored and round-tripped, **not enforced**. *"On / Freeze / Lock are stored for future visibility and editing rules; all layers still draw"* |
| **Ctrl+X** | Right-click ▸ Clipboard ▸ Cut | The label is shown, but the key is only wired inside the MTEXT editor. Use the menu item |
| **Insert Field…** (`Ctrl+F`) | MTEXT editor | *"Fields are not supported yet"* |
| **Background Mask…** | MTEXT editor | *"Background masking is not supported yet"* |
| **Paragraph…**, Paragraph Alignment, Bullets and Lists, Columns, Combine Paragraphs | MTEXT editor | *"Paragraph and column properties are not stored yet"* |
| **Visual style `SHADED`** on a **TIN surface** | View ribbon / `VS SHADED` | The value is accepted and reported, but surfaces are drawn as **triangle edges in all three styles** — verified by capturing a TIN at `2D`, `HIDDEN`, and `SHADED` and finding the three frames pixel-identical. `SHADED` fills only **imported meshes** (`IMPORTMODEL`) and **hatches**. Lit surfaces are a planned feature |
| **Xref display** fade | Settings → Display | *"Reserved: fade is a placeholder; no fade pass is applied yet"* |
| **Segments in a polyline curve** | Settings → Display | *"Hint for spline-fit polylines (not currently consumed; reserved)"* |
| **Rendered object smoothness**, **Contour lines per surface** | Settings → Display | *"Reserved for 3D pipeline"* |
| Raster/OLE and 3D display performance options | Settings → Display | Marked as placeholders |
| **Import…** page setups | Page Setup Manager | *"Import page setups from another drawing — coming in a later update"* |
| Plotter selection and PDF options | Page Setup Manager | *"Plotter selection / PDF options arrive with PDF plotting (a later update)"* |

---

## Settings tabs with no controls

These tabs exist for structure and are empty — *"(No GoSurvey-specific controls in this section
yet.)"*

- **Open and Save** — file-format and recovery options
- **Plot and Publish** — plot settings live per layout instead
- **3D Modeling** — labelled *"GoSurvey is 2D; 3D options are reserved."* The label predates the
  3D work; elevations, orbit, visual styles, TIN surfaces, and model import all work, they are just
  not configured here
- **Profiles** — saved option profiles
- **AEC Editor**

---

## Commands that do not exist

### Drawing

`SPLINE` · `POINT` (as a drawing command — use `CREATEPOINTS`) · `XLINE` · `RAY` · `DONUT` ·
`SOLID` · `REVCLOUD` · `WIPEOUT` · `TABLE` · `MLINE`

### Modifying

`MIRROR` · `STRETCH` · `EXTEND` · `BREAK` · `FILLET` · `CHAMFER` · `ARRAY` · `EXPLODE` · `LENGTHEN` ·
`ALIGN` exists but is the **coordinate-transformation** command, not AutoCAD's align

### Blocks

`BLOCK` · `INSERT` · `WBLOCK` · `ATTDEF` · block attributes · dynamic blocks · block libraries.
**There is no block support at all.** DWG export writes geometry exploded for this reason.

### Inquiry

`AREA` · `DIST` · `LIST` · `MASSPROP`. The only area reported is a circle's, in the Properties panel.

### View

`ZOOM PREVIOUS` · named views · `VIEW` · `DVIEW` · multiple model-space viewports · isometric view
presets

### Other

`XREF` / external references · `LAYOUT` command (layouts are managed from the status bar) ·
`PURGE` · `AUDIT` · `RECOVER` · `SCRIPT` · `LISP` or any scripting

---

## Features that are missing

| Missing | Notes |
|---|---|
| **Autosave, backup files, drawing recovery** | Save deliberately with `Ctrl+S` |
| **Recent files list** | Use File → Open |
| **Customisable keyboard shortcuts or aliases** | Fixed in the program. Right-click behaviour *is* configurable |
| **Command macros or scripting** | None |
| **Printing to a Windows printer** | Output is vector PDF only |
| **Snap-to-grid** | The grid is a visual reference only |
| **Object snap tracking, temporary tracking points** | Not present |
| **Polar coordinate entry** (`@100<45`) | Use bearing lock: `A 45` then the distance |
| **Parametric constraints** | Not present |
| **Annotative scaling of existing text** | Changing the plot scale does not resize existing text; it does reposition survey point labels |
| **Coordinate system / projection library** | Coordinates are unprojected numbers. Use `ALIGN` to fit to control |
| **Unit conversion** | The `UNITS` insertion setting is a **relabel only** — coordinates are never scaled |
| **Native DWG reading** | Planned; today DWG needs an external converter |
| **Associative dimensions in DXF** | Aligned dimensions export as exploded lines plus text |

---

## Import and export gaps

| Gap | Detail |
|---|---|
| **Binary DXF** | Not supported. *"in AutoCAD use Save As → ASCII DXF"* |
| **DXF paper space** | Paper-space entities and title blocks are **not imported**. The log reports the count |
| **Unsupported DXF entities** | Reported as `N unsupported ENTITIES record(s)`, never silently dropped |
| **DXF export coverage** | Some entity types have no export branch yet and are absent from the file. The log names what was excluded |
| **DWG export losses** | Block definitions and inserts (written exploded), paper-space layouts beyond the first, elevations, block attributes, multileaders, tables, Civil 3D objects and proxies. The dialog lists these before writing |
| **Civil 3D COGO points** | Custom objects — **not in a DXF at all**. Use a PNEZD/PENZD CSV |
| **Plant 3D drawings** | Largely `AcPp*` custom objects requiring Autodesk's object enabler; unreachable by any third-party reader |
| **Model import** | `IMPORTMODEL` brings in **geometry only**. Materials, animations, and cameras are named in the log as not imported |

---

## Behaviour worth knowing

Not defects, but they surprise people:

| Behaviour | Why |
|---|---|
| **Open always uses a new tab** | It never replaces what you are working on |
| **DXF import keeps existing survey points** | Only CAD geometry is replaced, so CSV-then-DXF and DXF-then-CSV both work |
| **FBK import replaces the whole traverse** | Commit or export first |
| **An empty point-group rule matches nothing** | Treating "no filter" as "everything" is how a whole drawing ends up in a surface by accident |
| **Changing the plot scale moves every point label** | Labels are laid out in plotted inches, so the sheet keeps looking the same at any scale |
| **Surfaces are not rebuilt automatically** | Press **Rebuild** after importing points |
| **Previews follow the cursor; picks commit at the snapped point** | Watch the snap glyph, not the rubber band |
| **DELETE and ZOOMWINDOW ignore snaps** | A nearby snap point would drag a window corner off target |
| **`PLOT` has no typed command** | Use the Layout ribbon button |

---

## Reporting

Something here out of date, or something missing that should be listed?
[Open an issue](https://github.com/chetjones003/GoSurvey/issues).

---

## Related

[[Troubleshooting]] · [[FAQ]] · [[Command Reference]] · [[Import and Export]]
