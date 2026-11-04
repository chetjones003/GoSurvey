# TASK-1109 — Reuse GoSurvey for Windows DWG opens and import multileader scale context

- Type:    bug
- Status:  implement (user visual review pending)
- Opened:  2026-11-02
- Owner:   Codex

## 1. Authority
- Goal: no numbered GOAL-NN applies; `spec/project.md` currently defines no GOAL-NN entries.
- Requirements: REQ-055 (accepted; DWG saved model view added by D-2026-11-03-d), REQ-367 (accepted; legacy LEADER + MTEXT fidelity amended by D-2026-11-03-c), REQ-384 (accepted), REQ-481 (accepted by D-2026-11-03-e).
- Constraints: REQ-201 (report unsupported/failed operations), REQ-453 (keep separately built worktrees isolated), CON-06 (smallest correct solution).
- Acceptance:
  - REQ-055: a DWG opens at the saved active model-space VPORT view and saved CANNOSCALE, with annotation scale independent of viewport zoom.
  - REQ-367: native multileaders round-trip and render with the DWG label attachment, rotation, text rectangle and arrow size; associated legacy LEADER + MTEXT callouts retain those same visible properties and flagged hookline.
  - REQ-384 increment 4: import uses default-scale context geometry when present; REQ-384 increment 5: R2018 round trip preserves annotative contexts.
  - REQ-481 acceptance 1–5: a normal Windows DWG launch activates the matching running executable and opens the file there; it opens normally without a running match; worktree executables remain separate; helper/test/crash modes are unaffected.
- Owning subsystem: Application lifecycle (`src/qt/main.cpp`), Platform process coordination, and DWG IO (`src/io/LibreDwgCad.cpp`, `src/io/LibreDwgAnnotContext.cpp`).

## 2. Scope
- In scope: forward a Windows DWG launch to an existing process for the same executable path; restore the saved active model-space VPORT view and CANNOSCALE; preserve native per-scale context membership for annotative multileaders; faithfully import/export the legacy LEADER + MTEXT callouts visible in MESSER; retain ordinary open behavior.
- Out of scope: full editing of custom MLEADERSTYLEs, arbitrary per-scale representations beyond the default context used by REQ-384, single-instance behavior for unrelated file types, and deduplicating already-open drawing tabs.
- Smallest change: add matching-process handoff at application startup; read the active model VPORT and `AcDbVariableDictionary` / `DICTIONARYVAR` CANNOSCALE; use the existing context-manager data for multileader scale membership; preserve associated MTEXT rectangle dimensions and legacy hookline direction/length in the multileader path.

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change / algorithm the spec didn't specify?
  - [x] No — REQ-481 specifies process reuse; implement it in application startup with the platform mechanism that fits existing dependencies. DWG context merge stays in the IO owner.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Should the missing single-instance requirement be recorded and both bugs fixed? | 2026-11-02 | Add requirement and fix both (user confirmed). |

## 5. Assumptions
- None.

## 6. Plan
- Approach: add regression coverage first. At normal startup, acquire an instance guard scoped to the executable path; if another matching process owns it, find its editor window and forward the DWG path using the existing Windows native-window boundary. Handle the received path in the editor's existing native event path and queue the ordinary open action. Keep helper/test/crash modes outside the guard. In DWG IO, read `CANNOSCALE` from `AcDbVariableDictionary` and write it there for AutoCAD, while retaining GoSurvey's EED compatibility; resolve each MULTILEADER context manager's scale names into `CadMultileader::annotativeVisibleScaleNames` so scale membership survives import and export.
- Files/functions to touch: `spec/project.md`, `spec/requirements.md`, this task log; `src/qt/main.cpp`, `src/qt/EditorChrome.cpp` / `EditorWindow::nativeEvent` and a focused `src/platform` helper only if needed; `src/io/LibreDwgCad.cpp`; focused Qt and DWG tests, plus build wiring only if needed.
- Test approach: verify instance identity/forwarding and separate executable paths using the existing native-event test seam; verify ordinary launch behavior and diagnostic/test modes. For DWG, assert saved VPORT center/height/twist and `CANNOSCALE` restoration, round-trip native MLeader context-manager scale names and label dimensions, and test legacy LEADER MTEXT glyph height/hookline direction/size.
- Steps:
  - [x] Confirm this plan against REQ-384 / REQ-481, layer ownership, and current test seams.
  - [x] Add regression tests that distinguish the current behavior.
  - [x] Implement the process handoff and multileader context merge.
  - [x] Run the full required validation suite/checklists.
  - [ ] Present labeled viewport captures for user review; close only after approval.

