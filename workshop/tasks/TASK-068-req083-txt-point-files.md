# TASK-068 — Accept `.txt` alongside `.csv` for point import and export (REQ-083)

- Type:    feature
- Status:  done — GUI acceptance pass completed by the user in app 2026-08-18
- Opened:  2026-08-17
- Owner:   Claude Code (AI assistant)

## 1. Authority
- Goal:         none named (raised directly by the user: "lets add .txt import alongside .csv
  for import points")
- Requirements: **REQ-083** — drafted for this task and currently `proposed`; CLAUDE.md step 1
  requires `accepted` before implementation, so §3 records the escalation. Also **REQ-001**
  (a `.txt` we cannot parse is refused per row, never guessed), **REQ-041** (the validation this
  must reuse rather than duplicate), **REQ-201** (no silent failure).
- Constraints:  CLAUDE.md rules 1–8; `spec/architecture.md` §2 layering (Platform is the bottom
  layer and may not name IO), §11 invariants 1–4.
- Acceptance:   REQ-083's five conditions, restated verbatim:
  1. the Import points chooser lists `.txt` files with its **default** filter selected, and
     picking one populates the path;
  2. the same comma-delimited bytes saved as `points.csv` and as `points.txt` import to identical
     points (ID, northing, easting, elevation, description) and produce an identical validation
     summary;
  3. a missing, empty, or locked `.txt` shows its REQ-041 message and Import is disabled — the
     same message the `.csv` of that state shows;
  4. a space- or tab-delimited `.txt` reports the per-row column error and adds no point to the
     drawing;
  5. in Export points, a typed name with no extension is saved with the chosen filter's extension,
     and a name typed as `points.txt` is saved as `points.txt` — not `points.txt.csv`; both files'
     bytes are identical for the same drawing and layout.
- Owning subsystem: **Platform** (`src/platform/WinFileDialogs.cpp` — the two choosers), with a
  pure helper in **util** and one wording change in **UI**. `src/io/SurveyCsv.cpp` is deliberately
  **not** touched.

## 2. Scope
- In scope: the open/save filter lists for point files; the save dialog's default-extension rule;
  a pure, unit-testable helper for that rule; the "Pick a CSV file" prompt wording.
- Out of scope: **delimiter detection or a delimiter control.** Offered to the user and declined —
  parsing stays comma-only. Recorded in REQ-083 as an anti-behaviour rather than as a silence, so
  a later contributor does not add sniffing as an obvious improvement.
- Out of scope: **renaming the `…CsvUtf8` dialog functions.** Their contract widens (they now also
  handle `.txt`) but their signatures do not, they have one call site each, and a rename would
  touch four files to change no behaviour. The header comment states the widened contract instead.
  Flagged rather than hidden — if a third point-file extension ever appears, rename then.
- Out of scope: the `surveyImportCsvPath` / `surveyExportCsvPath` field names, for the same reason.
- Smallest change: one filter string per chooser, one extension rule replacing a hard-coded
  `.csv` append, one pure helper so that rule is testable, one prompt string.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] **Yes, once — escalated, not decided here.** There is **no accepted requirement** for
      which extensions a point file may carry. REQ-041 governs validation only and says nothing
      about the chooser; no other REQ mentions the point-file extension. Building without one is
      exactly what CLAUDE.md step 1 forbids, so REQ-083 was drafted and goes to the user for a
      recorded decision. **No code until it is `accepted`.**
    - [ ] Everything else: **No.**
      - **No new dependency.** Win32 common dialogs already in use; no third-party anything.
      - **No new abstraction (REQ-301).** `pointfile::ExtensionToAppend` is a free function with
        one production caller, not an interface/template/policy object. It exists because the rule
        it holds is the one part of this change with a real failure mode (`points.txt.csv`), and
        because `WinFileDialogs.cpp` is Win32-only and cannot be linked by the test target — the
        same reasoning that produced `io/SurveyCsvValidate.hpp` for REQ-041.
      - **No upward dependency.** The helper is placed in `src/util/`, which is pure and below
        every layer, precisely so Platform is not made to name IO. Putting it beside
        `SurveyCsvValidate.hpp` in `io/` would have been a Platform → IO include and a §11
        invariant-1 violation. *(Noted: this is the first `util/` include in `src/platform/`.
        Legal — util depends on nothing — but it is a new edge and is called out for review.)*
      - **No data-format change.** File *contents* are untouched; only which names the chooser
        shows and which extension it appends.
      - **No global state, no ownership change, no algorithm the spec didn't specify.**

