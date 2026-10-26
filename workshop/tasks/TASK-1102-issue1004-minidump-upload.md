# TASK-1102: Add stack-focused crash minidumps to REQ-425 reports

## Authority

- GitHub issue #1004, first decision: write a minidump alongside the crash report and include it in the admin submission.
- REQ-425 §§1–6: preserve the existing report, automatic background send, local logs, and the `PRIVACY.md` disclosure.
- User decisions for this increment: `MiniDumpNormal`; upload each available dump automatically with the text report; delete uploaded dumps after 30 days; cap each dump at 10 MB; if the dump is unavailable or exceeds the cap, still send the text report.

## Plan

1. Extend the crash bundle with an adjacent `.dmp`; generate it through a short-lived helper process so `MiniDumpWriteDump` runs outside the crashing process; use only `MiniDumpNormal` and preserve the crash exception context when available.
2. Pass the dump with the existing crash report in one bounded multipart request. If capture or the size check fails, keep the current text-only POST path. Do not change startup reports.
3. Store dump bytes in private R2 and the opaque object key with the D1 crash report row. Add an Access-protected admin download route and report-table action; never expose the bucket publicly.
4. Configure a 30-day R2 lifecycle rule, document its eventual deletion window, and state the 10 MB per-dump / text-only fallback behavior.
5. Update REQ-425 and `PRIVACY.md`; keep the disclosure out of Settings and troubleshooting manuals; record the required four-source product documentation review.
6. Add focused C++ and Worker tests for successful, absent, oversized, malformed, and unauthorized cases. Build and run the crash-report Qt tests; capture the Settings surface after removing the disclosure and wait for user approval before committing or updating PRs.

## Plan verification

- The client stays within `platform/` for capture/transport, with disclosure only in `PRIVACY.md`; no third-party dependency is added (`dbghelp` is already linked).
- The telemetry Worker remains the anonymous upload endpoint; the admin Worker remains behind Cloudflare Access. R2 is private and only the admin Worker can read objects.
- Failure to create or upload a dump does not prevent the existing text report from being sent (REQ-201 silent network failure behavior).
- The selected limit and expiry are user-approved. The user chose a dedicated private `gosurvey-crash-dumps` bucket after Wrangler showed only the unrelated installer bucket. The bucket was created and the `crash-dumps/` prefix was given an enabled 30-day lifecycle rule; Wrangler confirmed the rule is active.
- No Worker deployment, production crash POST, or live D1 migration is part of this task. The chosen R2 bucket and prefix-scoped lifecycle rule have been provisioned; Worker rollout and the D1 migration remain operator steps after review.

## Files / subsystems

- GoSurvey: `src/platform/CrashReporter.*`, Qt entry point(s), crash reporter tests, Settings screenshot test, `spec/requirements.md`, `spec/project.md`, `PRIVACY.md`, and this task log.
- `gosurveycad-site`: `workers/telemetry/` (upload validation, R2 and D1 storage), `workers/admin/` (Access-protected download UI/route), migrations, Worker tests, and deployment/lifecycle documentation.

## Verification record

- Client capture/upload, Worker multipart storage, Access-protected admin download, the D1 migration, R2 bindings, the privacy disclosure, and operator docs are implemented.
- Product documentation review: README reviewed (no update needed); PRIVACY.md updated; in-app and public troubleshooting manuals reviewed (no update needed per D-2026-10-25-b); telemetry/admin operator docs updated.
- Backend checks: telemetry Worker tests PASS; admin Worker tests PASS.
- Client build: `build.bat` PASS after removing the Settings disclosure; `build.output-layout` PASS. All six Qt editor shards passed before this UI-only follow-up, and the affected Settings shard passed again against the updated binary.
- Worker tests: telemetry Worker PASS; admin Worker PASS.
- UI approval: user approved the refreshed Settings screenshot after the disclosure was removed. The disclosure remains in `PRIVACY.md` only.
- Final manual review: PASS. The requirement preserves existing text crash reporting in both front ends and scopes new minidump capture to Qt; Worker storage/download routes and prefix-scoped retention match the approved decisions.
- Deployment: no Worker deployment, production crash POST, or live D1 migration performed. The D1 migration and Worker rollout are deployment steps after review.
