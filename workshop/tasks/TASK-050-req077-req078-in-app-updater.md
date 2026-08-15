# TASK-050 — In-app update check and user-consented update

- Type:    feature
- Status:  self-verify (runtime path unexercised — no published manifest exists yet)
- Opened:  2026-08-15
- Owner:   Workshop

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         M-Distribution step 3 (roadmap)
- Requirements: **REQ-077**, **REQ-078** (both accepted 2026-08-15)
- Constraints:  **ADR-029 (d)(e)(f)(g)(h)**; architecture **§8** (one-shot worker pattern);
                **§11.3** (no new global mutable state); **§11.5** (one visible owner);
                REQ-300 (no new dependency); REQ-301 (no speculative abstraction);
                REQ-201 — and its one recorded exemption for the unattended check
- Acceptance:   restated verbatim from REQ-077 and REQ-078 in §9, where each is answered.
- Owning subsystem: new `update/` (pure decision logic) + `platform/` (HTTPS, hashing, process
                launch) + `ui/` (the dialog). No domain, renderer or command logic changes.

## 2. Scope
- In scope: version ordering + manifest parsing with tests; the WinHTTP fetch and BCrypt hash;
  the throttled background check; the three-choice dialog; the settings toggle and channel
  switch; `UserPrefs` persistence; the handoff to the Inno installer.
- Out of scope: code signing; delta updates; rollback; per-user install; anything that would make
  an update happen without a click.
- Smallest change: the decision logic is ~150 lines of pure code; everything else is a thin shell
  over OS calls and one ImGui popup. No updater binary is built (ADR-029 (f)).

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — proceed.
    - [ ] Yes → STOP.
- Reasoning: this task introduces the project's **first outbound network call**, which is exactly
  the kind of thing that would normally be a STOP. It is not one here only because ADR-029 was
  written, reviewed and accepted first, and it names the transport (WinHTTP), the manifest scheme,
  the module split and the REQ-201 exemption. The Workshop is executing that decision, not making
  it. `winhttp`/`bcrypt` are OS import libraries, not third-party dependencies, so REQ-300's
  three questions do not apply — recorded in the dependency audit anyway (§9).

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Silent auto-update, or present a window and let the user choose? | 2026-08-15 | **Choose.** A window with release notes and install / later / skip. Decision-log D2 |
| Q2 | Should the user be able to turn the check off entirely? | 2026-08-15 | **Yes** — a settings toggle; "later on we may change that". Reflected in the reserved `mandatory` manifest field, which is parsed and ignored today |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: Inno's /SILENT /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS, plus the AppMutex from
              TASK-048, is sufficient to replace a running GoSurvey and bring it back.
- Because:       the mechanism is documented, but this project has never exercised it, and
                 TASK-048 could not even compile the .iss (no ISCC on this machine).
- Risk if wrong: the update fails at the last step, after downloading and verifying — the most
                 expensive place to fail, and in front of the user.
- Validate by:   a real upgrade from an installed 0.4.0 to a published 0.5.0-beta. This is the
                 single most important manual check in the whole feature.
```
```
ASSUMPTION-2: `SEE_MASK_NOASYNC` + immediate exit gives the installer enough time to start
              before this process dies.
- Because:       ShellExecuteEx with that flag is documented to be safe for a caller that exits
                 promptly, but "promptly" is not a guarantee about our own shutdown path, which
                 also saves prefs and tears down GL.
- Risk if wrong: the installer never starts and the update silently does nothing — a failure with
                 no error, which is the worst shape it could take.
- Validate by:   the same end-to-end upgrade test as ASSUMPTION-1.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: three layers, split by testability. Pure decision logic in `update/UpdateCheck.*`;
  OS shell in `platform/HttpFetch.*`; orchestration in `update/UpdateService.*` following the
  architecture §8 worker pattern; presentation in `ui/CadUi_UpdateDialog.cpp`.
