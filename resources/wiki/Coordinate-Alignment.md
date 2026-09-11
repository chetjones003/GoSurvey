# Coordinate Alignment — ALIGN

**Command:** `ALIGN`, `AL`

ALIGN computes and applies a **2D Helmert (similarity) transformation** — scale, rotation, and
translation — from source→destination control-point pairs. This is the standard least-squares
four-parameter adjustment used to move a drawing from local coordinates onto a real-world
coordinate system.

---

## When to use it

- A drawing was drafted in **assumed or local coordinates** and you now have state-plane values for
  known points.
- A **traced or scanned** plan needs to be fitted to control.
- Two datasets that should agree do not, and you have common points in both.

---

## Workflow

### 1. Start the command

```
ALIGN
```

- **With objects already selected**, ALIGN uses that selection and skips to step 2:
  `ALIGN — N CAD, M survey point(s). Pick SOURCE survey point 1 in drawing (snap to it).`
- **With nothing selected**, ALIGN asks you to window-select what to transform:
  `ALIGN — window-select entities to transform, then press Enter. ESC cancels.`
  Press **Enter** to confirm.
- Selecting nothing and pressing Enter transforms **everything**:
  `ALIGN — no selection; all geometry will be transformed.`

### 2. Pick control-point pairs

For each pair:

1. **Source** — click, or type `X,Y`, at the point in its *current* drawing position. Use object
   snap so you land exactly on the existing survey marker.
2. **Destination** — click, or type, the real-world coordinates that point should have.

Repeat for as many pairs as you have.

| Pairs | Fit |
|---|---|
| **1** | Translation only |
| **2 or more** | Full Helmert — scale, rotation, and translation, least-squares |

### 3. Solve

Press **Enter**. The **Align results** window opens with the computed parameters and the per-pair
residuals.

### 4. Review and remove outliers

| Column | Meaning |
|---|---|
| **Pair** | Sequential pair number |
| **Src X** / **Src Y** | Source coordinates, pre-transform drawing space |
| **Dst X** / **Dst Y** | Destination real-world coordinates |
| **Resid** | Point error — the distance from the predicted destination to the actual one after the transformation |

**Point error (RMS)** is the root-mean-square of the per-pair residuals: the average distance by
which each source maps away from its intended destination. Lower is a better fit.

Click the **`-`** button on any row to remove that pair. **The solution updates live**, so you can
watch the RMS fall as you drop an outlier. This is the review step — use it before applying.

### 5. Apply

Two choices:

| Button state | What is applied |
|---|---|
| **Apply Scale** — checkbox ticked | The full Helmert, including the computed scale factor |
| **Apply** — checkbox unticked | Rotation and translation only, with scale forced to 1.0. The translation is re-derived from the centroids so the solution stays least-squares optimal |

**Which to choose.** If your drawing is at true scale and only misplaced or misrotated, apply
without scale — a computed scale of 0.9997 in that case is measurement noise, and applying it
distorts good geometry. If the drawing was traced, digitised, or drawn to an unknown scale, apply
the scale.

### 6. Report

A transformation report is added automatically to the **Reports** tab of the Properties panel, with
all parameters and the per-pair point errors.

---

## Control-point tagging

After applying, ALIGN tags the survey points it acted on so the drawing records what happened:

| Points | Tag | Meaning |
|---|---|---|
| **Source** points — those you snapped to as ALIGN sources | ` ADJ` appended to the description | These were **adjusted** by the transformation |
| **Destination** points — those at the real-world coordinates | ` CON` appended to the description | These are **control** points, held fixed |

**Destination points are restored to the exact destination values after the transform.** They do
not drift by the residual. Control stays control.

---

## Example — local drawing onto state plane

You have a boundary drawn on assumed coordinates and GPS values for two monuments.

1. Snap-select the boundary linework and the survey points, or select nothing to move everything.
2. `AL`
3. **Source 1** — snap to monument point 1 in the drawing.
   **Destination 1** — type `1543268.25,483112.90`
4. **Source 2** — snap to monument point 2.
   **Destination 2** — type `1544011.60,482780.44`
5. **Enter.** The results window shows the parameters and two residuals.
6. Read the point error. If it is a hundredth of a foot, you are fine. If it is two feet, one of
   the four coordinates is wrong — check before applying.
7. Untick **Apply Scale** (the drawing is at true scale) and press **Apply**.
8. `ZE` to reframe. The status readout now reports state-plane coordinates.
9. Open **Reports** and confirm the transformation record.

---

## Common problems

| Problem | Cause | Solution |
|---|---|---|
| The residual on one pair is far larger than the rest | A mis-snapped source, or a typo in a destination | Press **`-`** on that row and watch the RMS; then check that pair |
| The drawing came out slightly the wrong size | Scale was applied when it should not have been | Undo, then re-apply with **Apply Scale** unticked |
| Only translation happened | Only one pair was given | Add a second pair for rotation and scale |
| Nothing moved | Nothing was selected and Enter was not pressed to confirm the empty selection | Re-run; either select first, or press Enter at the selection prompt |
| A destination point drifted | It should not — destinations are restored exactly | Check you picked it as a *destination*, not as a second source |
| Sources are missing the ` ADJ` tag | The pick was a coordinate, not a snap onto an existing survey point | Tagging only applies to points ALIGN actually snapped to |

---

## Related

[[Survey Points]] · [[Coordinate Input]] · [[Object Snaps]] · [[Traverse Editor]] · [[Import and Export]]
