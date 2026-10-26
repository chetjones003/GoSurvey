# TASK-1107 — Gate releases on tests and prepare 0.7.5

- Type: bug fix / release preparation
- Status: in review
- Opened: 2026-10-26
- Owner: Workshop

## 1. Bug report

- **Title:** The manual release workflow can publish without running CTest.
- **Observed:** `.github/workflows/release.yml` builds the binaries and runs the PDB archive
  validation helper, but contains no `ctest` invocation. The current independent PR validation
  workflow does run tests, but a later manual dispatch of `release.yml` on `master` does not.
- **Expected:** REQ-202 requires a manual release dispatch to build and run the full test suite; a
  failing CTest run must prevent installer publication.
- **Reproduction:** Inspect the `release.yml` build job or dispatch it on `master`; after the build
  step it proceeds to symbol archiving and packaging without running CTest.
- **Evidence:** `rg -n ctest .github/workflows/release.yml` finds no test command. The workflow's
  build job ends the compile stage at `cmake --build --preset ninja-release` before archive and
  package steps.
- **Relevant files:** `.github/workflows/release.yml`, `CMakeLists.txt`, `RELEASE_NOTES.md`.

## 2. Authority

- Requirement: **REQ-202**; 0.7.5 target version already selected by the user.
- Acceptance: every release dispatch tests; publication is blocked by a failing CTest run; the
  installer version, stable tag, manifest version and release-note section use 0.7.5.
- Constraints: workflow remains manual-dispatch only; stable release remains version-gated; do not
  change installer, bootstrap, R2 PDB archive, or update-consent behavior.

## 3. Root cause

The release workflow invokes `cmake --build` but omits the CTest step required by REQ-202. Because
GitHub Actions stops the job on a nonzero command, inserting the full CTest preset after the build
and before PDB archiving/package publication provides the specified fail-closed gate.

The project version is still 0.7.4 on both `beta` and `master`, and tag `v0.7.4` exists. Dispatching
the stable workflow without a version bump would intentionally take the version-gate no-op path.
The 0.7.5 release-note section is already present, and the user selected 0.7.5 for this release.

## 4. Scope and plan

- Add `ctest --preset ninja-release --output-on-failure` after building all targets and before
  archiving symbols or packaging. This causes any test failure to stop the release job before
  publication.
- Bump the single authoritative `project(VERSION ...)` in `CMakeLists.txt` to 0.7.5. Do not edit
  generated version files or hard-code a version in the workflow or installer.
- Keep the release workflow manual-only and version-gated. After the preparation PR lands in beta,
  open the requested `beta` → `master` release PR. A successful master merge still requires an
  intentional manual dispatch of `release.yml` on `master`.
- No installer/bootstrap/PDB implementation changes are in scope; the user confirmed those artifacts
  are acceptable, and this audit confirms the current pipeline builds the EXE, verifies the exact
  PDB identity before private R2 upload, and excludes the PDB from release assets.

## 5. Architectural boundary check

- No new dependency, layer, runtime component, artifact format or external service.
- The version source remains `project(VERSION ...)` under ADR-029.

## 6. Verification plan

- Regression check: verify the release workflow contains a full CTest invocation after the build
  and before PDB archiving/package publication; run it against the pre-fix workflow to establish
  the failing result, then after the change.
- Run `build.bat` and the full `ctest --preset ninja-release --output-on-failure` suite.
- Exercise the release-note extraction for version 0.7.5 and verify the migration text reaches the
  generated manifest note.
- Verify the master version gate targets the absent `v0.7.5` tag and the workflow keeps
  `workflow_dispatch` as its only trigger.
- Pull-request CI must pass before merging this preparation into beta.

## 7. Product documentation review

- README, PRIVACY.md, in-app wiki and public wiki were reviewed for TASK-1106. No release-workflow
  documentation changes are required for this build/test gate and version bump.

## 8. Implementation log

- 2026-10-26: Audited `release.yml` against REQ-202. Confirmed no CTest command exists in the
  manual release pipeline. Confirmed both `beta` and `master` report version 0.7.4 and stable tag
  `v0.7.4` exists; `v0.7.5` does not.
- 2026-10-26: The workflow-contract regression check failed before the fix because no full CTest
  command existed. Added the full Ninja release CTest preset before PDB archiving and packaging,
  and bumped `project(VERSION ...)` to the user-selected 0.7.5.
- 2026-10-26: Post-fix workflow contract check passed: CTest order is after build and before PDB
  archiving/package; manual-only dispatch remains; the 0.7.5 version and release-note parser check
  passed. `tools/test-archive-release-pdb.ps1` passed. `build.bat` passed. The full
  `ctest --preset ninja-release --output-on-failure` run passed all 191 tests in 542.58 seconds.
- 2026-10-26: Pre-PR product documentation review — README already links to the correct install
  guide; PRIVACY.md needs no change because version bump/release testing changes no data handling;
  in-app and public wiki already describe the 0.7.5 install path and migration from TASK-1106.
  `docs/install.md` and the public release PDB guide match the release workflow. No additional doc
  edits are needed for this release-preparation PR.
- 2026-10-26: Opened [PR #1103](https://github.com/chetjones003/GoSurvey/pull/1103) to `beta` with
  `bug` and `needs review` labels. Required CI checks are running.

## 9. Self-verification

- [x] build-project — `build.bat` passed.
- [x] architecture-review — reuses the existing CMake/CTest release preset and the existing single
  CMake version source; no runtime architecture changed.
- [x] code-review — release dispatch remains manual-only and version-gated; full CTest now occurs
  before R2 symbol archiving and package/publish steps; no installer/PDB paths or publication logic
  changed.
- [x] dependency-audit — n/a; no dependencies changed.
- [x] performance-review — n/a; the added work is required release-only testing, not application
  runtime work.
- [x] testing — all 191 CTest tests and PDB archive validation passed; workflow/version/note
  contract check passed.

## 10. Outcome

- Requirements satisfied: REQ-202's build/test-before-publication gate and user-selected 0.7.5
  version source; CI still needs to validate the PR.
- Tests: all 191 passed locally; PDB archive validation passed.
- Release-preparation PR: #1103 open; required CI checks pending.
- Beta-to-master release PR: pending until 0.7.5 preparation lands in beta.
