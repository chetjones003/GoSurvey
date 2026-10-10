# TASK-045 — Raw description on survey points, and rule-based point groups

- Type:    feature
- Status:  done
- Opened:  2026-08-15
- Owner:   Workshop

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         M-Surfaces step 2 (roadmap) — the data source every surface consumes
- Requirements: **REQ-066** (accepted 2026-08-12), **REQ-067** (accepted 2026-08-12, clarified
                2026-08-15)
- Constraints:  CON — architecture §11.9 (references by id), §11.4 (no abstraction without 2 uses),
                REQ-200, REQ-201, REQ-301; ADR-005 (the `GOSURVEY` DXF XDATA schema)
- Acceptance:   restated verbatim from the spec:
  - **REQ-066** — a point imported with a field code keeps that code in `rawDescription` after
    `description` is edited to something else; a legacy `.gs`, and a legacy DXF point carrying the
    pre-REQ-066 XDATA, both load with `rawDescription` empty and are matched on `description`
    instead — not skipped, not defaulted to the description's text; `rawDescription` round-trips
    `.gs` and DXF unchanged, including when empty.
  - **REQ-067** — a group defined as `EG*` resolves to exactly the points whose description matches
    and to no others; the same rule against `rawDescription` resolves independently of an edited
    description; importing further `EG` points and re-resolving includes them with no edit to the
    group; an id-range rule `1-10, 20-30` excludes 11–19 and includes both endpoints; an explicit-id
    group is unchanged by newly imported points; deleting a point removes it from every group's
    resolved membership and leaves no dangling id behind in the stored rule; a rule that matches
    nothing resolves to an empty group and says so (REQ-201); **a group with no criterion filled
    resolves to empty, not to every point**; **two criteria filled resolve to the union of their
    matches, and a hand-picked id stays in the group even when it matches neither wildcard nor any
    id range**; groups round-trip `.gs`, and a legacy `.gs` with no group section loads unchanged.
- Owning subsystem: Domain/survey (field, rule, resolution), IO (`.gs`, DXF XDATA), UI (editor)

## 2. Scope
- In scope:
  - `SurveyPoint::rawDescription`; `.gs` + `GOSURVEY` XDATA persistence; importers populate it.
  - `PointGroup` + `PointGroupRule` (id ranges, description wildcard, raw-description wildcard,
    explicit ids), union semantics, on-demand resolution.
  - `.gs` persistence of the group table; undo coverage (creating/editing a group is undoable).
  - A Point Group editor UI: create, rename, delete, edit the rule, see resolved count.
  - `PointGroupTests` — a pure, GL-free test module, per the `EntityId.cpp` precedent.
- Out of scope:
  - **Exclusion rules** — offered and declined 2026-08-15, recorded in the decision log. Not built.
  - Any surface work (REQ-068+). Groups are useful on their own; nothing here consumes them yet.
  - Description-key expansion (raw code → expanded description). `rawDescription` is *stored*, not
    *derived*; a description-key table is a separate feature nothing has asked for.
  - Point groups in DXF/DWG. They are a GoSurvey concept with no interchange representation, same
    reasoning as REQ-068 surfaces.
- Smallest change: one string field on the point, one rule struct + resolver, one `.gs` section, one
  panel.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] **No — proceed.** The data-format changes (a `SurveyPoint` field, a second XDATA `1000`
      string, a new additive `.gs` section) are all mandated by REQ-066/REQ-067 themselves and were
      recorded when those were accepted; the one genuine ambiguity (how criteria combine) was
      escalated to the user and recorded in the decision log **before** this task opened. No new
      abstraction: `PointGroup` is a concrete struct and resolution is a free function.
    - [ ] Yes → STOP.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | REQ-067 says a rule "combines any of" four criteria — do filled criteria AND or OR? | 2026-08-15 (before code) | **OR (union).** Under AND a hand-picked point outside the id range would be dropped from its own group. Recorded in REQ-067 Revisions + decision log. |
| Q2 | Does a group support exclusion in this release? | 2026-08-15 (before code) | **No** — include-only. Real and useful, but not in REQ-067's acceptance; becomes its own REQ rather than unasked scope growth. |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: Wildcard matching is case-insensitive, and supports `*` (any run) and `?` (one char).
- Because:       REQ-067 says "wildcard" without defining the syntax or case rules.
- Risk if wrong: `eg*` would not match `EG` (or would), surprising the user. Low blast radius —
                 the matcher is one pure function with one call site.
