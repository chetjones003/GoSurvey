# PARAMETERS

**Command:** `PARAMETERS`
**Category overview:** [[Import and Export]]

In the block editor, open the Parameters Manager for the block being edited. The list shows each parameter's name, kind, current value, limits and number of linked actions. Double-click a name or value to edit it. Delete removes that parameter and its linked actions; Undo restores the change.

The command line also accepts `PARAMETERS <list number>, NAME|VALUE|MIN|MAX, <value>` or `PARAMETERS <list number>, DELETE`. Use `PARAMETERS` without extra values to open the manager.

Attribute definitions are shown in the manager but are edited with `ATTDEF` and related attribute commands. A parameter's min/max limits apply to its numeric value and any matching block-table cells.

See [[Commands/bedit]] and [[Command Reference]] for related tools.

---

## Related

- [[Commands/bedit]]
- [[Commands/attdef]]
- [[Commands/btable]]
- [[Command Reference]]
