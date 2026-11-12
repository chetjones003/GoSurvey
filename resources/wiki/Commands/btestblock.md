# BTESTBLOCK

**Command:** `BTESTBLOCK`
**Category overview:** [[Import and Export]]

Open a temporary test view of the block currently being edited with `BEDIT`. The test places one insert of the current block definition so you can inspect its geometry and try parameter values. Changes made while testing are discarded when you close the test with `BCLOSE`.

The block must contain at least one object. `BCLOSE` returns to the block editor; `BTESTBLOCK` starts a fresh test using the current definition. `BSAVE` and `BSAVEAS` are unavailable during the test. Do not save the drawing while the test is open: the test temporarily replaces the drawing model, so a save can overwrite the drawing file with the test view.

See [[Commands/bedit]] and [[Import and Export]] for related tools.

---

## Related

- [[Commands/bedit]]
- [[Commands/bclose]]
- [[Command Reference]]