- Validate by:   PointGroupTests pins both; raise with the user if field practice differs.

ASSUMPTION-2: An id range is parsed from a plain string ("1-500, 1200, 1400-1450") rather than a
              structured list, and an unparseable token is REPORTED and ignored, not silently
              dropped (REQ-201).
- Because:       REQ-067 gives the user-facing syntax by example but not the storage form.
- Risk if wrong: none to correctness; a structured form would only change the editor's shape.
- Validate by:   PointGroupTests covers reversed ranges, whitespace, and junk tokens.

ASSUMPTION-3: Resolution is computed on demand and never cached on the group.
- Because:       REQ-067 requires membership to follow the current point set. A cached member list
                 is the same staleness bug REQ-076 was built to prevent, one level up.
- Risk if wrong: a very large drawing could want a cache; measure before adding one (§11.7).
- Validate by:   the REQ-100 surface profile, once REQ-068 consumes groups.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: put every *rule* — wildcard match, id-range parse, union resolution — in a
  dependency-free `src/survey/PointGroupRule.{hpp,cpp}` so `GoSurveyTests` links it without the GUI
  stack (the `EntityId.cpp` / `DwgProbe.cpp` precedent). State-facing resolution over
  `AppCommandState` stays a thin wrapper in the survey module.
- Files/functions to touch:
  - `src/survey/SurveyPoints.hpp` — `rawDescription`.
  - `src/survey/PointGroupRule.{hpp,cpp}` — **new, pure**: `WildcardMatchCI`, `ParseIdRanges`,
    `IdInRanges`, `PointMatchesRule`.
  - `src/survey/PointGroups.{hpp,cpp}` — `PointGroup`, `ResolvePointGroup(st, group)`.
  - `src/commands/CadCommands.hpp` — the group table on `AppCommandState`, `DrawingDocument` and
    `DrawingGeometrySnapshot` (undoable, like `textStyles`).
  - `src/io/GsIo.cpp` — `rawDescription` on the point; a new additive `pointGroups` section.
  - `src/io/DxfIo.cpp` — emit a second XDATA `1000` for raw desc; read first `1000` = description,
    second = raw (legacy files have one, so raw loads empty).
  - `src/io/SurveyCsv.cpp` — an imported CSV description IS the field code: set both.
  - `src/ui/` — Point Group editor panel.
  - `tests/PointGroupTests.cpp`; `CMakeLists.txt` (both targets).
- Test approach:
  - happy path = `EG*` resolves exactly; raw-desc match survives a description edit; id ranges
    include endpoints and exclude gaps; union of two criteria; new points join on re-resolve.
  - failure mode = **empty rule resolves to empty, not all points**; unparseable range token is
    reported not absorbed; deleted point leaves no dangling id; a rule matching nothing is reported;
    legacy `.gs`/DXF load with `rawDescription` empty and fall back to `description`.
- Steps:
  - [x] 1. `rawDescription` + `.gs` + XDATA + CSV/importer population (REQ-066 complete on its own).
  - [x] 2. Pure `PointGroupRule` module + `PointGroupTests` (rules before storage).
  - [x] 3. `PointGroup` storage on state/document/snapshot + `.gs` section.
  - [x] 4. Resolution wrapper + REQ-201 reporting.
  - [x] 5. Point Group editor panel.
  - [x] 6. Self-verification (§9).

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1, Q2 — both before any code). Tests-first for step 2, since the
  rule semantics are the part the user just made a decision about and the part most easily got wrong.

## 8. Implementation log  (append as you work)
- 2026-08-15 — opened. Q1/Q2 answered and recorded in `spec/` before planning; §3 boundary check
  clean. Follows TASK-044, which is done and whose legacy migration is manually verified.
- 2026-08-15 — step 1 (REQ-066). `rawDescription` added; `.gs` additive; DXF uses a **second XDATA
  `1000` string distinguished by order**, because 1000 is the only general string code XDATA offers
  — a pre-REQ-066 POINT emits one, which is exactly why an old DXF loads with the field empty rather
  than misreading the description as a field code. Populated at the three places a description is
  authored: CSV import, point creation, and the Overwrite duplicate policy. **Merge deliberately
  does not touch it** — merge appends to the description of a point that keeps its original field
  code, and REQ-066 says the raw code is not rewritten by description edits.