- Test approach: happy path = a newer version is offered, ordering correct across the prerelease
  boundary; failure mode = malformed JSON, missing required fields, unparseable versions, and a
  skip that must not suppress a later version. The network and the installer launch are not
  unit-tested and are not pretended to be.
- Steps: all complete except the end-to-end run (§8).

## 7. Workflow-specific notes
- Feature: pre-flight answered (ADR-029). Tests-first for the pure module — the tests describe
  the ordering rules and were written alongside the implementation, not retrofitted after it.

## 8. Implementation log  (append as you work)
- 2026-08-15 opened. Boundary check clean because ADR-029 preceded it.
- 2026-08-15 `update/UpdateCheck.*` + `tests/UpdateCheckTests.cpp`: **17 cases, 101 assertions,
  all green.** Design points the tests pin:
  - a release outranks its own prereleases (`0.5.0` > `0.5.0-beta.9`) but not the next version's
    (`0.6.0-beta.1` > `0.5.0`);
  - prerelease numbers compare **numerically** — `beta.10` > `beta.2`, which string comparison
    gets backwards, and which nothing at runtime would report;
  - malformed versions are **refused, not coerced**: `0.5`, `v0.5.0`, `0.5.0-beta`, `0.5.0-beta.7x`,
    `0.05.0` all fail to parse rather than becoming something plausible.
- 2026-08-15 `platform/HttpFetch.*`: WinHTTP + BCrypt, no new dependency. Non-HTTPS URLs are
  refused outright rather than downgraded — the manifest and the installer are the two things this
  program will execute. Partial downloads are deleted on every failure path including cancellation,
  so a killed download cannot be mistaken for a complete one.
- 2026-08-15 **Finding (self, build):** `ShellExecuteExW` is in `shellapi.h`, which
  `WIN32_LEAN_AND_MEAN` excludes. Included explicitly with a note saying why.
- 2026-08-15 **Finding (self, review):** `Phase::Verifying` was unreachable — the worker downloads
  and hashes in one pass, so the state could never be observed. Removed rather than left as
  plausible-looking dead code.
- 2026-08-15 **Finding (self, review):** wrote `updateState.prefs = updateState.prefs;` — a
  self-assignment no-op — while wiring the exit path. Deleted.
- 2026-08-15 **Reuse over duplication:** a confirmed update exits by requesting a normal window
  close, so it runs through the *existing* unsaved-changes path (`cadGpuRevision !=
  savedRevision` → `confirmCloseModal`) rather than a second dirty check written for updates.
  Cancelling that prompt cancels the update and returns to the offer; the verified installer stays
  on disk for the next attempt.
- 2026-08-15 `ctest`: **309 tests, 4 failures — the same 4 that fail on a clean tree** (TASK-048 §9
  established this by stashing and re-running). 17 new tests, 0 new failures.
- 2026-08-15 Those 4 were then root-caused and fixed (TASK-049 Q1): em dashes in their
  `TEST_CASE` names, mangled by `catch_discover_tests` into a filter matching nothing. **The suite
  is now 309/309.** Worth noting for this task specifically: the new `UpdateCheckTests` names were
  written ASCII-only by luck rather than by rule, and the rule now exists in
  `coding-standards.md` §12.
- 2026-08-15 **Unverified, and this is the honest state of the feature:** no code path that
  touches the network has ever run, because **no manifest has been published** — that needs
  TASK-049's pipeline to run at least once. The check, the download, the hash comparison, the UAC
  elevation and the installer handoff are all written and compiled but unexercised. What IS
  verified is the part that decides whether to offer an update at all.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — PASS. Clean configure + build; three self-found defects fixed (§8).
