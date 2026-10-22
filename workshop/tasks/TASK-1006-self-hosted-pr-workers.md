# TASK-1006 — Register dedicated Windows workers for PR validation

- Type: feature / CI tooling
- Status: plan
- Opened: 2026-10-21

## Request and authority

The user wants the validated Windows laptop in production, without automatic
benchmark runs, and requests a worker for each PR workflow after learning that
one runner instance only accepts one job at a time. Context supplies the request:
three Windows jobs (build/test, clang-tidy and MSVC), separate worker workspaces,
shared hardware limits, all checks blocking, existing docs-only label policy.
The Ubuntu gate/security jobs use hosted Linux; they cannot run their existing
container actions on this Windows laptop.

Authority: accepted REQ-205 (CI/build performance tooling) and REQ-437 (required
full tests, changed-line clang-tidy, blocking diff-scoped /analyze with fail-closed
fallback, and label-gated docs skip). The user explicitly authorizes registration
and production routing. No new application behavior, dependency or ownership layer.

Acceptance:

- GitHub shows separate online workers for build/test, clang-tidy and MSVC.
- Each Windows job selects its role label and has an independent checkout/tree.
- Installed Qt/native graphics and local compiler caches serve production jobs;
  no timed duplicate build is required. Automatic benchmark push runs stop.
- Owner-authored same-repository PRs use the laptop when the production pool is
  enabled; forks/other PRs use hosted runners. A repository variable can select
  hosted execution. The existing required check names and failure policies remain.
- Concurrent workers share conservative caps: build 3, CTest 6 (4 Qt + 2 Catch
  shards), clang-tidy 2 and MSVC 1. These are initial limits, not a promised speedup.
- Provisioning verifies the official runner archive hash, never copies runner
  credentials, and keeps registration credentials out of logs and child workers.

## Verified implementation plan

Verification APPROVE: changes belong in CI/provisioning tooling. Reuse the current
online runner as build worker by adding a role label. Register two more instances
under the normal user's profile, with independent runner/work directories. The
existing interactive runner can run an owner-only bootstrap workflow using a
short-lived registration token supplied as an encrypted GitHub Actions secret.
The process launch must clear inherited job credentials/context before starting
the persistent listeners; each role launcher holds a mutex to avoid duplicates.

Files: worker registration/launch helpers; production environment/cache helpers;
full-validation.yml; manual-only self-hosted-benchmark.yml; temporary owner-only
bootstrap workflow; Windows runner guide. Enable production routing only after
all three role workers are online. Preserve the current explicit docs-only gate.

Risks: RAM contention on the 15.7 GiB laptop; competing compiler-cache daemons;
runner cleanup terminating newly launched listeners; inherited job credentials;
offline workers queuing checks. Separate cache endpoints/directories for analysis,
bounded cache commands, scoped child environment cleanup, and a hosted override
address these within the existing tooling boundary. The build worker keeps its
already-warm external disk cache. No product UI changes or screenshots required.

Verification scope: inspect script/workflow syntax and registration metadata;
observe actual runner online state and normal required CI results. The user did
not request new test cases or a further benchmark experiment for this rollout.
Do not add or independently run a new test suite for provisioning.

## Implementation / outcomes

Pending.

### Implementation ready for laptop provisioning

Added separate-role registration and mutex-protected interactive launchers. The
bootstrap uses the checksum-pinned official 2.337.0 package and a short-lived
registration secret. Its child listeners clear inherited Actions credentials and
context. Existing registration files are validated instead of copied/replaced.

Production Windows routing uses role labels for owner-authored same-repository
PRs when `GOSURVEY_PR_RUNNERS=self-hosted`; otherwise it selects hosted Windows.
Installed tool setup preserves the build's warm external cache and gives MSVC its
own disk cache/endpoint. Cache control has a 30-second deadline and job-owned
cleanup. Benchmark is manual-only. Documented startup and hosted override.

