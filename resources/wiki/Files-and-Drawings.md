# Files and Drawings

---

## The `.gs` drawing file

GoSurvey's native format. A `.gs` file is a **single UTF-8 text file containing one JSON object** —
no binary wrapper, no compression, no encryption. Saved files are pretty-printed.

One `.gs` file holds a complete workspace:

- CAD geometry — lines, polylines, circles, arcs, ellipses, hatches, meshes
- Annotations — text, MTEXT, dimensions
- The layer table with colours, linetypes, lineweights, and transparency
- The **survey point database**, including raw descriptions and label styles
- **Point groups** and **surfaces**
- **Paper layouts**, their page setups, and their viewports
- Text styles, plot scale, and insertion units
- PDF underlay references

Because it is plain JSON, a `.gs` file is diffable and greppable, which is useful when something
goes wrong.

---

## Drawing tabs

Multiple drawings are open at once as tabs above the viewport (`Drawing 1`, `Drawing 2`, `+`).

**Each tab is a completely independent document.** Geometry, layers, survey points, paper layouts,
plot scale, and undo history are all per-tab. Switching tabs switches all of it.

Tabs can be hidden with **Settings → Display → Display File Tabs**.

---

## New, Open, Save

| Action | How | Behaviour |
|---|---|---|
| **New** | File → New | Opens an empty drawing **in a new tab**, named `Drawing N` |
| **Open** | File → Open | Opens a `.gs` **in a new tab**, named after the file. Does not replace what you are working on |
| **Save** | `Ctrl+S`, File → Save | Writes to the current path. With no path yet, the Save-As dialog opens first |
| **Save As** | File → Save As… | New name and location; the tab is renamed to the file stem and later saves go to the new path |

There is **no autosave, no backup file, and no drawing recovery**. Save deliberately, and save often.

There is no recent-files list.

---

## Closing and unsaved changes

**File → Quit Application**, or the window close button.

If any tab has unsaved changes, an **Unsaved Changes** dialog appears listing them:

> *The following drawings have unsaved changes:*

| Button | Effect |
|---|---|
| **Save All & Close** | Saves every listed drawing, then exits |
| **Close Without Saving** | Discards the changes and exits |
| **Cancel** | Returns to the program |

The same prompt appears when an update is about to install, so an update can never take unsaved
work with it. Cancelling the save prompt cancels the update too — the verified installer stays on
disk for the next offer.

---

## Templates

New drawings start from a **startup template**.

**Settings → Files:**

> *Search paths, file locations, and startup template. GoSurvey loads a workspace .gs at startup;
> an empty Custom path uses the bundled `resources/default-template.gs` next to the executable.
> Preferences are saved in `gosurvey-user.json` beside the executable.*

| Control | What it does |
|---|---|
| **Startup template (.gs)** — Custom .gs path | Point at your own template |
| **Browse** | File dialog (Windows only in this build) |
| **Clear path (use bundled)** | Fall back to `resources/default-template.gs` |
| **Save startup preferences** | Writes `gosurvey-user.json` |

The dialog reports where the bundled template resolved to, or says it could not be found.

**To make your own template:** set up a drawing with your standard layers, text styles, layouts,
and page setups, save it as a `.gs`, and point the startup template at it.

---

## Where files live

| Path | Contents |
|---|---|
| `%ProgramFiles%\GoSurvey` | The installed program |
| Beside the executable | `gosurvey-user.json` — user preferences (display precision, angle format, startup template, update settings) |
| Beside the executable — `resources/` | `default-template.gs`, `fonts/`, `hatches/`, `icons/`, `layouts/`, `linetypes/` |
| `%APPDATA%\GoSurvey` | `history.log` — the undo history log |

Resources are resolved **relative to the executable**.

---

## Version compatibility

GoSurvey **opens drawings saved by any older version**, converting them as it loads. The file on
disk is not modified until you save.

Two cases are called out in the update dialog before you accept an update:

- If the update **changes the drawing file format**, you are told that drawings saved afterwards
  cannot be opened by the version you have now.
- In the rare case an update genuinely **cannot open existing drawings**, that appears as a
  prominent warning with an explanation, so you can back up first or decline.

---

## Updates

GoSurvey checks for a newer version each time it opens. Nothing downloads or installs on its own.

**Settings → System → Updates:**

| Setting | Default | Effect |
|---|---|---|
| **Check for updates on startup** | On | Turn it off and GoSurvey makes no network requests at all |
| **Include beta releases** | Off | Turn on to receive pre-release builds |

The dialog reports the current version, and any version you have chosen to skip.

Choosing **Update Now** downloads the installer, verifies it against a SHA-256 published with the
release, prompts you to save open drawings, then installs and reopens GoSurvey. The installer is
not code-signed yet, so Windows will ask for permission.

Betas are published more often and are less tested than stable releases. They are real working
builds — they are also where problems get found.

---

## Panel layouts

Separate from drawings: **View → Layout ▸ Save current as…** stores the arrangement of panels as a
named `.ini` file, and **Switch to** recalls one. **View → Reset layout** restores the built-in
arrangement.

---

## Common problems

| Problem | Cause | Solution |
|---|---|---|
| Opening a drawing did not replace the current one | Open always uses a new tab, by design | Close the tab you no longer want |
| Work was lost after a crash | There is no autosave or recovery | Save often; `Ctrl+S` is cheap |
| A `.gs` will not open | It may be truncated or hand-edited into invalid JSON | Check the log for the parse error; `.gs` is plain JSON, so a text editor can show you where |
| New drawings do not have my layers | No startup template is configured | Settings → Files → **Startup template (.gs)** |
| Preferences do not stick | `gosurvey-user.json` could not be written | *"Error: failed to write gosurvey-user.json (check directory permissions)"* — the folder beside the executable needs to be writable |
| An update prompt appeared mid-work | The startup check found a newer version | **Remind Me Later**, or turn the check off in Settings → System |

---

## Related

[[Import and Export]] · [[Settings and Options]] · [[Getting Started]] · [[Paper Space and Layouts]]
