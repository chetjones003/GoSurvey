# TASK-069 — A completed import is reported as an outcome, not re-validated (BUG-014, REQ-041 rev 3)

- Type:    bug
- Status:  self-verify — automated green; the GUI confirmation is the user's screenshot repro
- Opened:  2026-08-17
- Owner:   Claude Code (AI assistant)

## 1. Authority
- Goal:         none (user-reported defect, with screenshots)
- Requirements: **REQ-041 revision 3**, added by this task — see §3, the behaviour was unspecified.
  Related: REQ-201 (this is its inverse — a reported failure that did not happen).
- Constraints:  CLAUDE.md rules 1–8; root cause before fix; no speculative change.
- Acceptance:   REQ-041 rev 3's new condition, verbatim: *"once an import has run, the summary
  reports what that import did — 'Imported N point(s) — M row(s) skipped.' — and the panel does
  not re-validate the file it just imported. Import is disabled, because the file's rows are now in
  the drawing, and the summary says so rather than presenting it as a failure: a completed import
  never renders as an error colour and never shows the 'Cannot import' wording. The outcome stands
  until the user changes the path, the column order, or the header setting."*
- Owning subsystem: **IO** (`SurveyCsv.cpp` owns the summary strings and the panel's validation
  state), with the colour rule in **UI** and one new plain-data field in `AppCommandState`.

## 2. Scope
- In scope: the panel state after `SurveyCsvImportFile` returns; the colour rule for that state;
  the two UI call sites that also marked the preview dirty.
- Out of scope: the pre-import validation itself. Every REQ-041 message, count and block rule is
  untouched — the bug was never that the validation was wrong, it was that it ran at a moment when
  its answer described a different question.
- Out of scope: closing the window on success (considered, rejected by the user — it would hide the
  skipped-row count the user had just agreed to in the confirm prompt).
- Smallest change: stop marking the preview dirty on completion; write the outcome instead.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] **Yes, once — escalated.** The behaviour was **unspecified**. REQ-041 enumerates the
      pre-import states exhaustively and says nothing about the state after Import is pressed, so
      the code was not violating the requirement — it was filling a silence. Per the bug workflow
      that is a SPEC GAP, not a silent fix: REQ-041 revision 3 was written and recorded as
      **D-2026-08-17-d** before the fix was made.
    - [ ] Otherwise: **No.** `surveyImportJustImported` is a plain `bool` in the existing
      `AppCommandState` aggregate — not global state (it is passed, not summoned), not an
      abstraction, not a public API or data-format change. `ImportOutcomeSummary` is a free
      function in the header that already exists for exactly this purpose.

## 4. Questions  (workflow.md §5)
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | After a successful import, should the panel report the outcome and stop re-validating, close the window, or keep re-validating with softer wording? | 2026-08-17 | **Report the outcome and stop re-validating.** Import disabled, outcome stands until the user changes something. Recorded as D-2026-08-17-d. |

## 5. Assumptions
```
ASSUMPTION-1: A partially-successful import (some rows skipped) is still shown in the success
              colour, with the skip count in the text.
- Because:       REQ-041 rev 3 says a completed import never renders as an error colour, and does
                 not carve out the partial case.
- Risk if wrong: a user skims the colour and misses that rows were dropped.
- Validate by:   it is bounded — the skip count is on the first line, and the user has already
                 seen and confirmed the exact same count in the confirm-skip prompt seconds
                 earlier, which is the moment REQ-041 chose for that decision.
```

## 6. Root cause  (workflow.md, bug flow — evidence BEFORE fix)
- **Mechanism.** `SurveyCsvImportFile` ended with `st.surveyImportPreviewDirty = true`
  (`SurveyCsv.cpp:587`), and both UI call sites set it again
  (`CadUi_ImportExportPoints.cpp:85` and `:101`). On the next frame `DrawImportPointsPanel` saw the
  dirty flag and called `SurveyCsvRefreshImportPreview`, which re-scanned **the same file** against
  `st.surveyPoints` — a session that now contained the points the import had just created. Every
  row therefore collided with itself through `ScanDuplicateIds`, `okRows` fell to 0, and the
  file-level branch printed "Cannot import — no valid data rows" in the error colour with a
  duplicate-ID line per row.
