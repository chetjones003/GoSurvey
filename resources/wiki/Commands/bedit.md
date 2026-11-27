# BEDIT


![BEDIT command overview](wiki-img:commands/bedit.png)
**Command:** `BEDIT`
**Category overview:** [[Import and Export]]

Open the block editor: BEDIT <name>

While BEDIT is active, drawing Save and Save As are refused because the editor temporarily replaces
the drawing model. This covers every drawing-save route: `SAVE`, `QSAVE`, `SAVEAS`, Ctrl+S, the File
menu and the save prompt when a drawing closes. Autosave skips the drawing until the editor closes,
and a refused save leaves the drawing file untouched. Run `BCLOSE` to return to the drawing before saving. Use `BSAVE` or `BSAVEAS` to
save block-definition changes while the editor is open.

See [[Import and Export]] for related tools and workflows.

---

## Related

- [[Import and Export]]
- [[Command Reference]]