## 7. Workflow-specific notes
- Bug: root causes = normal Windows launches created a second editor instead of forwarding the DWG; foreign DWGs did not restore the active model VPORT or CANNOSCALE; native MLeader contexts were not merged; and legacy LEADER + MTEXT import discarded attachment/rotation/rect dimensions and the synthetic DIMASZ hookline. Regression tests assert the saved view, annotation-scale/context data, label geometry, and hookline path.

## 8. Implementation log
- 2026-11-02 — User confirmed expected behavior for both bugs and authorized recording REQ-481 and fixing both.
- 2026-11-03 — Direct inspection of the attached MESSER DWG showed LRP 18 is a legacy `LEADER` + `MTEXT`: MTEXT attachment 6 (middle-right), X-axis direction `(0,1)`, rectangle `5.40643 × 1.02115`, and Standard DIMSTYLE `DIMASZ=0.2`; GoSurvey imported attachment 1 and arrow size 0.18. User directed that text/arrow appearance match Civil 3D, so REQ-367 was amended under D-2026-11-03-c to cover this specific legacy callout path.
- 2026-11-03 — DWG fields confirmed the missing landing: `hookline_on=1`, `hookline_dir=1`, and `x_direction=(0,1,0)`. AutoCAD documents hookline length as one DIMASZ, so this drawing's landing is 3.2 units at the saved annotative scale factor 16. Import now adds that segment to the leader path.
- 2026-11-03 — A native MLeader export test exposed stored height 2 being serialized as 6.25 because export used a nested annotation's default plotted height at 50 model units/inch. Export now writes `labelHeightWorld` when supplied; the test checks raw serialized and reimported dimensions.
- 2026-11-03 — A focused Qt regression exposed a renderer unit mismatch: GoSurvey's CAD text height is cap height, while the TTF path treated it as the font em size. TTF setup now calibrates the font pixel size against `QFontMetricsF::capHeight()`; the test failed before the change and passes after it.
- 2026-11-03 — The user's follow-up that all leader tails were wrong was reproduced in the actual MESSER DWG. The LEADER path is arrow-tip-to-text-landing, but the synthesized hookline outer endpoint was appended after the landing, making the arrow slope meet the text-side end instead of the elbow. The endpoint is now inserted immediately before the original landing. The focused regression failed before the fix and passed after; a hidden Qt capture of the actual drawing shows the hookline and sloped segment meeting at the elbow. User visual approval remains pending.
- 2026-11-03 — The user then identified that hookline direction must change with text side. Inspection found the MESSER leaders reuse one horizontal vector and hookline flag while MTEXT attachment columns vary left/right. Hookline direction now accounts for the attachment column and saved direction flag. A paired import regression covering left- and right-attached MTEXT failed on the shared direction and passed after this change; the regenerated saved-view capture shows both orientations in the actual drawing.
- 2026-11-03 — The user approved the final visual comparison. End-to-end Windows process test: with the worktree's GoSurvey window fully open, launching that same executable with a DWG path containing spaces exited the second process with code 0, opened the drawing in the first window, and left one matching process. A separate launch while the first process was still on its splash screen did start a second process. The current machine's `.dwg` association points to AutoCAD, so this used `Start-Process` with the GoSurvey executable and DWG argument rather than Explorer's configured association.

## 9. Self-verification
- [x] build-project — `build.bat` passed on the final source tree.
- [x] architecture-review — DWG decoding/render data stays in IO/domain/render owners; process forwarding stays in Qt/Windows startup.
- [x] code-review — verified saved scale/viewport fallback, hookline and context edge cases, and path-scoped process ownership.
- [x] dependency-audit — no dependency/build manifest changes
- [x] performance-review — no new per-frame work; import-only DWG reads and launch-only process enumeration
- [x] testing — final Release build passed; full Release CTest suite passed (191/191) after path ordering and attachment-side direction corrections; focused paired-alignment regression and hidden Qt capture pass.

## 10. Verification result
- Submitted: 2026-11-03 — final source tree, Release suite, and end-to-end Windows open test
- Verdict: PASS (user approved the final visual comparison)
- Findings: no blocking findings; see `verification/reviews/TASK-1109-implementation-review.md`.
- Pre-PR product documentation review: `README.md` — reviewed, no update needed; `PRIVACY.md` — reviewed, no update needed; `resources/wiki/` (including Files and Drawings and Import and Export) — reviewed, no update needed for this bug fix; public manual wiki — reviewed, the configured GitHub Wiki URL redirects to the repository, so no editable manual page was available and no product-documentation change was needed.

## 11. Outcome
- Requirements satisfied: REQ-055, REQ-367, REQ-384, and REQ-481 (implementation verified; final visual comparison approved)
- Tests added: saved DWG viewport, legacy leader text/hookline, raw MLeader export dimensions, context round trip, and Windows handoff coverage
- Docs updated: requirements, project decision log, and this task log
- Done: pending
