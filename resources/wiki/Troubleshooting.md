# Troubleshooting

**Read the command log first.** GoSurvey states what it wants, what it did, and why it refused.
Press `F2` for the full console and scroll back — most of the answers below are already in there.

---

## Installation and startup

| Problem | Cause | Solution |
|---|---|---|
| *"Windows protected your PC"* on the installer | The installer is not code-signed yet | **More info → Run anyway**, if you trust the release |
| The program will not start — missing DLL | The Visual C++ runtime is not installed | Install the [Visual C++ Redistributable for x64](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) |
| Startup hangs on *"Checking for updates"* | Slow or blocked network | Press **Continue without checking**. Turn the check off in **Settings → System** |
| Graphics glitches, or the window is black | Driver or GPU incompatibility | **Settings → System** — turn off **Hardware Acceleration**, or tick **Prefer the integrated GPU** |
| The bundled template was not found | `resources/` is missing beside the executable | Reinstall. Resources resolve relative to the executable |

---

## Files

| Problem | Cause | Solution |
|---|---|---|
| Opening a drawing did not replace the current one | **Open** always uses a new tab, by design | Close the tab you no longer want |
| Work was lost after a crash | There is **no autosave and no recovery** | `Ctrl+S` often |
| A `.gs` will not open | Truncated or hand-edited into invalid JSON | The log names the parse error. A `.gs` is plain JSON — a text editor will show you where |
| Preferences do not persist | `gosurvey-user.json` could not be written | *"Error: failed to write gosurvey-user.json (check directory permissions)"* — the folder beside the executable must be writable |
| New drawings do not have my layers | No startup template | **Settings → Files → Startup template (.gs)** |

---

## Drawing

| Problem | Cause | Solution |
|---|---|---|
| Typing a number does nothing useful | No bearing lock, Ortho off — the command has no direction | Lock a bearing with `A`, or press `F8` |
| A line went the wrong way | Bearings are **clockwise from north**, not counter-clockwise from east | `90` is east, `180` south, `270` west |
| `@100<45` does nothing | **Polar coordinate entry is not supported** | Use `A 45` then `100` |
| `@100,50` is refused | The prompt has no previous point | Use absolute `X,Y` at a first point |
| A hatch will not fill | The boundary is not actually closed | Zoom to the corners; `JOIN` the chain, or redraw with `POLYLINE` / `RECT` |
| A hatch pattern looks solid or invisible | The pattern scale is far off for this drawing | Adjust **Scale** in the Hatch ribbon section |
| Segments will not join into one object | LINE makes separate segments by design | Use `POLYLINE`, or `JOIN` afterwards |
| New geometry will not snap to old geometry | The work plane is raised | Check the status bar. `UCS: Elev …` means new geometry is not at Z 0. `ELEV W` to return to world |
| MIRROR does nothing | It is on the ribbon but **not implemented** | See [[Known Limitations]] |

---

## Selection

| Problem | Cause | Solution |
|---|---|---|
| The wrong object gets picked | Several overlap under the cursor | Use the **SEL** panel to toggle candidates |
| A modify command asks for a window when I already selected | Nothing was actually selected | Selecting first skips the window step; check the highlight |
| Quick Select returns nothing | The value does not match exactly | Use the dropdown values where offered; check the operator |
| DELETE does nothing in paper space | Paper-space DELETE needs a selection first | `DELETE — select paper object(s) or viewport(s) first.` |
| Objects disappeared | Isolation is active | `UNISOLATEOBJECTS`, or right-click ▸ Isolate Objects ▸ End Object Isolation |

---

## Snapping

