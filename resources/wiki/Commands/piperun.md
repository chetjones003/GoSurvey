# PIPERUN

**Command:** `PIPERUN` — aliases: `PIPE`
**Category overview:** [[Drawing Tools]]

Routes a 3D pipe run.

### Workflow

1. **Nominal size**, with an optional pressure class: e.g. `4in` or `4in CS150`.
   Sizes: 0.5in, 0.75in, 1in, 1.25in, 1.5in, 2in, 2.5in, 3in, 4in, 6in, 8in, 10in, 12in.
   Classes: `CS150`, `CS300`.
2. **Wall thickness** in inches. **Enter** uses schedule 40.
3. **Points**: click or type them. `U` undoes the last point. `COMPASS` toggles the compass.
   `END` or **Enter** finishes.

`PIPERUN 4in` answers the size prompt on the same line. The run is drawn live from the second
point; **Esc** removes it.

See also [[Commands/pipefit]], [[Commands/pipesplit]], [[Commands/pipejoin]], [[Commands/pipeprop]],
[[Commands/pipesys]].

---

## Related

- [[Drawing Tools]]
- [[Command Reference]]
