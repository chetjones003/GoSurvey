# Getting Started

## What GoSurvey is

GoSurvey is a Windows CAD program built around survey work. Alongside ordinary drafting
(lines, arcs, text, dimensions, hatching, layers, sheets) it carries three things a general CAD
program does not:

- a **survey point database** — numbered points with northing, easting, elevation, description,
  layer, and label style, kept separate from the linework;
- **raw-observation reduction** — the Traverse Editor takes field angles and distances and
  computes coordinates and least-squares closure;
- **coordinate transformation** — ALIGN fits a 2D Helmert transformation from control pairs, so a
  local-coordinate drawing can be moved onto state-plane.

Drawings are saved as `.gs`. Exchange with other CAD is through **DXF** (built in) and **DWG**
(through an external converter). Survey points move through **CSV** in PNEZD/PENZD order.

---

## Install (Windows)

1. Open the [Releases](https://github.com/chetjones003/GoSurvey/releases) page.
2. Download the **`.exe` installer** from the latest release.
3. Run it. GoSurvey installs to `%ProgramFiles%\GoSurvey` and adds a Start Menu entry. A desktop
   shortcut is offered.
4. **SmartScreen** — the installer is not code-signed yet, so Windows may show *"Windows protected
   your PC"*. Choose **More info → Run anyway** if you trust the release.

**If the program will not start with a missing-DLL error**, install the
[Visual C++ Redistributable for x64](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).

### Updates

GoSurvey checks for a newer version **each time it opens**. A short *"Checking for updates"*
dialog appears while it does; with no internet connection the check is skipped, and if it is slow
you can press **Continue without checking**.

Nothing downloads or installs on its own. When an update exists you choose **Update Now**,
**Remind Me Later**, or **Skip This Version**. Choosing to update downloads the installer, verifies
it against a SHA-256 published with the release, prompts you to save open drawings, then installs
and reopens GoSurvey.

Both update settings live in **View → Settings… → System**:

| Setting | Default | Meaning |
|---|---|---|
| Check for updates on startup | On | Turn off and GoSurvey makes no network requests at all |
| Include beta releases | Off | Turn on to receive pre-release builds |

See [[Files and Drawings]] for how drawing compatibility is handled across updates.

---

## Starting the application

A splash screen appears while the program loads, then the **start screen** opens with recent
drawings, **Open a Drawing**, and **New Drawing**.

![GoSurvey start screen](wiki-img:01-main-window.png)

*The start screen when GoSurvey opens — recent drawings in the centre, account and feedback on the
right. Open a drawing or start a new one to reach model space.*

After you open or create a drawing, a tab such as **Drawing 1** becomes active. Take a moment with
[[User Interface]] — the manual assumes you know where the ribbon, command line, Properties panel,
and status bar are.

---

## Creating a new drawing

**File → New**

A new tab appears named `Drawing 2`, `Drawing 3`, and so on. Each tab is a **completely independent
document**: its own geometry, layers, survey points, paper layouts, plot scale, and undo history.
Switching tabs switches all of it.

There is no "new from template" dialog. New drawings start from the bundled template
(`resources/default-template.gs`) if one is present — see **Settings → Files**.

## Opening an existing drawing

**File → Open** → choose a `.gs` file.

The drawing opens **in a new tab**, named after the file. Opening does not replace what you are
working on.

GoSurvey opens drawings saved by any older version, converting them as it loads. The file on disk
is not modified until you save.

To bring in a drawing that is *not* a `.gs` file, use **File → Import DXF…** or
**File → Import DWG…** instead — see [[Import and Export]].

## Saving

| Action | How | What happens |
|---|---|---|
| **Save** | `Ctrl+S` or **File → Save** | Writes to the current file path. If the drawing has never been saved, the Save-As dialog opens first. |
| **Save As** | **File → Save As…** | Choose a new name and location. The drawing tab is renamed to the file stem, and later saves go to the new path. |

There is no autosave and no automatic backup file. Save deliberately.

## Closing

**File → Quit Application**, or the window's close button.

If **any** open tab has unsaved changes, a confirmation dialog appears before the program exits.
If everything is saved, it closes immediately.

---

## Your first drawing

1. Type `LINE` (or `L`) and press **Enter**.
   The log reads `LINE — specify first point (click or type X,Y / X Y). ESC to cancel.`
2. Type `0,0` and press **Enter**.
3. Type `100,0` and press **Enter** — a segment appears.
4. Type `@0,50` and press **Enter** — `@` means *relative to the last point*, so this goes 50 units
   north.
5. Press **Esc** to end the command.
6. Type `ZE` and press **Enter** to fit the drawing in view.

You now have a two-segment line. Select it by clicking a segment; grips appear at the endpoints and
the **Properties** panel on the left fills in with its layer, colour, and coordinates.

Next: [[Drawing Tools]] · [[Coordinate Input]]

---

## Your first survey workflow

**Goal:** get field points into a drawing, look at them, and measure between them.

1. **Import the points.** Type `IMPORTPOINTS` (or `IMPPTS`). In the dialog:
   - **Browse…** to your CSV.
   - Set **Column order** to match the file — `P,N,E,Z,D` is the common PNEZD order.
   - Tick **First row is header (skip)** if the file has one.
   - Check the **File preview** and **Validation summary**, then press **Import**.
2. **Frame them.** Type `ZE`.
3. **Look at the table.** Type `VIEWPOINTS` (or `VWPTS`) to see and edit every point's ID,
   easting, northing, elevation, layer, and description.
4. **Measure.** Type `INVERSE` (or `INV`), then snap to two points. The log reports ΔE, ΔN,
   horizontal distance, and bearing.
5. **Draw the boundary.** Type `LINE`, and with **OSNAP** on (press `F3`), snap point-to-point
   around the parcel.
6. **Save.** `Ctrl+S`.

Next: [[Survey Points]] · [[Object Snaps]] · [[Workflows]]

---

## Your first complete project

The full path — set up units, bring in points, reduce a traverse, draw and annotate, build a sheet,
plot a PDF — is laid out step by step in [[Workflows]].