- **Evidence.**
  1. The user's two screenshots: before Import, "Ready to import — 5 point(s)" in green; after
     Import, the five points are correctly in the drawing (IDs 1–5 with IP/CP/FH/NO ELEVATION
     labels) **and** the summary reads "Cannot import — no valid data rows … Point ID 1 already
     exists in the drawing (line 2)". Both states from one file, one after the other.
  2. The path, read end to end: `SurveyCsv.cpp:587` → `CadUi_ImportExportPoints.cpp:49` →
     `SurveyCsvRefreshImportPreview` → `sessionIds` built from `st.surveyPoints` (`:468–473`) →
     `ScanDuplicateIds` → `okRows <= 0` → `surveyImportFileBlocked = true` and the "Cannot import"
     string (`:488–491`).
  3. Reproduces with `.csv` and `.txt` alike, as reported — consistent with the cause, which is
     downstream of parsing and indifferent to the extension.
- **Not a regression from REQ-083.** The dirty-on-completion line predates it; the defect is
  present in 0.5.1 and every release with this panel. REQ-083 only made it visible on a second file
  type.
- **Regression risks.** (a) The pre-import states must keep working — the fix must not leave the
  panel permanently stuck in the outcome state. Protected by clearing the flag in
  `SurveyCsvRefreshImportPreview`, the one function every user change routes through. (b) The
  Import button must not stay enabled and allow a pointless second import that skips every row.
  Protected by keeping `surveyImportFileBlocked = true`.

## 7. Implementation log
- 2026-08-17 — root cause identified by reading the path; escalated the SPEC GAP; REQ-041 rev 3 and
  D-2026-08-17-d recorded **before** any code change.
- 2026-08-17 — `survey_csv::ImportOutcomeSummary` added to `io/SurveyCsvValidate.hpp` (pure, so the
  Catch2 target can hold the wording); regression case added to `SurveyCsvValidateTests.cpp`.
- 2026-08-17 — `SurveyCsvImportFile` no longer marks the preview dirty; it writes the outcome,
  blocks Import, zeroes the row counts and sets `surveyImportJustImported`.
  `SurveyCsvRefreshImportPreview` clears that flag — the single clear site.
- 2026-08-17 — UI: the two call sites stop setting the dirty flag (the importer owns the panel
  state afterwards), and the colour rule treats a completed import as success rather than as a
  block.
- **Honest note on ordering:** the regression test was written before the fix but after
  `ImportOutcomeSummary` existed, so it was never observed failing against the *shipped* code — the
  function it asserts on is new. What it genuinely pins is the wording contract, including the
  three strings a completed import may never contain. The state-machine half of the fix (dirty
  stays false; the flag clears on the next user change) is **not** covered by any automated test
  and rests on the GUI repro.

## 8. Self-verification
- [x] **build-project — PASS.** All targets, MSVC. No new warnings.
- [x] **architecture-review — PASS.** No layer crossed: IO already owned these strings and this
  state. No new global (a field in an existing passed-by-reference aggregate), no abstraction, no
  dependency, no `gl*`, no rejected approach revived.
- [x] **code-review — PASS.** Fix addresses the cause (the re-validation) rather than the symptom
  (the red text) — nothing is suppressed, no error swallowed, no test disabled, no message
  softened while the wrong scan still runs. Control flow unchanged elsewhere.
- [x] **testing — PASS.** `[surveycsv]` 6 cases / 41 assertions; full `ctest` **411/411**.

## 9. Verification result
- Submitted:  2026-08-17
- Verdict:    **PASS on the automatable half.** The acceptance condition is worded about a panel, so
  the confirming evidence is the GUI: re-run the exact repro from the screenshots and check that the
  summary reads "Imported 5 point(s) — 0 row(s) skipped." in the success colour with Import
  disabled, then change the column order and confirm normal validation resumes.
- Findings:   none blocking.
- Technical debt: none added. DEBT-REQ083-1 (overwrite prompt) is unrelated and still open.

## 10. Outcome
- Requirements satisfied: REQ-041 rev 3 (Acceptance met: pending the GUI re-run above)
- Tests added:            1 case / 15 assertions in `SurveyCsvValidateTests.cpp`
- Docs updated:           `spec/requirements.md` (REQ-041 rev 3), `spec/project.md` (D-2026-08-17-d)
- Done:                   pending the GUI re-run
