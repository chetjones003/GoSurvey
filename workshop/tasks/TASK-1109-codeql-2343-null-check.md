# TASK-1109 — Fix CodeQL #2343 inconsistent null check in block insertion

- Type:    fix
- Status:  verify
- Opened:  2026-10-27
- Owner:   Codex

## 1. Authority
- Goal:         Build/Platform quality; no product behavior change intended.
- Requirements: REQ-324 (accepted; CodeQL findings are surfaced and error-severity findings fail the CodeQL run).
- Constraints:  Third-party source and unrelated alerts are out of scope. Preserve current INSERT routing and explosion behavior.
- Acceptance:   `PlaceInsertImpl` uses a checked `ActivePaperGeometryTarget` result for any dereference; model-space inserts still land in model storage; paper-space inserts still land in the active layout; focused tests and the required Windows build/test validation pass.

## 2. Scope
- In scope: alert #2343 at `src/commands/CadBlocks.cpp:738`; focused block-insertion behavior test; this task record; beta PR requested by the user on 2026-10-27.
- Out of scope: third_party findings, any other alert, changes to `ActivePaperGeometryTarget`, unrelated cleanup.
- Smallest change: retain the helper's pointer result in a local, derive `paper` from it, and use the checked pointer in the paper branch.

## 3. Files / subsystems
- `src/commands/CadBlocks.cpp` — `PlaceInsertImpl`.
- `tests/CadBlockImportTests.cpp` — focused model/paper insertion behavior.
- `workshop/tasks/TASK-1109-codeql-2343-null-check.md` — plan and completion evidence.

## 4. Test approach
- Add regression coverage first for placement in model space and active paper space. Paper coverage checks that the block reference and attributes go to the layout vectors, while model vectors remain empty.
- Run focused block import tests, then the repository's Windows `build.bat` workflow and applicable CTest validation.

## 5. Architectural boundary check
- No architectural boundary change. This is local pointer-result reuse in the existing insert-routing function; no new helper or abstraction.

## 6. Plan verification
- SPEC check: REQ-324 is accepted and explicitly makes CodeQL an error-severity gate; this fix resolves a real inconsistent-null-check finding without suppressing it.
- Behavior check: current code routes on whether the helper returns a layout. Storing and reusing that same result preserves routing and closes the unsafe second lookup.
- Scope check: only the alert site, its regression coverage, and this task record are included. No SPEC gap or unresolved behavior question found.
- Risk: low. The regression covers both branches. No other call site or source file needs changing.
- Decision: plan verified; proceed to write regression tests before implementation.

## 7. Implementation / verification log
- 2026-10-27 — Confirmed alert #2343 remains open in the beta CodeQL environment (commit `a9f7d698599fb38ead7cf52d57b1131bafa529f0`) and reproduced the exact two-call pattern in current beta source. Cached `ActivePaperGeometryTarget` in `paperTarget` and reused the checked pointer for paper reference and attribute writes. Added a paper-space insertion regression; existing model-space insertion test continues to cover model routing.

### Verification
- PASS — `build.bat` Release; MSVC/CMake/Ninja built all targets including both frontends and helpers.
- PASS — `GoSurveyTests.exe '[block][insert]'`: 123 assertions in 22 test cases.
- PASS — CTest block import/block behavior and output layout: 4/4 tests.
- PASS — git diff --check.
- Pending — CI CodeQL rerun is needed to confirm alert #2343 closes on the updated beta analysis; no PR was requested at that stage.

## 8. Follow-up full validation and reviews (2026-10-27)

### Full CTest
- Command: `ctest --preset ninja-release` after the successful Release `build.bat`.
- Result: **190/191 passed; suite FAIL**. Unit shards 0–1 passed; Qt shards 0–4 passed; Qt shard 5 failed once; all 180 headless tests and all three build/deployment/parity tests passed (including all three block tests and `build.output-layout`).
- Failure: `qt.editor.shard-5`, `QtEditorTests::failureReportWindowSendsWithoutClosingAndCloseSendsNothing` at `tests/QtEditorTests.cpp:2739`. Expected the clipboard-success/browser-fallback message but got the clipboard-and-browser-failure fallback. This test is outside #2343's block-insertion path and was not changed.
- Rerun: `ctest --preset ninja-release -R '^qt[.]editor[.]shard-5$' --output-on-failure` passed 1/1 in 61.82s. The initial full-suite failure remains part of the result; the rerun did not reproduce it.
- Focused after formatting: `GoSurveyTests.exe '[block][insert]'` passed, 123 assertions in 22 test cases. `clang-format --dry-run --Werror -lines=2791:2815 tests/CadBlockImportTests.cpp` and `git diff --check` passed.

