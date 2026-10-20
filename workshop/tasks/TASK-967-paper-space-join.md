# TASK-967 — Implement JOIN for paper-space selections

- Type: feature / bug fix
- Status: implementation complete; delivery pending
- Opened: 2026-10-20
- GitHub issue: #973

## 1. Authority

- Goal: `spec/project.md` currently has no GOAL-NN entries. The user previously accepted continuing under an existing requirement for the paper-space workstream; proceed under accepted REQ-039 and record this gap without inventing a goal.
- Requirement: REQ-039 (accepted; paper-space JOIN operates on the active paper selection).
- Constraints: CON-01; preserve the existing model-space JOIN behavior and its eligibility rules.
- Acceptance: consume `selectedPaperEntities` and the active layout stores only; join eligible connected paper lines/polylines into expected paper geometry; report unsupported or disconnected selections without silent loss; create one undoable active-layout-only edit; headless transcript coverage for resulting paper geometry, model-space isolation, and refusal/no-op cases.

## 2. Scope

- In scope: paper-space JOIN selection routing, paper geometry input/output, active-layout isolation, undo, accurate skip/no-op reporting, and headless transcript coverage.
- Out of scope: changing model-space JOIN semantics, adding new entity eligibility, ImGui support, unrelated paper-space modifiers.

## 3. Architecture check

- Follow ADR-014's active-space branching and the existing per-layout paper stores; do not introduce a cross-space entity abstraction or duplicate geometry engine.
- Reuse the existing JOIN edge walk via an isolated temporary state populated only from the active layout. Copy results back to that layout only.
- Complete the paper-space box-selection route in the shared command input path so headless `BOX` transcripts can produce the same active paper selection consumed by JOIN. Model-space selection logic remains unchanged.

## 4. Plan (before implementation)

- Trace `ExecuteJoinSelection`, paper store representation, undo snapshot restoration, existing JOIN acceptance rules, and headless transcript harness.
- Implement a paper-space branch that reads only valid selected paper references, runs the applicable connected-line/polyline join flow, and applies results to the active layout in one undoable operation.
- Report unsupported, invalid, disconnected, or insufficient selections clearly and preserve all unjoined input geometry.
- Add transcript scenarios for successful line and polyline joins, multi-layout isolation, undo, unsupported/disconnected no-op, and model-space isolation.
- Run focused headless transcript(s), Qt build, and complete CTest suite; inspect the diff for model-path changes.

## 5. Files / subsystems affected

- `src/commands/CadCommands.cpp` — active-paper box selection, JOIN adapter, JOIN prompt/selection flow.
- `tests/headless/HeadlessDriver.cpp` — exact paper polyline vertex assertions.
- `tests/headless/transcripts/issue973-paper-space-join.txt` — paper JOIN acceptance coverage.

## 6. Verification

- Happy path: joined connected lines and an existing paper polyline produce the expected paper geometry on the active layout.
- Failure/edge cases: unsupported and disconnected selections are reported and left intact; undo restores the pre-JOIN paper geometry; a second layout and model stores remain unchanged.
- Architectural boundary: no Qt/ImGui UI changes, new dependency, or new layer; model-space JOIN algorithm and behavior remain unchanged.

## 7. Implementation log

- 2026-10-20 — Plan reviewed against accepted REQ-039, CON-01, ADR-014 and issue #973. The missing GOAL entry is recorded as a known spec gap; no new product behavior beyond the issue's accepted scope is inferred.
- 2026-10-20 — Added the active-paper JOIN path through the existing JOIN edge walk. The adapter stages only the active paper layout's line/polyline geometry, produces one undo entry on success, and writes the resulting stores back only to that layout. Added active-paper box selection in `finishBox`, which was needed for the headless selection window to feed paper refs to JOIN.
- 2026-10-20 — Added `PAPERPOLYVERT` headless assertions and a transcript covering exact joined vertices, line+polyline input, a second unchanged paper layout, undo, disconnected lines, an unsupported circle, and unchanged model geometry.
- 2026-10-20 — Release build passed. Full Release CTest passed 2666/2666 (626.87 seconds); after extending the transcript with the two-layout and existing-polyline cases, the focused `headless.issue973-paper-space-join` test passed.
- 2026-10-20 — Standards review found no documented-standard violation; one localized duplicate-checking smell is advisory: `PAPERPOLYVERT` parallels `POLYVERT` with layout-specific access. Spec review's missing curved-segment metadata concern does not apply: `PaperLayout` has no polyline bulge/normal stores; paper polylines contain only vertices/closure, so there is no such metadata to preserve. The review's two-layout coverage gap was addressed by the updated transcript.

## 8. Self-verification

- [x] build-project — Release build passed.
- [x] architecture-review — uses ADR-014's existing active-space branching; no new layer or dependency.
- [x] code-review — no blocking standards/spec findings; localized duplicate assertion logic is advisory.
- [x] testing — full Release CTest passed 2666/2666; final focused transcript passed after its last coverage update.

## 9. Verification result

- Verdict: PASS for the implementation and current verification. Changes remain uncommitted and have not been opened as a PR.
