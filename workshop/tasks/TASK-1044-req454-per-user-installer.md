# TASK-1044 — One-command per-user GoSurvey installer

- Type: feature
- Status: verify
- Opened: 2026-10-24
- Owner: Workshop

## 1. Authority

- Goal: M-Distribution (roadmap)
- Requirements: **REQ-454**; amended install/update behavior in **REQ-078** (accepted)
- Constraints: **ADR-029**, **ADR-072**, **ADR-073**, REQ-201 (visible failures), REQ-300 (no new dependency),
  REQ-301 (no speculative abstraction)
- Acceptance: as stated in REQ-454; REQ-078 retains explicit user choice and now requires the
  per-user install path after migration.
- Owning subsystem: `scripts/` bootstrap, `installer/GoSurvey.iss`, `src/update/UpdateService.cpp`,
  release documentation, and release manifest consumption. No CAD/domain changes.

## 2. Scope

- In scope: stable-release PowerShell bootstrap; manifest and SHA-256 validation before execution;
  silent per-user installation at `%LOCALAPPDATA%\Programs\GoSurvey`; one-time UAC migration of
  legacy Program Files installs while preserving settings/data and supported shortcuts; per-user
  in-app update launch; app-local VC runtime DLL staging/servicing for bootstrap/updater releases;
  one-line install docs and approved ASCII output preview.
- Out of scope: Authenticode signing or suppressing Windows security warnings; beta installs;
  restoring the retired `.gs` association; changing the explicit in-app update consent; non-Windows
  installers; delta updates or rollback.
- Smallest change: keep the published Inno EXE and `latest.json` as release artifacts, stage the
  licensed toolchain VC runtime files beside the app, let bootstrap/updater skip shared redist while
  retaining it for Store installs, change installer/updater to use one per-user path, add a PowerShell
  bootstrap that validates the stable manifest and installer, and document its command/banner.

## 3. Architectural boundary check

- Does this need a new abstraction/layer/dependency, ownership change, or public behavior change?
  - [x] Yes — migration and installation scope are architectural changes. They are authorized by
  accepted **ADR-072** and **ADR-073**; Workshop must implement those decisions and may not redefine
  migration or runtime deployment policy.
- Reasoning: bootstrap remains a script over the existing stable manifest, installer, and SHA-256
  contract. No third-party runtime dependency or updater binary is planned. Installer/updater path
  changes and app-local runtime skip behavior must remain consistent with the ADRs and existing Inno
  handoff. The Store path retains its shared redistributable.

## 4. Questions

| # | Question | Asked | Answer |
|---|---|---|---|
| Q1 | Stable release or rolling beta? | 2026-10-24 | Stable release. |
| Q2 | Verify published SHA-256 before install? | 2026-10-24 | Yes; abort clearly on missing/mismatched data. |
| Q3 | Fresh-install elevation/path policy? | 2026-10-24 | No elevation; per-user `%LOCALAPPDATA%\Programs\GoSurvey`; silent. |
| Q4 | Apply per-user policy to existing installs and in-app updates? | 2026-10-24 | Yes; all installs/updates migrate to per-user. |
| Q5 | One-time UAC for Program Files migration? | 2026-10-24 | Yes; future per-user installs/updates need none. |
| Q6 | Preserve `.gs` association? | 2026-10-24 | No; remove from acceptance because `.gs` is retired. |
| Q7 | Guarantee no Windows security warnings? | 2026-10-24 | No; keep bootstrap silent, OS-controlled warnings outside guarantee. |
| Q8 | Approved banner direction? | 2026-10-24 | Yes; use the approved generated ASCII preview direction. |
| Q9 | Runtime deployment for no-elevation installs? | 2026-10-24 | Use app-local licensed runtime DLLs and service them with each release. |

## 5. Assumptions

None. Stable manifest format and release URLs are already defined by REQ-202 / ADR-029. Migration
must preserve the current user's `%APPDATA%\GoSurvey` data and any supported shortcuts.