Actionlint (including new custom labels), PowerShell syntax, diff whitespace,
manual code/ownership/security review: PASS. No new tests added/run; normal CI
and worker tool inspections will provide remote execution evidence. Registration
and production activation are pending online worker confirmation.

### Startup diagnosis after user report

BUG REPORT: registration succeeded but analyzer jobs remain queued. Expected:
registered role workers must stay online before production routing is enabled.
Live GitHub evidence after run 39695342537: build runner online; clang/MSVC
workers registered as IDs 22/23 but offline. `GOSURVEY_PR_RUNNERS` is still hosted,
so the current build/test job is not a laptop performance measurement.

Root cause of the misleading successful provisioning result: the launcher only
reported Start-Process PIDs; it did not verify listener startup. The reason the
new launchers exited is not yet established. Ranked diagnostic hypotheses:
PowerShell startup/path failure; MSVC environment initialization failure; runner
connection/parent cleanup. Capture existing startup logs before relaunch and
require listener processes afterwards. This is targeted instrumentation and a
startup gate, not a speculative environment fix. REQ-205/437 plan APPROVE.

### Connection and shell findings on 61030d2

The diagnostic run captured both child logs at Listening for Jobs; GitHub then
reported both registered workers online. However the CIM snapshot showed zero
child listeners, causing a false failure in the newly added local-process gate.
Store PowerShell is selected on the laptop; the exact cause of its limited CIM
view is not established. Use the role launcher's existing lifetime mutex for
local liveness/idempotence, and let jobs assigned to each role prove GitHub job
connectivity. Do not treat a process list as authoritative connection status.

The same logs show the interactive user profile loading on CI steps, emitting
PSReadLine prediction/invalid-console errors. Windows job defaults and explicit
shells now use -NoProfile, retaining GitHub's pwsh command wrapper and native
exit-code propagation. This removes observed unwanted startup/profile activity;
no speedup is claimed until a production run completes.

Plan/code review APPROVE under the existing task authority: do not alter product
behavior or blocking checks. Keep production routing hosted until role jobs pass.

### Startup lifetime probe after 8a2871c

Run 39697414642 again connects both analyzers, but neither process nor launcher
mutex is visible in the following step, and GitHub reports both offline after
the job. This disproves the earlier conclusion that the local check alone was
false: startup succeeds briefly but does not survive. Root cause remains open.
The next bounded probe records child PID/session/executable, presence (not value)
of runner tracking, launcher finalization and parent Process.HasExited/exit code
while the spawning step remains alive. This distinguishes activation-path exits,
normal launcher exits and termination at parent-step/job cleanup. No provisioning
behavior is changed and no new test suite is added. Diagnostic plan APPROVE under
REQ-205/437; production stays hosted pending actual role-job success.

### Confirmed root cause and fix plan

Debug attempt 2 of run 39697886049 provides the mechanism: at 00:12:37 UTC,
Worker_20261022-001205-utc.log explicitly kills launcher PIDs 45488 and 34176
and their child trees. Both were alive within the spawning step. Runner 2.337.0
ProcessInvoker.ProcessExitedHandler kills a Windows step's descendant tree after
five seconds when output streams remain open. Start-Process's redirection path
inherits handles; removing RUNNER_TRACKING_ID only avoids later job cleanup,
not this separate step-level stream cleanup. The earlier Store/CIM hypothesis
was not the cause: the listeners had already been killed before inspection.

Verification plan APPROVE: launch using Windows ShellExecute (explicit -Verb
Open, hidden window, no parent-side stream redirection). Move log redirection
inside the child launcher so it owns its output file without inheriting the CI
step's pipes. Preserve normal-user checks, credential stripping and role mutexes.
Remove temporary lifetime probes. Validate syntax, repeat the same remote
registration run and require subsequent role jobs to execute before activation.
No new test cases; existing workflow is the before/after reproduction.
