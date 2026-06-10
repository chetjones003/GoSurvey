# Checklist: architecture-review

> Domain 1 of `../../review-checklist.md` and `spec/architecture.md` §11. **Any**
> unchecked box is a blocking finding.

## Layering
- [ ] No new dependency points upward across layers (`Entities → Editor` = fail).
- [ ] Change lives in the subsystem that owns the concern.
- [ ] No subsystem doing another subsystem's job.

## Ownership
- [ ] Every new resource has exactly one visible owner.
- [ ] No new `shared_ptr`/`Rc`/`Arc` without recorded justification.

## State & data flow
- [ ] No new global mutable state or hidden singleton access.
- [ ] Data flow explicit, not summoned from globals.

## Abstraction
- [ ] Any new interface/trait/template/generic names ≥2 present-day uses (REQ-301).
- [ ] No speculative "for future flexibility" generalization (CON-06).
- [ ] A simpler concrete version was considered and rejected for a stated reason.

## Boundaries & reversibility
- [ ] No `gl*`/backend call outside Renderer/Platform.
- [ ] No previously-rejected approach reintroduced without a new decision (CON-03).

## Verdict
```
AUDIT VERDICT — <change id> — <date>
- Layering ✓/✗  Ownership ✓/✗  State ✓/✗  Abstraction ✓/✗  Boundaries ✓/✗
- Blocking findings: <ids>
- Outcome: PASS | FAIL | SPEC GAP
```
