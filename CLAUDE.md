# CLAUDE.md

Rules for every agent in this repo. `AGENTS.md` points here.

**GitHub issues are the unit of work.** The issue and its comments define what to build and when
it is done.

## Git

- `master` = latest release, always release-ready. `beta` = development.
- Never commit to `master` or `beta`. Branch from `beta` as `feat/`, `fix/`, `docs/`, `refactor/`,
  `test/` or `chore/<name>`, one new worktree per task.
- Branch → PR → `beta` → release PR → `master`. Never merge a dev branch into `master`.
- After a merge into `beta`: delete the branch locally and on `origin`, and remove its worktree
  (`pwsh scripts/cleanup-merged-worktrees.ps1`, or `git worktree remove` + `git remote prune origin`).
  If `git cherry origin/beta <branch>` shows unmerged commits, tag them `archive/<name>` first.
  Leave the `fork` remote alone.
- **Emergency bypass:** only when the admin explicitly orders it (urgency alone is not enough), the
  admin may push or merge directly to `beta`/`master`. Record it in the PR or issue.

### Labels

Label every issue and PR when you create it, using existing repo labels. Keep one status label
current on the issue and its PR: `in progress` → `needs review` → `in review`, back to
`in progress` if review sends it back.

### Before opening a PR

1. **Docs review.** Check `README.md`, `PRIVACY.md`, `resources/wiki/` and the
   [GitHub Wiki](https://github.com/chetjones003/GoSurvey/wiki). Update what the change affects and
   list each source in the PR description as updated or "no change". Don't open the PR until an
   affected wiki is updated.
2. **Self-review** `git diff <base>...HEAD` the way PR-Agent will: error paths, boundary values,
   locale/units, read-only and permission guards, undo, and every acceptance criterion in the
   issue. Copy guards from sibling commands (e.g. `CREATEVIEWFRAMES` for read-only projects). Run
   the `code-review` skill and fix its findings.

### PR-Agent: fix everything in one push

PR-Agent costs money per run. It reviews once when the PR opens or is marked ready. The owner posts
`/review` for one confirmation pass; agents never trigger reviews.

1. Collect every finding into one numbered list: `gh pr view <n> --comments`,
   `gh api repos/{owner}/{repo}/pulls/<n>/comments --paginate`,
   `gh api repos/{owner}/{repo}/pulls/<n>/reviews --paginate`.
2. Fix the whole class of each finding across the diff, not just the named instance.
3. Test each fix, including the boundary the finding names.
4. Re-run the self-review on the new diff.
5. Push once. Never push partial fixes.
6. If a finding is wrong, reply with evidence.

If the confirmation pass still finds issues, repeat 1–5 and note in the PR which self-review item
would have caught each one.

## Environment

Windows-native: MSVC, CMake, Ninja. `build.bat` and `CMakePresets.json` are authoritative. Needs an
MSVC x64 Qt 6.8+ kit; set `$env:GOSURVEY_QT_ROOT` if it isn't at `C:\Qt\6.8.3\msvc2022_64`.

| Task | Command (PowerShell, repo root) |
| --- | --- |
| Build everything (Release) | `.\build.bat` |
| Qt GUI tests (hidden desktop) | `ctest --preset ninja-release -R "^qt[.]editor$"` |
| Full suite | `ctest --preset ninja-release` |
| Output layout check | `ctest --preset ninja-release -R "^build[.]output-layout$"` |
| Clean rebuild | `.\build.bat release --clean-first` |
| Run the app | `.\run.bat` |

Debug: `.\build.bat debug`, `.\run.bat debug`, `ctest --preset ninja-debug`. Direct CMake:
`cmake --build --preset ninja-release` from an MSVC dev shell. From Git Bash/WSL, call the `.bat`
files via `cmd.exe /c`. Report test failures explicitly; never call a partial pass green.

### Worktrees

Build and run only from your own worktree; each has its own `build/`. Never close or touch a
GoSurvey process from another worktree. If a build is blocked, check the process path first.

### Build output

- `build/`: only `GoSurvey.exe`, `GoSurveyQtTests.exe` and their runtime files (DLLs, `qt.conf`,
  `qt-plugins/`, `resources/`, `.ini`). Debug goes in `build/debug/`.
- `build/.cmake/<preset>/`: caches, generated headers, libs, symbols, helper executables.
- `build/artifacts/`: screenshots, logs, test reports. Never commit them.
- `build.bat` runs `tools/tidy-build-output.ps1`, which archives clutter to
  `build/artifacts/legacy-output/`. To tidy without building, close apps and run
  `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\tidy-build-output.ps1 -RuntimeDirectory .\build`.

## Working on an issue

1. **Understand:** read the issue and its comments, `GLOSSARY.md`, relevant `docs/adr/`, and the
   code and tests you'll change.
2. **Clarify from evidence first:** code, tests, `resources/wiki/`, Autodesk docs, the installed
   Civil 3D, or `accoreconsole.exe` for geometry, units and DWG/DXF. Ask the user only when
   evidence can't settle it or the choice is irreversible. Ask one question at a time, in plain
   English, with options and a recommendation.
3. **Implement** the smallest correct change, with happy-path and edge-case tests (`tdd`,
   `diagnosing-bugs`).
4. **Verify:** build, run the relevant tests, run `code-review`.
5. **Record** hard-to-reverse decisions as an ADR in `docs/adr/` and new terms in `GLOSSARY.md`
   (`domain-modeling`). Skip routine choices.
6. **Open the PR** against `beta` with `Closes #N`.

### Qt UI changes: Civil 3D comparison

Visible Qt UI changes must pass this panel before the work is done. User approval is not required.

1. Capture every affected surface and state (normal, busy, error, empty, disabled) from the hidden
   test run (`tools/run-qt-tests-hidden.ps1`) with `widget->grab()` or `grabFramebuffer()`.
2. Reproduce the same task and view in Civil 3D (match size, scale, theme, camera and zoom; note
   what can't be matched) and capture it.
3. Three agents score the pairs independently, 0–2 each (0 = material mismatch, 1 = noticeable,
   2 = close), citing locations: structure/geometry, hierarchy/typography, color/visual weight,
   state/feedback, overall composition.
4. Pass when every item's median is ≥ 1 and there's no blocker. A blocker is a mismatch that hides
   a work area, makes a control unreadable or changes meaning, cited by at least two reviewers.
   On a fail: fix, recapture, and run a fresh panel.
5. Save captures, settings, score sheets and the result under `build/artifacts/`, and report it in
   the PR.

If Civil 3D can't be used or matched, mark the comparison **inconclusive** in the PR. Never claim
a pass from unpaired screenshots.

## Coding principles

- Prefer simple, readable code, existing patterns, minimal dependencies and small changes.
- Avoid clever code, global state, duplicate architecture and unapproved architectural changes.
- Add an abstraction only when it has two concrete uses today.
- Note assumptions and tech debt in the PR. Ask before changing architecture or behavior beyond
  the issue.

## Agent skills

### Issue tracker

GitHub Issues (`chetjones003/GoSurvey`) via `gh`. See `docs/agents/issue-tracker.md`.

### Triage labels

Defaults: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See
`docs/agents/triage-labels.md`.

### Domain docs

Single-context: `GLOSSARY.md` and `docs/adr/` at the repo root, created when first needed. See
`docs/agents/domain.md`.
