# TASK-issue-1025 — Qt LGPL attribution and installer licence notices

- Type:    docs
- Status:  implement
- Opened:  2026-11-08
- Owner:   Claude (implement-issue flow, GitHub #1025)

## 1. Authority
- Goal:         GOAL-NN from spec/project.md (distribution and licensing of the installer)
- Requirements: REQ-520 (accepted 2026-11-08, D-2026-11-08-j); context ADR-041(e), ADR-029, REQ-440
- Constraints:  none new. The product is GPL-3.0-or-later (ADR-041(e)).
- Acceptance:   REQ-520 items 1 to 7, verbatim in spec/requirements.md
- Owning subsystem: installer/ (packaging). No application code changes.

## 2. Scope
- In scope:        installer/License.txt; installer/licenses/ (new licence texts); installer/GoSurvey.iss
                   ([Files] entry for the licences folder); workshop task log.
- Out of scope:    vendored libraries not named today (OpenXLSX, libe57format, xerces-c, laszip, miniz, nlohmann,
                   pugixml, md4c, stb_image): follow-up, recorded in D-2026-11-08-j. Code changes. Changing the
                   Qt version.
- Smallest change: correct the notice, add the licence texts the notice refers to, and install them.

## 3. Architectural boundary check
- [x] No — proceed. Packaging content only; no new dependency, no code path.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Remove or keep the historical MIT grant? | 2026-11-08 | Remove (recommended) |
| Q2 | How do the licence texts reach the user? | 2026-11-08 | Install a licenses folder (recommended) |
| Q3 | Accept REQ-520 as drafted? | 2026-11-08 | Accept as drafted (recommended) |

## 5. Assumptions
ASSUMPTION-1: The Qt version shipped is the kit `build.bat` uses by default (6.8.3, `C:\Qt\6.8.3\msvc2022_64`).
- Because:       REQ-520 says the notice names the shipped version.
- Risk if wrong: the notice names the wrong version.
- Validate by:   reading the version from the kit used for the built payload.

ASSUMPTION-2: The payload's Qt component list is the one on the release build (Qt6Core, Qt6Gui, Qt6Network,
Qt6OpenGL, Qt6OpenGLWidgets, Qt6Svg, Qt6Widgets; plugins generic, iconengines, imageformats, networkinformation,
platforms, styles, tls).
- Because:       read from `build/` on 2026-11-08 (release build, old base; the list does not depend on the base).
- Risk if wrong: a file is missing from the notice.
- Validate by:   re-listing `build/` and the built installer's payload before the PR.

## 6. Plan
- Approach: rewrite installer/License.txt (GoSurvey licence, Qt section, Qt-bundled components, other shipped DLLs,
  stale notices removed); add installer/licenses/ with GPL-3.0 and LGPL-3.0 texts and the full texts of the
  permissive licences the notice names; add a [Files] entry in GoSurvey.iss.
- Files/functions to touch: installer/License.txt, installer/GoSurvey.iss, installer/licenses/* (new).
- Test approach: happy path = built installer payload contains every Qt file and the licences folder, and every
  file in the payload is named in License.txt; failure mode = a file present in the payload but not named (checked
  by listing both sides).
- Steps:
  - [x] spec: D-2026-11-08-j and REQ-520
  - [ ] collect licence texts from primary sources
  - [ ] write installer/License.txt
  - [ ] add installer/licenses/ and the [Files] entry
  - [ ] verify the payload against the notice
  - [ ] build the installer if Inno Setup is available; otherwise record that it is not

## 7. Workflow-specific notes
- Docs: each statement cross-checked against the Qt SBOM (`C:\Qt\6.8.3\msvc2022_64\sbom`) and the shipped files.

## 8. Implementation log
- 2026-11-08 REQ-520 and D-2026-11-08-j recorded (user accepted the drafted REQ; chose MIT removal and licences folder).
- 2026-11-08 Release build on the pre-rebase base finished (exit 0). Payload listed; Mesa 11.2.2 identified in
  opengl32sw.dll; dxcompiler.dll is LLVM-based.

## 9. Self-verification
- [x] build-project        — PASS: `.\build.bat` (Release) on the rebased branch, exit 0
- [x] architecture-review  — n-a (packaging only, no code path)
- [ ] code-review          — pending council review of the PR
- [x] dependency-audit     — n-a (no dependency change; Qt 6.8.3 unchanged)
- [x] performance-review   — n-a
- [x] testing              — PASS: `installer/GoSurvey.iss` compiled with Inno Setup 6.7.3 (GoSurvey-0.7.8-Installer.exe);
      compile log lists 21 licence files, 11 qt-plugins files and every Qt DLL packed; all 24 shipped DLL/plugin
      files are named in License.txt (script check); every licence file the notice names exists in installer/licenses/.
      The installer was NOT executed on the dev machine (shared AppId with the user's real install; would touch
      registration and shortcuts). Install-time rendering of the UTF-8 characters in License.txt is not verified.

## 10. Verification result
- Submitted: pending PR
- Acceptance: REQ-520 items 1-7 checked against the notice, the compiled installer's file list, and the spec.
