# TASK-1004: Validate the spare Windows 11 runner

## Request and authority

The user installed the supplied environment, registered `gosurvey-victus-ci`, and
confirmed restarting it in a normal interactive PowerShell session. Continue the
CI performance investigation by checking the machine and measuring build/test
behavior before routing ordinary required checks onto it.

Accepted REQ-205 permits build tooling and compiler caching. REQ-437 keeps all
required checks blocking and retains the explicit docs-only label. Follow native
MSVC/CMake/Ninja and existing Qt 6.8.3 tooling. No product UI or API changes.

## Acceptance

- Execute only on this named laptop, from an owner-triggered trusted benchmark.
- Reject an elevated or session-0 process before source checkout/execution.
- Record CPU, RAM, graphics, installed tool versions, Qt and worker limits.
- Build the same four targets as CI, measure a second clean-tree compilation
  using the local compiler cache, and run the complete CTest suite with native
  graphics. Report failures rather than claiming GUI support without evidence.
- Upload timings, cache statistics and test logs, including evidence on failure.
- Keep existing hosted required workflows and branch protection unchanged.

## Verified plan: APPROVE

1. Add a repeatable PowerShell benchmark using the installed environment helper.
   Use a persistent per-user cache outside checkout, bounded CPU/RAM concurrency,
   checked native exit codes, and progress reports saved even on failure.
2. Add an isolated workflow targeted with a unique runner label. Restrict its
   bootstrap push trigger to this branch in the owner's repository, and gate on
   the owner actor. No pull_request or pull_request_target trigger. Use read-only
   contents permission and do not persist checkout credentials. A future manual
   trigger requires the workflow to reach the default branch; the branch push
   supplies the initial run without changing master.
3. Validate workflow/YAML/PowerShell syntax, label the online runner, push and open
   a beta PR, then monitor the actual laptop job through T3.

Architectural review: tooling only; no application dependency, ownership, release
or runtime behavior changes. GUI screenshots are not a changed product surface;
no product approval cycle applies. Risk: the old laptop may lack tools, memory or
working OpenGL; the benchmark must stop/report such failures. Initial build cache
warmth is unknown and must be reported from actual counters. Nothing purchases
infrastructure, opens inbound ports, or enables unattended login.

## Verification plan

Static checks first; actual preflight/build/cache/CTest behavior is verified on
the newly registered laptop, without provisioning packages on this developer
machine. Preserve complete tests. Record measured results before claiming any
performance improvement or the three-minute target.

## Outcome

Implementation complete; hardware verification pending.

- Added the dedicated workflow, PowerShell benchmark, actionlint's explicit custom
  label configuration, and operating instructions. Existing required workflows
  and protection settings are unchanged.
- Runner API confirmed ID 21, name `gosurvey-victus-ci`, Windows, online and idle.
  Added the matching custom routing label without replacing built-in labels.
- PowerShell syntax, YAML and actionlint pass. A local helper-only check confirmed
  that a native exit 19 propagates as failure, exit 0 passes, and both stage
  results are persisted. No C++ build, installer or hardware test was executed on
  the development workstation for this change.
- Static architecture/code-quality review PASS: trusted owner-pushed source,
  normal interactive execution checked before checkout, credentials not persisted,
  native failures propagated, and artifacts retained on failure. Hardware execution
  must still validate dependencies and GUI support; no performance claim is made.
- Public-repository migration constraint: fork workflow approval is currently
  `first_time_contributors`. This experiment has no PR trigger; broad persistent
  runner PR routing still needs a deliberate trust/isolation policy.