### Review verdicts
- **Architecture review — PASS** (architecture-review checklist; `spec/architecture.md` §11). The change is in `PlaceInsertImpl`, which owns INSERT routing. `paperTarget` is a borrowed pointer to existing state-owned layout data; it is reused only after checking it. No dependency edge, ownership transfer, global state, abstraction, backend call, or rejected structure was added. Blocking findings: none.
- **Code review — PASS for the source diff** (code-review checklist; REQ-324). Reusing the checked lookup result is the smallest clear correction to the second lookup/dereference; model routing, paper routing, and the existing explode refusal remain unchanged. `const` is used for the local pointer binding, no new error path is swallowed, and there are no dependency or performance changes. Blocking source findings: none; advisory findings: none. CodeQL alert-closure evidence remains pending, as recorded below.
- **Testing checklist — PASS for executed test validation.** After the initial 190/191 run and a passing rerun of Qt shard 5, the second complete CTest run passed 191/191. The new paper-routing behavior test asserts preserved routing but cannot fail against the old code because `ActivePaperGeometryTarget(st)` is deterministic for the unchanged state; it does not directly prove CodeQL clears the static finding. No `must` product requirement was added or changed; the relevant static-analysis authority is accepted, `should`-priority REQ-324. REQ-324 alert-closure evidence remains pending a CodeQL analysis of a revision containing this fix.
- **Build-project checklist — incomplete / cannot claim PASS**. The worktree's Release `build.bat` built all targets and the output-layout test passed; formatting and diff hygiene pass. This follow-up did not run a second clean build for determinism, a Debug build, or CodeQL analysis, so those checklist items remain unverified.

### Scope and remaining evidence
- No fix was made for the unrelated Qt clipboard/browser test failure; it did not reproduce on the shard rerun and changing it would exceed the authorized #2343 scope.
- The CodeQL workflow has no PR/push trigger per REQ-324. This unpushed worktree cannot produce a CodeQL result for the fix; alert closure remains unverified until an analyzed revision includes it. At that stage no PR had been requested; the user subsequently requested a PR to beta. No unrelated alert was touched.

### Final full-suite rerun
- Command: `ctest --preset ninja-release` after the clang-format correction and focused test rerun.
- Result: **PASS — 191/191 tests passed** in 553.43s. Both unit shards, all six Qt shards, all 180 headless tests, and all three build/deployment/parity tests passed.
- History retained: the first full run was 190/191 because Qt shard 5 failed once on the unrelated clipboard/browser message assertion; its individual rerun passed 1/1, and this second complete run passed all six Qt shards including shard 5.
- Final source hygiene: `git diff --check` passed; final diff remains limited to the #2343 source fix, its paper-routing behavior test, and this task record. No unrelated test code changed.
## 9. Pre-PR documentation review and PR request (2026-10-27)

User requested a focused PR from `fix/codeql-2343-null-check` to `beta`; do not merge. The initial no-PR scope restriction is superseded by that request. REQ-453 review completed before PR creation:

| Source | Review and outcome |
|---|---|
| `README.md` | Reviewed. It describes product capabilities and contributor workflow; this internal pointer reuse makes no product/workflow change. No update needed. |
| `PRIVACY.md` | Reviewed. No telemetry, diagnostics, or data handling changes. No update needed. |
| `resources/wiki/` | Reviewed the local `Commands/insert.md` and `Paper-Space-and-Layouts.md` entries. The INSERT routing behavior is preserved; no user-facing instructions change. No update needed. |
| Public manual wiki linked by `gosurveycad.com` | Confirmed the live site's “Read the manual” link points to the GoSurvey GitHub Wiki. Reviewed `Paper-Space-and-Layouts.md`, `Command-Reference.md`, `FAQ.md`, and `Known-Limitations.md` at wiki commit `d30cbaeec96452eff13d78a6ef4b873411789284`. No page needs an update for this behavior-preserving fix. The FAQ/limitations pages contain pre-existing claims that GoSurvey has no block/INSERT support; that unrelated documentation drift predates this change and is not included in this narrowly scoped PR. |

PR labels: `bug` (type) and `needs review` (ready status). PR body must cite REQ-324, report both full-suite runs (first 190/191 transient Qt failure, second 191/191), the four documentation review outcomes above, and that alert closure awaits the next supported beta CodeQL scan. No UI screenshots or external product-doc updates apply.

### PR execution
- Created PR [#1126](https://github.com/chetjones003/GoSurvey/pull/1126) to `beta` after explicit user authorization. At creation, head SHA was `246f8323317bc26c688f8ffd70ae8c0c1f12cbe0`; labels `bug` and `needs review` were present. Initial PR inspection: docs-only policy and docs-only classification checks passed; PR gate, full build/test, clang-tidy, MSVC analysis, dependency scan, and gitleaks checks were pending; no review decision was recorded. PR remains open and unmerged; alert closure awaits a supported beta CodeQL scan.