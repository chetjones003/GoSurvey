# TASK-247 — Start Screen Billboard (What's New)

- Type:    feature
- Status:  self-verify
- Opened:  2026-09-10
- Owner:   workshop

## 1. Authority
- Goal:         surface release notes for the installed version
- Requirements: REQ-336 (accepted, D-2026-09-10-d, ADR-056)
- Constraints:  REQ-300 (md4c vendored), REQ-301 (no speculative abstraction)
- Acceptance:   see REQ-336 Acceptance in spec/requirements.md
- Owning subsystem: UI / IO / Build (per REQ-336 Owner-layer)

## 2. Scope
- In scope: billboard window, Help→About, dismiss prefs, md4c + ImGui layer, CI gate, authoring lock
- Out of scope: fetching notes from GitHub; second About dialog; markdown images
- Smallest change: as planned in Verification

## 3. Architectural boundary check
- [x] Yes — new dependency md4c — **already recorded** as ADR-056 / D-2026-09-10-d before Workshop.
- No further architectural decisions by Workshop.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| (interview) | content source, About, URL, markdown, lock | 2026-09-10 | recorded in D-2026-09-10-d |

## 5. Assumptions
ASSUMPTION-1: starter resources/whats-new.md is placeholder until Chet writes real notes
- Because: no release notes text was supplied
- Risk if wrong: users see generic text until the file is updated
- Validate by: Chet reviews and unlocks before push of real notes

## 6. Plan
- Approach: vendor md4c; pure dismiss logic + tests; load markdown; ImGui window; menu; CMake/CI; hooks/rule
- Steps: done

## 8. Implementation log
- 2026-09-10: SPEC + ADR accepted; Workshop implementation on feat/start-screen-billboard
- 2026-09-10: build green; WhatsNewLogicTests 5/5; resource copy + CI gates + authoring lock landed

## 9. Self-verification
- [x] build-project        — PASS
- [x] architecture-review  — PASS (ADR-056)
- [x] code-review          — PASS
- [x] dependency-audit     — PASS (md4c VENDORED.md)
- [x] performance-review   — n/a
- [x] testing              — PASS (5 cases / 11 assertions)

## 10. Verification result
- Submitted:  2026-09-10
- Verdict:    PASS (self-verify; GUI manual pass pending)
- Findings:   none blocking

## 11. Outcome
- Requirements satisfied: REQ-336 (implementation + CI + lock; real notes text + GUI pass pending user)
- Tests added: WhatsNewLogicTests [req336]
- Docs updated: requirements.md, project.md, architecture.md (ADR-056), roadmap.md
- Done: pending user GUI check and review of resources/whats-new.md before unlocking that file for push
