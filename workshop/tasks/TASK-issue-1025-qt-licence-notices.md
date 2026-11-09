# TASK-issue-1025 — Qt LGPL attribution and installer licence notices

- Type:    docs
- Status:  submitted
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
                   pugixml, stb_image): follow-up, recorded in D-2026-11-08-j. Code changes. Changing the
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
  stale notices removed); add installer/licenses/ with the licence texts the notice names; add a [Files] entry in
  GoSurvey.iss and clear the folder on upgrade.
- Files/functions to touch: installer/License.txt, installer/GoSurvey.iss, installer/InfoAfter.txt,
  installer/licenses/* (new), .gitignore.
- Test approach: happy path = payload check (every shipped DLL and plugin named in the notice) and licence-reference
  check (every named licence file exists, every staged file is named), plus an Inno Setup compile whose packed-file
  list is inspected; failure mode = a shipped file not named, or a named licence file missing.
- Steps:
  - [x] spec: D-2026-11-08-j and REQ-520
  - [x] collect licence texts from primary sources (Qt LICENSES folder at v6.8.3; Mesa licence page at tag mesa-11.2.2;
        DXC v1.8.2502; GNU)
  - [x] write installer/License.txt
  - [x] add installer/licenses/ and the [Files] entry
  - [x] verify the payload against the notice
  - [x] build the installer (Inno Setup 6.7.3 installed per-user); not executed (see section 9)

## 7. Workflow-specific notes
- Docs: each statement cross-checked against the Qt SBOM (`C:\Qt\6.8.3\msvc2022_64\sbom`) and the shipped files.

## 8. Implementation log
- 2026-11-08 REQ-520 and D-2026-11-08-j recorded (user accepted the drafted REQ; chose MIT removal and licences folder).
- 2026-11-08 Release build on the rebased branch: exit 0. Installer compiled: GoSurvey-0.7.8-Installer.exe.
- 2026-11-08 PR #1178 opened. Council review (three independent Haiku agents, read-only) found:
  BLOCKING: the Mesa licence file was Apache-2.0 text, not Mesa's; several Qt Gui and Test components missing;
  SHOULD: PDFium is MIT in the vendored tree (BSD was wrong); GLEW author misspelled; InfoAfter.txt listed only
  PDFium and GLEW; Qt6Test.dll shipped although tools/verify-qt-deployment.ps1 excludes it; licence folder not
  cleared on upgrade; a SipHash line said GoSurvey "uses" CC0; libpsl holder missing; FreeType attributions off.
- 2026-11-08 Fixes: Mesa-LICENSE.txt replaced with Mesa's licence page (tag mesa-11.2.2, from the gitlab mirror);
  PDFium-LICENSE.txt and GLEW-LICENSE.txt added from the vendored trees; X11.txt, Imlib2.txt and MIT-open-group.txt
  added from Qt's LICENSES; notice corrected from the SBOM for the missing Gui components, the Test component and
  libpsl; GoSurvey.iss excludes Qt6Test.dll and clears {app}\licenses on upgrade; InfoAfter.txt third-party block
  now lists Qt, PDFium and GLEW.
- 2026-11-08 Follow-up for the vendored libraries not yet named: issue #1180.
- 2026-11-08 Council pass 2 (three fresh Haiku agents, read-only) on 9cae444b: no blocking findings; confirmed the
  pass-1 fixes. Fixed in this round: PDFium library licence text (BSD-3-Clause with its copyright line) added as
  PDFium-BSD-3-Clause.txt; LibreDWG pin corrected to tag 0.14 (the vendored base); GLEW notice lists the Brian Paul
  and Khronos lines its licence file carries; Open Group line added for the PCF driver's MIT-open-group; smooth-scaling
  and xserverhelper holders completed from the SBOM; md4c removed from the unnamed list (it is named).
  The full notice is installed as licenses\NOTICE.txt and the post-install block lists LibreDWG, Qt, PDFium and GLEW.
  innoextract cannot read Inno Setup 6.7; installing the setup on this machine is unsafe (live GoSurvey install).

## 9. Self-verification
- [x] build-project        — PASS: `.\build.bat` (Release) on the rebased branch, exit 0 (before the notice fixes;
      those files are not compiled into the application)
- [x] architecture-review  — n-a (packaging only, no code path)
- [ ] code-review          — council pass 1 (three Haiku agents) and pass 2 done; findings fixed as recorded in
      section 8; the pass-3 confirmation is recorded in section 8 when it runs
- [x] dependency-audit     — n-a (no dependency change; Qt 6.8.3 unchanged)
- [x] performance-review   — n-a
- [x] testing              — PASS (packaging): Inno Setup compile of installer/GoSurvey.iss after the latest fixes.
      The compile record (build/artifacts/issue-1025/iscc-packed-files.log) lists 27 licence files, the NOTICE copy,
      11 qt-plugins files, 7 Qt DLLs and 5 other DLLs, and no Qt6Test.dll. Payload check: 23 of 23 shipped files named.
      Licence-reference check: every named licence file exists; every staged file is named.
      NOT done: the installer was not run and its installed files were not listed. This machine has a live GoSurvey
      install (per-user registration, folder, Start Menu shortcut, %APPDATA%\GoSurvey and a running process) that
      shares the installer's AppId, so running the setup here would touch it. innoextract 1.9 cannot read Inno Setup
      6.7's loader (revision 2).
      Not verified: how the UTF-8 characters in License.txt display on the installer's licence page (Inno documents
      UTF-8 with or without a BOM as accepted).

## 10. Verification result
- Submitted: PR #1178.
- Acceptance, by REQ-520's own numbering (spec/requirements.md):
  1. Qt is named (name, LGPL-3.0, version 6.8.3, modules, source location): MET.
  2. Every shipped Qt DLL and plugin is covered by a notice: MET for the 23 files in the local build payload.
  3. Other shipped DLLs (Mesa, DXC, Microsoft runtimes) are covered: MET.
  4. One licence statement for GoSurvey (GPL-3.0-or-later; MIT grant removed): MET.
  5. Stale notices removed (GLFW, Dear ImGui): MET.
  6. The licences folder exists and its texts are shipped: MET (27 files, packed by GoSurvey.iss).
  7. The built installer contains the files: MET AT COMPILE TIME (compile record), NOT on an installed machine.
     The item says "verified on the built installer"; the installed files were not listed (see section 9).
- Issue #1025's own criterion 5 ("a built installer is inspected to confirm the files are present") is met by the
  compile record, which is the compiler's list of every file packed into the installer. The reviewers asked for a
  stronger check; that needs a machine without a live GoSurvey install, or innoextract with an Inno-6.7-capable
  build.
- Follow-up: #1180 (vendored libraries not named; treat as a release gate for the notice's completeness).
