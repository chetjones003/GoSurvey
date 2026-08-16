# TASK-049 — CI pipeline: build, test, package, publish

- Type:    feature
- Status:  self-verify (blocked on a user decision — see §4 Q1 and §8)
- Opened:  2026-08-15
- Owner:   Workshop

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         M-Distribution step 2 (roadmap)
- Requirements: **REQ-202** (accepted 2026-08-15)
- Constraints:  **ADR-029 (c)(d)**; REQ-200 (the published artifact must *be* the reproducible
                build); REQ-300 (no new project dependency — CI tooling is not a link-time
                dependency, but every action used is pinned)
- Acceptance:   restated verbatim from REQ-202:
  - a push to a feature branch produces a downloadable installer artifact and creates no release
    and no tag;
  - a push to `beta` leaves exactly one `channel-beta` prerelease in the releases list regardless of
    how many times it is pushed, carrying the newest installer;
  - a push to `master` with an unchanged version publishes nothing and fails nothing;
  - a push to `master` with a bumped version creates tag `v<version>` and a stable release;
  - a failing `ctest` run publishes no release;
  - the installer's `AppVersion`, the release tag, and the manifest's `version` field are equal on
    every published release;
  - the manifest's SHA-256 matches the published installer.
- Owning subsystem: Build/Platform. No application code is touched by this task.

## 2. Scope
- In scope: `.github/workflows/release.yml` — the whole pipeline, all three triggers, dependency
  caching, installer packaging, manifest generation, and the no-op signing placeholder.
- Out of scope: the updater that consumes the manifest (TASK-050); actually configuring code
  signing; fixing the pre-existing test failures (see Q1 — they belong to TASK-044/046).
- Smallest change: one workflow file. One job rather than three, because splitting build from
  publish would mean passing a ~5 MB installer between jobs through the artifact store to gain
  nothing — the publish steps are already conditional on the branch.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — proceed.
    - [ ] Yes → STOP.
- Reasoning: the channel model, the manifest hosting scheme and the version gate were all decided
  in **ADR-029 (c)(d)** and accepted before this task opened. `latest.json`'s schema is a new data
  format, but it is specified in REQ-202 and ADR-029 (d), not invented here. Nothing in `src/`
  changes.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | `ctest` currently fails 4 of 292 tests on a clean tree (pre-existing, from TASK-044/046). REQ-202 requires a failing suite to block publication — so as written, **no release will ever publish** until they are fixed. Fix them first, quarantine them explicitly, or drop the gate? | 2026-08-15 | **RESOLVED — fix them.** User chose the fix over quarantine. Root cause was not the tested logic at all: em dashes in four `TEST_CASE` names are mangled by `catch_discover_tests`' codepage round-trip, so CTest's filter matched nothing ("No test cases matched") and scored it a failure. Names made ASCII; **309/309 pass**. Gate stays ON and now means something. Rule recorded in `coding-standards.md` §12 |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: Inno Setup is present on the windows-latest runner image; if it is not, `choco
              install innosetup` supplies it.
- Because:       the image's installed-software list is not something this task can verify offline,
                 and it changes over time.
- Risk if wrong: none beyond ~40 s of install time — the step probes first and installs only on a
                 miss, so it is correct either way. This is why it was written as a probe rather
                 than as a bare `choco install`.
- Validate by:   the first pipeline run; the step logs which path it took.
```
```
ASSUMPTION-2: Caching build/_deps is sufficient for FetchContent to skip re-downloading glfw,
              imgui, glew, pdfium and Catch2 on a cache hit.
- Because:       FetchContent also keeps subbuild stamp files, and whether the restored tree alone
                 satisfies it is not verifiable without a runner.
- Risk if wrong: builds are slow (~10 min) but correct. Never a correctness risk — a cache miss
                 falls back to a full download.
- Validate by:   comparing wall time between the first run and the second.
```
```
ASSUMPTION-3: `github.run_number` is monotonically increasing per workflow, making it a sound
              beta ordinal (0.5.0-beta.2 < 0.5.0-beta.10 under numeric comparison).
- Because:       it is documented as such, but the failure would be silent and would surface only
                 as users not being offered an update.
- Risk if wrong: beta ordering breaks and a newer beta looks older than an existing one.
- Validate by:   TASK-050's version-ordering tests pin the comparison itself; the run-number
                 property is observed across the first few beta publishes.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: one `windows-latest` job. Resolve version + channel from the branch, build, test,
  package, then take exactly one of three publish paths by branch condition.