## 6. Plan

- Approach: stage and validate the toolchain's licensed app-local VC runtime files in release CI;
  make bootstrap and updater skip shared redist while retaining it for Store installs; inspect and
  retain the existing manifest schema; verify the installer SHA-256 before any
  launch; select the existing Inno silent switch for fresh setup; route only legacy Program Files
  cleanup through one-time UAC; install all files and shortcuts into the current user's profile;
  change the updater to launch the same per-user installer without `runas`; retain update consent.
- Files/functions to touch: `installer/GoSurvey.iss`, `src/update/UpdateService.cpp`,
  `.github/workflows/release.yml`, new `scripts/install.ps1`, `docs/install.md`, `README.md`, and
  this task log.
- Verification plan: review exact Inno switches, elevation behavior, runtime DLL staging and skip
  behavior (including unchanged Store redist path), migration preservation,
  manifest URL/schema, hash error paths, stable-only selection, and shortcut handling; exercise valid
  and invalid manifests/hashes, failed download, clean per-user install, Program Files migration,
  and user-approved update. Confirm the console output against the approved preview. Do not claim
  Windows security-warning suppression.
- Steps:
  - [x] Verify the plan against REQ-454, REQ-078, ADR-029, ADR-072, ADR-073 and the
    installer/updater/release packaging code.
  - [x] Implement bootstrap, per-user installer behavior, migration and updater handoff.
  - [x] Add the one-line docs and approved output preview.
  - [ ] Complete permitted verification and record evidence/limitations.

## 7. Workflow-specific notes

- Feature: pre-flight decisions recorded in D-2026-10-24-c; implementation may begin only after the
  Verification Layer approves this plan. Tests/verification will cover no-execution on any manifest,
  download, or hash failure and migration/update paths.

## 8. Implementation log

- 2026-10-24: Specification accepted and plan opened. Verification approved the plan before any
  implementation code was written.
- 2026-10-24: User selected app-local VC runtime deployment. Verification re-reviewed ADR-073,
  release servicing, bootstrap/updater skip behavior, and unchanged Store behavior before work resumed.
- 2026-10-24: Implementation complete. `build.bat release` passed; the changed updater compiled.
  PowerShell parser validation and `git diff --check` passed. No installer was executed and no tests
  were run; live download/hash failure, migration/UAC, Store, and in-app update behavior remain for
  platform end-to-end verification.

## 9. Self-verification

- Console presentation follow-up: replaced the parcel diagram with a colored survey-map splash
  card, GS logo and wordmark; added live download progress, active-step spinners and a concise
  completion/error summary. Source remains ASCII with glyphs constructed at runtime. Updated
  `docs/install.md` with the presentation and fallback behavior.
- Preview evidence: `build/artifacts/installer-preview/` contains actual Windows PowerShell 5.1
  captures in Windows Terminal and classic conhost, including download, install, success and hash
  failure. The local preview harness uses a generated payload and replaces setup with a short
  sleep; these captures do not demonstrate a real installation or migration. The user approved
  this banner direction before PR preparation. The preview page is `installer-preview.html` in that
  evidence directory.

- [x] build-project
- [ ] architecture-review
- [ ] code-review
- [ ] dependency-audit
- [ ] performance-review
- [ ] testing

## 10. Verification result

- Submitted: 2026-10-24 (plan and revised runtime-plan review)
- Plan verdict: PASS — approved for Workshop implementation; see
  `verification/reviews/TASK-1044-plan-review.md`.
- Implementation verdict: pending platform end-to-end verification. Build, PowerShell parse, and
  whitespace checks passed. Real installation, migration/UAC and release CI packaging were not run.
- Findings: none from the completed static/build checks.

## 11. Outcome

- Requirements satisfied: implementation present; end-to-end acceptance pending
- Tests added: pending
- Refactors: none
- Docs updated: README one-line command and `docs/install.md` install instructions/output preview
- Done: pending platform end-to-end verification
