# TASK-1410 — Inventory current update behaviour

- Type:    docs
- Status:  done
- Opened:  2026-11-18
- Owner:   Codex

## 1. Authority
- Goal:         Parent issue #1403, user story 56 (preserve the current updater's user-facing behavior in the Rust/Tauri port)
- Requirements: REQ-077, REQ-078 (accepted)
- Constraints:  None specific to this inventory
- Acceptance:   A list of what the current update code does
- Owning subsystem: `update/` decision and orchestration; Platform HTTPS/download/install; Qt UI and UserPrefs

## 2. Scope
- In scope:        Describe current startup check, decision rules, offer, settings, download/hash, user consent, install/migration, and failures; cite source files; identify observable differences from REQ-077/078.
- Out of scope:    Change updater behavior, change the SPEC, or assert that unexercised production paths have passed end-to-end.
- Smallest change: Add a focused inventory in `docs/` and record its evidence and review here.

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — documentation only.
    - [ ] Yes → STOP.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| — | None; the issue explicitly requests an inventory of existing behavior. | — | — |

## 5. Assumptions
- This is a factual inventory of the checked-out implementation, not a claim that every listed path has been verified against a live release.
- Because: issue #1410 asks what the current update code does.
- Risk if wrong: readers could mistake an implementation detail for an accepted product requirement.
- Validate by: cross-check statements against source and distinguish code behavior from REQ-077/078.

## 6. Plan
- Approach: Trace the update path from startup and settings through pure decision logic, network/download helpers, UI choices, persistence, and installer handoff; document behavior and coverage limits.
- Files/functions to touch: `docs/update-behavior-inventory.md`; this task log.
- Test approach: Documentation-only; verify each statement against cited source locations and review the diff. No runtime tests are needed to inventory code.
- Steps:
  - [x] Record authority, scope, and documentation plan.
  - [x] Verify plan against accepted requirements and architecture.
  - [x] Write the code-grounded behavior inventory.
  - [x] Self-review source references and commit.

## 7. Workflow-specific notes
- Docs: every behavior statement will be cross-checked against current code; code/spec differences and untested paths will be labeled.

## 8. Implementation log
- 2026-11-18 — Issue #1410 fetched; title confirmed. Identified accepted REQ-077/078 and parent #1403's updater-parity question. No UI or code change planned.
- 2026-11-18 — Plan verified: REQ-077/078 define the existing updater surface and ADR-029 assigns decision logic, transport, and UI to the modules listed above. The requested inventory stays inside those boundaries and makes implementation/spec differences visible.
- 2026-11-18 — Added `docs/update-behavior-inventory.md`. Cross-checked startup and settings paths, decision/parser rules, transport and hash behavior, installer handoff, and pure tests against source. Documented the splash/dialog difference, timeout shape, and stale Not Now reuse comment. `git diff --check` passed; no code changed and no runtime tests were run.

## 9. Self-verification
- [ ] build-project        — n/a (documentation only)
- [ ] architecture-review  — n/a (no architectural change)
- [x] code-review          — PASS (manual source-to-inventory review; no code changed)
- [ ] dependency-audit     — n/a
- [ ] performance-review   — n/a
- [ ] testing              — n/a (no code changed)

## 10. Verification result
- Submitted:  2026-11-18
- Verdict:    PASS
- Findings:   None. Documentation-only change; source references and implementation/spec distinctions reviewed.

## 11. Outcome
- Requirements satisfied: REQ-077 / REQ-078 inventory (Acceptance met: yes; the requested list is in `docs/update-behavior-inventory.md`)
- Tests added:            none
- Refactors:              none
- Docs updated:           `docs/update-behavior-inventory.md`, this task log
- Done:                   2026-11-18