- Files/functions to touch: `.github/workflows/release.yml` (new). Nothing else.
- Test approach: there is no unit test for a CI pipeline; it is verified by running it. happy path
  = a push to a feature branch produces an artifact; failure mode = the version gate, verified by
  pushing to master twice without bumping and confirming the second run publishes nothing and
  still succeeds.
- Steps:
  - [x] triggers, permissions, checkout with tags
  - [x] MSVC + Ninja + dependency cache
  - [x] version/channel resolution from `project(VERSION)` + branch
  - [x] version gate against the remote tag list
  - [x] configure/build/test
  - [x] ISCC packaging + no-op sign step
  - [x] manifest generation with SHA-256
  - [x] three publish paths
  - [ ] observe a real run  ← cannot be done from here; needs a push

## 7. Workflow-specific notes
- Feature: pre-flight answered? Yes — ADR-029 settled the design. Tests-first? Not applicable and
  not faked: a workflow's behaviour is observable only by running it on the platform, and a local
  harness asserting on YAML structure would test the file, not the pipeline.

## 8. Implementation log  (append as you work)
- 2026-08-15 opened. Boundary check clean.
- 2026-08-15 Workflow written. Design points worth recording:
  - **The version gate queries the remote** (`git ls-remote --tags origin`), not the local clone. A
    tag pushed by a concurrent run would be invisible to a local check, and the failure mode of
    getting that wrong is a duplicate release.
  - **Ninja is installed rather than adding a Visual Studio CI preset**, so CI runs the same
    `ninja-release` preset a developer runs. A second generator would be a second set of behaviours
    to debug whenever CI and local disagree.
  - **The cache key is `hashFiles('CMakeLists.txt')`** — that file holds every pinned dependency ref
    (glfw 3.4, the imgui SHA, glew 2.2.0, the pdfium URL, Catch2 v3.5.2), so the key changes when
    and only when a dependency does.
  - **The manifest URL is constructed from the tag the asset is about to be published under**,
    rather than relying on the `releases/latest/download/...` redirect at write time. The redirect
    is how the *client* will fetch it (ADR-029 (d)); the manifest itself states a concrete URL so
    it is correct at the moment it is written.
  - **The Inno Setup step probes before installing**, so it is correct whether or not the runner
    image ships it (ASSUMPTION-1).
- 2026-08-15 **Blocked on Q1.** `ctest` fails 4/292 on a clean tree — verified pre-existing in
  TASK-048 §9 by stashing all changes and re-running. REQ-202's acceptance says "a failing `ctest`
  run publishes no release", and the pipeline implements exactly that, which means **master will
  build, test, fail, and publish nothing** until those 4 are resolved. This is the pipeline behaving
  as specified, not a defect in it — but it makes the feature inert on arrival, so it is the user's
  call, not the Workshop's. Raised rather than quietly weakened.
- 2026-08-15 **First real run (31910767883): FAILED at Build, and it found something.** The
  workflow itself parsed and ran; checkout, MSVC setup, Ninja, cache, version resolution and
  configure all passed, and the branch-gated steps correctly skipped. The build then failed with
  ~50 × **C2362** in `ViewportRenderer.cpp`: `goto finish_render` jumps over that many variable
  initializations.
  **This is not a regression and not caused by anything in this workstream.** It is a compiler
  difference that had never been visible before: `ninja-release` pins no compiler, so locally
  CMake picks **clang** off PATH (`C:\Program Files\LLVM\bin`), while `msvc-dev-cmd` puts
  `cl.exe` first on the runner. So the first CI build was the first time this codebase had ever
  been compiled with MSVC — and it does not compile with MSVC. MSVC is the correct one here:
  jumping over an initialization is ill-formed C++, and clang is being permissive.
  **Fix: pin clang in CI**, matching what a developer builds and what the shipped 0.4.0 installer
  actually was. Porting the renderer to be MSVC-clean is a real refactor in a hot path, is not
  what REQ-202 asks for, and would mean CI shipping a binary built by a toolchain the project has
  never otherwise used. Recorded as debt instead.
