# Public repository file triage

## Authority and scope

- Authority: the user directed removal of unnecessary public repository files, retaining `docs/install.md`, `docs/installer-exit-codes.md`, `docs/qt-074-parity-ledger.md`, and `docs/qt-074-signoff-packet.md`; also remove `build-qt.bat` and `site/`.
- The user explicitly directed proceeding without adding an accepted product requirement.
- Preserve the Qt parity verifier and its two input documents.

## Implementation

- Removed operator, release, and historical migration documents from `docs/`, the `site/about.html` copy, and the `build-qt.bat` forwarding wrapper.
- Updated active links and instructions in `README.md`, `CLAUDE.md`, `tools/setup-windows-build-runner.ps1`, and `verification/release-checklist.md`.
- Kept the installer guides and Qt parity verification documents.

## Product documentation review

- `README.md`: reviewed; removed references to the deleted runner guide and build wrapper.
- `PRIVACY.md`: reviewed; no update needed.
- `resources/wiki/`: reviewed; no user-manual update needed for this repository cleanup.
- Public manual: the configured GitHub Wiki URL redirects to the repository landing page. The manual linked by `README.md` at `gosurveycad.com/docs/` could not be fetched with the available web tool. No external manual update is needed because the changes do not change product behavior or manual content.

## Verification

- `git diff --check`: PASS.
- Qt parity verifier remains registered in CMake and retains both input files.
- Builds and tests were not run; this change removes documentation and a forwarding batch wrapper.
