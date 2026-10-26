# TASK-1103: Archive release PDBs privately (issue #1004)

## Requirement authority

- GitHub issue #1004, decision 2: archive each release build's full PDB in private Cloudflare storage, keyed by version and build ID; never include it in installer/release assets/Pages or download it in the app.
- REQ-425 §8: crash reports carry the commit and PDB GUID/age needed to locate the matching archived symbols.
- GitHub issue #1004 pipeline note: archive the PDB from the exact build that ships, with no rebuild or re-sign after archiving.
- User-approved infrastructure from TASK-1102: use the dedicated private `gosurvey-crash-dumps` R2 bucket; its 30-day lifecycle rule is scoped to `crash-dumps/`.

## Acceptance criteria

- Every beta build published by the release workflow and each newly published stable release archives the full `GoSurvey.pdb` produced by that same build.
- The private object key includes the resolved version and build identity (commit and PDB GUID/age), so the crash report identifies the matching object without overwriting another build.
- The archive uses bucket-scoped R2 S3 credentials and the existing private `gosurvey-crash-dumps` bucket under a separate `symbols/` prefix. The crash-dump lifecycle rule remains limited to `crash-dumps/`.
- Missing credentials, missing/mismatched PDB identity, or upload failure fails the eligible release workflow. Non-release refs and stable runs rejected by the existing version gate do not upload.
- The PDB remains absent from GitHub release assets, the installer, Pages assets, and app/client download behavior.
- Operator documentation explains the required repository secrets and how an authorized operator retrieves a PDB.

## Files / subsystems

- `.github/workflows/release.yml`: archive in the existing build job, gated to beta and newly published stable releases, before artifact staging.
- `tools/archive-release-pdb.ps1` plus focused tests: validate build metadata/CodeView identity and upload with AWS CLI's R2-compatible S3 endpoint.
- `docs/release-pdb-archive.md`: scoped token setup and retrieval.
- `spec/requirements.md`, `spec/project.md`: record the issue's release archive decision as testable REQ-425 acceptance and decision history.
- This task log: test results and REQ-453 documentation review.

## Approach

Read the RSDS CodeView GUID and age embedded in the just-built executable, compare it to the PDB's CodeView identity, and use the report-compatible `<GUID>,<age>` identity in `symbols/<version>/<commit>/<GUID>,<age>.pdb`. Upload the exact build output through AWS CLI to the bucket-scoped R2 S3 API. Do not add the PDB to the `gosurvey-bin` artifact or installer inputs. Avoid changing the existing lifecycle configuration.

## Verification plan

- Script tests: valid matching executable/PDB metadata produces the expected key; malformed, missing, or mismatched identity fails; upload arguments target only the private R2 bucket and preserve the PDB path.
- Workflow review: beta and newly published stable builds archive after build; feature refs and already-released stable versions skip; archive failures fail the eligible workflow; uploaded runtime/package artifact paths still exclude PDBs.
- Check docs for required bucket-scoped Object Read & Write token and retrieval command.
- Product documentation review before PR: README, PRIVACY.md, `resources/wiki/`, and public user manual wiki; record each outcome. This is internal symbol-archive behavior and should not alter user-facing crash disclosure.
- `git diff --check`; run the focused PowerShell tests. No Qt UI changes, so no screenshot review.

## Architectural-boundary check and verdict

The release workflow owns build artifacts and can archive the already-built symbol file without a new runtime layer, application dependency, or shipped-data change. Existing R2 S3 compatibility and AWS CLI are sufficient; the credentials are scoped to the existing private bucket. The issue fixes the archive's privacy, keying, and exact-build requirements; no further product choice is required for this slice.

**Verification: APPROVE.** The implementation is limited to the release pipeline, its validation helper/tests, operator documentation, and recording the already accepted issue decision in the SPEC.

## Product documentation review

- `README.md`: reviewed; no update needed for internal release-symbol handling.
- `PRIVACY.md`: reviewed; no update needed because this archives developer symbols, not additional user data. Existing minidump disclosure remains unchanged.
- `resources/wiki/`: reviewed crash/troubleshooting references; no update needed. No PDB or diagnostic disclosure is added to the in-app manuals.
- Public GoSurvey GitHub Wiki: reviewed its troubleshooting page; no update needed. It contains no PDB or automatic-dump disclosure, consistent with D-2026-10-25-b.
- Operator setup and retrieval are documented in `docs/release-pdb-archive.md`.

## Completion report

- **Implementation:** the release workflow tests the archive helper, then archives the full PDB from the same `ninja-release` output before staging `gosurvey-bin`. It runs only for beta and newly publishable stable releases. The executable's CodeView GUID/age must match the PDB summary; the object key includes version, 12-character commit, and that identity.
- **Tests:** `tools/test-archive-release-pdb.ps1` PASS (valid identity/key, malformed or ambiguous CodeView data, missing PDB metadata/input, invalid commit, and mismatched PDB); real local `dumpbin` + `llvm-pdbutil` dry run PASS and produced the expected `symbols/` object key; release workflow YAML parses; `git diff --check` PASS.
- **Build:** no C++ build run; this slice changes the release workflow, PowerShell helper, specifications, and operator documentation only.
- **Architecture/code review:** PASS. No runtime layer, application dependency, new public artifact, or application download path. The PDB stays out of both the `gosurvey-bin` artifact and explicit Inno Setup input paths.
- **Secrets:** `gh secret list` returned no repository secret names. Configure `CLOUDFLARE_ACCOUNT_ID`, `R2_ACCESS_KEY_ID`, and `R2_SECRET_ACCESS_KEY` before dispatching an eligible release. Until then, the guarded release step will fail closed rather than silently publish without the symbol archive.
- **Assumptions:** PDB objects use a separate `symbols/` prefix, untouched by the existing `crash-dumps/` 30-day lifecycle rule. The archive is not given an expiry rule.
- **Technical debt:** the issue's report-window restyle remains the next issue #1004 slice.
- **PR:** update the existing GoSurvey PR #1101 with this slice; status returns to `needs review` after the implementation is pushed.

## Assumptions / risks

- The R2 bucket-scoped token and account ID will be configured as GitHub repository secrets `R2_ACCESS_KEY_ID`, `R2_SECRET_ACCESS_KEY`, and `CLOUDFLARE_ACCOUNT_ID`; no secret is requested or stored in the repository.
- PDB objects have no expiration rule: the existing 30-day rule applies only to `crash-dumps/`, while issue #1004 requires release symbol archives to remain available for later report triage.
- A release without the required credentials will fail before publishing its package, making operator setup a prerequisite for the next release dispatch.
