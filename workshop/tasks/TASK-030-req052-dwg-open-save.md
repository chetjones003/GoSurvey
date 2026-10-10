# TASK-030 — REQ-052 Phase 1: open and save DWG (external-converter route)

- Date:      2026-07-30
- Status:    complete (Phase 1)
- Authority: REQ-052 (accepted), ADR-024 (accepted), CON-07, REQ-201
- Plan:      `docs/dwg-plan.txt`

---

## 1. Authority

| Item | Reference |
|------|-----------|
| Requirement | REQ-052 — Open and save DWG drawings (accepted 2026-07-30) |
| Architecture | ADR-024 — DWG support in phases: external converter first, native codec after |
| Decision log | `spec/project.md` — 2026-07-30 entries (REQ-052/ADR-024; `.gs` → `.dwg` direction) |
| Constraints | REQ-201 (never fail silently), CON-07 (artifacts out of the source tree) |

## 2. Architectural-boundary check

| Question | Answer |
|----------|--------|
| New abstraction? | No. `io/DwgIo` exposes four free functions; no interface, no template. |
| New module? | Yes — `io/DwgIo` and `platform/ProcessRun`. **Both recorded in ADR-024.** |
| New dependency? | No. Uses Win32 `CreateProcessW` and an external tool the user already has. |
| Layer direction | `ui → io → platform`. Downward only (architecture §2). |
| New global state? | No. Two fields added to the existing `AppCommandState`. |
| Data-format change? | No. `.gs` and the DXF wire format are untouched. |

## 3. What was built

| File | Change |
|------|--------|
| `src/platform/ProcessRun.{hpp,cpp}` | **new** — launch a child hidden, quote args per `CommandLineToArgvW`, bound its lifetime, terminate on timeout. |
| `src/io/DwgIo.{hpp,cpp}` | **new** — converter discovery, `DwgVersionName`, `ImportDwgFile`, `ExportDwgFile`, RAII temp dirs. |
| `src/platform/WinFileDialogs.{hpp,cpp}` | `BrowseOpenFileDwgUtf8` / `BrowseSaveFileDwgUtf8` (+ non-Win32 stubs). |
| `src/ui/CadUi.cpp` | File-menu Import/Export DWG, disabled with an explanatory tooltip when no converter exists; `DrawDwgLossyExportModal`. |
| `src/ui/CadUi.hpp`, `src/app/main.cpp` | Modal declaration + per-frame call. |
| `src/commands/CadCommands.hpp` | `dwgLossyExportModal`, `dwgPendingExportPath`. |
| `src/io/DxfIo.cpp` | **defect fix** — TEXT emitters, see §5. |
| `CMakeLists.txt` | Two new sources. |

Converter discovery order: `GOSURVEY_DWG_CONVERTER` env override → ODA File Converter →
newest installed AutoCAD `accoreconsole`.

## 4. Verification evidence

Built: `GoSurvey-0.4.0.exe` links clean. Existing suite: **611 assertions in 98 test cases, all pass.**

The `GoSurveyTests` target does not link `DxfIo`/`DwgIo` (they pull in `CadCommands.cpp` →
pdfium/WinRT/ImGui), so Phase 1 was verified with two throwaway harnesses that link the **real**
modules. Both are recorded here rather than committed — see the debt item in §6.

Harness A — `DwgIo` against stub DXF functions:

| Check | Result |
|-------|--------|
| Converter located (`AutoCAD 2026 (accoreconsole)`) | PASS |
| `DwgVersionName` on the reference file → "AutoCAD 2018" | PASS |
| `DwgVersionName` on empty / missing / non-DWG content → empty | PASS |
| `ImportDwgFile` on `26-084 - Master.dwg` → 487,112-line DXF handed to the importer | PASS |
| Non-DWG, missing file → refused with a specific reason | PASS |
| No temp directories left behind | PASS |

Harness B — the **real** `ExportDxfFile` → `ExportDwgFile` shipping path:

| Check | Result |
|-------|--------|
| `ExportDxfFile` writes a DXF | PASS |
| `ExportDwgFile` produces a tagged AC1032 DWG | PASS (18,442 bytes) |
| Unwritable destination fails cleanly, creates nothing | PASS |