| Problem | Cause | Solution |
|---|---|---|
| Nothing snaps | OSNAP is off | `F3`, or the status-bar **OSNAP** button |
| It never snaps to the feature I want | That snap type is unticked | Right-click **OSNAP** and tick it |
| It keeps grabbing the wrong feature | Two candidates are close, and the wrong one ranks higher | **Shift + right-click** for a one-shot override |
| Perpendicular offers nothing | No reference point in the running command | Perpendicular needs a previous point to be perpendicular *from* |
| Lines that cross in plan give no intersection | Different elevations — an *apparent* intersection, not a real one | Enable **Apparent intersection**, or orbit to check |
| The commit landed away from the preview | Previews follow the cursor; picks commit at the **snapped** point | Watch the snap glyph before you click |
| PDF snap finds nothing | The underlay's **Lines** toggle is off, or the raster DPI is too low | Check the toggle; re-attach at a higher DPI |

---

## Coordinates

| Problem | Cause | Solution |
|---|---|---|
| Typed coordinates land in the wrong place | Typed values are always **world** coordinates | Check the status-bar readout |
| Imported geometry is miles from the points | Different coordinate systems | `ALIGN` with control pairs |
| The readout shows too few decimals | Display precision | `UNITS` → **Length → Precision** |
| Bearings display in a format I do not use | Display format | `UNITS` → **Angle → Type** |
| Points imported into a different county | Northing and easting are swapped | Re-import with the other column-order preset |

---

## Survey calculations

| Problem | Cause | Solution |
|---|---|---|
| Traverse closure shows nothing | Not marked as a closed loop | Tick **Closed Loop** and recompute |
| A summary field will not accept typing | Summary rows are view-only | Expand the leg and edit the observation set |
| Vertical angles reduce wrongly | The **Zenith°** checkbox does not match the instrument | Checked = zenith (90° level); unchecked = elevation angle (0° level) |
| A slope distance is ignored | No vertical angle to reduce it with | Enter the vertical angle |
| `Least squares unavailable` | Not enough redundancy, or degenerate geometry | Add observations; check for a duplicated station |
| Std dev of unit weight is far from 1 | The a-priori standard errors do not describe the observations | Adjust **Angle σ**, **Dist σ**, **Dist ppm** |
| One residual is far larger than the rest | A blunder in that observation | Go back and check it — do not adjust it away |
| ALIGN left the drawing slightly the wrong size | Scale was applied when it should not have been | Undo; re-apply with **Apply Scale** unticked |
| A surface is stale after an import | Surfaces are not rebuilt automatically | **Survey → Surfaces → Rebuild** |
| `SURFELEV` says a pick is off the surface | It is outside the TIN, and the program will not extrapolate | Add shots, or pick inside |
| A point group stopped matching | The rule keys on **description**, which was edited | Key it on **Raw description matches** |
| A point group matches 0 points | An empty rule matches nothing, by design | Fill in at least one criterion; try `*EG*` |

---

## Import

| Problem | Cause | Solution |
|---|---|---|
| DXF import brings in nothing | Binary DXF | *"Binary DXF is not supported — in AutoCAD use Save As → ASCII DXF."* |
| DXF import brings in nothing, and the log mentions group 67 | The geometry is all in paper space, which is not imported | Move it to model space in the source program |
| CSV import brings in nothing | The column order does not match | Check the **File preview** and change **Column order** |
| Civil 3D points did not import | They are custom objects, absent from the DXF | Export a PNEZD CSV from Civil 3D |
| Point IDs collided | Two sources numbered from 1 | Choose **Renumber** in the duplicate policy, or **offset** at the DXF merge prompt |
| DWG menu items are greyed out | No converter installed | Install the free ODA File Converter, or set `GOSURVEY_DWG_CONVERTER` |
| DWG conversion timed out | A large file, or the converter could not start | Check the path; try converting to DXF outside GoSurvey |
| `IMPORTMODEL` refuses the scale | Zero or non-finite | Give a non-zero finite number |
| Part of an imported model is missing | Only geometry is imported | The log names what was skipped — *"Not imported (geometry only): …"* |

---

## Export

