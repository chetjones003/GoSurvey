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

## First laptop run: startup failures

- Run 39669157648 on fcc92622 passed normal-user/desktop and checkout checks, then
  failed preflight because bare `clang-tidy --version` aborted with duplicate
  LLVM `color` option registration (exit 3). Compilation/tests did not start.
  MSVC 2022, CMake 4.0.1, Ninja 1.12.1 and Python 3.13.3 were reached successfully.
- The job shell also loaded an interactive PSReadLine profile, producing terminal
  errors. The logged command had no `-NoProfile`; use an explicit noninteractive
  no-profile shell for this workflow.
- Leading LLVM hypotheses: a VS-bundled executable shadows standalone LLVM; the
  interactive profile changes command resolution; or the installed LLVM package
  itself cannot start. Underlying LLVM packaging/DLL cause is not yet proven.
- Plan verification APPROVE: record hardware/tool paths before version probes;
  prefer the standalone installed executable, verify candidate startup, and fail
  if no installed candidate works. Preserve the selected executable/version and
  unsuccessful attempts as evidence and put the selected directory first on PATH.
  This validates an installed tool; it does not waive static-analysis readiness.
  The next laptop run must establish which executable actually works.
- Validation: an exit-3 fixture is rejected when it is the only candidate; a
  subsequent healthy fixture is selected with both attempts retained. The local
  standalone LLVM 21.1.8 executable passes the same probe. PowerShell syntax,
  actionlint and patch whitespace pass. This does not establish the laptop's
  installed LLVM version or underlying packaging cause; hardware rerun pending.

## Second laptop run: cache-control hang

- Run 39670013936 on 7ad97b90 exceeded the 45-minute job deadline. Its only
  benchmark stage was preflight (2516.332 seconds); no compilation or tests ran.
  The last completed command was `sccache --version`; execution then blocked in
  the first `sccache --show-stats --stats-format=json` capture, with no deadline.
- Standalone LLVM 20.1.4 starts correctly. The previous PATH selected Chocolatey's
  `winlibs/tools/mingw64/bin/clang-tidy.exe`, rather than standalone LLVM.
- Hardware recorded: Intel i5-12450H, 12 logical processors, 15.7 GiB RAM,
  Intel UHD / NVIDIA GTX 1650, MSVC 14.44.35207, six build/test workers.
- Ranked unresolved hypotheses: an unresponsive listener on sccache's local
  endpoint; cache-backend initialization; or captured-output completion. No
  evidence yet supports reinstalling tools, deleting caches or killing daemons.
- Plan verification APPROVE under REQ-205: bound cache-control client processes
  to 30 seconds, retain separate stdout/stderr files, record only cache process
  paths/PIDs and endpoint ownership on failure, and enable narrowly scoped
  client/command traces during preflight. Do not record environment secrets,
  silently disable caching, extend the job timeout, or alter required checks.
  The laptop rerun is the diagnostic feedback loop, not proof of resolution.
- Validation plan: reproduce a real sccache client waiting on a silent loopback
  listener, verify the bounded call fails and retains diagnostics, and verify a
  healthy local cache response still parses. Check syntax and whitespace.
  Source reference: mozilla/sccache v0.18.0 `src/commands.rs` ShowStats dispatch
  and `docs/Configuration.md` (client logging and local endpoint configuration).
- Local verification PASS: unbounded v0.18.0 `--show-stats` stays blocked against
  a silent TCP listener; the actual bounded helper fails that same client after
  one second, stops only its client PID, and retains stderr/report evidence.
  A separate healthy daemon returns valid JSON and text statistics and is shut
  down by the fixture. This reproduces the missing-deadline failure mechanism;
  it does not prove which laptop listener/backend caused the original hang.
- PowerShell parser, workflow actionlint and patch whitespace PASS. Tooling-only
  architecture/code review PASS: no product changes, skipped tests, cache deletion,
  daemon termination, secret dumps, or change to blocking validation. Laptop
  cache diagnosis and actual build/test measurements remain pending.

## Third laptop run: connect before server startup

- Run 39677022219 on 8185b3e0 failed preflight in 38.697 seconds, as intended
  by the new deadline. Trace stops at `connect_to_server(127.0.0.1:4226)`;
  there is no `request_stats` or sent request. Process and socket inspection
  confirms no sccache process or listener exists after the client is stopped.
  This rules out a cache server waiting to answer, backend work after connection,
  and output-pipe completion as causes of this particular stall.
