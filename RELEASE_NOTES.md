# Release notes

These notes are shown **to users inside GoSurvey**, in the update dialog, before they accept an
update. Write them for surveyors, not for developers.

- Say what changed **in the program**, in the words a user would use: commands, drawings, points,
  surfaces, plotting.
- Leave out anything a user cannot see: refactors, build changes, CI, test counts, requirement
  and task numbers, file and function names, compiler behaviour.
- One line per change. If a change needs a paragraph to explain, it probably needs a shorter one.
- Keep the newest version at the top.

The heading must be the plain three-part version (`## 0.5.0`). Every beta in that cycle shows the
same section, because a beta is a preview of that release. A version with no section here shows a
short fallback message instead — which is a missed opportunity, not a failure.

---

## 0.5.3

**Right-click now does what you tell it to**
- **Settings → User Preferences → Right-click Customization…** opens a dialog where you set what
  right-click means in each situation: with nothing selected, with objects selected, and part-way
  through a command. Each one is either "repeat the last command" or "open the shortcut menu".
- These choices already existed, but were buried in three unlabelled drop-downs. They are now
  spelled out, and **Cancel** puts everything back the way it was when you opened the dialog.
- New: **time-sensitive right-click**. Turn it on and a quick right-click acts as ENTER, while
  holding the button a moment longer opens the shortcut menu — so you can finish a command and
  reach the menu with the same button. You set how long "a moment" is, in milliseconds.
- It is switched **off** to begin with, so right-click keeps behaving exactly as it does today
  until you decide otherwise. With it on, the "no selection" and "during a command" choices grey
  out, because the length of the click is deciding those instead.

**A fuller right-click menu in the drawing**
- **Recent Input** lists what you last typed at the command line, newest first. Pick one to run it
  again.
- **Isolate Objects** — see below.
- **Clipboard** for cut, copy and paste, and **Basic Modify Tools** for move, copy, rotate, scale,
  erase, offset, trim and join, without going back to the ribbon.
- **Pan**, **Zoom** and **Free Orbit**, plus **Quick Select** and **Options**.

**Isolate Objects — new**
- Select what you are working on and choose **Isolate Objects**: everything else disappears until
  you choose **End Object Isolation**. **Hide Objects** does the opposite and hides just what you
  picked.
- Hidden objects cannot be picked while they are hidden — a window drag across where they used to
  be will not quietly pull them into your selection.
- Nothing is deleted, and nothing is saved: a drawing always opens showing everything.

**Free Orbit**
- Orbiting the model is now a command as well as a shortcut. Choose **Free Orbit** from the
  right-click menu (or type ORBIT) and drag with the left mouse button; Esc, Enter or a right-click
  ends it. Shift and the middle mouse button still orbit as before.

---

## 0.5.2

**Point files can be .txt as well as .csv**
- Import points and Export points now accept both. A points file that came off your data collector
  named `.txt` shows up in the file browser straight away, instead of only appearing once you
  switch the file type to "All files".
- The two are the same thing to GoSurvey. The file still has to be comma-separated, and the column
  order you choose still decides how it is read.
- When exporting, a name you type ending in `.csv` or `.txt` is saved exactly as you typed it. A
  name with no ending gets the one you picked in the file type list.
- A `.txt` whose columns are separated by spaces or tabs is **not** read. It lists the rows it
  could not understand and adds nothing to the drawing, rather than guessing where the columns
  are — save it comma-separated and import it again.

---

## 0.5.1

**Fixes worth knowing about**
- Loading a points file from the survey points table no longer closes GoSurvey. It used to shut
  the program down without warning, losing anything unsaved in the drawing.
- The **Groups** button on the Survey toolbar now appears. It was missing, so point groups could
  only be opened by typing the command.

**A new look**
- The dark theme has been rebuilt. Panels and dialogs now sit clearly in front of the drawing
  instead of blending into it, and boxed areas inside a window read as their own panel.
- The light theme is unchanged, and switching between them under Settings → Display still works
  as before.

**Points and layers tables**
- Both tables now behave like a spreadsheet: click a column heading to sort by it, drag to resize
  or reorder columns, and the heading row stays put while you scroll.
- Rows are the height of one line, so many more points fit on screen at once.
- Sorting only changes what you see. It never reorders the points in your drawing or in a saved
  file, and editing or deleting a row always acts on the row you are looking at.
- Coordinate rows in Properties now carry a coloured X, Y or Z marker.

**Anonymous usage reporting — new in this release**
- GoSurvey now reports anonymous usage, so development can be aimed at what people actually run
  rather than at guesses. This exists only to guide work on the program.
- What it sends: a random ID that identifies this installation and nothing else, the version you
  are running, whether you are on stable or beta, and that you are on Windows. It sends this once
  when installed, and at most once a day after that.
- What it never sends: your name, email, company, computer name, file names, drawings, survey
  data, or location. The random ID is not built from anything about you or your machine, and it
  cannot be traced back to either.
- Nothing you draw or measure ever leaves your computer.
- You can read all of this inside the program, under Settings → System → Anonymous Usage Data.

**Surfaces**
- New **Elev/Grade** tool: pick a point to read its elevation on the surface, pick a second to get
  the grade, slope and distance between them.
- Where two surfaces overlap — existing and proposed — you get a named line for each, so you can
  read both without switching anything.
- A pick outside the surface says so instead of inventing a number.

**Fixes**
- Offsetting an object no longer produces a copy that shares the original's identity, which could
  make later edits act on the wrong object.
- On laptops with two graphics cards, GoSurvey now asks for the faster one instead of taking
  whichever Windows offered.

## 0.5.0

**Surfaces and points**
- Build TIN surfaces from your survey points to model existing ground.
- Group points by rule — number range, description, or hand-picked — and reuse the group anywhere.
- Points now keep a separate raw description alongside the description you edit.

**Staying up to date**
- GoSurvey checks for a new version each time it opens and asks before installing anything.
- Choose between stable releases and beta previews in Settings → System → Updates.
- Updates download, verify themselves, prompt you to save open work, then install and reopen.

**Drawings**
- Double-clicking a `.gs` file now opens that drawing instead of an empty one.
- Drawings saved by older versions of GoSurvey keep opening; they are converted as they load, and
  the file on disk is not changed until you save.
