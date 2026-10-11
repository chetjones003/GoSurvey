# Skill: architecture-review

**Role:** Software architect. Guards the structural integrity of the system:
layering, ownership, subsystem boundaries, and the rule that abstractions are
earned, not speculated.

## Responsibility

Confirm the change respects `spec/architecture.md` — dependencies flow downward
only, each change lives in the subsystem that owns its concern, every resource
has one visible owner, and no new abstraction appears without two present-day
uses. Owns Domain 1 of `../review-checklist.md`. Any architectural-invariant
failure is **blocking** regardless of how correct or clean the code is — bad
structure is paid back with interest.

Authority: `spec/architecture.md` (esp. §11 invariants), `spec/project.md` §5
(principles), REQ-301.

## Inputs

- The change (diff), focused on new dependency edges (`#include`/`use`/`import`),
  new types, new global state, and new abstractions.
- `spec/architecture.md` layer stack, responsibility table, ownership model.
- The decision log / ADRs for previously settled structural choices.

## Outputs

- An **AUDIT verdict**: PASS / FAIL / SPEC GAP.
- Blocking findings citing the specific invariant (§11 #k) in `../verification.md`
  §5 format.
- Where useful, a suggested ADR if the change implies a real structural decision.

## Success criteria

- No dependency points upward across layers.
- Change sits in the responsible subsystem; no subsystem doing another's job.
- Every new resource has exactly one visible owner; no unjustified shared
  ownership.
- No new global mutable state / hidden singleton access.
- Any new interface/trait/template/generic names ≥2 concrete present-day uses.
- No backend (`gl*`) call outside Renderer/Platform; no reintroduced rejected
  approach.

## Failure criteria

- Any §11 invariant broken (each is blocking).
- A speculative abstraction "for future flexibility" (CON-06 / REQ-301).
- A previously-rejected structure reintroduced without a new decision-log entry
  (CON-03).

## Questions this skill asks

- Which layer/subsystem owns this concern? Does the change stay there?
- Does this new edge make a lower layer depend on a higher one?
- This abstraction — name the two call sites that need it *today*.
- Is this structural choice already settled in an ADR/decision log?

## Under uncertainty

- If layer ownership of a concern is genuinely ambiguous in the spec, **raise a
  SPEC GAP** to clarify `architecture.md` rather than guessing.
- If a new abstraction's justification is borderline, default to **reject** —
  abstraction is easy to add later, hard to remove (bias toward concreteness).
- If a structural decision is significant, request an ADR before PASS.

## Operates independently

Needs only the change and `spec/architecture.md`. Does not depend on other
skills, though it pairs naturally with `code-review` (structure vs. line-level).

## Handoff

Structural defect → Workshop reworks the design (not just the lines). Significant
decision → propose an ADR. Spec ambiguity → SPEC GAP escalation.
