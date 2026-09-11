# TEXT


![TEXT command overview](wiki-img:commands/text.png)
**Command:** `TEXT`
**Category overview:** [[Annotation]]

**Ribbon:** Annotate → Text **Command:** `TEXT`

Single-line text at an insertion point.

```
TEXT — pick insertion, then height / rotation / string on command line (defaults from plot scale).
TEXT: Insertion — click or X,Y | ESC cancel
TEXT: Height — Enter for plot-scale default | ESC cancel
TEXT: Rotation ° CW from north — decimal/DMS or Enter=0 | ESC cancel
TEXT: Enter content | ESC cancel
```

1. **Insertion** — click or type `X,Y`.
2. **Height** — type a model height, or press **Enter** to use the plot-scale default.
3. **Rotation** — degrees clockwise from north, decimal or DMS. **Enter** gives 0.
4. **Content** — type the text and press Enter.

### The plot-scale default height

New TEXT and MTEXT get their model height from two things:

```
model height = default text height (inches on the sheet) × model units per plotted inch
```

- **Default text height (in)** — Properties panel → General, with nothing selected.
- **Model units per plotted inch** — the annotation-scale dropdown on the status bar, or
  `PLOTSCALE` / `PSCALE`.

So at `1" = 50'` with a 0.10 in default height, new text is 5 model units tall — and it plots at
0.10 in regardless of scale. See [[Plotting]].

---

---

## Related

- [[Annotation]]
- [[Command Reference]]