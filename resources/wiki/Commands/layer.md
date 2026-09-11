# LAYER


![LAYER command overview](wiki-img:commands/layer.png)
**Command:** `LAYER` — aliases: `LA`
**Category overview:** [[Layers]]

**Command:** `LAYER`, `LA`

> *Layers group objects for display and DXF. New geometry uses the current layer from the ribbon
> (top right). Layer 0 cannot be renamed or deleted.*

![Layer Manager](wiki-img:03-layer-manager.png)

*The Layer Manager. **VP Freeze** and **VP Color** are greyed out here because this is model
space — they act on the current viewport in a paper layout.*

### Creating a layer

Type a name in **New layer name** and press **Add layer**. The log confirms `Layer added: <name>`,
or reports why it was refused.

### The table

| Column | What it does |
|---|---|
| **Name** | The layer name. Editable, except for layer `0` |
| **On** | Stored, **not yet applied** — see the note below |
| **Freeze** | Stored, **not yet applied** |
| **Lock** | Stored, **not yet applied** |
| **Plot** | **Working.** When off, this layer's geometry — and any viewports on it — is excluded from plots |
| **Current** | Makes this the current layer for new geometry |
| **Color** | Layer colour, used by entities set to ByLayer |
| **Linetype** | Layer linetype, used by entities set to ByLayer |
| **Lineweight** | Layer lineweight, used by entities set to ByLayer |
| **Transparency** | Layer transparency, used by entities set to defaults |
| **VP Freeze** | Freezes this layer **in the current viewport only** |
| **VP Color** | Overrides this layer's colour **in the current viewport only** |
| *(last column)* | Delete the layer |

The **Name** column and the header row stay put while you scroll.

> **On, Freeze, and Lock are recorded but not yet enforced.** The dialog says so:
> *"On / Freeze / Lock are stored for future visibility and editing rules; all layers still draw."*
> They round-trip through the drawing file and DXF, so setting them is not wasted — they just do
> not change what you see yet. To hide objects today, use `ISOLATEOBJECTS` / `HIDEOBJECTS`
> ([[Object Selection]]) or per-viewport **VP Freeze**.

### Colour, linetype, lineweight, transparency

> *Colour, linetype, lineweight, and transparency apply to entities set to ByLayer / defaults.*

An object with an explicit colour keeps it. An object set to ByLayer follows its layer. Set an
object back to ByLayer in the Properties panel to hand control back to the layer.

Linetypes come from the bundled `acad.lin` and `acadiso.lin` libraries — `CONTINUOUS`, `BORDER`,
`CENTER`, `DASHED`, `DASHDOT`, `DIVIDE`, `DOT`, `HIDDEN`, `PHANTOM`, their `2` and `X2` variants,
the `ACAD_ISO*W100` family, and pattern linetypes such as `FENCELINE1`.

### Rules

- **Layer `0` cannot be renamed or deleted.**
- Renaming succeeds with `Layer renamed.` in the log; a rejected rename says why.
- The layer table is synchronised with the geometry every time the dialog is drawn, so a layer
  referenced by an object always appears.

---

---

## Related

- [[Layers]]
- [[Command Reference]]