# Verification Skills

A set of independent, single-responsibility skills that together act as a
professional software-engineering review team. Each skill is a specialist the
Verification Layer (`../verification.md`) can invoke on its own or in sequence.

## The team

| Skill | Specialist role | Backs |
|-------|-----------------|-------|
| `build-project` | Build engineer | `../build-checklist.md` |
| `testing` | QA / test engineer | `../testing-checklist.md` |
| `debugging` | Diagnostic engineer | correctness findings |
| `code-review` | Senior code reviewer | `../review-checklist.md` Domain 2 |
| `architecture-review` | Software architect | `../review-checklist.md` Domain 1 |
| `performance-review` | Performance engineer | `../review-checklist.md` Domain 4 |
| `dependency-audit` | Supply-chain reviewer | `../review-checklist.md` Domain 3 |
| `release-review` | Release manager | `../release-checklist.md` |

## Each skill ships three files

- **`SKILL.md`** — responsibility, inputs, outputs, success/failure criteria,
  questions it asks, what it does under uncertainty, and how it operates
  independently.
- **`PROMPT.md`** — the operating prompt: the persona and procedure the skill
  adopts when invoked.
- **`CHECKLIST.md`** — the concrete checks and the pass/fail verdict block.

## How they work together

Authority flows from the spec; each skill judges only against `spec/` and never
invents requirements. A typical full review runs them in this order, each gating
the next:

```
build-project ──▶ architecture-review ──▶ code-review ──▶ dependency-audit
      │                                         │
      │                              (debugging assists on any
      │                               correctness finding)
      ▼                                         ▼
   testing ──────────────────────────▶ performance-review ──▶ release-review
```

- Any skill can run **standalone** — each defines its own inputs, gate, and
  verdict, so it does not depend on the others having run first (it will note a
  missing upstream gate rather than assume it).
- Every skill emits the same verdict vocabulary — **PASS / FAIL / SPEC GAP** —
  and the shared finding format from `../verification.md` §5, so their outputs
  compose into a single review record.
- All escalation (SPEC GAP, STANDOFF, RISK ACCEPTANCE) and "must ask the user"
  rules are inherited from `../verification.md` §6–§7.

## Shared contract (all skills)

- **Judge against the spec, not taste.** Every finding cites a `REQ`/`CON`/
  invariant/standard. A finding with no spec citation is invalid → downgrade to
  advisory or escalate as SPEC GAP.
- **Be specific.** Findings name file:line, the violated item, the observed
  behavior, and the concrete fix.
- **Empowered to reject.** Each skill may return FAIL; correctness and safety
  findings are never waivable.
- **Ask, don't guess.** When the spec is ambiguous or the call is the user's,
  stop and ask (`../verification.md` §7).
