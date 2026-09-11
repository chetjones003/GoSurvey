# Command Line

The command line is where GoSurvey talks to you and where you talk back. Read it. Every command
states what it wants, what it did, and why it refused.

---

## The two forms

| Form | How to get it | What it looks like |
|---|---|---|
| **Floating bar** *(default)* | Default | A translucent bar near the bottom of the viewport, with a fading history tail |
| **Classic dock** | **View → Classic command dock** | A docked panel at the bottom of the window |

| Key | Effect |
|---|---|
| `F1` | Open the in-app user manual — contextual to what you are typing or hovering |
| `F2` | Toggle the full-height **console** view (floating bar only) |
| `Ctrl+9` | Hide and restore the bar (floating bar only) |

`Esc` never closes the console — it is reserved for cancelling commands.

![In-app user manual (F1)](wiki-img:20-in-app-wiki.png)

While typing a command name, **`F1`** opens that command's wiki page when the input fuzzy-matches a
registered command.

---

## Parts

| Part | What it shows |
|---|---|
| **Log** | Everything the program has said. In the floating bar the recent tail is fully opaque and **fades when idle**; `F2` shows the whole thing |
| **Input** | Where you type, with a green prompt |
| **Hint line** | What is valid at the current step. `[TOKEN]` markers are **clickable links** that submit that option |

**Right-click the log ▸ Copy log to clipboard** to take a session record away with you — useful
when reporting a problem.

---

## Entering commands

**Press Enter to submit from anywhere.** You do not have to click into the input box first. This is
the single most useful thing to know about the command line.

Command names are **not case-sensitive**.

### Autocomplete

Typing opens a suggestion popup **at the crosshair**, not at the command bar, so your eyes stay on
the drawing.

| Key | Effect |
|---|---|
| `↑` / `↓` | Move the highlight |
| `Enter` | Run the highlighted command |
| `Tab` | Complete to the highlighted entry |
| Click a row | Run it directly |
| `Esc` | Dismiss the popup |

Each row shows the command **name in capitals**, its icon, and a one-line description.

Matching is **prefix-based** and ranked, so the obvious answer comes first:

| Rank | Match |
|---|---|
| 1 | The query is exactly the command name |
| 2 | The query is exactly an alias — this is why `l` lists **LINE** before **LAYER** |
| 3 | The command name starts with the query |
| 4 | An alias starts with the query |

So `e` lists ELLIPSE and EXPORTPOINTS — not REGEN or DELETE, which merely contain an `e`.

### Fuzzy matching on submit

A submitted command that is not an exact name or alias is matched **fuzzily**. A strong match runs
directly. Otherwise you get suggestions:

```
Unknown command. Did you mean: LINE, ALIGN, ELLIPSE?
```

With nothing close:

```
Unknown command. Type HELP.
```

### Command history

- **Recent Input** on the viewport right-click menu lists the commands you have typed, newest
  first. Choosing one re-submits it.
- The command bar has the same history dropdown. The two cannot disagree — they are the same list.
- **Right-click in the viewport** repeats the last command (unless you have configured it
  otherwise — see [[Object Selection]]).

---

## Answering prompts

Once a command is running, the input takes its arguments rather than command names.

| Input | Meaning |
|---|---|
| `X,Y` or `X Y` | An absolute point |
| `@dx,dy` | A point relative to the previous one |
| A number | A distance, radius, factor, or angle — depending on what is being asked |
| A letter or word | A command option: `D`, `3P`, `A`, `2P`, `R`, `C`, `L`, `T`, `H`, `V`, `CLOSE`, `END`, `W` |
| Blank **Enter** | Accept the default, or keep the current value at a system-variable prompt |
| `Esc` | Cancel |

The hint line always names the options that are valid **right now**, and you can click them instead
of typing.

Full coordinate and angle syntax: [[Coordinate Input]]

---

## One-line command forms

Most commands prompt step by step. These also take everything on one line:

| Form | Effect |
|---|---|
| `TRIMSTATE 1` | Set the TRIM mode |
| `ELEV 125.4` · `ELEV W` | Set the work-plane elevation, or return to world |
| `VS SHADED` | Set the visual style |
| `PSCALE 50` | Set the plot scale |
| `IMPORTMODEL "C:\m.glb" 0.0833 100 200 0` | Path, scale, insertion X Y Z |
| `BENCH 250000 900` | Benchmark segments and frames |

Typing the bare command instead opens the prompt or a file dialog. A path containing spaces must be
quoted.

---

## Dynamic input

While a point is expected, two live fields follow the crosshair showing world X and Y. `Tab` moves
between them, typing locks a field, `Enter` or a click commits. Prompts expecting a bearing, angle,
distance, or option show a single field. See [[Coordinate Input]].

---

## Reading the log

Messages follow a consistent shape:

| Kind | Example |
|---|---|
| **What is wanted** | `LINE — specify first point (click or type X,Y / X Y). ESC to cancel.` |
| **What happened** | `Imported tank.glb — 184320 triangles, 12 parts, scale 0.0833.` |
| **What did not happen, and why** | `SURFELEV — outside surface. No elevation at that point.` |
| **What was skipped** | `Not imported (geometry only): materials, animations, cameras.` |
| **A refusal, with the reason** | `ID — finish or cancel the active command first.` |

The program does not fail silently. If something you expected did not happen, the reason is in the
log — press `F2` and scroll back.

---

## HELP

Typing `HELP` prints a compact command list, plus notes on LINE bearing entry, ROTATE, SCALE,
INVERSE, TRIM, OFFSET, ZOOM, and OVERKILL. It is a reminder, not a reference — this wiki is the
reference.

---

## Common problems

| Problem | Cause | Solution |
|---|---|---|
| Typing does nothing | The command bar is hidden | `Ctrl+9`, or **View → Command line** |
| `F3` / `F8` do not fire while I type | They should — they are mode keys and work during typing | If they still do not, check that the window has focus |
| A command reports `finish or cancel the active command first` | Another command is running | Press `Esc` and retry |
| The autocomplete popup blocks a click in the drawing | Clicks that land on the popup are ignored by the viewport, by design | `Esc` to dismiss it |
| The log scrolled past the message I needed | The floating bar shows only a tail | `F2` for the full console |
| I cannot find what a command wants | The hint line under the input says, at every step | Read it, and click the `[OPTION]` links |

---

## Related

[[Command Reference]] · [[Coordinate Input]] · [[Keyboard Shortcuts]] · [[Object Selection]]
