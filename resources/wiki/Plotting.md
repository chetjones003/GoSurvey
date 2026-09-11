# Plotting

GoSurvey plots to **vector PDF**. There is no Windows printer output — you produce a PDF and print
or issue that.

---

## Plot scale — the two different things called "scale"

These are easy to confuse, so be clear which one you mean.

| Scale | Set by | Controls |
|---|---|---|
| **Annotation scale** — model units per plotted inch | Status-bar dropdown, or `PLOTSCALE` / `PSCALE` | The **size things are created at**: new text height, survey point marker size, survey label placement |
| **Viewport scale** — model units per paper inch | Viewports window, per viewport | How much model the **viewport shows** on the sheet |

For a sheet to come out right, set them to the same value. At `1" = 50'`, the annotation scale is
`50` and each viewport's **Scale (model/in)** is `50`.

### PLOTSCALE (`PSCALE`)

```
PSCALE 50
Plot scale: 1 plotted inch = 50.000000 model units.
```

A bad value answers:
`PLOTSCALE — usage: PLOTSCALE <model_units_per_plotted_inch> (example: 50 for 1"=50').`

The status-bar dropdown offers the common presets.

**Changing the plot scale repositions every survey point label**, because labels are laid out in
plotted inches around their point. This is deliberate: the sheet keeps looking the same at any
scale. Existing text is **not** resized.

### Text height

```
model height = default text height (inches on the sheet) × model units per plotted inch
```

**Default text height (in)** is in the Properties panel → General, with nothing selected. At
`1" = 50'` with 0.10 in, new text is 5 model units tall and plots at 0.10 in. See [[Annotation]].

---

## Plotting one layout

**Ribbon (Layout):** Plot

1. Switch to the layout you want. Plot is only available in paper space —
   `PLOT — switch to a paper layout first.`
2. Press **Plot**.
3. Choose an output path. The default filename is the layout name plus `.pdf`.
4. The log reports `PLOT — wrote 1 page(s) to <path>`.

There is **no typed `PLOT` command** — use the ribbon button.

---

## Batch plot

**Ribbon (Layout):** Batch

Plots several layouts into **one multi-page PDF**.

1. Press **Batch**. The current layout is pre-ticked.
2. *Select layouts to plot (one page each, in order):* — tick the layouts you want.
3. Press **Plot to PDF…** and choose an output path (default `plot.pdf`).

Layouts are plotted in **layout order**, one page each, regardless of the order you ticked them.
Reorder them first with **Move or Copy…** on the layout tab if the page order matters.

**Plot to PDF…** stays disabled until at least one layout is ticked.

---

## What gets plotted

| Included | Excluded |
|---|---|
| Paper-space geometry and text on the layout | Layers whose **Plot** flag is off |
| Model geometry visible through each viewport, at that viewport's scale and framing | Viewport borders, when the viewport's layer is not plottable |
| Layer colours, linetypes, and lineweights | Layers frozen in that viewport via **VP Freeze** |
| TrueType and SHX text | |

Per-viewport layer freezing and colour overrides are honoured — a layer frozen in one viewport does
not appear in that viewport's page.

---

## Setting a sheet up

1. **Create a layout** — the **+** button on the status bar, or **☰ → New paper layout**.
2. **Right-click the tab ▸ Page Setup Manager…** — choose the **Paper size** and
   **Drawing orientation**. The dialog reports the resulting plot size in inches.
3. **Right-click the tab ▸ Viewports…** — **+ Add viewport**, then set its **X**, **Y**, **W**,
   **H** in paper inches, its **Center X / Center Y** in model coordinates, and its
   **Scale (model/in)**.
4. Put the viewport on a **non-plottable layer** if you do not want its border on the sheet.
5. **Draw the border and titleblock** in paper space, in paper inches.
6. Set the **annotation scale** on the status bar to match the viewport scale, so text and point
   labels come out at the right size.
7. Turn **VPLOCK** on so later panning cannot rescale the viewport.
8. **Plot**.

---

## Messages

| Message | Meaning |
|---|---|
| `PLOT — wrote N page(s) to <path>` | Success |
| `PLOT — switch to a paper layout first.` | You are in model space |
| `PLOT — no layouts selected.` | Batch plot with nothing ticked |
| `PLOT — no plottable layouts.` | Every selected layout is excluded |
| `PLOT — no output path.` | The save dialog was cancelled |
| `PLOT — could not open output file: <path>` | The path is not writable, or the PDF is open elsewhere |
| `PLOT — could not create the PDF document.` / `PLOT — failed to write the PDF.` | The PDF could not be produced |
| `PLOT — note: could not embed TrueType font '<name>'; substituted …` | A font could not be embedded; a substitute was used. The PDF is still valid, but check the text |

---

## Common problems

| Problem | Cause | Solution |
|---|---|---|
| The **Plot** button is not on the ribbon | You are in model space | Switch to a paper layout |
| Text is far too big or too small on the sheet | Annotation scale does not match the viewport scale | Set both to the same value, then recreate the text |
| Survey point labels moved after I changed the plot scale | Labels are placed in plotted inches, so they reposition by design | Set the plot scale before you finish the sheet |
| The viewport border shows on the PDF | Its layer is plottable | Move the viewport to a non-plottable layer |
| A layer is missing from the PDF | Its **Plot** flag is off, or it is VP-frozen in that viewport | Check the Layer Manager, **Plot** and **VP Freeze** columns |
| Fonts look wrong in the PDF | The font could not be embedded and was substituted | Read the log line naming the font; use a different text style |
| Cannot write the PDF | The file is open in a PDF reader | Close it and plot again |
| Batch pages came out in the wrong order | Pages follow layout order, not tick order | Reorder with **Move or Copy…** on the layout tabs |

---

## Related

[[Paper Space and Layouts]] · [[Layers]] · [[Annotation]] · [[Survey Points]]
