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
- PR #1103 merged into beta; beta-to-master PR #1104 merged into master after all required checks
  passed. The first manual 0.7.5 release attempt then exposed the test-environment defect recorded
  in §11; no stable tag or release was published.

## 11. Release-run failure follow-up (2026-10-26)

- **Observed:** After PR #1104 merged 0.7.5 to `master`, the manual release run
  [#40261704780](https://github.com/chetjones003/GoSurvey/actions/runs/40261704780) built
  successfully but failed its new CTest gate: 7/191 tests failed. The Qt editor shards could not
  initialize the required OpenGL 3.3 renderer (the hosted runner exposed OpenGL 3.0); shard 2 also
  exited with `0xc0000409`. `qt.deployment` reported that its clean-copy smoke test exited 2.
  Publication and symbol upload were skipped, as required.
- **Expected:** REQ-202's release gate must run the full test suite in an environment capable of
  exercising the same supported application path as PR validation; a genuine test failure must
  still stop publication.
- **Reproduction/evidence:** The Test release log shows OpenGL 3.0 where GoSurvey requires 3.3,
  all six Qt editor shards failing, and the deployment smoke-test exit 2. The full-validation
  workflow on the same source revision passed all required checks after downloading a SHA256-pinned
  Mesa build, staging its runtime dependencies for isolated deployment, setting
  `QT_OPENGL=desktop`/`GALLIUM_DRIVER=llvmpipe`, and running CTest with eight workers. The release
  workflow had none of that setup and ran CTest serially.
- **Root cause:** The release workflow added CTest without carrying over full-validation's required
  hosted OpenGL setup. Its tests therefore exercised a weaker runner graphics driver than the
  validated PR job. The isolated deployment test inherits this environment mismatch. The shard-2
  abnormal exit is observed in the same failing editor run; this change will retest it under the
  known-good pinned driver and matching test concurrency rather than suppressing it.
- **Updated plan:** Reuse full-validation's pinned Mesa version and checksum in release CI; stage
  only its test runtime dependencies before CTest, set the same OpenGL environment, and run CTest
  with eight workers. Save any colliding Qt runtime files and restore/remove staged test files in
  a `finally` path even when CTest fails, so Mesa test DLLs cannot enter the installer. Keep the
  gate fail-closed and before PDB archival/package publication.
- **Regression verification:** Run a workflow contract check confirming matching Mesa URL/hash,
  OpenGL environment, eight-worker CTest, test-only DLL cleanup, and ordering before publication;
  then dispatch the release workflow from the merged master version and require all tests to pass
  before accepting the release artifacts.
- **Product documentation review:** README, PRIVACY.md, in-app wiki and public wiki were reviewed
  for this CI-only correction; no product-facing documentation changes are needed.

### Implementation and verification status

- Updated `.github/workflows/release.yml` to download the same checksum-pinned Mesa build as
  full-validation, stage the Mesa test DLLs that are present, set the validated desktop
  OpenGL/llvmpipe environment,
  and run the complete suite with eight workers. A `finally` block restores pre-existing Qt DLLs
  or removes Mesa-only files before symbol archiving or packaging, even after a failed test run.
- Updated the full-validation workflow header comment to reflect the shared test prerequisites.
- Workflow contract verification passed: PyYAML parsed the workflow; assertions confirmed Mesa
  version/hash, required DLLs, test environment, CTest parallelism, cleanup, and ordering before PDB
  archiving. `git diff --check` passed.
- Product sources are unchanged; the full hosted build/test and release publication check remain
  pending the PR checks and the next intentional stable workflow dispatch after merge.

## 12. Mesa staging follow-up (2026-10-26)

- **Observed:** Release run [#40270411644](https://github.com/chetjones003/GoSurvey/actions/runs/40270411644)
  completed its build but failed before CTest. The staging step required `libglapi.dll`, which is
  absent from the pinned Mesa package's x64 directory. CTest and publication were skipped.
- **Root cause:** The release fix treated all four DLLs as mandatory, while the passing
  full-validation workflow stages each dependency only when the package contains it. The required
  OpenGL entry point is `opengl32.dll`; the other runtime dependencies are optional for this Mesa
  package.
- **Updated plan:** Back up any existing app DLLs first. Require `opengl32.dll`, but stage each
  additional Mesa dependency only when present; leave absent ones alone. The existing `finally`
  block then restores all pre-existing DLLs and removes only Mesa-only files after CTest.
- **Verification:** Re-run the workflow contract check for required-versus-optional staging and
  cleanup, then validate the full Windows release path with a new master dispatch. No release was
  published by run #40270411644.
- Updated release staging so existing destination DLLs are backed up first; `opengl32.dll` is
  required, while the other three Mesa DLLs are copied only when present. The `finally` restore
  logic remains unchanged.
- Workflow contract verification and `git diff --check` pass. The hosted build/test and stable
  release remain pending this correction's PR and a subsequent intentional master dispatch.
