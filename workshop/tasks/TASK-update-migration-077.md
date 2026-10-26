# TASK-update-migration-077 — Correct install migration and idempotent setup

- Type: bug fix / release preparation
- Status: ready for PR to beta
- Opened: 2026-10-26
- User direction: fix the 0.7.4-to-0.7.6 migration; ensure the bootstrap and installer check whether the current version is installed; bring the change through beta, master, and release.
- The user explicitly approved the updated What's New content on 2026-10-26 before commit/push, satisfying REQ-336.
- Release version selected from current stable 0.7.6: 0.7.7.

## Authority and diagnosis

- REQ-454 requires migration from the machine-wide Program Files install to leave one working per-user installation and working shortcuts.
- REQ-078 governs the in-app update handoff; it remains user-approved.
- REQ-336 locks `resources/whats-new.md` until the user explicitly reviews and approves it.
- Observed machine state: HKLM contains the legacy `{02774040-27C9-4A9A-A999-86E641C42F78}_is1` entry named `GoSurvey version 0.7.4`; the per-user 0.7.6 entry and shortcut coexist with a shared Start Menu shortcut targeting `C:\Program Files\GoSurvey\GoSurvey.exe`.
- Root cause: `Get-MachineInstall` required the registry entry's `DisplayName` to equal `GoSurvey`, but historical installers used `GoSurvey version <version>`. The bootstrap therefore skipped the old uninstall and proceeded to install the per-user copy.

## Scope

- Recognize both legacy versioned and current unversioned GoSurvey uninstall display names.
- Verify the old machine-wide registration is gone and remove its leftover shared Start Menu shortcut before reporting migration complete.
- Make the PowerShell bootstrap skip download/install when the per-user version is equal to or newer than the stable manifest version, while still migrating a Program Files install.
- Make direct Inno Setup launches skip replacing an equal or newer per-user or machine-wide install.
- Set CMake's authoritative project version to 0.7.7 and prepare matching release notes and What's New content.

## Product documentation review (REQ-453)

- README: reviewed; no update needed (install command already links to detailed install documentation).
- PRIVACY.md: reviewed; no update needed (no data collection or handling change).
- `resources/wiki/`: Getting Started and install/update guidance reviewed; Getting Started updated to describe the cleanup and no-op behavior. Other pages are unaffected.
- Public manual wiki: Home, Getting Started, Files and Drawings, and Troubleshooting reviewed and updated for 0.7.7; published to the GoSurvey wiki as commit `d30cbae` before opening the PR.
- `docs/install.md`: updated to describe the no-op check and cleanup of the old shared shortcut.
- `resources/whats-new.md`: updated through 0.7.7; user approval received before commit/push.

## Verification and release path

- Regression coverage: versioned 0.7.4 registry detection; equal/newer/older comparisons; pre-download check ordering; migration postconditions; Inno current-version guard.
- Focused PowerShell regression and manifest tests passed.
- Full Windows release build passed; full CTest passed 191/191; hidden-desktop What's New test passed and saved top and 0.7.7 section screenshots; `build.output-layout` passed; `git diff --check` passed.
- Compile/package the Inno installer in CI; local Inno Setup compiler is not installed.
- Public wiki update is published; commit and open the fix PR to `beta`, wait for required checks/reviews, then prepare the separate `beta` → `master` PR.
- Run `.github/workflows/release.yml` manually against `master` after the promotion PR merges; verify the 0.7.7 tag, release assets, and stable manifest.
