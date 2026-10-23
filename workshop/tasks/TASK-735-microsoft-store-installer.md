# TASK-735 — Microsoft Store silent installer

## Requirement authority

- **REQ-453** — the Store submission runs the existing installer without installer UI and the
  downloaded installer file contains the runtime needed for offline installation. Direct downloads
  remain interactive, and the in-app update still requires the user's explicit choice under REQ-078.
- **CON-06** — smallest implementation, no new dependency.

## Scope and files

- `installer/GoSurvey.iss` — require the bundled runtime, use Store's very-silent launch mode, and
  run the child redistributable quietly.
- `docs/microsoft-store.md` — record the Store submission command line, artifact URL constraint,
  and validation steps.
- `.github/workflows/release.yml` — validate offline payload inputs and publish Store submission
  metadata with stable releases.
- `spec/requirements.md`, `spec/project.md` — record REQ-453 and the user's decision.

## Implementation approach

Use Inno Setup's existing `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-` command-line options
for Store submission. Do not change the default install mode, the public download, or REQ-078's
explicit user choice. Keep the Store submission pointed at a versioned, immutable installer
release asset, not the rolling beta asset.

## Plan verification

- **Scope:** accepted. This reuses the existing EXE installer and Inno's built-in silent mode;
  it does not change application behavior or introduce a package format.
- **Architecture:** accepted. Installer configuration and distribution documentation only; no
  runtime dependency or application-layer change.
- **Risk:** suppressing message boxes can choose an abort response on a setup error. Keep
  `/NORESTART`; do not force-close applications or hide UAC. The Store allows a UAC prompt.
- **Acceptance evidence:** inspect the script's `[Run]` entries and verify the script can compile;
  document the exact Partner Center install parameters and live-install validation. No Store
  submission or Partner Center account action is part of this task.

## Verification and completion

- Verify the script's existing very-silent compatibility and redistributable arguments.
- Compile the Inno Setup installer if the compiler is available; otherwise report the missing
  prerequisite. Do not run the application build or test suite for this installer-only change.
- Record outcomes and any Store certification work still required.

## Implementation

- Added accepted **REQ-453** and **D-2026-10-23-c**. Clarified REQ-078 so the user's explicit
  choice controls whether an update starts; its installer can then run silently, and Store
  deployment is also permitted without changing the ordinary interactive path.
- Added `docs/microsoft-store.md` with the Store command line, stable immutable URL rule, offline
  installation expectation, and Partner Center validation procedure.
- Updated the release workflow to reject missing application/DLL/resource/runtime inputs and to
  attach a generated `store-submission.txt` (versioned URL, SHA-256, and exact parameters) to each
  stable release. The rolling beta release does not receive Store metadata.
- Required the VC++ redistributable at compile time and removed its optional install guard so an
  incomplete, online-dependent installer cannot be produced.
- Confirmed `skipifsilent` on the post-install launch and `/install /quiet /norestart` for VC++.

## Verification result

- `git diff --check`: pass.
- Release workflow inspection: packaging checks the EXE, PDFium DLL, resources directory, and
  VC++ runtime before invoking ISCC; the stable release attaches generated Store metadata, while
  the beta path does not.
- Static inspection: the installer embeds `GoSurvey.exe`, the complete `resources/` tree,
  `pdfium.dll`, and the required VC++ redistributable; no installer-time download is configured.
- Inno Setup compile and offline install: not run. `ISCC.exe` and `build/vc_redist.x64.exe` are not
  present in this environment. REQ-453's package/runtime acceptance remains pending until the
  Windows validation in `docs/microsoft-store.md` is completed.
- Application tests were not run; this change only affects installer packaging and documentation.