- Mechanism: the benchmark queries statistics before starting a server, causing
  sccache's blocking TCP connect to an absent endpoint on this laptop. Why Windows
  delays/refuses no connection response remains unknown; firewall/security
  configuration is a hypothesis, not an established diagnosis. No firewall,
  antivirus, system network or inbound access changes are justified.
- Plan verification APPROVE: explicitly start a benchmark-owned server before
  the first stats request. Allocate a free OS-selected loopback port, propagate
  it through SCCACHE_SERVER_PORT to all build clients, retain the existing disk
  cache, and stop only that server afterward. Preserve the command deadlines;
  fail if server startup, statistics, compilation or tests fail. There is a
  small bind race between allocating and starting; startup failure stays visible.
- Regression plan: the real sccache silent-listener fixture remains failing with
  a deadline. Exercise the actual startup helper against a fresh local endpoint,
  then query JSON/text stats and stop that owned server. The laptop rerun must
  prove that connections to a running server work on that host.
- Local verification PASS: actual startup helper launches v0.18.0 on its selected
  endpoint, records/propagates the port, and JSON/text stats parse successfully.
  The silent-listener timeout check still passes. PowerShell parser, actionlint
  and patch whitespace PASS. Review confirms startup/stat errors remain failures,
  only the owned endpoint is stopped, and no test/protection behavior changes.
  Hardware validation remains pending; no performance claim is made.

## Fourth laptop run: bundled-library runtime incompatibility

- Run 39677654768 on e79e2039 verifies the cache startup fix: preflight 5.513s,
  configure 10.825s, every cache-control call below 0.14s, owned server stopped.
  The initial build fails after 311.21s at GoSurvey linking (not a full build
  timing). Cache counters show 239 successful compilations, 239 misses, no hits
  or uncacheable calls. Cached rebuild and CTest were not reached.
- Root cause: MSVC 14.44.35207 cannot resolve the bundled E57Format/csmap objects'
  `__std_find_first_not_of_trivial_pos_1`, `_pos_2`, and
  `__std_find_last_not_of_trivial_pos_2` runtime references. These implementations
  were added in MSVC 14.50 (Microsoft STL changelog, #5206). The local 14.51
  msvcprt.lib exports these exact symbols. The setup script incorrectly treats
  any installed C++ workload as sufficient and otherwise installs VS 2022.
- Plan verification APPROVE: select/install stable VS 2026 Build Tools with
  MSVC 14.50+ and verify its default x64 tools. Add a tiny compile/link preflight
  using these three actual symbols, retaining source/object/link output under
  artifacts. Do not modify vendor binaries or add fake compatibility functions.
  Document the one-time machine upgrade; runner jobs remain non-elevated and
  never install tools. Installation on the laptop needs its administrator session.
- Validation plan: compile/link the real probe against local MSVC 14.51, verify
  setup's read-only plan and syntax, and retain the existing laptop link failure
  as evidence of the older runtime. Actual hardware rerun follows installation.
- Sources: https://github.com/microsoft/STL/wiki/Changelog#msvc-build-tools-1450
  and https://learn.microsoft.com/en-us/cpp/overview/acquire-msvc . Microsoft
  binary-compatibility guidance requires linking with tools at least as new as
  the newest binary input; updating only the redistributable cannot fix linking.
- Local regression PASS: the real probe fails with the same three unresolved
  functions on 14.44.35207 and links successfully on 14.51.36231. The tiny probe
  explicitly links msvcprt.lib, matching the vendored libraries' default-library
  directive; its executable is not run. The installer plan makes no changes.
- Developer-shell validation also found VS 2026 expects vswhere on PATH. The
  generated helper now adds its directory and explicitly selects the default
  supported toolset, avoiding fallback to older side-by-side tools.
- Hardware verification is pending the laptop's one-time administrator install.
  Runner jobs cannot perform that privileged step. Keep hosted validation in
  place and preserve caches/tests; resume benchmark after the helper is refreshed.
- PowerShell syntax (installer, generated environment and benchmark), actionlint,
  read-only installer plan and whitespace PASS. Tooling architecture/code review
  PASS: stable toolchain provisioning only, no vendor binary changes, runtime
  shims, product behavior changes, skipped coverage or weakened required checks.
