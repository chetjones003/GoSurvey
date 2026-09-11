# Drafting Aids

Ortho, grid, dynamic input, the work plane, and the aids that are present but not finished.

---

## Ortho — `F8`

**Toggle:** `F8`, or the status-bar **ORTHO** button.

Constrains picks and rubber-band previews to **horizontal or vertical** from the current anchor.
Which of the two you get follows the cursor: move mostly sideways and you get horizontal, mostly up
or down and you get vertical.

Ortho affects:

- draw commands (LINE, POLYLINE, RECT and the rest),
- **grip drags** — the dragged point is constrained to the horizontal or vertical line through the
  grip start,
- the TRIM trim-line,
- typed distances: with Ortho on and **no bearing lock**, a bare number is a distance along the
  horizontal or vertical toward the cursor.

**An object snap always beats Ortho.** If the cursor snaps to something, the constraint is skipped —
the same rule in draw commands and grip drags, so the two never disagree.

`F8` works even while the command line has keyboard focus, because it is a mode key rather than a
character.

---

## Grid

**Toggle:** the status-bar **GRID** button.

A minor grid follows the view and rescales as you zoom. It is a **visual reference only** — there
is no snap-to-grid, and the grid spacing is not user-configurable.

---

## Dynamic input

While a command expects a **coordinate point**, two live fields follow the crosshair showing the
current **world** X and Y at the display precision from `UNITS`. The active field is highlighted.

| Key | Effect |
|---|---|
| Type | Locks the active field to what you type |
| `Tab` | Moves between the X and Y fields |
| `Enter` | Commits the point |
| Click in the viewport | Also commits the point |

Prompts that expect a bearing, angle, distance, option, or command name show a **single** field.

---

## Elevation and the work plane — `ELEV` / `UCS`

**Command:** `ELEV`, `UCS`

Sets the Z that new geometry is drawn at. The status bar always reports the current plane:

| Readout | Meaning |
|---|---|
| `UCS: World` | New geometry lands on the world XY plane, Z = 0 |
| `UCS: Elev 125.400` | New geometry lands at Z = 125.400 |

### Usage

| Input | Effect |
|---|---|
| `ELEV 125.4` | Sets the elevation in one line |
| `ELEV` then a value | Prompted form |
| `ELEV` then blank **Enter** | Keeps the current value — `Elevation unchanged (125.4000).` |
| `ELEV W` or `UCS W` | Back to world — `UCS = World — new geometry is drawn on the world XY plane.` |

Non-numeric input answers `ELEV — usage: ELEV <elevation>, or ELEV W for world.`

> **Why the readout matters.** With a raised work plane, geometry you draw is not at Z = 0 and
> nothing in a plan view says so. The status bar is the one place that tells you. If new linework
> refuses to snap to older linework, check this field first.

---

## Bearing lock

Not a mode toggle but the main drafting aid for survey work: `A <bearing>` and `2P` in LINE and
POLYLINE lock the direction so you can enter a distance. Full detail in [[Coordinate Input]].

---

## Object snap

`F3` and the **OSNAP** button. See [[Object Snaps]].

---

## Aids that do not work yet

Be aware of these — the controls exist and respond, but nothing happens in the drawing.

| Aid | State |
|---|---|
| **POLAR** (status bar) | The button toggles and lights up, but **polar tracking is not implemented**. Its tooltip says *"Polar tracking (UI only for now)"* |
| **Object snap tracking** | Not present |
| **Temporary tracking points** | Not present |
| **Snap-to-grid** | Not present — the grid is display only |
| **Parametric constraints** | Not present |
| **Construction geometry** (XLINE / RAY) | Not present |

See [[Known Limitations]].

---

## Related

[[Coordinate Input]] · [[Object Snaps]] · [[Drawing Tools]] · [[Views and Navigation]]
