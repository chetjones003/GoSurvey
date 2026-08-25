# TASK-102 — Count and declare every entity the DXF exporter writes

- Type:    bug
- Status:  **#72 done (2026-08-25)** · **#71 blocked (SPEC GAP — see §3 and §12)**
- Opened:  2026-08-25
- Owner:   Nathan Johnson

> The two defects share a root cause and a function but not a fate. #72's oracle is
> expressible with today's harness, so it is fixed, tested and shipped. #71's is not —
> it needs the REQ-204 invariant proposed in §12 — so it waits for a recorded decision
> rather than shipping untested. Splitting them was the user's call (Q4).

Upstream issues: chetjones003/GoSurvey#71 (duplicate handles, `sev:corrupt`),
chetjones003/GoSurvey#72 (undefined LAYER reference). Both filed by TASK-083 as
OBS-1 / OBS-2 and deliberately left unfixed there.

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         DXF is the interchange format REQ-052 builds DWG on top of; a file
                other software refuses is a file the user cannot hand over.
- Requirements: REQ-053 (accepted) — "RECT command, and polylines survive a DXF/DWG save"
                REQ-204 (accepted 2026-08-16) — invariant row: *"DXF export → import →
                export is stable / An entity type silently dropped by an exporter with
                no branch for it"*
- Constraints:  Windows 11 / MSVC (project.md §7). No new dependency.
- Acceptance:   restated verbatim from the governing sources —
  - REQ-204 invariant table: **"DXF export → import → export is stable."**
  - REQ-204 acceptance: **"each listed invariant has a fixture that deliberately
    breaks it and proves the check fires — a check that has never failed is not
    known to be a check."**
  - REQ-053 statement: a rectangle is stored as a polyline and **survives a DXF/DWG
    save**. Surviving a save into a file a consumer rejects does not satisfy this.
- Owning subsystem: `io` (`src/io/DxfIo.cpp`). ADR-031 records `DxfIo`'s dependency
  direction; nothing here changes it.

## 2. Scope
- In scope:
  - `entityHandleCount` counts every store the writer actually emits (#71).
  - the `addLayerName` sweep names every layer the writer actually emits (#72).
- Out of scope:
  - **#63** (no ARC / ELLIPSE export branch, and DXF import tessellating an arc into
    48 segments at `DxfIo.cpp:1153`). Separate defect, separate task. Issue #71 notes
    the two must be sequenced — arcs and ellipses consume **zero** handles today
    precisely because they have no branch, so fixing #63 must add them to the sum this
    task corrects. Getting that sum right is this task's job; adding the branch is not.
  - whether the DXF **importer** should register a layer it meets only on an entity.
    Issue #72 is explicit that this would *mask* the export defect rather than fix it.
- Smallest change: two additive loops in one function. No signature changes, no new
  helper, no reordering of emitted records — every existing handle keeps its value.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change /
  global state / public-API or data-format change / algorithm the spec didn't specify?
    - [x] **No** for the *fix* — two loops added beside five identical existing ones,
          inside the subsystem that owns the file format. Nothing crosses a §11
          invariant.
    - [x] **Yes** for the *regression test* → **STOP. Status: blocked.** See §12.
          #71 is invisible to every oracle this project has: `dxf-export-stable`
          compares two exports of the same drawing, and both collide identically.
          Catching it needs a new REQ-204 invariant — *"every group-5 handle in an
          exported DXF is unique, and `$HANDSEED` exceeds all of them"* — and adding
          a row to REQ-204's invariant table is a **spec change**, not the Workshop's
          to make (CLAUDE.md: "The spec changes ONLY by a recorded decision").

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | #71 cannot be tested without a new REQ-204 invariant. File the SPEC GAP and pause, file it and proceed anyway, or fix #71 untested as recorded debt? | 2026-08-25 | **File the SPEC GAP, then pause** for sign-off before any code. |
| Q2 | Delivery route, given READ permission on the upstream repo? | 2026-08-25 | Branch on `nrjohnson2604/GoSurvey`, then PR to `chetjones003:beta`. |
| Q3 | Should the handle count be corrected in place, or should OBJECTS handles be allocated *after* the entity section is written, making the count structurally impossible to get wrong? | 2026-08-25 | **OPEN** — see ASSUMPTION-1. Recommend in place; the alternative reorders emitted records. |
| Q4 | Ship #72 alone, given it needs no spec change, or hold both until #71 is unblocked? | 2026-08-25 | **Ship #72 now.** #71 continues under this task. |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: The correct fix is to repair the count in place, not to restructure
              handle allocation so the count cannot drift again.
- Because:       allocating OBJECTS handles after the entity section would change
                 the handle VALUES of existing records, and $HANDSEED with them.
                 That is a change to bytes this project has stability requirements
                 about (REQ-079's `.gs` analogue; REQ-204's DXF row).
- Risk if wrong: the same omission recurs the next time an entity kind is added —
                 which is exactly how #71 and #72 were born (REQ-053 added the
                 LWPOLYLINE branch and neither sweep). #63 will add two more stores.