- 2026-08-15 — step 2 (REQ-067 rules), tests first. The matcher is **iterative with backtracking,
  not recursive**, so `"*a*a*a*a*"` against a long description cannot blow the stack; a test pins
  that. `ParseIntStrict` is used instead of `std::stoi` because `stoi("12abc")` returns 12, which
  would silently widen an id range instead of reporting junk.
- 2026-08-15 — **DECISION within boundary**: a raw-description rule falls back to `description` when
  a point carries no raw code. Without it, every rule keyed on raw description would silently match
  nothing on any pre-REQ-066 drawing — which is REQ-066's stated fallback ("matched on `description`
  instead — not skipped") applied at the consuming end. Covered by a test.
- 2026-08-15 — steps 3–4. **Deviation from the plan, stated:** §6 listed a new
  `src/survey/PointGroups.{hpp,cpp}` module pair. It was not created. `PointGroup` turned out to be a
  name plus a rule with no dependency on state, so it lives in the pure `PointGroupRule.hpp`, and the
  two state-facing functions went into the existing survey module. A module pair with one struct and
  two functions in it would have been a folder, not a boundary (REQ-301, CLAUDE.md rule 2). Groups
  are in the undo snapshot beside `textStyles`, so creating or editing one undoes in a single step.
- 2026-08-15 — step 5. Panel in its own TU (`CadUi_PointGroups.cpp`) rather than more of the
  12k-line `CadUi.cpp`, per the `CadUi_TraverseEditor.cpp` precedent. It shows the **resolved count
  and point numbers live** while the rule is typed: the two mistakes this feature makes easy — a
  rule matching nothing, and an empty rule the user reads as "everything" — are both invisible
  otherwise, and the empty case gets its own worded warning rather than a bare "0".

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — **PASS**. Clean rebuild (`--clean-first`), no new warnings. Two remain
      and are pre-existing in files this task did not touch: `-Wmicrosoft-goto` in
      `ViewportRenderer.cpp:943` and an unused const in `PdfAttach.cpp:523`.
- [x] architecture-review  — **PASS**. No Workshop architectural decision; the one ambiguity was
      escalated and recorded before code. §11.9 respected — groups store **point ids**, never point
      indices, so deleting a point cannot silently transfer membership. §11.4: no new abstraction —
      `PointGroup` is a struct, resolution a free function. §11.7: resolution is O(points) and runs
      on demand, never per frame from a hot path (the panel calls it only while open).
- [x] code-review          — **PASS**. Ownership unchanged; no new globals; every error branch
      reports (bad range tokens, empty match, duplicate rename) rather than swallowing.
- [x] dependency-audit     — **n-a**. No dependency added.
- [x] performance-review   — **PASS / n-a**. Nothing enters a measured hot path. Resolution is not
      cached by design (ASSUMPTION-3); revisit only if the REQ-100 surface profile shows it.
- [x] testing              — **PASS**. `PointGroupTests`: 20 cases / 67 assertions. Full suite
      **275 cases, 65,302 assertions, green** (was 255 after TASK-044; no existing test changed).

### Coverage gap, stated rather than hidden
Same **DEBT-7** constraint as TASK-044: `GsIo.cpp` and `DxfIo.cpp` are not linkable in the test
target, so the `.gs` group section and the **DXF two-`1000` XDATA round trip** are covered by
shape-level tests plus reasoning, not by an end-to-end automated test. The rules themselves — which
is where the behaviour lives — are fully covered. Manual verification of the DXF raw-description
round trip is outstanding and listed for the user.

## 10. Verification result
- Submitted:  2026-08-15
- Verdict:    PASS (self-verification; §9 all green)
- Findings:   none blocking. Q1/Q2 resolved by user before code.

## 11. Outcome
- Requirements satisfied: REQ-066, REQ-067 (Acceptance met: yes, except the IO round trips noted
  above, which are manual-verify only under DEBT-7)
- Tests added:            `tests/PointGroupTests.cpp` (20 cases)
- New modules:            `src/survey/PointGroupRule.{hpp,cpp}` (pure),
                          `src/ui/CadUi_PointGroups.cpp`
- Technical debt:         DEBT-7 (unchanged, inherited) — IO wiring not automatically testable.
- Docs updated:           `spec/requirements.md` (REQ-067 union clarification + two acceptance
                          conditions), `spec/project.md` (decision-log entry 2026-08-15)
- Done:                   2026-08-15
