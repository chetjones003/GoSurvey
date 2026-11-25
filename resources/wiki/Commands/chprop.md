# CHPROP

**Command:** `CHPROP`
**Category overview:** [[Properties]]

Changes one property at a time on the selected objects.

### Workflow

1. **Select objects**, then press **Enter**.
2. **Property to change**: `Color`, `LAyer`, `LType` or `LWeight`. **Enter** ends the command.
3. Type the new value:

| Property | Accepts |
|---|---|
| Color | `ByLayer`, `ByBlock`, `1`–`255`, a color name, or `#RRGGBB` |
| Layer | an existing layer name |
| Linetype | `ByLayer`, `ByBlock`, `Continuous`, `DASHED`, `HIDDEN`, `CENTER`, `PHANTOM`, `DIVIDE`, `BORDER` |
| Lineweight | a width in mm, or `ByLayer` |

The prompt returns to step 2, so you can change several properties in one run.

---

## Related

- [[Properties]]
- [[Command Reference]]
