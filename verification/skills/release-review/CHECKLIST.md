# Checklist: release-review

> Mirrors `../../release-checklist.md`. Any unchecked correctness, data-safety, or
> authorization box = **HOLD**. Run only when shipping.

## Upstream gates (on the release commit)
- [ ] `build-project` PASS.
- [ ] `architecture-review` / `code-review` PASS; no open blocking findings.
- [ ] `dependency-audit` PASS.
- [ ] `performance-review` PASS (within budget).
- [ ] `testing` PASS (full suite green).

## Scope & correctness
- [ ] Every behavior traces to an `accepted` REQ; nothing out-of-scope (§4).
- [ ] Release matches the roadmap milestone.
- [ ] No open correctness or data-loss finding (never waivable).
- [ ] Data-format change versioned/compatible, or explicitly user-approved.

## Provenance
- [ ] Built from a tagged, clean, committed revision; reproducible (REQ-200).
- [ ] Version bumped; changelog/release notes complete (breaking changes listed).

## Dependencies & rollback
- [ ] Shipped dependencies recorded and license-compatible; none dev-only leaked.
- [ ] Rollback path exists; known issues documented.

## Authorization (mandatory)
- [ ] User explicitly authorized this exact scope now (who/when recorded).

## Verdict
```
RELEASE GATE — <version> — <date>
- Upstream: build ✓ review ✓ deps ✓ perf ✓ test ✓
- Scope traced: yes/NO   Correctness/data: clean/OPEN
- Provenance: ok/NO   Rollback: yes/NO
- User authorized: yes/NO (who/when: …)
- Outcome: CLEAR TO RELEASE | HOLD
```
