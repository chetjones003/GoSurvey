# AGENTS.md

Instructions for AI coding agents working in this repository.

**Read [`CLAUDE.md`](CLAUDE.md) first.** It is the governance document for this repo (SPEC ->
VERIFICATION -> WORKSHOP, the git workflow, the Windows build tooling, and the SPEC GAP rules) and
applies to every agent, not only Claude.

## Qt UI changes: show screenshots in chat

When the work adds or changes visible Qt UI (a new window, dialog, ribbon or Settings page, a layout
tweak, a restyle), running the tests is not enough. Before calling the work done:

* Capture screenshots of every affected surface and its meaningful states (for example: normal,
  busy, error, empty, disabled). Use the Qt test run on the hidden desktop
  (`tools/run-qt-tests-hidden.ps1`, which `ctest -R qt.editor` uses) with `widget->grab()` or, for the
  3D viewport, `grabFramebuffer()`. Save the PNGs under `build/`; never commit them.
* Paste them **inline in the chat reply** as Markdown images with absolute forward-slash paths, for
  example `![Settings](C:/Users/<user>/source/repos/GoSurvey/build/qt-shot.png)`. Do **not** publish
  them as an HTML page, and do not put them inside a question popup or other tool call: the user could
  not see images shown that way. Crop or split wide captures so text is readable at chat width.
* Label each image, say what changed, then **stop and wait** for the user to approve or ask for tweaks.
  Apply the requested changes and show new screenshots until they approve. Only then commit and open
  the PR.
