# TASK-1039 — Specify and verify the node-based measurement editor

- Type: feature
- Status: implementation in progress on `feat/node-measurement-editor-1059`
- Opened: 2026-10-22
- Owner: Codex

## 1. Authority
- Goal: make traverse measurement networks visually understandable and editable; project goal identifier to be confirmed against `spec/project.md` before implementation.
- Requirements: REQ-452 (accepted), with REQ-403, REQ-419..423 and REQ-431 as related accepted requirements.
- Constraints: existing downward-only architecture; preserve old `.gstrav` readability; original raw source files remain unchanged.
- Acceptance: see REQ-452 in `spec/requirements.md`.
- Owning subsystem: Qt UI, Commands, Traverse Domain, and traverse IO, each retaining the responsibility stated in `spec/architecture.md`.

## 2. Scope
- In scope: implement REQ-452 across the command language, traverse domain, traverse IO, and Qt adjustment workspace. GitHub implementation tickets: #1061–#1064.
- Out of scope: adding a graph-library dependency or changing original raw source files.
- Smallest change: add RAWSET and metadata support to existing command/domain/IO owners, then project one shared model into the Qt graph and text editor.

## 3. Architectural boundary check
- Yes — public GSTC grammar and `.gsdat`/`.gstrav` data-format changes are architectural decisions. D-2026-10-22-j and REQ-452 now record the user-selected RAWSET syntax, attached metadata, ordering rule, and backward-compatibility requirement. These are approved spec decisions; no new dependency, layer, or global state is authorized.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|---|---|---|
| Q1 | What exact record shape should preserve each F1/F2 set and provenance in GSTC text? | 2026-10-22 | One `RAWSET` record per set with optional F1/F2 tuples and a source ID. |
| Q2 | How should layout metadata comments identify commands across edits and reordering? | 2026-10-22 | Stable opaque ID and layout comment immediately precede and travel with each command; group and source mappings use separate parse-ignored comments. |
| Q3 | How should otherwise unrelated commands be ordered, and where should manual overrides persist? | 2026-10-22 | Stable source order breaks ties after dependencies; manual overrides are scoped to ambiguous/cyclic portions and saved in metadata. |

## 5. Assumptions
None. All public format decisions are recorded in D-2026-10-22-j and REQ-452.

## 6. Plan / Verification
- Approach: assess REQ-452 against the existing GSTC parser, `TraverseMeasSet`, `.gstrav` version 1 serializer, `.gsdat` persistence, and the Qt adjustment workspace. Keep UI interaction in Qt, parsing/ordering in Commands, raw invariants in Domain, persistence in IO.
- Files/functions to touch during implementation (not authorized yet): `TraverseCommandLang.*`, traverse domain types, `TraverseIo.*`, `.gsdat` IO, `AdjustmentWindow.*` and focused tests.
- Required tests: one set with F1 only, F2 only, both, and neither (the neither case must be rejected); multiple files with overlapping station pairs and distinct provenance; incomplete/malformed/non-finite RAWSET values and unknown source IDs; text/graph edits both ways; invalid text retains the stale graph; stable deterministic order and ambiguous/cyclic warnings; manual override round-trip; `.gsdat` metadata ignored by measurement parsing; `.gstrav` v1 load and new-format save/reopen of raw sets and layout.
- Steps:
  - [x] Record user-confirmed feature brief as proposed REQ-452.
  - [x] Review architecture, current command grammar, raw-set model, and save formats.
  - [x] Resolve public record and metadata association contracts with user.
  - [x] Re-run verification against the completed data contracts.
  - [x] Obtain user acceptance of REQ-452 and Verification APPROVE before opening Workshop implementation.
  - [x] Create dependency graph: #1061 RAWSET/domain/order; #1062 `.gsdat`/`.gstrav` persistence after #1061; #1063 Qt graph editor after #1061 and #1062; #1064 Qt/IO acceptance after #1063.
  - [x] Create integration branch from the open #1060 feature branch so this work remains stacked on its prerequisite.
  - [ ] Implement #1061 with TDD; merge to integration branch.
  - [ ] Implement #1062 and #1063 as their dependencies clear; merge each contribution to the integration branch.
  - [ ] Complete #1064, build/test, capture UI states, and request visual review.
  - [ ] Run standards/spec code review and resolve findings; update traceability and task log.

## 7. Verification result
- Architectural impact: substantial but contained within existing layers; adds command grammar, projection/order logic, raw-source provenance representation, and layout persistence. No new dependency is needed or proposed.
- Risks: F1/F2 readings or source identity lost during conversion; metadata detached from nodes after edits; `.gstrav` backward incompatibility; comments accidentally parsed as commands; ambiguous dependency ordering; UI scale/performance for large raw imports; inconsistent two-way updates.
- Verification verdict: **APPROVE**.
- Domains: architecture ✓; code quality plan ✓; dependencies ✓ (none proposed); performance plan ✓ (large raw sets require a scale test before claiming performance); format compatibility ✓.
- Findings: none blocking. The previous specification gaps are resolved by user decision D-2026-10-22-j and accepted REQ-452.
- Required implementation evidence: satisfy the parser/domain/IO and Qt tests above; run the Release build and relevant CTest suites per CLAUDE.md; capture affected Qt surfaces and present them for user review before completion.
- Verification was completed before implementation. Workshop implementation began 2026-10-22 on the integration branch; acceptance review and implementation results remain pending.

## 8. Outcome
- Requirements satisfied: REQ-452 accepted; implementation has not started.
- Tests run: none; this is a specification/verification phase.
- Docs updated: `spec/requirements.md`, `spec/project.md`, this task log.
- Done: 2026-10-22 (specification and verification complete).
