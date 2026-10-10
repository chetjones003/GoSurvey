# TASK — Projects P0: SPEC (issue #696)

- Branch: `docs/issue696-projects-spec`
- Authority: GitHub #696 decisions 1–14; **REQ-373…REQ-383**, ADR-065 (proposed), D-2026-10-05-d/-e
- Scope: spec only. **No code.** Files: `spec/requirements.md`, `spec/architecture.md`,
  `spec/project.md`, `spec/roadmap.md`.
- Architectural-boundary check: no code or layer change; ADR-065 proposes a fourth isolation boundary
  (project-owned point database) for the user to accept before P3.

## Phase → REQ map

| Phase | REQ |
|-------|-----|
| P1 foundation, lock | REQ-373, REQ-374, REQ-382 |
| P2 settings | REQ-375 |
| P3 point database | REQ-376 (+ ADR-065) |
| P4 toolspace, rules | REQ-377 |
| P5 add drawing | REQ-378 |
| P6 file tracking, health | REQ-379 |
| P7 pack | REQ-380 |
| P8 turnovers | REQ-381 |
| P9 warnings | REQ-383 |

## Open points (raised, not guessed)

- ADR-065 is **proposed**; P3 is gated on acceptance.
- "Stale lock" rule: fixed in P1 (REQ-382 revision).
- `.gspack` container format: chosen in P7 under REQ-300.
- P8 turnover contents beyond contents/date/recipient: SPEC GAP at P8 if needed.

## Verification

Spec-only; checked against the issue's decision table, rules, UI and acceptance sections.