## 4. Questions  (workflow.md §5 — asked before planning)
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | How should a `.txt` point file be parsed — auto-detect the delimiter, comma only, or add a user-facing delimiter control? | 2026-08-17 | **Comma only — just accept `.txt`.** No sniffing, no new control. |
| Q2 | Does this cover Export points too, or import only? | 2026-08-17 | **Both import and export.** |
| Q3 | REQ-083 is `proposed`; accepting it is a spec decision, not the Workshop's. | 2026-08-17 | **Accepted.** Recorded as D-2026-08-17-c in `spec/project.md` §9. |

## 5. Assumptions
```
ASSUMPTION-1: A user who selects the "Text (*.txt)" filter and types a name with no extension
              wants .txt, not .csv.
- Because:       REQ-083 says "the chosen filter's extension" but the Win32 chooser does not
                 report intent beyond nFilterIndex.
- Risk if wrong: a file lands as points.txt where the user expected points.csv — visible
                 immediately in the save dialog's own directory listing, and harmless, since
                 REQ-083 makes the two interchangeable on the way back in.
- Validate by:   the manual export check in §6; reversible in one line if wrong.
```
```
ASSUMPTION-2: The Win32 open chooser shows both extensions from a single semicolon-separated
              pattern ("*.csv;*.txt") under one filter entry.
- Because:       this is documented OPENFILENAME behaviour but has not been exercised in this
                 codebase — every existing filter here is single-extension.
- Risk if wrong: acceptance condition 1 fails visibly on the first Browse… click.
- Validate by:   the manual import check in §6, which is the first step after building.
```

## 6. Plan  (write BEFORE any code)
- Approach: the importer is already extension-blind — `SurveyCsvImportFile` opens whatever path it
  is handed and `SurveyCsvRefreshImportPreview` probes it the same way, so **`.txt` support is
  almost entirely the absence of a filter**. The work is therefore confined to the two choosers,
  plus the one place a hard-coded `.csv` is appended. `src/io/SurveyCsv.cpp` is not edited at all —
  that is the point of REQ-083's "no second code path" clause, and any diff to it during this task
  is a finding against the task.
- Files/functions to touch:
  - `src/util/PointFileExt.hpp` *(new)* — `pointfile::ExtensionToAppend(name, txtFilterChosen)`
    returning `".csv"`, `".txt"`, or `""` when the name already ends in either (case-insensitive).
  - `src/platform/WinFileDialogs.cpp` — `BrowseOpenFileCsvUtf8` filter list;
    `BrowseSaveFileCsvUtf8` filter list + replace the unconditional `.csv` append with the helper,
    applied to the UTF-8 result (so the helper works in narrow chars) with an explicit capacity
    check.
  - `src/platform/WinFileDialogs.hpp` — doc comments stating the widened contract.
  - `src/io/SurveyCsv.cpp` — one string: "Pick a CSV file to preview." → "Pick a CSV or TXT point
    file to preview." *(wording only; the surrounding logic is untouched.)*
  - `tests/PointFileExtTests.cpp` *(new)* + one line in `CMakeLists.txt`.
- Filter list (identical for both choosers, default index 1):
  `Point file (*.csv;*.txt)` · `CSV (*.csv)` · `Text (*.txt)` · `All (*.*)`
- Test approach:
  - happy path — `points` + CSV filter → `.csv`; `points` + Text filter → `.txt`;
    `points.csv` → `""`; `points.txt` → `""`; `POINTS.TXT` → `""` (case-insensitive).
  - failure mode — `points.dat` and `job.2026` are **not** treated as point files and still get an
    extension appended (the bug being guarded is the inverse: matching too loosely and producing a
    name the user did not ask for); empty name; a name ending in a bare `.`; a name shorter than
    the extension it is compared against (the off-by-one that a naive `size() - 4` index invites).
  - manual (the Win32 chooser and the REQ-041 path cannot be linked by the test target) — the five
    acceptance conditions, run in the app.
