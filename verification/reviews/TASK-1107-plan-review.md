# TASK-1107 Plan Review — 0.7.5 release gate

**Review type:** Self-review against the verification layer checklist because delegated review is
disabled in this run.

## Finding

**FAIL — workflow does not run CTest.** `spec/requirements.md` REQ-202 requires the manually
dispatched release workflow to build and run the test suite, and says a failing CTest run must
block publication. `.github/workflows/release.yml` contains `cmake --build --preset ninja-release`
but no `ctest` command before it proceeds to private PDB archiving and package publication. This is
the concrete defect TASK-1107 addresses.

## Plan assessment

- **Specification:** The plan adds the exact missing CTest gate required by REQ-202. It does not
  weaken the manual-only trigger or version gate.
- **Version source:** A 0.7.5 bump in `CMakeLists.txt` is necessary because the version source is
  `project(VERSION ...)`, the user selected 0.7.5, and tag `v0.7.4` already exists. The workflow
  would otherwise deliberately skip stable publication.
- **Architecture and scope:** The plan reuses existing CMake/CTest presets and the existing version
  source. No product/runtime architecture or dependency changes.
- **Risks and verification:** Full CTest runs before PDB upload and packaging, so failures stop the
  job before external publication. Existing PR validation remains the review gate for the beta
  preparation and beta-to-master promotion.

**Plan verdict: PASS WITH FIX REQUIRED.** Implement only the CTest gate and the user-selected
0.7.5 version bump. Do not dispatch the stable workflow as part of the preparation PR.

## Follow-up review — hosted release test environment

**Finding: FAIL — CTest prerequisites differ between release and full validation.** Release run
40261704780 built successfully but failed 7/191 tests. The Qt shards found OpenGL 3.0, below the
application's OpenGL 3.3 requirement, and the clean deployment smoke test exited 2. Full validation
passed the same revision with a pinned Mesa runtime, `QT_OPENGL=desktop`,
`GALLIUM_DRIVER=llvmpipe`, and eight CTest workers. The release gate must reproduce those test
prerequisites while ensuring the temporary Mesa DLLs are restored or removed before packaging.

**Updated plan verdict: PASS WITH FIX REQUIRED.** Reuse the pinned, checksummed Mesa setup and
runtime staging from full validation, apply the same Qt environment and CTest parallelism, and
restore test-only DLLs even on failure. Keep CTest before all PDB and installer publication steps.
Retain CTest's failure as a hard stop; do not skip or weaken any test.

## Second follow-up — optional Mesa DLLs

Release run 40270411644 built successfully but stopped at Mesa staging because the pinned package
does not contain `libglapi.dll`. The full-validation workflow intentionally stages optional
dependencies only when present. **Plan verdict: PASS WITH FIX REQUIRED.** Require `opengl32.dll`,
back up existing destination files before examining source availability, copy other Mesa DLLs only
when present, and retain `finally` cleanup so original Qt files are restored.
