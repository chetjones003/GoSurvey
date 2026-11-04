# TASK-1009 — Plot styles and non-PDF output (issue #1009)

- Type: feature
- Status: implementation complete; ready for combined PR review
- GitHub: #1009

## 1. Confirmed feature request

The user confirmed this scope on 2026-11-04:

- Apply existing CTB/STB files to vector PDF output only, through Page Setup, for single and batch plots.
- Preserve and use imported STB assignments; new GoSurvey entities default to `Normal`.
- Do not add a plot-style editor or assignment UI.
- PNG/raster, DWF, and direct system-printer output are excluded.
- Keep current PDF output unchanged when no table is selected.

## 2. Evidence pass

### Repository

- REQ-029 and REQ-030 specify single-layout and batch **vector PDF** output using bundled PDFium. They do not specify alternate devices or plot styles.
- ADR-007 selects PDFium vector PDF with no new dependency. It explicitly defers direct-to-OS-printer/GDI output.
- `PageSetup` in `src/commands/PaperSpace.hpp` fixes the plot device to built-in PDF and has no plot-style reference. `PlotLayoutsToPdf` in `src/io/PdfPlot.cpp` owns the existing PDF path.
- `src/io/DxfIo.cpp` currently writes a placeholder `ACAD_PLOTSTYLENAME` dictionary, but source search found no CTB/STB implementation or plot-style value model.
- `resources/wiki/Plotting.md` and `Paper-Space-and-Layouts.md` document vector PDF as the only output device and printing PDFs externally.
- #1009 has no accepted REQ, and its body marks expected behavior unknown.

### Autodesk Help (web)

