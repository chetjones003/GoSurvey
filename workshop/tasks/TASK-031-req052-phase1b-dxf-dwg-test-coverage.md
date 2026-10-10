# TASK-031 — REQ-052 Phase 1b: committed test coverage for DXF/DWG IO

- Date:      2026-07-30
- Status:    complete
- Authority: REQ-052 (accepted), ADR-024 (accepted), REQ-201
- Retires:   TASK-030 DEBT-2
- Plan:      `docs/dwg-plan.txt` PHASE 1b

---

## 1. Why this task existed

TASK-030 shipped a fix for a severe pre-existing defect: `DxfIo`'s TEXT emitters wrote group 73
without the second `AcDbText` subclass marker, so **AutoCAD discarded any GoSurvey DXF containing
single-line text** — the whole drawing, not just the text.

That defect survived because `GoSurveyTests` cannot link `DxfIo`/`DwgIo`: both reach
`CadCommands.cpp`, which pulls in pdfium, WinRT and ImGui. The writer had no automated coverage at
all. Every later DWG phase multiplies that exposure, so closing the gap came before more format
work.

## 2. Architectural-boundary check

| Question | Answer |
|----------|--------|
| New abstraction? | No. `DxfEntityEmit.hpp` is pure record composition with two present-day call sites, following the established `PlotFont.hpp` / `HatchGeom.hpp` / `OrthoConstrain.hpp` / `ColorContrast.hpp` / `MtextToolbar.hpp` precedent for extracting testable logic. |
| New module? | `io/DxfEntityEmit.hpp` (header-only) and `io/DwgProbe.cpp`. The latter is a **translation-unit split**, not a new API: every function stays declared in `DwgIo.hpp`, so ADR-024's four-function seam is unchanged. |
| New dependency? | No. |
| Public API change? | No. `DwgVersionNameFromTag` is additive (it was previously an unnamed branch inside `DwgVersionName`). |
| Behaviour change? | **None** — proven byte-identical, see §4. |

## 3. What was built

| File | Change |
|------|--------|
| `src/io/DxfEntityEmit.hpp` | **new** — `DxfOutPair`, `DxfTextRecord`, `DxfAppendTextRecord`, `DxfTransparency440`. Header-only, no `AppCommandState`. Numeric fields are passed pre-formatted so composition cannot alter a single output byte. |
| `src/io/DwgProbe.cpp` | **new** — `DwgVersionNameFromTag`, `DwgVersionName`, `FindDwgConverter` and helpers moved out of `DwgIo.cpp`; depends only on `<filesystem>`. |
| `src/io/DwgIo.{hpp,cpp}` | Probe half removed; header documents the split and adds `DwgVersionNameFromTag`. |
| `src/io/DxfIo.cpp` | Both TEXT emitters now build a `DxfTextRecord`; the transparency lambda delegates to `DxfTransparency440` so one packing rule exists, not two. |
| `tests/DxfEntityEmitTests.cpp` | **new** — 5 cases. |
| `tests/DwgProbeTests.cpp` | **new** — 6 cases. |
| `CMakeLists.txt` | New app source; new test sources + `src/io/DwgProbe.cpp` and the `src/io` include dir on the test target. |

Also fixed while moving it: converter classification compared against `"accoreconsole"` and
`"ACCORECONSOLE"` literally, so a mixed-case filename such as `AccoreConsole.exe` was misclassified
as ODA File Converter. Now a proper case-insensitive compare, with a test.

## 4. Verification evidence

**Suite: 698 assertions in 109 test cases, all pass** (was 611 / 98 — 87 new assertions, 11 new
cases). `ctest`: 109/109 passed.

**Mutation test — the coverage was proven, not assumed.** The original defect was deliberately
re-introduced in `DxfEntityEmit.hpp` (second `AcDbText` marker deleted) and the suite rebuilt:

```
test cases:  5 |  4 passed | 1 failed
assertions: 49 | 45 passed | 4 failed
```

Three sections failed — "AcDbText is declared exactly twice", "group 73 follows the SECOND AcDbText
marker", and "group 7 is an AcDbText property". The mutation was then reverted and the suite
returned to green. **These tests demonstrably catch the bug that shipped.**

**Refactor fidelity — byte-identical output.** The real `ExportDxfFile` was run before and after
the refactor over the same drawing and the results compared byte for byte:

```
pre-refactor : 8281 bytes
post-refactor: 8281 bytes
RESULT: BYTE-IDENTICAL
```

**End-to-end still green.** The real `ExportDxfFile` → `ExportDwgFile` path still produces a tagged
AC1032 DWG (18,442 bytes) and the failure path still refuses an unwritable destination cleanly.

## 5. What is covered now

- TEXT record: opens as `0 TEXT`; exactly one `AcDbEntity` and exactly **two** `AcDbText` markers;
  group 73 after the second marker; group 7 between the two markers; layer/linetype/colour/
  lineweight inside the `AcDbEntity` block; geometry → text → rotation ordering; handle and owner
  before the first marker; all field values round-trip; null destination tolerated.
- Group 440: omitted when opaque (an explicit opaque 440 is not the same as ByLayer), emitted
  inside the `AcDbEntity` block when set; `0x02000000 | alpha` packing at 0 / 0.5 / 1.0; clamping
  of out-of-range input; null out-pointer.
- DWG version detection: all ten known release tags; "not a DWG" distinguished from "a DWG we do
  not know"; wrong-length tags; real files including non-DWG content, a file shorter than the tag,
  an empty file, a missing path, an empty path, and `nullptr`.
- Converter discovery: env override honoured and classified; case-insensitive classification; a
  nonexistent override never reported as usable; display name always populated; the cache holds
  until `forceRescan`.

## 6. Debt

- **DEBT-2 (TASK-030) is retired** for record composition and the DWG probe.
- **DEBT-4 — remaining `DxfIo` emitters are still untested.** Only TEXT was extracted, because it
  is the record that broke and the one with the non-obvious two-subclass rule. LINE, CIRCLE, POINT,
  MTEXT, HATCH and the DIMENSION expansions still have no coverage, and neither does the section /
  symbol-table structure or any import path. Removal condition: extract the remaining record
  emitters the same way as each is next touched, rather than in one large refactor.
- **DEBT-1 and DEBT-3 (TASK-030) are unchanged** — the save is still lossy and the converter path
  is still env-var only.

## 7. Completion report

```
COMPLETION REPORT — TASK-031 — 2026-07-30
- Requirements satisfied:  REQ-052 (Phase 1b; supports the acceptance conditions, adds no new ones)
- Summary:                 Extracted DXF TEXT record composition and the DWG probe into
                           test-linkable units and committed 11 test cases over them, including the
                           exact regression that shipped in TASK-030.
- Tests:                   698 assertions / 109 cases green (from 611 / 98). ctest 109/109.
                           Mutation-tested: re-introducing the defect fails 4 assertions.
- Verification verdict:    PASS (findings resolved: converter classification was case-sensitive)
- Assumptions:             none new
- Architectural decisions: none made by Workshop. The header-only extraction follows the
                           PlotFont/HatchGeom/OrthoConstrain precedent; the DwgProbe split is a
                           translation-unit move that leaves ADR-024's seam intact.
- Dependencies:            none added
- Technical debt noted:    DEBT-2 retired; DEBT-4 raised (remaining emitters untested);
                           DEBT-1/DEBT-3 unchanged
- Build:                   clean; app and test target both link
- Docs updated:            docs/dwg-plan.txt, spec/requirements.md, workshop/tasks/TASK-031
```