- Validate by:   Q3. If the answer is "restructure," this task is re-planned, not
                 patched — and it likely becomes architectural in its own right.

ASSUMPTION-2: Over-counting is safe; under-counting corrupts.
- Because:       a too-high entityHandleCount leaves a GAP in the handle sequence
                 before the OBJECTS block. DXF permits gaps; it does not permit
                 duplicates. A too-low count is what #71 IS.
- Risk if wrong: none identified — but it decides the shape of the fix, so it is
                 recorded rather than assumed silently. It means the corrected count
                 may safely ignore the writer's per-entity `continue` guards.
- Validate by:   the new uniqueness invariant from §12, once it exists.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: make both sweeps iterate the same stores the writer iterates. The writer
  is the authority on what it emits; both sweeps are stale copies of that list.

- **Files/functions to touch:** `src/io/DxfIo.cpp`, `ExportDxfFile_Impl` only.
  - `:2139` `entityHandleCount` — add the two uncounted stores.
  - `:2099-2110` the `addLayerName` sweep — add the two unnamed stores.

- **The measured gap.** Every `entHandle++` site in the writer, against what the sum
  at `:2139` actually counts:

  | Writer loop | line | counted at `:2139`? |
  |---|---|---|
  | `nSeg` lines | 2950 | yes |
  | `nCirc` circles | 2986 | yes |
  | **polylines** (`userPolylineOffsets`) | **3032** | **NO — #71 as filed** |
  | survey points | 3063 | yes |
  | annotations (Text / Dim ×4) | 3130-3215 | yes |
  | **filled regions → HATCH** (`cadFilledRegions`) | **3279** | **NO — not in either issue** |

  and the `addLayerName` sweep against the layers the writer emits:

  | Layer source | swept at `:2099`? |
  |---|---|
  | `userLineAttrs`, `userCircleAttrs`, `cadAnnotationAttrs`, `surveyPoints`, `drawingLayerTable` | yes |
  | **`userPolylineAttrs`** (`:3028` emits `at.layer`) | **NO — #72 as filed** |
  | **`cadFilledRegionAttrs`** (`:3276` emits `at.layer`) | **NO — not in either issue** |

  > **Finding beyond the filed issues.** Both issues name `userPolylineAttrs` only.
  > `cadFilledRegions` / `cadFilledRegionAttrs` are omitted from the *same two sweeps*
  > for the *same reason* — the HATCH branch was added without updating either. A fix
  > that adds only polylines leaves both bugs live for any drawing containing a hatch.
  > To be reported upstream as a comment on #71 and #72 rather than silently folded in.

- **Test approach:**
  - happy path — six `RECT`s export with all handles unique and `$HANDSEED` above
    them all; the same with a hatch present; the same with both.
  - failure mode — #72's `GHOST` fixture: import a DXF whose only entity is an
    LWPOLYLINE on an undeclared layer, re-export, assert the LAYER table defines
    `GHOST`. This one **is** expressible with today's oracles.
  - #71's oracle is blocked on §12.

- Steps:
  - [x] write the `GHOST` regression transcript for #72 — confirmed it fails first
  - [x] fix the `addLayerName` sweep (polylines + filled regions)
  - [x] confirm it passes, and the full suite stays green — **574/574, 2026-08-25**
  - [x] self-verify (§9) — #72 half
  - [ ] **BLOCKED** — obtain the recorded decision on the REQ-204 invariant (§12)
  - [ ] write the handle-uniqueness check + a fixture that proves it fires (REQ-204
        acceptance requires the fixture, not merely the check)
  - [ ] fix `entityHandleCount` (polylines + filled regions)

