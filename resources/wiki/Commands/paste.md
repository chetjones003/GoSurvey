# PASTE


![PASTE command overview](wiki-img:commands/paste.png)
**Command:** `PASTE`
**Category overview:** [[Modify Tools]]

Paste from clipboard

`PASTE` carries selected model-space survey points with copied geometry. A point-number collision
opens the existing **Overwrite / Renumber / Cancel** question. When the destination drawing uses
shared project points, **Overwrite** replaces matching points throughout that project; **Renumber**
preserves them. If the source and destination both have defined, different coordinate systems,
GoSurvey blocks the paste and offers **Convert and
paste** or **Cancel**; conversion preserves the copied content's ground position. Unit mismatches
warn and offer conversion. These checks apply between standalone drawings and across projects too.
Paper-space pastes do not include survey points.

See [[Modify Tools]] for related tools and workflows.

---

## Related

- [[Modify Tools]]
- [[Command Reference]]
