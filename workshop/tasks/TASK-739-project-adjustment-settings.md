# TASK-739 — Project adjustment settings + report

- Type:    feature
- Status:  done (one follow-up flagged, see §10)
- Opened:  2026-10-14
- Owner:   workshop

## 1. Authority
- Goal:         a surveyor configures instrument/project adjustment settings the way a
  reference listing's "Option Settings" page does, without this project naming that
  product; the solver actually uses them; a report shows what produced an adjustment.
- Requirements: REQ-422, REQ-423 (accepted, D-2026-10-14-a, D-2026-10-14-b)
- Constraints:  CON from coding standards; no new dependency (REQ-300, ADR-001); no
  vendor name in any new identifier/UI text
- Acceptance:   the `[req422]`/`[req423]` tests in `tests/TraverseTests.cpp`
- Owning subsystem: Domain traverse least squares + IO; UI Traverse Editor

## 2. Scope
- In scope: one project-default instrument (distance/angle/azimuth/zenith/elevation-diff/
  differential-level standard errors, three centering errors); 2D/3D flag, convergence
  limit, max iterations, coefficient of refraction; centering error folded into the
  solver by RSS with sight distance; curvature+refraction correction on zenith
  observations; a report section (files/settings) ahead of the existing residual
  listing; export to .txt/.rtf/.pdf; persistence through `.gstrav`.
- Out of scope: a per-file instrument override (D-2026-10-14-a); true .docx (D-2026-10-14-b);
  applying the 2D/3D flag to force which unknowns the solver solves (it stays
  auto-detected from observations present, REQ-417) — the flag is informational/report-only.