## 7. Workflow-specific notes
- **Bug — root cause (one mechanism, two symptoms):** `ExportDxfFile_Impl` maintains
  **three** independent lists of "the entities this file will contain" — the handle
  count at `:2139`, the layer-name sweep at `:2099`, and the write loops themselves at
  `:2950-3279`. Only the third is exercised by any test. When REQ-053 added the
  LWPOLYLINE branch (and, earlier, when HATCH was added) the writer was updated and
  the two book-keeping lists were not. #71 and #72 are that single omission observed
  through two different downstream consumers: the handle allocator and the LAYER table.
- **Regression test fails-before?** #72: yes, expressible today. #71: **no oracle can
  express it today** — that is the SPEC GAP in §12, not an excuse to skip the test.

## 8. Implementation log  (append as you work)
- 2026-08-25 opened. Both defects confirmed still live on `beta` @ `7d453cd`
  (synced to `upstream/beta` this session) by direct read of `src/io/DxfIo.cpp` —
  not taken on the issues' word, which were measured at `9983f76`.
- 2026-08-25 root cause identified (§7). Evidence: the two tables in §6.
- 2026-08-25 **new finding** — `cadFilledRegions` / `cadFilledRegionAttrs` are missing
  from both sweeps as well. Neither issue mentions them. Scope widened to match the
  root cause rather than the issue text.
- 2026-08-25 Status → **blocked (SPEC GAP)**. Q1 answered: file and pause.
- 2026-08-25 Q4 answered — ship #72 alone. Resumed on the #72 half only.
- 2026-08-25 `EXPECT LAYERSDEFINED <dxf>` added to the transcript driver. It parses the
  WRITTEN FILE's group-code pairs and asserts every ENTITIES-section group 8 has an
  `AcDbLayerTableRecord`. A document-side assertion cannot see this defect; the whole of
  it is in what the exporter emits. Carries a vacuity guard (a file with no entity
  referencing a layer fails rather than passes) in the spirit of `EXPECT DIFFERENTFILE`.
- 2026-08-25 `samples/ghost-layer-entities.dxf` added: an LWPOLYLINE on `GHOST` and a
  solid HATCH on `GHOST2`, in a file with **no TABLES section**. The fixture is an
  imported file on purpose — `drawingLayerTable` masks the defect for any layer created
  in-session, so a drawing built with `CMD` would exercise the masked path and pass
  either way.
- 2026-08-25 **fails-before confirmed** on the unfixed exporter:
  `LAYERSDEFINED: 2 layer(s) referenced by an entity but absent from the LAYER table:
  GHOST, GHOST2 (1 defined, 2 referenced)`. Note it caught **GHOST2** — the hatch — which
  no issue reports. The new finding is now evidenced, not merely read.
