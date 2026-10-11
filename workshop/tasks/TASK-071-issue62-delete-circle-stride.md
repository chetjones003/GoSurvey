# TASK-071 — Issue #62: DELETE erases 3 floats from the stride-4 circle store

- Type:    bug
- Status:  done — fixed, tested, and verified against the unfixed binary
- Opened:  2026-08-18
- Owner:   Workshop

## 1. Authority

- Requirements: **REQ-076** (an entity's identity and its stored fields are stable),
                **REQ-079** (a `.gs` written by GoSurvey reopens as the drawing that was saved),
                **REQ-201** (no silent failure)
- Constraints:  **architecture §11.8** — flat stores are interleaved, and `userCirclesCxCyZR` is
                stride 4 (`cx, cy, z, r`); REQ-101 (±0.01 ft); REQ-301 (no new abstraction)
- Acceptance:   deleting N circles removes exactly 4N floats and leaves every other circle's centre,
                elevation and radius unchanged — on screen and in the saved `.gs`.
- Owning subsystem: Commands (`ExecuteDeleteSelection` owns the erase).

## 2. Bug report (issue #62, filed by the harness 2026-08-18)

| # | Observed | Expected |
|---|----------|----------|
| 1 | `flat-strides` fires after deleting a circle: `userCirclesCxCyZR.size()=1 is not a multiple of 4` | the store stays a whole number of circles |
| 2 | Every circle after the erased one shifts by one float and reads its predecessor's **radius as its centre X**. Three circles at `(0,0) r1`, `(500,0) r2`, `(1000,0) r3` → after deleting the first, the survivor claims centre `(1, 500)` radius `2` | the survivors are untouched |
| 3 | `SAVEAS` writes the corrupted array out, so the damage outlives the session | nothing to write — the array is correct |

Reproducer already in-tree and committed: `tests/headless/transcripts/regression-62-delete-circle-stride.txt`
(registered `DISABLED` pending this fix).

## 3. Root cause (evidence, not hypothesis)

`src/commands/CadCommands.cpp`, the circle loop in `ExecuteDeleteSelection`:

```cpp
const size_t k = static_cast<size_t>(idx) * 4;   // stride 4 — correct
if (k + 3 >= st.userCirclesCxCyZR.size())        // bound — correct
  continue;
st.userCirclesCxCyZR.erase(begin + k, begin + (k + 3));   // <-- removes THREE floats
```

`erase(first, last)` is a half-open range, so `[k, k+3)` is three elements where the stride is four.
The LINE loop twenty lines above is the same shape and is correct: stride 6, `erase(k, k + 6)`.

**The guard is not part of the bug.** `k + 3 >= size()` is the right bound for the last circle
(`k = size-4`, so `k+3 = size-1`, which is in range), and it mirrors the LINE loop's `k + 5`.
Only the erase range is wrong.

### Sibling audit — done before fixing, not after

Every flat-store erase in `src/` was checked, because this defect's whole character is that one
member of a family disagrees with the rest:

| Site | Store | Stride | Range | Verdict |
|---|---|---|---|---|
| `CadCommands.cpp:8646` | `userLinesFlat` | 6 | `[k, k+6)` | correct |
| **`CadCommands.cpp:8658`** | **`userCirclesCxCyZR`** | **4** | **`[k, k+3)`** | **the defect** |
| `CadCommands.cpp:9280` | `userLinesFlat` | 6 | `[k, k+6)` | correct |
| `CadCommands.cpp:10504` | `userLinesFlat` | 6 | `[k, k+6)` | correct |
| `CadCommands.cpp:575` | `paperPolyVerts` | 3 | `[3·v0, 3·v1)` | correct |
| `CadCommands.cpp:8475`, `:10909` | `userPolylineVerts` | 3 | `[3a, 3b)` | correct |

`userCirclesCxCyZR` is erased in **exactly one place**, so this is the whole of the defect. Arcs and
ellipses are vectors of structs (stride 1) and are not exposed to this class at all.

## 4. Architectural boundary check  (workflow.md §4)

- New abstraction / layer / dependency / ownership change / global / public API / data format?
    - [x] **No — proceed.** One literal changes inside the function that already owns the erase.

Issue #62 suggests a shared stride-aware erase helper so the class becomes unrepresentable. That is
an **architectural** change (a new abstraction, REQ-301) and is **not** made here: it needs two or
more present-day concrete uses argued on their merits, and a bug fix is not the place to decide it.
Recorded in §8 as a follow-up rather than smuggled in.

## 5. Assumptions

```
ASSUMPTION-1: no persisted drawing needs repair.
- Because:       the corruption is produced at delete time and written from memory. A .gs already
                 saved from a corrupted store holds wrong-but-well-formed numbers; nothing in the
                 file records that they were once right, so no migration could restore them.
- Risk if wrong: a user who deleted a circle and saved has silently wrong geometry that this fix
                 does not repair and cannot detect.
- Validate by:   stated to the user in the completion report rather than resolved in code. The
                 `flat-strides` invariant WILL catch the one detectable case — an odd-sized array —
                 on the next load of such a file.
```

## 6. Plan

- `src/commands/CadCommands.cpp` — `k + 3` → `k + 4` in the circle erase range.
- Re-enable `headless.regression-62-delete-circle-stride` in `CMakeLists.txt` and rewrite the
  transcript's header from "fails today, which is the point" to a regression statement.
- Strengthen the transcript while re-enabling it: asserting only that the invariant is silent would
  pass on a fix that erased four floats from the WRONG offset. It must assert the surviving circles'
  actual values, which is the part a user would notice.

## 7. Steps
- [x] sibling audit (§3)
- [x] the fix
- [x] transcript strengthened + re-enabled
- [x] full suite

## 8. Implementation log

- 2026-08-18 — sibling audit first (§3): every flat-store erase in `src/` checked before touching
  anything, because this defect's whole character is one member of a family disagreeing with the
  rest. `userCirclesCxCyZR` is erased in exactly one place, so the one-line fix is the whole fix.
- 2026-08-18 — `k + 3` → `k + 4`, with a comment naming the half-open range, the stride and #62 so
  the next reader does not have to re-derive why 4 is right.
- 2026-08-18 — **the regression transcript was strengthened before being re-enabled.** As written
  for the bug report it asserted only `CHECK ALL` and entity counts, and that is not enough: a fix
  that erased four floats from the WRONG offset would leave the array a clean multiple of 4 with
  every circle still in the wrong place, and the test would pass. It now asserts where the survivors
  actually are, by box-selecting each at its original location and probing the corrupted location
  `(1, 500)` to confirm nothing is there.
- 2026-08-18 — **an `EXPECT SAMEFILE` against a separately-built expected drawing was tried and
  rejected**, which is worth recording because it looks like the obvious strongest test. It cannot
  work here: REQ-076 says ids are never reused, so the survivors keep ids 2 and 3 while a freshly
  drawn pair gets 1 and 2, and `nextEntityId` differs too. The files would differ for a reason that
  has nothing to do with this defect. Location assertions express the symptom directly instead.
- 2026-08-18 — the transcript's discriminating power was **measured, not asserted**. Against the
  unfixed binary it fails at `flat-strides` (step 17). With the invariant checks stripped out, so
  only the geometric assertions remain, it still fails — `BOX 490 -10 510 10` selects 0 because that
  circle had moved to `(1, 500)`. Both halves catch the bug independently, which is what the header's
  claim about `CHECK ALL` requires in order to be true rather than plausible.
- 2026-08-18 — full suite **428/428** (427 before; the re-enabled regression is the new one), and a
  **2000-seed fuzz sweep at 0 failures** — the delete path is heavily exercised by the generator, so
  a stride fix there is worth a sweep rather than a unit test alone.

## 9. Self-verification

- [x] build-project       — PASS. Clean; no new warning.
- [x] architecture-review — PASS. One literal inside the function that already owns the erase. No new
      abstraction, layer, dependency, global, public API or data-format change. #62's suggested
      stride-aware helper was deliberately NOT taken — see §4 and DEBT-1.
- [x] code-review         — PASS. The fix addresses the root cause (a half-open range read as
      inclusive), not the symptom the invariant reported. The sibling audit rules out the same
      mistake elsewhere rather than assuming it is unique.
- [x] dependency-audit    — PASS. None added.
- [x] performance-review  — n/a. Same erase, four elements instead of three.
- [x] testing             — PASS. 428/428 plus a 2000-seed sweep. The regression test is proven to
      fail against the unfixed binary in BOTH of its independent halves.

## 10. Verification result

- Submitted: 2026-08-18
- Verdict:   **PASS**
- Findings:  none outstanding.

## 11. Outcome

COMPLETION REPORT — TASK-071 — 2026-08-18
- Requirements satisfied:  REQ-076, REQ-079, architecture §11.8 (Acceptance met: yes — deleting a
                           circle removes exactly 4 floats and leaves every other circle's centre,
                           elevation and radius untouched, demonstrated by location assertions)
- Summary:                 `ExecuteDeleteSelection`'s circle erase removed 3 floats from a stride-4
                           store, shifting every later circle by one slot so each read its
                           predecessor's radius as its centre X — and saving wrote that out.
                           `k + 3` → `k + 4`.
- Tests:                   `headless.regression-62-delete-circle-stride`, re-enabled and
                           strengthened; proven to fail before the fix in both halves. Suite 428/428
                           green; 2000-seed fuzz sweep clean.
- Verification verdict:    PASS (findings resolved: none)
- Assumptions:             ASSUMPTION-1 (already-saved drawings cannot be repaired) — **open and
                           stated to the user**, not resolved in code.
- Architectural decisions: none made by Workshop. #62's suggested stride-aware erase helper is an
                           abstraction under REQ-301 and was refused rather than smuggled in.
- Dependencies:            none added
- Technical debt noted:    DEBT-1 — hand-written per-store erase loops remain the shape that
                           produced this and #60. A stride-aware helper would make the class
                           unrepresentable. *Removal condition:* raise it as a spec decision when a
                           third instance appears, or when a new flat store is added.
- Build:                   reproducible, clean, MSVC via the pinned preset
- Docs updated:            `docs/fuzz-harness.md` findings table; this log
- Done:                    2026-08-18
