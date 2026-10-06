# TASK — Refresh DWG fidelity tracker (#601) after #688 and REQ-372

- Branch: `docs/issue601-tracker-refresh-688-624`
- Authority: GitHub issue #601, `docs/dwg-feature-gaps.md`
- Scope: Sync gap inventory and GitHub #601 checklist with `beta` after **#688** closed (REQ-384, PRs #704–#710) and **#624** partial (REQ-371, REQ-372 shipped; lights/sun SPEC GAP).
- Out of scope: Lights/sun implementation; closing parent #601 (blocked on #624 lights).

## Deliverables

- Update `docs/dwg-feature-gaps.md`: evidence commit, REQ-372 shipped, only lights/sun under #624.
- Update `workshop/tasks/ISSUE-601-body-2026-10-05.md` for `gh issue edit 601`.

## Verification

- Child issue states match `gh issue view` (#688 closed, #624 open).
- No SPEC changes.
