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
