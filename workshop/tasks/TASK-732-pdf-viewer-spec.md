# TASK-732 — Specify the built-in PDF viewer (issue #732, phase 0)

- Type:    docs
- Status:  submitted
- Opened:  2026-10-06
- Owner:   Claude

## 1. Authority
- Requirements: REQ-387, REQ-388, REQ-389 (written by this task; accepted with D-2026-10-06-a)
- Decision: D-2026-10-06-a; architecture ADR-067
- Owning subsystem: SPEC only (no code)

## 2. Scope
- In scope: the three REQs with acceptance criteria and the performance target, ADR-067, the decision
  record, the roadmap entry.
- Out of scope: any code. Phase 1 (viewer), 2 (annotations), 3 (split) each get their own task and PR
  after this spec PR is merged.

## 3. Architectural boundary check
- Architecturally significant choices (annotation storage, overwrite policy, window kind, threading)
  were put to the user and approved 2026-10-06 (see D-2026-10-06-a).

## 4. Assumptions
- "New window" is read as a floating, dockable GoSurvey window (ADR-067 (a)), not a second OS window.
  The user should say if a true second OS window is wanted; that would change ADR-067 (a).
- 250 ms first page and the 256 MB cache default are proposals to be measured by `BENCH PDFVIEW`.

## 5. Next tasks
- TASK-732-p1 viewer + routing + cache + bench (REQ-387)
- TASK-732-p2 annotations (REQ-388)
- TASK-732-p3 split (REQ-389)