- Steps:
  - [ ] REQ-083 accepted and the decision recorded in `spec/project.md` §9 — **gate**
  - [ ] add `util/PointFileExt.hpp` + `tests/PointFileExtTests.cpp` + CMake entry; run them red→green
  - [ ] widen the two filter lists; replace the `.csv` append with the helper
  - [ ] update the two header doc comments and the preview prompt string
  - [ ] build; run the full ctest suite (not just the new file)
  - [ ] manual pass over all five acceptance conditions, including a locked `.txt` and a
        space-delimited `.txt`
  - [ ] flip REQ-083 to `accepted`/trace row to `implemented`; completion report

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1, Q2 above; Q3 outstanding and blocking). Tests-first for the
  pure helper — it is the only automatable part, so it is written before the dialog edit.

## 8. Implementation log
- 2026-08-17 — opened. Spec read; REQ-083 drafted `proposed`; trace row added. Boundary check
  returned **Yes** on the missing requirement → escalated to the user. No code written.
- 2026-08-17 — REQ-083 accepted by the user (D-2026-08-17-c). Status → `accepted`, trace row
  likewise. Gate cleared.
- 2026-08-17 — added `src/util/PointFileExt.hpp` and `tests/PointFileExtTests.cpp` (+ CMake entry).
  **Honest note on ordering:** the plan said tests-first and the helper was in fact written first,
  so the tests were never observed red. They are not vacuous — `ExtensionToAppend("points.txt", …)
  == ""` fails by construction against the previous unconditional append, which is the regression
  they exist to hold — but the discipline slipped and is recorded rather than glossed.
- 2026-08-17 — widened both filter lists to one shared `kPointFileFilter`; replaced the
  unconditional `.csv` append in `BrowseSaveFileCsvUtf8` with the helper, applied after the UTF-8
  conversion so the rule is narrow-char and testable. Doc comments on both dialog functions state
  the widened contract.
- 2026-08-17 — wording: `SurveyCsv.cpp` preview prompt and `SurveyPoints.cpp`'s IMPORTPOINTS hint
  now name CSV **or** TXT. `src/io/SurveyCsv.cpp` gained no logic and no branch — verified by
  grepping the whole import path for extension tests: there are none, which is why acceptance
  condition 2 holds by construction rather than by a second code path being kept in step.
- 2026-08-17 — added three fixtures under `samples/` for the GUI pass: `points-req083.csv` and
  `points-req083.txt` are **byte-identical** (MD5 `e0f61ac4…` both), and `points-req083-spaced.txt`
  is the space-delimited file that must be refused.
- 2026-08-17 — build + full suite green (below). Attempted to automate acceptance conditions 2 and
  4 through the REQ-203 headless driver and **could not**: `IMPORTPOINTS` only opens the window
  (`SurveyPoints.cpp:906`) and the import itself is triggered by the panel's Import button, which
  headless has no way to press. Adding a driver verb for it would be test-infrastructure work
  outside this requirement, so it was not done — recorded as the reason those conditions stay
  manual, rather than left looking like an oversight.

- 2026-08-17 — user directed this to ship as **0.5.2** with the GUI pass still outstanding, and to
  leave the unrelated snap-pick local-coordinate work uncommitted in the tree rather than sweep it
  into the release. Both recorded here because a release containing an unverified acceptance
  condition is a decision, not an oversight — the conditions in §10 are still open and the release
  can be pulled if the pass fails.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] **build-project — PASS.** MSVC via `vcvars64` + `cmake --build build`: all 14 targets,
  including `GoSurvey.exe`, `gosurvey_headless.exe` and `GoSurveyTests.exe`. No warning from
  `WinFileDialogs.cpp`, `PointFileExt.hpp` or `PointFileExtTests.cpp`; the C4244/C4530 warnings in
  the log are pre-existing in `CadUi.cpp` / `CadCommands.cpp` and untouched here.
