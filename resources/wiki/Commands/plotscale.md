# PLOTSCALE


![PLOTSCALE command overview](wiki-img:commands/plotscale.png)
**Command:** `PLOTSCALE` — aliases: `PSCALE`
**Category overview:** [[Paper Space and Layouts]]

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

---

## Related

- [[Paper Space and Layouts]]
- [[Command Reference]]