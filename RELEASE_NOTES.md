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

## 0.5.1

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
