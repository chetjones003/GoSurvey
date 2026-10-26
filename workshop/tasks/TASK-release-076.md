# TASK-release-076 — Prepare GoSurvey 0.7.6

- Type: release preparation / product documentation
- Status: in progress
- Opened: 2026-10-26
- User-selected version: 0.7.6

## 1. Authority

- REQ-202: release version is sourced from `project(VERSION ...)`; stable publishing is manual and
  version-gated.
- REQ-078: update offers show release notes from the manifest.
- REQ-336: the installed-version What's New billboard is sourced from `resources/whats-new.md`.
- User direction: include merged PR #1111 in the next beta-to-master promotion and update What's New.

## 2. Scope and plan

- Bump the authoritative CMake version from 0.7.5 to the user-selected 0.7.6.
- Add concise user-facing 0.7.6 notes for native paper-space viewport round-tripping in DWG/DXF and
  the PowerShell stable-installer fix.
- Bring the in-app What's New content current through 0.7.6, including the 0.7.5 install migration
  and the changes since the existing 0.7.2 section.
- Open a focused PR against `beta`; after it merges, prepare the separate `beta` to `master`
  release-promotion PR. The master merge will not publish on its own; `release.yml` is
  `workflow_dispatch` only.

## 3. Files and boundaries

- `CMakeLists.txt`: the single version source (ADR-029).
- `RELEASE_NOTES.md`: notes consumed by update offers and the release workflow.
- `resources/whats-new.md`: bundled What's New content. REQ-336 requires the user to review and
  explicitly approve this file before it is committed or pushed.
- This task record.

No application behavior, release workflow, installer, PDB, or data-handling changes are planned.

## 4. Product documentation review (REQ-453)

- README: reviewed; no update needed for these release notes/version metadata.
- PRIVACY.md: reviewed; no change to data handling is part of this release-preparation task.
- In-app manual (`resources/wiki/`): reviewed; the paper-space and exchange pages were updated for
  PR #1111. The obsolete DWG multi-layout export limitation was removed from Import and Export,
  Known Limitations, and Troubleshooting.
- Public manual wiki: reviewed; paper-space and import/export pages were updated for PR #1111.
  Stale DXF paper-space import statements and the obsolete DWG multi-layout export limitation are
  being corrected before the promotion PR.
- In-app What's New billboard: update through 0.7.6; obtain the review approval required by REQ-336
  before committing or pushing the content.

## 5. Verification plan

- Check the version remains 0.7.6 in CMake and the release workflow continues to read that one
  source, with manual dispatch and the existing stable tag gate unchanged.
- Check that the release-note extractor finds the 0.7.6 section and that the What's New content
  has a matching 0.7.6 heading.
- Require the focused PR's full build/test, clang-tidy, MSVC analysis, and security checks before
  merging into `beta`.
- Check the beta-to-master comparison includes PR #1111 and the 0.7.6 version bump before opening
  the release-promotion PR.

## 6. Plan verification

The user selected 0.7.6, so no version decision remains. The version bump stays in a focused branch
and follows the repo's branch-to-beta then beta-to-master flow. The copy uses already approved
product behavior from REQ-113/REQ-078 and PR #1111; the required REQ-336 review gate remains in
force for the bundled What's New file.

- Public wiki paper-space corrections published to the GoSurvey wiki: commit 50b31d7 (DXF paper-space import, removed obsolete DWG multi-layout limitation, and troubleshooting).