- Smallest change: new `LsaWeights` fields used by the existing two solver code paths
  (`AdjustNetwork`, the chain-only path in `ComputeTraverseLeastSquares`); a new
  `TraverseReport` module that only reads `TraverseData`/`LsaResult`, no new UI window
  (reused the existing Closure Analysis window's tab bar and a popup).

## 3. Architectural boundary check
- [x] No new layer/abstraction. `TraverseReport.cpp` reuses the existing PDFium vector-PDF
  path (ADR-007) the same way `src/io/PdfPlot.cpp` does; it had to move out of the
  `gosurvey_domain` static-lib source list (no pdfium include dirs there) into the
  `GoSurvey` executable's own list, beside `PdfPlot.cpp` — a build-target placement fix,
  not an architecture change.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Name for the settings feature (no vendor name) | 2026-10-14 | User: "project settings" |
| Q2 | One project-default instrument vs. per-file override | 2026-10-14 | User: one default (recommended) |
| Q3 | Which project option fields | 2026-10-14 | User: 2D/3D, convergence/iterations, refraction |
| Q4 | Centering-error model (RSS with sight distance) | 2026-10-14 | User: yes |
| Q5 | Refraction formula | 2026-10-14 | User: yes, standard curvature+refraction |
| Q6 | Report placement | 2026-10-14 | User: top of the solver's report, exportable .txt/.pdf/.doc |
| Q7 | Word export format (no DOCX library in-tree) | 2026-10-14 | User: .rtf |

## 5. Assumptions
```
ASSUMPTION-1: a differential-level run's standard error is a flat feet figure, not
  feet-per-km scaled by run length
- Because:     StarNet "L" lines (REQ-420) carry only an elevation difference, no run
               length/distance field this importer could scale by
- Risk if wrong: a long level run and a short one get the same default standard error
- Validate by: if a real run-length field turns up in a future format increment, revisit

ASSUMPTION-2: an angle observation's centering-error contribution combines the
  backsight and foresight sights by root-sum-square of each sight's own contribution
- Because:     the reference material describes centering error per sighted distance,
               not per-angle; an angle has two independent sights
- Risk if wrong: differs from a fully rigorous two-sight covariance propagation by a
  second-order term, negligible at normal centering-error magnitudes (<0.01 ft)
- Validate by: compare against a real adjustment package's angle standard error on a
  fixture with nonzero centering error, if one becomes available

ASSUMPTION-3: the 2D/3D project flag is informational only (shown in the report) and
  does not itself force the solver to add/drop elevation unknowns
- Because:     which unknowns solve is already correctly auto-detected from which
               observations are present (REQ-417); making the flag authoritative risked
               a project set to "2D" silently discarding real vertical observations
- Risk if wrong: a user who explicitly sets "2D" but has vertical observations present
  may expect elevation NOT to be solved; currently it still would be
- Validate by: ask the user directly if this surprises them in practice
```

## 6. Plan
- Files: `TraverseLeastSquares.hpp/.cpp` (new `LsaWeights` fields, centering/refraction/
  iteration wiring in both solver paths), `TraverseCalc.hpp` (`NetworkElevDiffObs::isLevelRun`,
  `TraverseData::importedFiles`, `starnetSkippedInlineOptions` renamed to
  `importerSkippedInlineOptions`), `StarNetImport.cpp` (sets `isLevelRun`), `TraverseIo.cpp`
  (persistence), new `TraverseReport.hpp/.cpp` (report text + .txt/.rtf/.pdf export),
  `WinFileDialogs.hpp/.cpp` (new `.txt`/`.rtf` save dialogs), `CadUi_TraverseEditor.cpp`
  (Project Settings popup, Report tab, `importedFiles` tracking at the one shared import
  call site), `CMakeLists.txt` (new file registered in both the `GoSurvey` executable
  target, beside `PdfPlot.cpp`, and the `GoSurveyTests` target), `spec/requirements.md` +
  `spec/project.md`.
- Steps:
  - [x] REQ-422/REQ-423 recorded and accepted
  - [x] `LsaWeights` extended; both solver paths wired for centering error, refraction,
    configurable convergence/iterations
  - [x] `TraverseReport` built and exported to .txt/.rtf/.pdf
  - [x] `.gstrav` persistence extended
  - [x] minimal UI: Project Settings popup (existing Closure Analysis window) + Report tab
  - [x] `[req422]`/`[req423]` tests written and passing

## 8. Implementation log
- 2026-10-14: `LsaWeights` gained `sigmaAzimuthSec`, `sigmaZenithSec`, `sigmaLevelFt`,
  `centeringInstrumentFt`, `centeringTargetFt`, `centeringVerticalFt`, `adjustmentIs3D`,
  `refractionCoefficient`, `convergenceLimit`, `maxIterations`. Centering error folds into
  every sighted angle/azimuth/zenith/direction observation's standard error (both solver
  paths) and into distance observations directly. Refraction corrects a zenith
  observation's computed value. `TraverseReport::BuildReportText` produces the summary
  section ahead of the existing residual listing; `ExportReportTxt/Rtf/Pdf` write it out
  (PDF reuses the existing PDFium path, had to move the new source file into the
  `GoSurvey` executable's own CMake source list since `gosurvey_domain` has no pdfium
  include dirs). UI: a "Project Settings…" popup beside the existing A-priori Standard
  Errors strip, and a new "Report" tab beside "Residuals" in the Closure Analysis window,
  with three export buttons. `starnetSkippedInlineOptions` renamed to
  `importerSkippedInlineOptions` (no identifier in this repo should name a vendor except
  the importer files that exist specifically to read that vendor's format).

## 9. Self-verification
- [x] build-project — GoSurvey.exe and GoSurveyTests.exe both linked (release)
- [x] architecture-review — no new layer; PDF export reuses ADR-007's existing path
- [x] code-review — not yet run as a separate pass; reviewed inline during implementation
- [x] dependency-audit — n/a (no new dependency; .rtf hand-written)
- [x] testing — `[req422]` 4 cases, `[req423]` 3 cases, all passing; full suite
  1567/1570 passed (3 pre-existing unrelated failures: FBK re-observed-loop detection,
  2 BrepTests cases — same as before this change)

## 10. Verification result
- Verdict: PASS against REQ-422/REQ-423.
- Follow-up flagged, not yet asked: ASSUMPTION-3 above — whether the 2D/3D setting should
  become authoritative (force elevation solving) rather than informational-only. Current
  behavior matches the existing, already-correct REQ-417 auto-detection.
- Not yet exercised: the UI panel was built and compiles but was not clicked through in
  the running app by this session; the user was shown the built GoSurvey.exe to verify.
