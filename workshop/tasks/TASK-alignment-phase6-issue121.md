# TASK-alignment-phase6-issue121 — Alignment design criteria and checks (REQ-502, increment 6)

- Type:    feature
- Status:  in review (PR #1152)
- Opened:  2026-11-04
- Owner:   chetjones003 (decision), Claude (implementation)
- GitHub:  #121 "Implement Civil 3D-Style Alignments"
- Branch:  feat/implement-issue-121 (from origin/beta)
- Predecessor: TASK-alignment-phase1-issue121.md (increments 1–5)

## 1. Authority
- Requirement: **REQ-502** (accepted 2026-11-02). Phasing per the REQ-480..503 block: Phase 6 = REQ-502.
- Also touched: REQ-491 (style, design check set — see §6), REQ-494 (report findings), REQ-485 (layout Constraint
  column — see §6), REQ-501 (Properties row).
- Acceptance (REQ-502):
  1. R=250 with a minimum-radius check of 400 produces the warning and the alignment still saves.
  2. A station with two speeds uses the higher one for validation.
  3. A Miscellaneous alignment refuses a road criteria file; a Rail alignment refuses a road file.
- Owner layer: Domain (`src/alignment/AlignmentDesign.*`), Commands (`CadCommands_Alignment.cpp`), IO (`GsIo.cpp`), Qt.

## 2. What the spec requires (REQ-502 statement) and where it lands

| Requirement | Where | Status |
| --- | --- | --- |
| Design speeds, one per station; entity validated at the highest speed | `AlignmentDesign.cpp`, `ALIGNMENTSPEED` | done (commit db472b8f) |
| Named drawing-owned check sets (radius table, min tangent, min/max A, min spiral length) | `AlignmentCheckSet`, `ALIGNMENTCHECKSET` | done (db472b8f) |
| Check always true/false; never blocks editing | `CheckDesign` returns findings only | done |
| Failure shown in the layout table | `EditorAlignmentLayout.cpp`, "Design check" column | done (this increment) |
| Failure shown in `LIST` | `RunAlignmentList`, `WARNING: design check` lines | done (this increment) |
| Failure shown in the report | `AlignmentReport.cpp`, findings block | done (db472b8f) |
| Failure shown on the drawing as a flagged marker | `QtAnnotations.cpp`, red flag per failing entity | done (this increment); visual check pending |
| Superelevation and sight distance: data fields and report placeholders, no calculation | `maxSuperelevation`, `minSightDistance`, report lines | done (this increment) |
| Compound spiral length not validated | `CheckDesign` skips compound spirals | done (db472b8f) |
| Rail uses separate criteria; Miscellaneous takes none | `CriteriaAllowed` | done (db472b8f) |
| No regional table shipped | — | done (none shipped) |

## 3. Implementation approach
- Domain: `FindCheckSet` (case-insensitive) is the single lookup used by the commands, the layout table and the viewport.
  Checks run on demand; no cached findings in the document.
- Persistence: `maxSuperelevation` and `minSightDistance` added to each check set in `GsIo` (saved only as values; old
  files load them as 0). The check set equality used by the round-trip test includes them.
- Viewport: a flag is drawn at each failing entity's midpoint, height 4 drawing units, red `#c0392b`. Drawn regardless of
  the style's visibility settings, because a design failure must not be hidden by a style. Cost: `CheckDesign` runs once per
  frame per alignment that has a check set. This is an O(entities × speeds) scan per frame; acceptable for current sizes,
  but it is the per-frame cost the viewport-hover memory note warns about. Revisit if a large alignment shows lag.

## 4. Test approach
- `tests/AlignmentDesignTests.cpp` (tag `[req502]`):
  - `FindCheckSet` capitalisation and empty-name cases.
  - `ALIGNMENTLIST` emits the exact warning text; `ALIGNMENTREPORT` emits both stub lines.
  - Stub values and the full check set survive a save and reload.
  - Earlier cases still cover acceptance 1, 2 and 3 and the radius-table rule, persistence, deletion and missing-set load.
- Results: `[alignment]` 141 cases / 3795 assertions pass (Release). Full ctest: see §5.

## 5. Evidence
- Build: `build.bat` Release exit 0 (Qt app and test binaries built, including the layout and viewport changes).
- Full suite: see the completion record below.

## 6. Open items and gaps

Closed in the 2026-11-04 follow-up (decisions D-2026-11-04-a/b, user choices for the line check):
- Style design-check set (REQ-491): `AlignmentStyle.checkSetName`, `ALIGNMENTSTYLE SET, <style>, CHECKSET, <set>|NONE`,
  and the Design tab combo (`alignmentStyleCheckSet`). Effective criteria = the alignment's own set, else its style's
  (D-2026-11-04-b). Deleting a set clears style references; a missing style reference is reported and cleared on load.
- Line check: `minLineLength` on the check set, `ALIGNMENTCHECKSET SET, <name>, MINLINE, <n>`; every tangent shorter than
  it is a finding. Zero switches it off.
- Acceptance wording: warnings print at display precision (D-2026-11-04-a).

Still open:
1. **Layout Constraint column (REQ-485).** The spec says the Constraint column shows the value required by the criteria. The
   column still shows the tangency constraint only.
2. **Per-entity meets/violates marking in the report (REQ-494).** The report lists findings but does not mark each entity as
   meeting or violating. CSV is unchanged (adding a column would change the 18-column contract).
3. **Flag size.** Fixed at 4 drawing units; it does not scale with the style's marker size.
4. **Panel follow-ups (non-blocking, cited by two or more reviewers).** The layout table's Design check text is cut
   off before the required minimum, and Civil 3D shows the minimum in its own column; the red flag reads less clearly
   than Civil 3D's amber "!" triangle; the style Design tab is mostly empty below its one combo box.

Closed: **Civil 3D visual comparison (CLAUDE.md §3.6)** — panel run 2026-11-04, result **PASS** (see §7).
## 7. Completion record
- Requirements: REQ-502 statement items, except §6 items 1–4.
- Tests: `[req502]` and `[alignment]` pass (Release). Full ctest: pending the run recorded below.
- Panel: **PASS** (2026-11-04). Three pairs (plan-view marker, entity table, check-set choice) against Civil 3D 2026;
  every rubric item's median is 1 or 2 and no reviewer reported a blocker. The Civil 3D side was built in a new
  drawing inside the user's running session (Imperial NCS template, AASHTO 2011 criteria, 30 mi/h) and closed unsaved
  afterwards. Unmatched settings, the three score sheets, medians and the decision are under
  `build/artifacts/panel-502/` (`TASK-AND-SETTINGS.md`, `reviewer-A/B/C.md`, `RESULT.md`).
  Session note: an earlier mis-click in the user's MESSER drawing left a pending selection window; it was cancelled
  with Escape and nothing in that drawing was selected or changed.
