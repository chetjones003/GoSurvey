# Release Checklist

> The final gate, run **only when shipping** a build to users. A release is an
> outward-facing, hard-to-reverse action — the highest-confidence verdict the
> Verification Layer issues. Releasing **requires explicit user confirmation**
> (`verification.md` §7.6); Verification never ships on its own initiative.

Authority: all of `spec/`, plus the verdicts of `build-`, `review-`, and
`testing-checklist.md`.

---

## Pass/fail rule

> **PASS (clear to release)** only if all upstream gates passed, every shipped
> behavior traces to a satisfied requirement, no known correctness/safety
> finding is open, and the user has explicitly authorized the release.

---

## 1. Upstream gates

- [ ] `build-checklist.md` — PASS on the exact release commit.
- [ ] `review-checklist.md` — PASS; no open **blocking** findings.
- [ ] `testing-checklist.md` — PASS; full suite green on the release commit.
- [ ] All advisory findings are either resolved or recorded as accepted with a
      tracking task.

## 2. Requirement & scope confirmation

- [ ] Every behavior in this release traces to an `accepted` `REQ-NNN`.
- [ ] Nothing out-of-scope (`spec/project.md` §4) snuck into the release.
- [ ] The release's contents match what the roadmap milestone claimed
      (`spec/roadmap.md`).

## 3. Correctness & data safety

- [ ] No open correctness or data-loss finding (these are **never** waivable).
- [ ] Data-format / round-trip behavior is unchanged, or a format change is
      versioned and backward-compatible — or its incompatibility is explicitly
      documented and user-approved (`verification.md` §7.6).
- [ ] Migration path exists and is tested if persisted data layout changed.

## 4. Versioning & provenance

- [ ] Version number bumped per the project's scheme (semver or stated scheme).
- [ ] Release is built from a tagged, committed, clean revision — not a dirty
      working tree.
- [ ] Build is reproducible from the tag (REQ-200).
- [ ] Changelog/release notes list user-visible changes and any breaking change.

## 5. Dependencies & licensing

- [ ] All shipped dependencies are recorded (`project.md` decision log) and
      license-compatible.
- [ ] No unpinned or development-only dependency leaked into the release build.
- [ ] Third-party license/attribution notices included if required.

## 6. Performance & footprint

- [ ] Release build meets the performance budgets in `spec/requirements.md` on
      reference hardware.
- [ ] Binary size / startup time within expected range; no surprise regression.

## 7. Rollback & risk

- [ ] A rollback path exists (previous release retained / revert plan written).
- [ ] Known issues for this release are documented.
- [ ] Any shipped-with-known-finding item carries a user-approved risk-acceptance
      waiver with an expiry and follow-up task (`verification.md` §6.3).

## 8. Final human authorization *(mandatory)*

- [ ] The user has been shown the release scope and explicitly authorized
      shipping. **Verification must ask, not assume** (`verification.md` §7.6).
- [ ] Time/owner of authorization recorded.

---

## Verdict

```
RELEASE GATE — <version> — <date>
- Upstream gates:     build ✓  review ✓  test ✓
- Scope traced:       yes / NO
- Correctness/data:   clean / OPEN FINDING (blocks)
- Versioning:         done / NO
- Dependencies:       clean / NO
- Performance:        within budget / REGRESSION
- Rollback ready:     yes / NO
- User authorized:    yes / NO  (who/when: …)
- Outcome:            CLEAR TO RELEASE | HOLD
```

A single unchecked correctness, data-safety, or authorization box = **HOLD**.
Verification holds the release and reports exactly which box failed. Schedule and
deadline pressure are not grounds to override a HOLD — escalate via
`verification.md` §6.2 (standoff) or §6.3 (waiver), both of which require a human
decision.
