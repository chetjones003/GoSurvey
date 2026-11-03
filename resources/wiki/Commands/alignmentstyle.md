# ALIGNMENTSTYLE

**Command:** `ALIGNMENTSTYLE` — no alias  
**Category overview:** [[Alignments]]

Manage **alignment display styles** (REQ-491): tangent, curve and spiral components with color,
linetype, lineweight and visibility. Each alignment references a style by name (default **Standard**).

```
ALIGNMENTSTYLE LIST
ALIGNMENTSTYLE NEW <style>
ALIGNMENTSTYLE DELETE <style>
ALIGNMENTSTYLE ASSIGN <alignment>, <style>
ALIGNMENTSTYLE SHOW|HIDE <style>, <tangent|curve|spiral>
ALIGNMENTSTYLE COLOR <style>, <tangent|curve|spiral>, <color>
```

**Standard** cannot be deleted. A style in use by any alignment cannot be deleted until those
alignments use another style. Missing style names on load resolve to **Standard** and are logged.

Styles are saved with the drawing. The viewport draws each alignment entity using its style's
component settings (same pattern as surface styles).

See [[Alignments]] for creation and editing commands.

---

## Related

- [[Alignments]]
- [[Command Reference]]
