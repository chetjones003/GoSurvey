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
