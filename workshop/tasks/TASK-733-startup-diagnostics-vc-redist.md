# TASK-733 — Startup failure dialog + VC++ redist in installer

## Authority

User request (2026-10-06): bundle runtime dependencies and show a Windows dialog when the app fails before the main UI.

## Scope

- **In:** Inno Setup ships and runs `VC_redist.x64.exe`; CI downloads it before `ISCC`. `StartupFailure` module shows a scrollable Win32 dialog and appends `%APPDATA%\GoSurvey\startup-failure.log` on GLFW/OpenGL init failure. **Send This Report** copies the report and opens a pre-filled GitHub issue (user signs in on github.com). Wiki troubleshooting updates.
- **Out:** Bundling a software OpenGL stack (Mesa/ANGLE) — OpenGL is supplied by the GPU driver, not redistributable like VC++. Loader failures before `main` (missing `pdfium.dll`) still use Windows loader dialogs.

## Files

- `src/platform/StartupFailure.{hpp,cpp}`, `src/app/main.cpp`, `src/render/ViewportRenderer.{hpp,cpp}`
- `installer/GoSurvey.iss`, `.github/workflows/release.yml`, `tools/fetch-vc-redist.ps1`
- `resources/wiki/Troubleshooting.md`, `Getting-Started.md`

## Test

- Build `GoSurvey` on Windows; simulate failure paths via code review (no automated test for Win32 modal).
- Package job: assert `build\vc_redist.x64.exe` present before ISCC.
