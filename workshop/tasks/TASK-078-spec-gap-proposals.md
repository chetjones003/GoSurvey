# SPEC GAP proposals — TASK-0.7.8 items 8 and 9

- Status: accepted in `spec/requirements.md` (REQ-458, REQ-399 amendment D-2026-10-29-h)
- Parent: `workshop/tasks/TASK-0.7.8-interaction-bug-batch.md`

## Item 8 — Model viewport zoom status bar (proposed REQ-458)

**Problem:** Task batch item 8 needs an accepted requirement. **REQ-456 is already assigned**
(paper-space LEADER/SPLINE/POINT/… parity, issue #1019).

**User direction already captured (2026-10-29):**

- Ratio presets (e.g. 1:50), plus dialog to add custom ratios.
- Custom zoom scales persist with the drawing via DWG/DXF (not `.gs`).
- Zoom scale list is separate from annotation scales.

**Proposal for SPEC (outline only):**

- Statement: status-bar control for **model-space** editor viewport zoom ratio; distinct from plot scale
  and paper viewport model-to-paper scale (REQ-027).
- Acceptance: preset list, add-custom dialog validation, persistence round-trip, no effect on annotation
  scale list.

**Recommendation:** Accept as **REQ-458** with a decision entry referencing this task and user
2026-10-29 notes; then implement in a follow-up PR.

## Item 9 — PDF table mixed-validity rows (REQ-399 amendment)

**Problem:** Mixed tables drop incomplete rows in `TryParseTable` without showing them in review.
REQ-399 clause 4 treats unparsed **regions**; it does not require listing skipped **rows** beside valid
candidates.

**User direction (2026-10-29):** Valid rows stay reviewable; invalid/incomplete rows reported separately;
good rows must not be invalidated.

**Proposal for SPEC amendment:**

- Extend REQ-399 review (clause 5): for table extractions, rows that parse as table body but lack both
  coordinates (or fail numeric parse) appear in a separate **skipped rows** list with source text and
  reason; valid rows remain in the candidate list unchanged.

**Recommendation:** Record decision amending REQ-399 acceptance with `[req399]` tests for mixed table +
  footer/note row; then extend `ExtractResult` / Qt review panel.