- [x] **architecture-review — PASS.** Layering: the one new edge is `src/platform` → `src/util`,
  which is not upward (util depends on nothing); the alternative, `io/SurveyCsvValidate.hpp`, would
  have been Platform → IO and an invariant-1 violation. Change sits in the subsystem that owns file
  choosers. Ownership: no new resource. State: no new global, nothing summoned. Abstraction: a free
  function, not an interface/template — REQ-301 not engaged; the simpler concrete version (the rule
  inline in `WinFileDialogs.cpp`) was considered and rejected for a stated reason (Win32-only TU,
  unlinkable by the Catch2 target, and this is the one rule here with a real failure mode).
  Boundaries: no `gl*`; no previously-rejected approach (CON-03 not engaged).
- [x] **code-review — PASS with one advisory.** Correctness: acceptance condition 5's helper half is
  test-covered; conditions 1/3/4 and 5's GUI half are **not yet run** — see §10, they are not
  claimed. Edge cases: empty name, one-character name, name shorter than the extension, trailing
  bare dot, `.csv.bak`, `.txtt`, mixed case — all pinned. Buffer: the append checks
  `len + ext.size() + 1 > utf8Cap` before writing. Ownership/const: no allocation, inputs
  `string_view` by value, return `string_view` into a literal. Names follow §5.
  *Advisory (not blocking):* the capacity failure returns `false` without logging. Platform has no
  logger and `bool` is this API's existing failure convention — the `WideToUtf8` call two lines
  above fails the same way, and for a path long enough to trip mine that conversion fails first. No
  new failure mode; consistent with the surrounding code.
- [x] **dependency-audit — n-a.** No dependency added, removed or version-changed.
- [x] **performance-review — n-a.** A file chooser runs once per user click; the helper is two
  bounded `memcmp`-shaped loops over ≤4 characters.
- [x] **testing — PASS.** `PointFileExtTests` 26 assertions / 5 cases green. Full Catch2 suite
  **397 cases / 203,763 assertions**, and full `ctest` **409/409**, including every headless
  transcript — so the two wording edits and the dialog change broke no existing behaviour.

## 10. Verification result
- Submitted:  2026-08-17
- Verdict:    **PASS.** Everything automatable passed 2026-08-17; the five GUI conditions below were
  run and confirmed by the user in the application on 2026-08-18. They are now claimed, not pending:
  path cannot be linked by the test target and cannot be driven headlessly (see §8):
  1. Import → Browse… lists `samples/points-req083.txt` with the **default** filter selected.
  2. Importing `points-req083.csv` and `points-req083.txt` yields identical points and an identical
     validation summary. *(Holds by construction — the import path contains no extension test at
     all — but "by construction" is an argument, not a run.)*
  3. A `.txt` that is missing / empty / open in another application shows its REQ-041 message with
     Import disabled.
  4. `points-req083-spaced.txt` reports per-row column errors and adds no point.
  5. Export: typing `points` saves `points.csv`; typing `points.txt` saves `points.txt`.
- Findings:   none blocking; one advisory recorded in §9 (unlogged capacity failure, pre-existing
  convention).
- Technical debt: **DEBT-REQ083-1 — `OFN_OVERWRITEPROMPT` does not see the appended extension.**
  Because the extension is added *after* `GetSaveFileNameW` returns, typing `points` when
  `points.csv` already exists overwrites it with no prompt. **Pre-existing and codebase-wide** —
  every save chooser in `WinFileDialogs.cpp` (DXF, DWG, GS, PDF) appends the same way — and
  untouched by this change, so it is named rather than fixed here. *Removal condition:* it cannot
  be fixed with `lpstrDefExt` without breaking ASSUMPTION-1 (Windows would append `csv` before the
  filter choice could be read), so the fix is an explicit `PathFileExists` check after the append,
  applied to all six choosers as its own task.

## 11. Outcome
- Requirements satisfied: REQ-083 (Acceptance met: **yes** — the automatable half 2026-08-17, the
  five GUI conditions confirmed by the user in app 2026-08-18)
- Tests added:            `tests/PointFileExtTests.cpp` (5 cases, 26 assertions)
- Refactors:              none
- Docs updated:           `spec/requirements.md` (REQ-083 + trace row), `spec/project.md`
  (D-2026-08-17-c), `src/platform/WinFileDialogs.hpp` (contract comments)
- Done:                   2026-08-18 (GUI acceptance pass confirmed by the user)