- 2026-08-25 #72 fixed: two loops added to the `addLayerName` sweep. Test passes; full
  suite **574/574 green** (was 573 + this one), `dxf-export-stable` still the only
  DISABLED case (#63, untouched).
- 2026-08-25 #71 **measured on the fixed tree** to confirm the #72 fix does not perturb it.
  Adding LAYER rows moves `symAfterLayers`, and `entityHandleStart` derives from it — but
  it stays pinned at `0x1000` by the `std::max(0x1000ull, lastSymHandle + 1ull)` floor, so
  handle values are unchanged. Six RECTs still emit the exact sequence issue #71 recorded
  at `9983f76`:
  `1004 3 4 5 6 7 8 9 A B C 2 10 … 1F 1000 1001 1002 1003 1004 1005 1000 1001 1003 1002`
  — 38 handles, 33 unique, duplicates `1000-1004`, `$HANDSEED` = `1004` while `1005` is
  in use. Independently reproduced; the issue's measurement holds.
- 2026-08-25 #71's **filled-region half evidenced**, and it is worse than the polyline
  half. A document holding only the imported polyline + hatch has
  `entityHandleCount == 0` (no lines, circles, survey points or annotations), so
  `objDictRoot` lands on `0x1000` and **every entity in the file collides**:
  handles `1000 1001` are each written twice. The six-RECT case at least keeps some
  entities clear of the OBJECTS block; this one keeps none.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
**#72 half:**
- [x] build-project        — PASS, clean, MSVC Release via `build.bat`
- [x] architecture-review  — PASS. Two loops beside five identical existing ones in the
                             subsystem that owns the format. No new abstraction, no
                             dependency, no ownership change, no data-format change —
                             the LAYER table gaining a row it should always have had is
                             the requirement being met, not the format changing.
- [x] code-review          — PASS. The added loops match the four above them line for
                             line, including the `.layer` empty→"0" handling inside
                             `addLayerName`.
- [x] dependency-audit     — n-a (no dependency change; `<set>` is stdlib, test-only)
- [x] performance-review   — n-a (export path, not a frame-budget path; two loops over
                             stores already iterated moments later by the writer)
- [x] testing              — PASS. Fails-before verified, passes-after, suite 574/574.

**#71 half:** not run — blocked before implementation.

## 10. Verification result
- Submitted:  2026-08-25 (#72 half)
- Verdict:    PASS (#72) · blocked (#71)
- Findings:   none outstanding on the #72 half

## 11. Outcome  (#72 half)
- Requirements satisfied: REQ-053, REQ-204's export-stability row (Acceptance met: yes,
  for the layer-definition half — the handle half is #71 and remains open)
- Tests added:            `headless.regression-72-dxf-export-layer-defined`
                          (+ `EXPECT LAYERSDEFINED` driver verb,
                             + `samples/ghost-layer-entities.dxf` fixture)
- Refactors:              none
- Docs updated:           none (no user-visible behaviour change — an invalid file
                          became a valid one)
- Technical debt noted:   **DEBT-1** — the three-lists problem in §7 is fixed twice by
                          hand, not structurally. `entityHandleCount` (#71) and
                          `addLayerName` (#72) still restate what the writer emits, so
                          the next entity kind added can reintroduce both. Removal
                          condition: Q3 resolved in favour of deriving both from the
                          writer's own iteration. Follow-up rides with #71.
- Done:                   2026-08-25 (#72 half only)

## 12. SPEC GAP — proposed REQ-204 invariant
> Filed per CLAUDE.md "Escalation: the SPEC GAP". Status: **awaiting a recorded
> decision in `spec/project.md`.** No code is written until it lands.

**The gap.** REQ-204's invariant table has a row for *"DXF export → import → export is
stable."* That invariant is a **differential** oracle: it compares two exports and
reports a difference. #71 produces byte-identical output on both passes — the handle
collision is deterministic, so it collides the same way twice. The invariant is
satisfied by a file AutoCAD would reject. REQ-204's stated purpose is "find the state
corruptions nobody thought to write a test for"; here the corruption is in the *written
artifact*, which no current invariant inspects.

**Proposed amendment** — one new row in REQ-204's invariant table:

| Invariant | What a violation means |
|---|---|
| An exported DXF's entity handles are unique, and `$HANDSEED` exceeds every handle in the file | An exporter allocating handles from a count that does not match what it writes — an invalid file, produced silently |

and its matching acceptance condition, in REQ-204's existing style:

> - a fixture drawing that deliberately duplicates a handle proves the check fires.

**Why this is Specification's call, not the Workshop's.** REQ-204's acceptance says
*"each listed invariant has a fixture that deliberately breaks it and proves the check
fires."* Adding a row therefore adds an acceptance obligation to an `accepted`
requirement. The Workshop implements invariants; it does not decide what the invariant
set is.

**Recommendation.** Accept. The invariant is cheap (a set-insert over group-5 values on
the written file), it belongs to REQ-204's parser half where the fuzzer already reads
DXF bytes, and it is the only thing that can hold #63's fix honest when that lands —
#63 adds two more entity kinds to the very sum that is wrong here.

**If declined**, the fallback is Q1's option 3: fix #71 unguarded, record it as
technical debt with a removal condition, and open a follow-up. This is worse and is not
recommended — an untested fix to a `sev:corrupt` defect is indistinguishable from an
untested non-fix.
