# Keyboard Shortcuts

Every key GoSurvey listens for.

---

## Global

| Key | Action | Notes |
|---|---|---|
| `Enter` | Submit the command input | **Works from anywhere** — no need to click the command line first |
| `Esc` | Cancel | Walks back one layer at a time — see below |
| `Ctrl+S` | Save | Prompts for a path if the drawing has none |
| `Ctrl+Z` | Undo | Not while typing in a text field |
| `Ctrl+Shift+Z` | Redo | |
| `Ctrl+Y` | Redo | Alternative binding |
| `Ctrl+C` | Copy selection to the clipboard | Not while typing |
| `Ctrl+V` | Paste from the clipboard at the cursor | Not while typing |
| `Delete` | Start DELETE | Erases the selection immediately if there is one; otherwise asks for a window. Not while typing |
| `F1` | Open the in-app user manual | Contextual — command page, UI hover topic, or Home |

`Ctrl+Z`, `Ctrl+Y`, `Ctrl+C`, `Ctrl+V`, and `Delete` are **suppressed while a text field has
focus**, so they behave as ordinary text editing there.

![In-app user manual (F1)](wiki-img:20-in-app-wiki.png)

---

## Mode toggles

| Key | Action | Notes |
|---|---|---|
| `F3` | Toggle **object snap** | Works **even while typing in the command line** |
| `F8` | Toggle **Ortho** | Works **even while typing in the command line** |

These two are mode keys, not characters, so handling them during text input never interferes with
typing a command.

The status bar carries the same toggles, plus **GRID**, **POLAR** *(not yet functional)*,
**VPLOCK**, and **SEL**.

---

## Command line

| Key | Action |
|---|---|
| `Enter` | Submit; with the autocomplete popup open, run the highlighted command |
| `Tab` | Complete to the highlighted suggestion |
| `↑` / `↓` | Move the autocomplete highlight |
| `Esc` | Dismiss the autocomplete popup |
| `F2` | Toggle the full-height console (floating bar only) |
| `Ctrl+9` | Hide and restore the command bar (floating bar only) |

---

## Dynamic input

| Key | Action |
|---|---|
| Type | Lock the active field to the typed value |
| `Tab` | Move between the X and Y fields |
| `Enter` | Commit the point |

---

## MTEXT editor

Active only while the in-drawing rich-text editor is open.

| Key | Action |
|---|---|
| `Enter` | New line |
| `Ctrl+Enter` | Reformat |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo **inside the text box** |
| `Ctrl+C` / `Ctrl+X` | Copy / cut **inside the text box** |
| `Ctrl+R` | Find and Replace |
| `Ctrl+F` | Insert Field — *not supported yet* |
| `F1` | Help |
| `Esc` | Cancel the edit |

---

## Mouse

| Input | Action |
|---|---|
| **Left click** | Pick a point, or select an object |
| **Left drag** (from empty space) | Selection window |
| **Middle drag** | Pan |
| **Wheel** | Zoom, cursor-centred |
| **Right click** | Repeat the last command, or open the shortcut menu — configurable |
| **Shift + right click** | One-shot object-snap override menu |
| **Double-click a viewport** *(paper space)* | Enter floating model space |
| **Double-click MTEXT** | Open the rich-text editor |
| **Right-click the OSNAP button** | Snap-type checklist |
| **Right-click a layout tab** | Rename · Move or Copy… · Page Setup Manager… · Viewports… · Delete |
| **Right-click the command log** | Copy log to clipboard |

---

## What `Esc` does, in order

`Esc` cancels the innermost thing that is running:

1. An open modal, such as the copy-duplicate dialog
2. The MTEXT editor
3. A grip drag — the original position is restored
4. A `2P` bearing pick — cancels the pick only, not the whole LINE or POLYLINE
5. The running command
6. Otherwise — clears the selection, closes the Create Points panel, cancels a pending zoom

---

## Command aliases

Typing a short alias is faster than any shortcut. The full list is in [[Command Reference]]; the
ones worth memorising:

| Alias | Command |
|---|---|
| `L` | LINE |
| `PL` | POLYLINE |
| `C` | CIRCLE |
| `M` | MOVE |
| `CP` | COPY |
| `RO` | ROTATE |
| `SC` | SCALE |
| `O` | OFFSET |
| `TR` | TRIM |
| `J` | JOIN |
| `DEL` | DELETE |
| `ZE` | ZOOMEXTENTS |
| `ZW` | ZOOMWINDOW |
| `RE` | REGEN |
| `LA` | LAYER |
| `UN` | UNITS |
| `INV` | INVERSE |
| `SE` | SURFELEV |
| `AL` | ALIGN |
| `QS` | QUICKSELECT |
| `OK` | OVERKILL |
| `CRTPTS` | CREATEPOINTS |
| `VWPTS` | VIEWPOINTS |
| `IMPPTS` | IMPORTPOINTS |
| `EXPPTS` | EXPORTPOINTS |

---

## Not available

GoSurvey has **no user-customisable keyboard shortcuts**, no `.pgp`-style alias file, and no
command macros. Aliases are fixed in the program.

---

## Related

[[Command Line]] · [[Command Reference]] · [[Object Selection]] · [[Object Snaps]]
