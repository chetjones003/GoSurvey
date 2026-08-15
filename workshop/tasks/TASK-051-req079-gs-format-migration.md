# TASK-051 — `.gs` format versioning and forward migration

- Type:    feature
- Status:  self-verify
- Opened:  2026-08-15
- Owner:   Workshop

## 1. Authority
- Goal:         backward compatibility for user drawings across releases
- Requirements: **REQ-079** (accepted 2026-08-15); supports REQ-078's breaking-change warning
- Constraints:  **ADR-030**; REQ-201 (no silent failures); REQ-301 (no speculative abstraction);
                architecture §11 invariants; REQ-200
- Acceptance:   restated from REQ-079 — current-version file loads unmigrated and resaves
                byte-identical; older file loads; migrations compose across several versions;
                newer file refused naming both versions; missing/zero/negative version refused;
                a failing step reports which step and loads nothing; every `samples/` file opens.
- Owning subsystem: IO.

## 2. Scope
- In scope: the version-range check, the migration chain and its tests, and the reader change.
- Out of scope: any actual format change (the table ships empty — `.gs` is still version 1);
  backward migration; repairing corrupt files.
- Smallest change: `.gs` already carried a version field. The work is making it *usable* —
  nothing new is stored in the file.

## 3. Architectural boundary check
- [x] No — proceed. ADR-030 was written and accepted before this task opened.
- The data-format change is the ADR's, not the Workshop's. Note the abstraction in ADR-030 (e)
  (passing the step table in) is justified by two present-day uses — production and tests — which
  is exactly the REQ-301 bar rather than a speculative seam.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Version + migrate, or keep flagging breaks and accept that old drawings stop opening? | 2026-08-15 | **Version + migrate.** "Open old drawings and the new version convert them"; flag only genuine, unmigratable breaks |

## 5. Assumptions
```
ASSUMPTION-1: Migrating the parsed JSON before the typed loader is sufficient for every change the
              format will realistically need.
- Because:       there are no migrations yet, so the design is unexercised by a real case.
- Risk if wrong: a future change needs data the JSON tree alone does not carry (e.g. something
                 derived at load), and the step has to be split or moved.
- Validate by:   the first real migration. Until then this is a designed-for case, not a proven one.
```

## 6. Plan
- `io/GsMigrate.*` pure module; reader accepts `<=` its own version and calls it; tests drive the
  chaining with synthetic tables.
- Test approach: happy = same-version no-op, one step, multi-step composition; failure = newer
  file, malformed version, gap in the chain, failing step.

## 7. Workflow-specific notes
- Feature: tests written against the composition logic specifically, because the shipped table is
  empty and testing only it would assert nothing.

## 8. Implementation log
- 2026-08-15 **Finding that reframed the task.** `.gs` has written `"version": 1` since the
  beginning, but the reader compared it with `!=`. Bumping `kGsFormatVersion` would have made
  **every existing drawing unopenable in both directions**, so the field was decorative and eleven
  changes across REQ-044…REQ-076 were routed around it with a "tolerant key, additive only, no
  version bump" workaround — each carrying a comment saying so. This task is less "add versioning"
  than "make the versioning that already exists usable before a non-additive change forces the
  issue."
- 2026-08-15 Reader now accepts `<= kGsFormatVersion` and migrates; refuses only newer files, with
  a message naming both versions and saying *newer version of GoSurvey* rather than "unsupported",
  because the latter sends people hunting for a damaged file.
- 2026-08-15 `GsMigrateTests`: **8 cases / 34 assertions, green.** The composition test is the one
  that matters — a v1 file reaching a v4 build runs 1→2→3→4 in order — since that is what must
  still hold in three years, and it is unreachable through the (empty) production table.
- 2026-08-15 Verified against a real drawing rather than only unit tests: `samples/surface-demo.gs`
  loads through the new path and renders its survey points and labels.
- 2026-08-15 **Unrelated pre-existing bug found while testing:** `int main()` takes no `argv`, so
  the application cannot receive a file path at all. The `.gs` association registered by the
  installer passes `"%1"`, which is silently ignored — double-clicking a drawing opens GoSurvey
  **empty**. Not caused by this task and not fixed by it; recorded as BUG-012.

## 9. Self-verification
- [x] build-project        — PASS (MSVC).
- [x] architecture-review  — PASS. New module is pure and sits in the owning subsystem; the one
      abstraction meets REQ-301 with two present-day uses.
- [x] code-review          — PASS. Every failure path reports which version pair failed (REQ-201);
      no path silently skips a step.
- [x] dependency-audit     — n-a (nlohmann already vendored).
- [x] performance-review   — n-a. Same-version load short-circuits before any work.
- [x] testing              — PASS. 8 new cases; full suite **317/317**.

## 10. Verification result
- Submitted:  2026-08-15
- Verdict:    <pending>
- Findings:   none outstanding

## 11. Outcome
- Requirements satisfied: REQ-079 — mechanism complete and tested; **unexercised by a real
  migration**, because none exists yet (ASSUMPTION-1).
- Tests added:            `tests/GsMigrateTests.cpp` — 8 cases, 34 assertions
- Docs updated:           REQ-079, ADR-030, decision log, this log
- Technical debt:         BUG-012 (`main()` ignores `argv`, so the `.gs` association is inert)
- Done:                   <pending>