| Problem | Cause | Solution |
|---|---|---|
| Some entities are missing from the DXF | Those types have no export branch yet | Read the `DXF export — excluded …` log line; see [[Known Limitations]] |
| Dimensions are not live in AutoCAD | Aligned dimensions export as **exploded lines plus text** | Expected. Recreate them in the receiving program if they must be associative |
| DWG export lost blocks and layouts | Documented; the dialog warns before writing | Use DXF where possible |
| `DXF export — could not open file for writing` | The path is not writable, or the file is open elsewhere | Close it and retry |

---

## Display

| Problem | Cause | Solution |
|---|---|---|
| The drawing looks stale | GPU cache | `REGEN` / `RE` |
| Zoom extents shows a huge empty area | An entity at a wild coordinate | The DXF import log prints both the full and the outlier-excluded bounding box — find and delete it |
| A surface is a mess of triangle edges | Surfaces are drawn as triangle edges in every visual style; `SHADED` does not fill them | Expected. `ORBIT` to read the shape, `SURFELEV` for numbers |
| Diagonal lines look jagged | Line smoothing is off | **Settings → System → Smooth line display** |
| Panels are in the wrong place | Layout drift | **View → Reset layout** |
| The command bar is gone | Hidden | `Ctrl+9`, or **View → Command line** |
| Turning a layer **On** off changes nothing | On/Freeze/Lock are stored but **not enforced yet** | Use `HIDEOBJECTS` / `ISOLATEOBJECTS`, or **VP Freeze** in a layout |

---

## Performance

| Problem | Cause | Solution |
|---|---|---|
| The display is slow on a large drawing | Dense geometry or a big surface | Isolate what you are working on; lower **Arc and circle smoothness** |
| A large model import takes minutes | Expected, especially through the DWG converter | The log warns before it starts |
| The program feels slow on a laptop | The integrated GPU is in use | **Settings → System** — untick **Prefer the integrated GPU** |
| Measuring frame performance | — | `BENCH`, `BENCH SURFACE`, `BENCH MESH` |

---

## Commands

| Problem | Cause | Solution |
|---|---|---|
| `<command> — finish or cancel the active command first` | Another command is running | `Esc`, then retry |
| `Unknown command. Did you mean: …?` | Typo, or the command does not exist | Pick a suggestion, or check [[Command Reference]] |
| A command runs but nothing happens | Its precondition failed | The log says which — read it |
| I cannot find the PLOT command | **There is no typed PLOT command** | Use the Layout ribbon button in paper space |
| The autocomplete popup blocks a click | Clicks on the popup are ignored by the viewport, by design | `Esc` to dismiss it |

---

## Plotting

| Problem | Cause | Solution |
|---|---|---|
| The **Plot** button is not on the ribbon | You are in model space | Switch to a paper layout |
| Text is far too big or small on the sheet | Annotation scale does not match the viewport scale | Set both to the same value, then recreate the text |
| Point labels moved after a scale change | Labels are placed in plotted inches, so they reposition by design | Set the plot scale before finishing the sheet |
| The viewport border prints | Its layer is plottable | Move the viewport to a non-plottable layer |
| A layer is missing from the PDF | Its **Plot** flag is off, or it is VP-frozen | Check the Layer Manager |
| Fonts look wrong in the PDF | A font could not be embedded | The log names it — *"could not embed TrueType font '…'; substituted …"*. Use a different text style |
| Cannot write the PDF | It is open in a reader | Close it and plot again |
| Batch pages are in the wrong order | Pages follow **layout order**, not tick order | Reorder with **Move or Copy…** |

---

## Still stuck

1. Press `F2` and read the log from the point where things went wrong.
2. **Right-click the log ▸ Copy log to clipboard.**
3. [Open an issue](https://github.com/chetjones003/GoSurvey/issues) with the log, the version from
   **Settings → System**, and what you were doing.

---

## Related

[[FAQ]] · [[Known Limitations]] · [[Command Line]] · [[Workflows]]
