# Checklist: dependency-audit

> Domain 3 of `../../review-checklist.md`. Run per added/upgraded dependency. Any
> "no" is blocking.

## Per dependency
- [ ] **Necessity:** cannot reasonably be implemented in-tree.
- [ ] **Recorded:** has a `project.md` decision-log entry with rationale (REQ-300).
- [ ] **Health:** actively maintained, not abandoned.
- [ ] **Cost:** acceptable build-time / binary-size / transitive-graph impact.
- [ ] **License:** compatible with `project.md` §1.
- [ ] **No duplication:** does not re-solve an already-covered problem.
- [ ] **Pinned:** version locked and lockfile committed.

## Summary table
```
| dependency | version | necessity | license | maintained | cost | decision |
|------------|---------|-----------|---------|------------|------|----------|
| <name>     | <ver>   | yes/no    | <lic>   | yes/no     | ok?  | allow/REJECT |
```

## Verdict
```
DEPENDENCY VERDICT — <change id> — <date>
- New/upgraded: <count>
- Rejected: <names or none>
- Unknown-license escalations: <names or none>
- Outcome: PASS | FAIL | SPEC GAP
```