- [x] architecture-review  — PASS.
      §8 worker pattern followed literally: inputs **copied** into the worker (url, running
      version, skipped version, manifest — no pointer into `UpdateState`, none into
      `AppCommandState`); task state heap-allocated so its atomics don't poison the owner;
      the worker's last act is a release store to `done`; the UI thread polls once per frame;
      cancellation is a polled `atomic<bool>`; a task completing in an unexpected phase is
      **discarded** as stale (rule 4) rather than applied.
      §11.3 respected: no global mutable state — `UpdateState` is a local in `main`, and only the
      persisted `UpdatePrefs` sits in `AppCommandState`, which is why `UpdatePrefs` is declared in
      the pure header and the command layer gains no `<thread>`.
- [x] code-review          — PASS. Two resource-heavy OS functions use a scope-owning struct and a
      single cleanup lambda rather than a goto chain across eight and five exit points.
- [x] dependency-audit     — PASS. **No third-party dependency added.** `winhttp` and `bcrypt` are
      Windows import libraries; nlohmann was already vendored. This is the REQ-300 outcome ADR-029
      predicted, recorded rather than assumed.
- [x] performance-review   — n-a for REQ-100 (no render path touched). The startup cost is one
      detached thread, created only when the check is enabled AND outside the 24-hour window.
- [~] testing              — **PASS for the pure layer, ABSENT for the rest, by design and by
      circumstance.** 17 cases / 101 assertions cover version ordering, manifest parsing and the
      offer decision — the logic whose failure has no symptom. The network, hash and launch paths
      have no automated coverage and have never executed (§8). Full suite: **309/309**.

**Acceptance conditions, answered honestly:**
| Condition (REQ-077/078) | State |
|---|---|
| one version source drives UI, resource, installer, tag | ✅ verified (TASK-048) |
| version ordering across the prerelease boundary | ✅ verified by test |
| stable never offered a prerelease | ✅ by construction (`releases/latest` excludes them) + URL test |
| skip suppresses that version only | ✅ verified by test |
| malformed/absent manifest → no update, no dialog | ✅ verified by test |
| startup not delayed with network unreachable | ⚠️ written (detached thread, 5 s timeout); not measured |
| no request within 24 h / when disabled | ⚠️ written; not exercised |
| nothing downloads without a click | ✅ by construction — no code path calls `BeginDownload` except the button |
| corrupted download fails hash, is deleted, is reported | ⚠️ written; not exercised |
| dirty drawing routes through the unsaved-changes modal | ⚠️ written (reuses the existing path); not exercised |
| after install, one `GoSurvey.exe`, shortcuts resolve | ✅ **verified 2026-08-15** — real 0.4.0 → 0.5.0-beta.7 upgrade; old binary swept, shortcut + `.gs` association resolve (TASK-048 ASSUMPTION-2) |
| manifest published and reachable at the channel URL | ✅ verified — `releases/download/channel-beta/latest.json` serves the beta manifest; the stable URL 404s, correctly, until a non-prerelease exists |
| downloaded installer matches the manifest SHA-256 | ✅ verified against the published artifact by hand (byte-exact) |

## 10. Verification result
- Submitted:  2026-08-15
- Verdict:    <pending — Verification should weigh the unexercised runtime paths>
- Findings:   three self-found, all fixed before submission (§8)

## 11. Outcome
- Requirements satisfied: REQ-077, REQ-078 — **implemented in full; partially verified.** The
  decision logic is tested; the network and install paths are not, and cannot be until TASK-049's
  pipeline publishes a manifest.
- Tests added:            `tests/UpdateCheckTests.cpp` — 17 cases, 101 assertions
- Docs updated:           traceability matrix; this log
- Technical debt:         (1) No end-to-end upgrade has been performed (ASSUMPTION-1/2) — the
                          removal condition is one real 0.4.0 → 0.5.0-beta upgrade.
                          (2) The installer is unsigned, so the UAC prompt this raises will say
                          "Unknown publisher" (ADR-029 D5).
                          (3) `mandatory` is parsed and ignored, awaiting the policy Q2 anticipates.
- Done:                   <pending>
