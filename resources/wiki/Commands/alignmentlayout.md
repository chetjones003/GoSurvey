# ALIGNMENTLAYOUT

**Command:** `ALIGNMENTLAYOUT`
**Category overview:** [[Alignments]]

Lays out an alignment by its PIs (points of intersection), like Civil 3D's **Tangent-Tangent** tools. The **Alignment
Layout Tools** window, which opens after Create Alignment - Layout, runs it:

| Tool | What it does |
|---|---|
| Tangent-Tangent (No curves) | Tangents from PI to PI. |
| Tangent-Tangent (With curves) | The same, with a curve at every corner (and spirals when Curve and Spiral Settings has them). |
| Curve and Spiral Settings... | The curve radius and the clothoid spiral lengths before and after each curve. |
| Delete Sub-entity | Removes one tangent, curve or spiral by its number. |
| Alignment Grid View | Opens the Alignment Layout table ([[Commands/editalignment]]). |
| Undo / Redo | During a layout, Undo takes back the last PI. |

At the prompt, click or type each PI: `X,Y` in world coordinates, or `@dx,dy` from the previous PI. `U` takes back a
PI; Enter or Esc finishes and keeps what was built; one `UNDO` afterwards removes the whole layout. An alignment that
already has geometry is continued from its end.

With curves, each corner gets the settings radius. Tangents are Fixed and curves are Free between them, so grips and
the layout table keep the curve tangent to both. A corner too tight for the radius (its tangent distance does not fit
on the tangents) gets a smaller curve, as in Civil 3D: the radius whose tangent distance is 40 % of the shorter
tangent, and the radius used is reported. With spirals, a corner they do not fit stays a PI and is reported.

`ALIGNMENTLAYOUT <name>[, CURVES|NOCURVES][, FROM=<x> <y>][, DIRECTION=<azimuth°>]`

`FROM` starts at a point; `DIRECTION` holds the first tangent to that azimuth. Tangent-Tangent starts without curves
and keeps the last mode chosen.

---

## Related

- [[Alignments]]
- [[Commands/alignmentcurvesettings]]
- [[Command Reference]]
