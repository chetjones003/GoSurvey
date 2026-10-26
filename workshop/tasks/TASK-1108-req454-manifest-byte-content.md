# TASK-1108 — Parse byte-backed stable release manifests

- Type: bug
- Status: submitted
- Opened: 2026-10-26
- Owner: Workshop

## 1. Authority
- Goal: M-Distribution
- Requirements: **REQ-454** (accepted); **REQ-201** (accepted)
- Constraints: **REQ-300** (no new dependency), **REQ-301** (minimal abstractions), **CON-06** (smallest correct change)
- Acceptance:
  - The documented one-line PowerShell command installs the latest stable release, never the beta channel.
  - The installer runs only after the release manifest and SHA-256 are present and valid; a failed download, missing or invalid manifest data, or hash mismatch reports a clear error and does not run the installer.
- Owning subsystem: Build/Platform — `scripts/install.ps1` owns the bootstrap and stable-manifest consumption.

## 2. Scope
- In scope: correctly parse a stable JSON manifest returned as byte content by PowerShell 7's `Invoke-WebRequest`; add a deterministic regression test and run it in Windows PR validation.
- Out of scope: changing the stable-channel policy, release URLs, SHA-256 validation, installer behavior, or the published manifest format.
- Smallest change: normalize `Invoke-WebRequest.Content` from UTF-8 bytes to text before the existing JSON parser, while preserving compatibility with string content.

## 3. Architectural boundary check
- Does this need a new abstraction / layer / dependency / ownership change / global state / public API or data format change / algorithm the spec did not specify?
  - [x] No — one private parsing helper in the existing bootstrap handles the two response-content representations; no dependency, format, or policy changes.

## 4. Questions
None. The user confirmed the bug report and asked to proceed.

## 5. Assumptions
None.

## 6. Plan
- Approach: detect byte-array content from `Invoke-WebRequest`, decode it as UTF-8, then parse JSON. Leave all existing stable-channel, required-field, URL, and hash guards intact.
- Files/functions to touch: `scripts/install.ps1`; `tests/powershell/install-manifest-parser.tests.ps1`; `.github/workflows/full-validation.yml`; this task log and its plan review.
- Test approach: happy path — byte-array and string JSON both parse to the expected stable manifest; failure mode — malformed JSON still throws. Run the exact live request/parser path and the deterministic test on Windows.
- Steps:
  - [x] Reproduce the reported error with the exact `Invoke-WebRequest` content-to-JSON path.
  - [x] Identify root cause and verify UTF-8 decoding parses the same response as stable JSON.
  - [x] Verify this plan against REQ-454, subsystem ownership, and the architecture boundary.
  - [x] Add the regression test first and observe it fail against the current script.
  - [x] Normalize response content and route the bootstrap through the tested parser.
  - [x] Run the local regression test, inspect the final diff, and submit the full Windows validation to CI.

## 7. Workflow-specific notes
- Bug: root cause is PowerShell 7.6.9 returning the release asset as `System.Byte[]` (`application/octet-stream`); piping that array into `ConvertFrom-Json` yields `System.Object[]` without manifest properties, so the existing stable-channel guard rejects a valid manifest. A direct UTF-8 decode of the same response parses `version=0.7.5`, `channel=stable`.
- Regression test fails before the fix because the expected private parser helper does not yet exist, then exercises its byte and string inputs after implementation.

## 8. Implementation log
- 2026-10-26: Confirmed report: the user's one-line bootstrap prints “The release manifest is not marked as stable.”
- 2026-10-26: Reproduced on PowerShell 7.6.9. The exact `Invoke-WebRequest` response has content type `application/octet-stream` and `.Content` type `System.Byte[]`; direct piping creates `System.Object[]` with an empty channel. UTF-8 decoding before parsing returns the valid stable v0.7.5 manifest.
- 2026-10-26: Plan review passed; no requirement, policy, manifest format, or architecture change is needed.
- 2026-10-26: Added `ConvertFrom-ReleaseManifestContent` to decode byte-array responses as UTF-8 while retaining string support, and changed the bootstrap to use it. Added a Pester-free PowerShell regression test and wired it into the Windows full-validation workflow.
- 2026-10-26: Regression test failed before implementation because the parser helper was absent. After implementation it passed byte-backed, string-backed, and malformed-JSON cases. The exact live `Invoke-WebRequest` response parsed as `0.7.5 / stable`; `git diff --check` passed.
- 2026-10-26: Pre-PR product documentation review — README reviewed; no change because the one-line install command and stable-release promise remain correct. PRIVACY.md reviewed; no change because manifest parsing does not change collected or transmitted data. `resources/wiki/` Getting Started, Troubleshooting, and Files and Drawings reviewed; no change because their install and update instructions remain accurate. Public GoSurvey Wiki [Getting Started](https://github.com/chetjones003/GoSurvey/wiki/Getting-Started) and [Troubleshooting](https://github.com/chetjones003/GoSurvey/wiki/Troubleshooting) reviewed; no update needed because this fix changes PowerShell's handling of the existing manifest response, not the documented install process.
- 2026-10-26: Opened [PR #1110](https://github.com/chetjones003/GoSurvey/pull/1110) against `beta` with `bug` and `needs review` labels. Full Windows validation and required PR checks are pending.

## 9. Self-verification
- [ ] build-project — PASS (full Windows CI pending)
- [x] architecture-review — PASS (local parsing helper; no layer/dependency/format change)
- [x] code-review — PASS (strict channel and hash checks remain unchanged)
- [x] dependency-audit — n/a (no dependency changes)
- [x] performance-review — n/a (installer bootstrap parsing only)
- [x] testing — PASS (local regression test passes; CI pending)

## 10. Verification result
- Submitted: pending
- Verdict: pending
- Findings: pending

## 11. Outcome
- Requirements satisfied: REQ-454 (Acceptance met: pending)
- Tests added: pending
- Refactors: none
- Docs updated: pending pre-PR documentation review
- Done: pending