- 2026-08-15 **Run 31911817777 — MSVC build/test/installer all green; failed at "Write the update
  manifest" with `Unexpected token 'MSVC'`.** Cause: `notes = "${{ github.event.head_commit.message }}"`
  interpolated the commit message into the PowerShell *source*, and that commit's message contained
  the quoted phrase `"MSVC toolchain"`, which closed the string literal early.
  **This is a script-injection hole, not a quoting nit.** A `${{ }}` expression is substituted into
  the script text before pwsh parses it, so a commit message containing PowerShell would have run
  on the runner — which holds a `contents: write` token, i.e. the ability to publish releases and
  push tags. It failed by accident this time; it would not have failed on purpose. Fixed with
  GitHub's documented pattern: untrusted context passed via `env:` and read as `$env:VAR`, where it
  is data the script reads rather than text the script is made of. `github.ref_name` was audited
  and moved the same way. Every remaining `${{ }}` inside a `run:` block was checked and is
  machine-generated and constrained (version strings from a `[0-9.]+` regex, `run_number`,
  `github.repository`, `github.token`).
- 2026-08-15 **Unverified before that run:** the workflow had never run. No YAML validator, `python` or `yamllint`
  is available on this machine, so even its syntax is unchecked. `gh` (2.92.0, authenticated as
  chetjones003) is present, so the publish steps' commands are at least the right ones for the
  installed CLI. First real verification is a push.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — n-a for the pipeline itself; the tree it builds is green (TASK-048).
- [x] architecture-review  — PASS. No `src/` change; no Workshop architectural decision.
- [x] code-review          — PASS by reading. Every non-obvious choice carries its reason inline.
- [x] dependency-audit     — PASS. Three third-party actions, each pinned to a major tag:
      `actions/checkout@v4`, `actions/cache@v4`, `actions/upload-artifact@v4` (all first-party
      GitHub) plus `ilammy/msvc-dev-cmd@v1`, which is **not** first-party — noted as the one
      supply-chain surface introduced here, and a candidate for SHA-pinning.
- [x] performance-review   — n-a. CI wall time is the only cost; addressed by the `_deps` cache.
- [~] testing              — **NOT RUN. The pipeline has never executed.** §8 records why.

## 10. Verification result
- Submitted:  2026-08-15
- Verdict:    <pending — blocked on Q1>
- Findings:   Q1 (test gate vs. 4 pre-existing failures) raised to the user

## 11. Outcome
- Requirements satisfied: REQ-202 — **claimed, not demonstrated.** Every acceptance condition is
  implemented; none has been observed, because observing them requires a push.
- Tests added:            none — see §7
- Docs updated:           this log
- Technical debt:         (0) ~~The codebase does not compile with MSVC.~~ **CLEARED 2026-08-15.**
                          The skipped region now has its own block scope, so `finish_render` sits
                          outside the scope of everything the jump passes over — legal under
                          [stmt.dcl]/3 for both compilers. Verified both ways: **MSVC 309/309**
                          (separate `cl.exe` build tree) and **clang 309/309**. The change is 11
                          added lines and **no modified ones**, so no render behaviour could have
                          moved. CI still pins clang deliberately — it must reproduce the binary
                          that actually ships — but the project is no longer clang-*only*, and the
                          `if(MSVC)` branches in `CMakeLists.txt` are live again rather than dead.
                          **Superseded same day:** rather than keep two toolchains in play, the
                          project pinned MSVC in `CMakePresets.json` for both presets and CI
                          stopped passing any compiler of its own (decision log 2026-08-15). The
                          residual risk this entry described — MSVC support rotting because CI
                          never exercised it — is gone, because MSVC is now the only compiler
                          either side uses. Local build/ reconfigured and verified: 248/248,
                          309/309.
                          (0b) ~~**REQ-100's recorded p95 (8.93 ms) is now invalid** — it was
                          measured on a clang build. Needs a `BENCH` re-run under MSVC.~~
                          **CLEARED 2026-08-15** by TASK-052: re-measured on the reference machine
                          under MSVC at **p95 9.27 ms** (segments) and **9.32 ms** (surface), both
                          inside the 16 ms budget. The required density barely moved; the *headroom*
                          did — 750k segments passed under clang and fails under MSVC, so the
                          "3–4× headroom" claim was corrected to ~2× across the spec and the roadmap
                          risk table. That correction is the real content of this debt item.
                          (1) `ilammy/msvc-dev-cmd@v1` is a third-party action on a moving tag;
                          SHA-pin it if the supply-chain surface matters.
                          (2) The signing step is a deliberate no-op (ADR-029 D5).
                          (3) REQ-200 now rests on a runner image we do not control (roadmap risk
                          table).
- Done:                   <pending>
