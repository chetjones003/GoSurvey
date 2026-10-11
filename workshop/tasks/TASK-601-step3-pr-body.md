## Summary

- Updates `docs/dwg-feature-gaps.md` after **#617** (PR #677), **#618** (PR #678), and **#622** (closed, PR #689 doc pass) shipped on `beta`.
- Only **#688** and **#624** remain under parent tracker **#601**.

## Test plan

- [x] Issue states verified via `gh issue view` (#617 open → close after merge; #618/#622 closed).
- [x] Doc links and PR numbers match merged history.
- [ ] After merge: run `./dev/gh issue edit 601 --body-file workshop/tasks/ISSUE-601-body-2026-10-05.md` and close **#617**.

## Acceptance (#601 tracker refresh)

- [x] Gap inventory matches `beta` child issue states.
- [ ] GitHub #601 body synced (post-merge step in test plan).
