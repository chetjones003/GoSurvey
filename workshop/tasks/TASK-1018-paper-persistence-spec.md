# TASK-1018 — Restate paper-space persistence in the SPEC (DWG/DXF, not .gs)

- Type:    docs
- Status:  submitted
- Opened:  2026-10-28
- Owner:   Claude (implement-issue)

## 1. Authority
- Goal:         none specific; the issue is a SPEC accuracy fix (audit #751 O3)
- Requirements: REQ-113 (accepted; DXF/DWG paper-space content, persistence in DWG and DXF)
- Decisions:    D-2026-10-20-a (paper-space persistence restated against DWG and DXF);
                D-2026-09-03-h (`.gs` retired as a standalone format)
- Constraints:  spec/ is the source of truth; no behaviour change
- Acceptance:   the SPEC describes DWG and DXF persistence for paper space, with `.gs` removed
                (GitHub issue #1018, audit #751 O3)
- Owning subsystem: spec only (no code)

## 2. Scope
- In scope:        current-requirement text in REQ-031, REQ-037, REQ-038, REQ-039, REQ-046,
                   REQ-061, REQ-371 (requirements.md); stale paper-persistence wording in the
                   architecture decisions that had no addendum (architecture.md)
- Out of scope:    REQ-022/023/etc. (model-space and survey .gs wording); the historical
                   decision bodies that already carry a 2026-10-20 addendum; traceability
                   evidence names (test names still say `.gs` and still exercise the JSON
                   trailer inside the DWG, ADR-044); the public wiki line about the `.gs` drawing
- Smallest change: reword the persistence clauses to DWG/DXF and add revision lines; no
                   acceptance criterion is removed

## 3. Architectural boundary check
- [x] No — documentation only. No new abstraction, dependency, or data-format change.

## 4. Questions
None. The decision was already recorded (D-2026-10-20-a); this task applies it.

## 5. Assumptions
ASSUMPTION-1: The `.gs` JSON schema still runs inside the `.dwg` as the ADR-044 trailer, so
"persisted in the DWG file" is accurate for the code as it stands.
- Because:       D-2026-09-03-h says the JSON schema is "unaffected and unretired".
- Risk if wrong: the spec would name DWG where the code still writes a separate `.gs` file.
- Validate by:   the existing save/reopen tests, which cover the trailer inside `.dwg`.

## 6. Documentation review (REQ-453)
- README.md — reviewed; no paper persistence statement; no update needed.
- PRIVACY.md — reviewed; no update needed.
- In-app help wiki (`resources/wiki/`) — reviewed; no paper `.gs` persistence claim; no update needed.
- Public GitHub wiki — reviewed; no paper persistence claim changed by this spec-only fix.
  Pre-existing line outside this issue's scope: Settings-and-Options.md lists layouts under
  "The `.gs` drawing". Left for the `.gs` retirement work.

## 7. Verification
- Spec-only change. No code compiled or tested. Checked by re-scanning the paper-space passages
  for remaining `.gs` / "DXF deferred" wording; the remaining hits are dated addenda or historical
  decision bodies, and REQ-113 supersedes them.
- Line endings unchanged (LF, matching `origin/beta`).