End-to-end acceptance — the DWG GoSurvey wrote was reopened by **AutoCAD 2026** (`accoreconsole`
exit 0, no recovery prompt) and re-exported: **3 LINE, 1 CIRCLE, 1 TEXT**, on layers `BOUNDARY`,
`MONUMENTS`, `TEXT`, with the string `GOSURVEY DWG TEST` intact — exactly what was exported.

## 5. Defect found and fixed (pre-existing, not introduced here)

Harness B failed at first with `accoreconsole` exit 53. Root cause, from the AutoCAD log:

```
The following error was encountered while reading in TEXT starting at line 1306:
Unexpected DXF group code: 73
Invalid or incomplete DXF input -- drawing discarded.
```

`AcDbText` is declared **twice** in a DXF TEXT record; group 73 (vertical justification) belongs to
the second subclass. Both of `DxfIo.cpp`'s TEXT emitters wrote 73 with only one marker, and also
emitted group 7 (text style) inside `AcDbEntity` instead of `AcDbText`.

Scope of the bug is wider than DWG: **any** DXF GoSurvey exported containing a single-line TEXT was
rejected outright by AutoCAD — the whole drawing discarded, not just the text. Fixed by ordering both
emitters to the DXF reference (`…40, 1, 50, 7, 71, 72, 210/220/230, 100 AcDbText, 73`). Field values
are unchanged, so the GoSurvey importer round-trip is unaffected; the 611-assertion suite still passes.

## 6. Assumptions, debt, escalations

- **ASSUMPTION-1** — an installed converter is acceptable for Phase 1 only. Risk if wrong: DWG is
  unusable for users without one. Validated by: the menu items disable themselves and say what to
  install. Retired by the native codec.
- **ASSUMPTION-2** — writing AC1032 is the right default, since it matches what `DxfIo` already tags
  and the user's own drawings. Validate by: UI-03 (a version selector) if anyone needs older output.
- **DEBT-1 — the save is lossy.** Blocks, extra layouts, elevations, attributes and proxies are
  dropped. Removal condition: native writer + the unknown-object preservation channel (the user has
  decided a save **must** preserve them). Follow-up: the Phase 5 tasks in `docs/dwg-plan.txt`.
- **DEBT-2 — DWG/DXF IO has no committed automated test**, because `GoSurveyTests` cannot link
  `DxfIo` without dragging in pdfium/WinRT/ImGui. This is exactly why the TEXT defect in §5 survived.
  Removal condition: split the DXF/DWG writers' pure emit logic behind a seam the test target can
  link, then commit the harness checks as real tests. **This is the highest-value follow-up here.**
- **DEBT-3 — the converter path is not user-configurable in the UI**, only by environment variable.
- **No architectural decisions were made by the Workshop.** The codec route was escalated as a SPEC
  GAP and decided by the user; the two new modules are recorded in ADR-024.

## 7. Completion report

```
COMPLETION REPORT — TASK-030 — 2026-07-30
- Requirements satisfied:  REQ-052 Phase 1 (Acceptance met: yes, for the converter route)
- Summary:                 DWG import/export through an out-of-process DWG<->DXF converter behind a
                           four-function seam; menu entries, converter discovery, and a save
                           confirmation that enumerates what the route drops.
- Tests:                   611 assertions / 98 cases green (existing suite, no regression).
                           Two harnesses over the real modules: all checks pass, incl. the written
                           DWG reopening in AutoCAD 2026 with its entities, layers and text.
- Verification verdict:    PASS (findings resolved: the DXF TEXT group-73 defect, §5)
- Assumptions:             ASSUMPTION-1, ASSUMPTION-2 (documented, open)
- Architectural decisions: none made by Workshop (escalated: SPEC GAP -> ADR-024)
- Dependencies:            none added
- Technical debt noted:    DEBT-1 (lossy save), DEBT-2 (no committed DXF/DWG test), DEBT-3 (converter
                           path not configurable in the UI)
- Build:                   clean; artifacts in build/ (CON-07)
- Docs updated:            docs/dwg-plan.txt, spec/requirements.md, spec/architecture.md,
                           spec/project.md
```