- [About Plot Style Tables, AutoCAD 2025](https://help.autodesk.com/cloudhelp/2025/ENU/AutoCAD-Core/files/GUID-2FD01085-DDD8-49D2-A910-E63EA45A7FED.htm): distinguishes color-dependent CTB tables, which map object colors to plot characteristics, from named STB tables.
- The same page says CTB has one entry per 256 object colors, while STB provides user-defined named styles assignable to objects or layers. AutoCAD's Plot Style Manager adds, deletes, renames, copies, and edits table files.
- [Plot Style Table Editor, AutoCAD 2025](https://help.autodesk.com/cloudhelp/2025/ENU/AutoCAD-Core/files/GUID-56184373-FC19-49A0-9E67-181C4F5C19B7.htm): documents CTB/STB files as separately editable plot-style tables attached to a layout/model tab.
- [About Plotter Configurations and Plot Styles, AutoCAD 2025](https://help.autodesk.com/cloudhelp/2025/ENU/AutoCAD-Customization/files/GUID-1FB1C1F5-82CD-4912-8549-84FF7DD91EBB.htm): says PC3 files configure output devices, while CTB/STB files control object appearance. This supports treating plot-style support and adding output devices as separate scope decisions.

### Installed product

- AutoCAD 2025 and `accoreconsole.exe` are installed. The machine has Civil 3D 2025 Object Enabler only; no Civil 3D application is installed.
- A read-only console probe of `-PLOT` reached the prompt `Detailed plot configuration? [Yes/No] <No>:` and was cancelled. The process exited with code 0. This confirms the product exposes detailed plot configuration, but did not proceed far enough to validate a particular plot style/device behavior.
- No output file was produced; the probe did not validate CTB/STB behavior.

## 3. Authority and decisions

- Requirement: REQ-485 (accepted), with REQ-029/030/048/049/113/201 and REQ-300 constraints.
- Decision: D-2026-11-04-a; architecture: ADR-076.
- Acceptance: REQ-485 Acceptance 1–6.
- Architectural impact: Domain/GoSurvey data gains table-reference and plot-style-name values; DWG/DXF IO preserves native STB assignments; the existing PDF plotter applies existing CTB/STB files. No new dependency, editor, assignment UI, or output backend.

## 4. Plan

1. Inspect native DWG/DXF plot-style dictionary and layer/entity references; map named style names into drawing-owned values with entity-over-layer precedence and `Normal` fallback.
2. Add optional CTB/STB file reference to page setup and GoSurvey serialization. Keep the external table unembedded and report unavailable or invalid references at plot time.
3. Read existing CTB/STB data in-tree and apply its supported plot properties in the PDFium vector path. Preserve existing path output exactly when no table is selected.
4. Extend Page Setup with table selection; support single and batch plotting using each selected layout's setup.
5. Add focused fixtures/tests for CTB and STB behavior, no-table compatibility, invalid tables, assignments through DWG/DXF and GoSurvey persistence, and single/batch output.
6. Update in-app help and paper-space documentation. Before the eventual combined PR, complete the required README, PRIVACY, in-app wiki, and public wiki documentation preflight.

Likely areas: `src/commands/PaperSpace.hpp`, drawing/entity/layer values in `src/commands/CadCommands.hpp`, `src/io/GsIo.cpp`, `src/io/DxfIo.cpp`, LibreDWG mapping, `src/io/PdfPlot.cpp`, Page Setup UI in `src/qt/EditorWindow.cpp`, plus plot and import/export tests.

## 5. Verification plan review — 2026-11-04

**Verdict: APPROVE the plan for implementation, with the format-coverage checks below treated as blocking acceptance evidence.**

- **Architecture:** Responsibilities follow ADR-076: Domain owns assignments and page-setup references; IO owns GoSurvey/DWG/DXF serialization; the existing PDF plotter owns table evaluation. The plan adds no dependency, global, or new subsystem. The referenced external table is not embedded; missing references fail plotting explicitly.
- **Correctness risks:** CTB effective color resolution; STB entity-over-layer precedence; default `Normal`; table path missing after reopen/on another machine; malformed or unsupported table data; partial/incorrect output in multi-layout batch; maintaining vector output and no-table identity. Unknown CTB/STB file variants must not be silently treated as valid. Verify native handle/name mapping against real DWG/DXF fixtures, including layer and entity assignment.
- **Dependencies/performance:** No dependency change is authorized. Table parsing must stay out of interactive render paths and run only during plot. Avoid repeated reparsing within a batch; cache only within the plotting operation unless measurements justify longer-lived state.
- **Required tests:**
  1. No selected table produces the current output for single and batch plots.
  2. A CTB fixture proves distinct effective colors map to the fixture's output characteristics; PDF remains vector.
  3. An STB fixture proves entity assignment overrides layer assignment, layer assignment applies when entity assignment is absent, and fallback is `Normal`; PDF remains vector.
  4. Imported assignments round-trip through DWG and DXF, and through GoSurvey save/reopen; new GoSurvey-created objects use `Normal`.
  5. Page Setup selection persists through GoSurvey save/reopen and is honored by single and batch plotting per selected layout.
  6. Missing, malformed, or unsupported tables report a failure and do not report successful PDF creation; drawing state is unchanged.
  7. Applying a table changes output only; geometry, colors, layer values, and style assignments remain unchanged.
- **Smallest implementation:** Reuse PageSetup, drawing-owned entity/layer attributes, existing native DWG/DXF plot-style structures, and the existing PDFium vector writer. Add only the fields and parser/output mapping required by REQ-485. Do not build an editor or general output backend.
- **Gate:** This is plan approval only. No implementation code, builds, or tests were performed before approval.

## 6. Implementation record

### Implemented

- Added plot-style assignment fields to GoSurvey entity and layer data, with GoSurvey JSON persistence and `Normal` defaults.
- Added Page Setup selection for an existing CTB/STB file and persisted its path per layout/setup.
- Added an in-tree CTB/STB parser using the already bundled zlib decoder; the PDF vector plot path applies color, screening, grayscale, and lineweight settings. Missing/invalid selected files fail before output is reported as successful. Batch plotting uses each layout's setup.
- Added DWG and DXF style-name import/export and DXF dictionary resolution. DXF import maps entity group 390 and layer plot-style handles; explicit `ByLayer`/`ByBlock` values round-trip, and missing entity assignments retain the `Normal` default.
- Added REQ-485 coverage for CTB/STB parsing, table errors, vector PDF plotting, GoSurvey persistence, and DWG/DXF round-trips.
- Updated in-app plotting and paper-space wiki documentation.

### Build and tests

- `.\build.bat` — PASS (Release; Qt and domain/test/helper targets built). The environment printed a non-fatal `vswhere.exe` lookup warning; MSVC build completed successfully.
- `GoSurveyTests.exe "[req485]"` — PASS, 54 assertions in 5 test cases (rerun after final DXF changes).
- `ctest --preset ninja-release --output-on-failure` — PASS, 194/194 passed. The first run exposed a stale paper-space transcript expectation: it still expected model-only OVERKILL after the approved #1005 scope added active-layout cleanup, and also expected paper JOIN refusal despite its existing #973 support. Updated the transcript to verify active-layout OVERKILL and model-space isolation; focused transcript rerun passed, followed by the full 194/194 pass.
- Page Setup screenshot captured by the Qt test at `build/artifacts/qt-editor/manual/shard-3/artifacts/qt-897-page-setup-editor.png`; it includes the new Plot style table row. Civil 3D is not installed on this machine, so the product comparison is **inconclusive** under CLAUDE.md §3 Verify.

### Verification review

- Architecture: PASS by self-review. Domain owns style names and setup references; IO owns serialization/table reading; PDF plotting applies table values in the existing output path. No new dependency or global state was added.
- Dependencies: PASS. No dependency added; the existing bundled zlib decoder is reused.
- Performance: PASS by path review. Parsing occurs during plotting/import only, not interactive rendering. No performance claim is made.
- Code quality/testing: PASS for REQ-485; focused suite and the complete 194-test CTest suite pass. `git diff --check` reports no whitespace errors.
- Documentation pre-PR review remains pending for README.md, PRIVACY.md, and the public manual wiki. The in-app wiki and Paper-Space-and-Layouts.md were updated. Complete the remaining review before opening the combined PR.

### Status

Implementation and code verification are complete. The Page Setup screenshot is available, but Civil 3D comparison is inconclusive because Civil 3D is not installed; this limitation remains visible in the completion record. README and PRIVACY were reviewed and did not require changes. In-app and public manual wiki pages were updated for the combined PR.
