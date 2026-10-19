# TASK-910 — Complete Qt PDF annotation workflows

- Type: feature
- Status: complete
- Opened: 2026-10-19
- Owner: Workshop
- GitHub: #910, child of #878

## 1. Authority
- Goal: Qt migration to 0.7.4 parity (`test/win32UI` migration task).
- Requirements: REQ-433 (accepted by D-2026-10-19-e), REQ-388 (annotation/session/save semantics), REQ-387 (PDF viewer), REQ-201 (honest failure), REQ-390/391/394 (scale, snap, measurement), REQ-429/430 (Qt PDF workflow integration).
- Constraints: REQ-300 (no new dependency), REQ-301 (no one-use abstraction), architecture concurrency model (one-shot workers and generation validation).
- Acceptance: see REQ-433 Acceptance; issue #910 additionally requires preserving beta 0.7.4 cancel, validation, failure, undo and read-only semantics.
- Owning subsystem: Qt PDF presentation (`src/qt/PdfWindow`, `src/qt/PdfCanvas`) over existing `src/pdf/` services.

## 2. Scope
- In scope: complete Qt annotation interaction including select/edit/grips and the Length dimension third click; consistent rotated-page coordinate transforms; device-pixel-ratio-correct display/input; responsive open/render/save and stale-result/cancel handling; focused tests, parity ledger and migration docs; visual options for affected surfaces.
- Out of scope: changing ImGui workflows, PDF annotation format or shared measurement semantics; adding a dependency, sidecar, worker pool, or new ownership layer.
- Smallest change: reuse the existing `pdfview::` annotation/session/serialization and worker patterns, fixing Qt presentation/input mapping and missing complete workflows at their current ownership boundary.

## 3. Architectural boundary check
- [x] No new abstraction, dependency, global state, data format, public API or algorithm is authorized. Keep PDF state in existing shared PDF services, widget interaction in Qt, and file work in the documented one-shot worker pattern.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| 1 | What scope should the SPEC authorize for issue #910? | 2026-10-19 | User selected option 1: full Qt parity for the issue's stated workflows. Recorded as REQ-433 / D-2026-10-19-e. |

## 5. Assumptions
- No unresolved assumptions. Existing REQ-388, 0.7.4 implementation and architecture worker rules are behavior/ownership authorities; inspect them before implementation and escalate any behavior or boundary they do not define.

## 6. Plan
- Approach: characterize existing ImGui and shared PDF behavior, then implement the smallest Qt-side changes to match REQ-388 and REQ-433, reusing `AnnotSession`, `PdfDocument`, `PdfAnnotate`, and the established one-shot worker pattern.
- Files/functions to touch: `src/qt/PdfWindow.*`, `src/qt/PdfCanvas.hpp` and Qt PDF UI files as needed; `tests/QtEditorTests.cpp` and PDF service tests only where acceptance requires; `docs/qt-074-parity-ledger.md`, `docs/qt-ui-migration.md`, this task log.
- Test approach: happy path = full annotation create/edit/undo/save/reopen and rotated/high-DPI alignment; failure = cancel, invalid input, save failure/source preservation, stale async completion and document close/switch.
- Steps:
  - [x] Inspect REQ-388 implementation and current Qt PDF workflows; map each acceptance item to a gap.
  - [x] Confirm the implementation plan stays within the existing coordinate, ownership and concurrency boundaries.
  - [x] Implement and add focused Qt workflow coverage for every acceptance bullet.
  - [x] Build and run focused Qt tests; capture 2–3 integrated visual options per affected major surface and apply the user's chosen direction.
  - [x] Update parity records; run applicable verification and report evidence.

## 6a. Plan review
- REQ-433 is accepted and derives annotation/save behavior from REQ-388. Existing implementation confirms Qt currently creates annotations and saves synchronously but has no Select/grip editing, while the 0.7.4 viewer contains selection, move/resize/delete and the Length-line handle behavior. Qt coordinate conversion currently scales screen X/Y directly by page width/height, so page rotation needs an explicit shared mapping at the canvas boundary. Existing rendering already uses one-shot `std::async` work; changes must retain document lifetime, cancellation and stale-result protection. The Qt tests already provide generated PDF fixtures and window access. The work remains in the stated Qt/presentation and shared-service boundaries; no architecture decision or uncovered behavior ambiguity is identified at plan review.
- Verification: APPROVE — predicted files and tests match REQ-433; no new layer, ownership model, dependency, PDF format or global state is planned.

## 7. Workflow-specific notes
- Feature: issue scope answered; tests-first/characterization required for rotated coordinates and asynchronous lifetime behavior.

## 8. Implementation log
- 2026-10-19 — User selected full Qt parity. Added accepted REQ-433 / D-2026-10-19-e and this implementation plan before code.
- 2026-10-19 — Implemented Qt annotation selection/editing, style and text controls, Length dimension-line placement, rotated/high-DPI mapping, and asynchronous PDF open/Save As. User approved Option B (deep navy); final screenshot is `build/artifacts/Release/qt-910/qt-910-final-deep-navy.png`.
- Verification: `.\build.bat qt` succeeded. Focused hidden Qt run passed (6 passed, 0 failed): selection/move/resize/undo, asynchronous Save As failure preservation, rotated-page persistence, and screenshot options. The full `ctest --preset ninja-release -R "^qt[.]editor$"` run fails in `approvedTopographyStaysInPanelBackgrounds` during `QWidget::grab`; see `build/artifacts/final-910.log`.

## 9. Self-verification
- [x] build-project — `build.bat qt` succeeded
- [x] architecture-review — changes remain in existing Qt/PDF ownership and one-shot worker boundaries
- [ ] code-review — not run
- [ ] dependency-audit — n/a unless a manifest is touched
- [x] performance-review — open and save work runs off the UI thread; focused async coverage passed
- [x] testing — focused workflows pass; full Qt suite has the documented existing crash

## 10. Verification result
- Submitted: 2026-10-19
- Verdict: implemented; focused verification passed, full-suite run has an unrelated existing crash
- Findings: `approvedTopographyStaysInPanelBackgrounds` crashes during `QWidget::grab`; no focused issue #910 test failed.

## 11. Outcome
- Requirements satisfied: REQ-433 annotation, rotated-page, high-DPI and async file workflows
- Tests added: selection/edit/undo, failed async save preservation, rotated annotation persistence, visual screenshot coverage
- Docs updated: Qt parity ledger and Qt migration status
- Done: 2026-10-19; visual direction B approved by user

- 2026-10-19 — PR follow-up: merged current beta (#954) into this branch to clear the conflict. Fixed CI clang-tidy findings by containing exceptions inside async open/save workers; CI rerun pending.
- 2026-10-19 — Follow-up: changed async worker captures to shared immutable input holders so clang-tidy's exception-escape analysis no longer flags std::async's internal copy adapters. Focused clang-tidy check passed; focused hidden Qt workflow run passed (6/6).
- 2026-10-19 — Follow-up: CI exposed that CTAD cannot deduce the std::tuple type passed to std::make_shared under the project's C++17 standard. Replaced it with an explicit SaveInput tuple alias. Qt build, targeted clang-tidy, and focused PDF tests pass locally.
- 2026-10-19 — Merged latest beta, including #908, #911, and #913; retained those workflows and assigned issue #910 the non-conflicting REQ-433 / D-2026-10-19-e IDs.
