# ALIGNMENTCURVESETTINGS

**Command:** `ALIGNMENTCURVESETTINGS`
**Category overview:** [[Alignments]]

The **Curve and Spiral Settings** that Tangent-Tangent (With curves) uses ([[Commands/alignmentlayout]]):

- **Default radius**: the radius of every curve (default 100 ft, or 30 m in a metric drawing).
- **Spiral in / Spiral out**: clothoid spirals before and after the curve, by length. The window shows the A value
  (√(radius × length)). The curve's own angle is what is left of the corner after the spirals; when the spirals take
  the whole corner it stays a PI.

`ALIGNMENTCURVESETTINGS [RADIUS=<r>|DEFAULT][, SPIRALIN=<length>|OFF][, SPIRALOUT=<length>|OFF]`

With no arguments it reports the current settings. They last for the session.

---

## Related

- [[Alignments]]
- [[Commands/alignmentlayout]]
- [[Command Reference]]
