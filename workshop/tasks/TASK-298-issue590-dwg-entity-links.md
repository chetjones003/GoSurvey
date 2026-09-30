# TASK-298 — GoSurvey DWG files open in AutoCAD without Recover (entity links)

- Type:    bug
- Status:  submitted
- Opened:  2026-09-30
- Owner:   Workshop
- GitHub:  #590

## 1. Authority

- Requirements: REQ-170 (accepted) — "The file AutoCAD opens must do so **without a Recover prompt**
  for the entity set we emit"; REQ-052 (accepted) — "the DWG that GoSurvey writes **opens in AutoCAD
  with no recovery prompt**". ADR-041 (c)/(d).
- Acceptance (REQ-170, verbatim): "File ▸ Export DWG (default) writes R2004; AutoCAD or ODA File
  Converter (oracle) opens it **without Recover** and the emitted entity counts match the log".
  This task delivers the "opens without Recover" half for the R2000 files GoSurvey writes today.
- Owning subsystem: IO (`src/io/LibreDwg.cpp`, `src/io/LibreDwgCad.cpp`).

## 2. Scope

- In scope: every DWG GoSurvey writes through LibreDWG (Export DWG / Save, and the
  `LibreDwgWriteMinimalR2000` probe) opens in AutoCAD 2027 with no `eDwgCRCDoesNotMatch`.
- Out of scope: the R2004 default REQ-170 names (GoSurvey writes R2000 today — pre-existing, and
  LibreDWG 0.13.4 crashes building an R2004 document from scratch, see finding 5); the REQ-362
  GEODATA write (its spike is re-run separately now that this is fixed); upstream LibreDWG patch.
- Smallest change: one IO helper that rewrites the entity links LibreDWG builds, called before
  both encoders run.

## 3. Architectural boundary check

- [x] No — no new dependency, layer, data format or global state. The helper sets fields LibreDWG
  already writes; the file format is unchanged (R2000, same trailer).

## 4. Root cause (evidence, 2026-09-30)

1. Every CRC in a GoSurvey R2000 file is arithmetically correct: file header, header variables,
   classes, each object-map page and each of the 92 objects (Python re-computation, same code
   also validates an AutoCAD-saved R2000 file).
2. The trailer is not it: an AutoCAD R2000 file with GoSurvey's trailer appended opens; a GoSurvey
   file with its trailer cut off still fails.
3. The encoder is not it: LibreDWG re-encoding an AutoCAD-saved R2000 file opens. Only drawings
   **built** with `dwg_new_Document` + `dwg_add_*` fail (R2000 and R14 alike).
4. Ruled out by patching one thing at a time: file-header bytes (maintenance version, preview
   address), AuxHeader, MEASUREMENT, the second file header, and all 348 non-handle header
   variables copied from AutoCAD's file.
5. **Cause:** in R13–R2000 each entity carries `nolinks`. `nolinks = 1` means "my previous and next
   entities are the handles one below and one above mine"; otherwise explicit prev/next handles
   follow. LibreDWG's add API sets `nolinks = 1` on each new entity and clears it on the previous
   one when the next arrives, so the **last entity of every block keeps `nolinks = 1`** and claims a
   next entity (handle + 1) that does not exist. AutoCAD reports that broken chain as
   `eDwgCRCDoesNotMatch` (LibreDWG's NEWS records ODA reporting "CRC does not match" for other
   invalid values too). Giving the lone LINE of a from-scratch file explicit null links makes it
   open; re-linking the real `extracted-a.dwg` (52 LWPOLYLINEs) the same way makes it open.
   AutoCAD's own R2000 file writes `nolinks = 0` with explicit links on every entity.
6. Side finding: `dwg_new_Document(R_2004, …)` + `dwg_write_file` crashes LibreDWG 0.13.4.

## 5. Plan

- `LibreDwgLinkBlockEntities(Dwg_Data*)` in `src/io/LibreDwg.cpp` (declared in `LibreDwg.hpp`):
  for every BLOCK_HEADER, walk its `entities[]` in order and give each entity `nolinks = 0`,
  `prev_entity` = the previous one (null for the first), `next_entity` = the following one (null
  for the last), as absolute soft pointers. Two uses: `LibreDwgWriteMinimalR2000` and
  `ExportLibreCadFile` (after `FillFromState`, before either encoder).
- Tests (fail before the fix, pass after):
  - `LibreDwgTests.cpp`: the minimal R2000 file's LINE is read back with `nolinks == 0` and null
    prev/next.
  - `LibreDwgCadTests.cpp`: an export with a line, circle, arc, polyline and text reads back as one
    model-space chain — first prev null, each next = the following entity, last next null, every
    `nolinks == 0`, chain length = entity count.
- Oracle (manual, AutoCAD not in CI): `accoreconsole /i` on the minimal file, the REQ-071
  transcript output, and a mixed-entity export.

## 6. Implementation log

- 2026-09-30 root cause found (section 4); plan written.
- 2026-09-30 implemented as planned. The test helper selects model-space entities by `entmode == 2`
  (R2000 stores no owner handle for them) and skips the block's BLOCK / ENDBLK markers. Self-review:
  the helper first collects the block's valid entities, then links them, so a null or non-entity
  slot in `entities[]` can neither crash it nor become a link target.

## 7. Completion report

- Tests (`tests/LibreDwgCadTests.cpp`, GoSurveySnapTests, `[issue590]`, 2 cases / 27 assertions,
  pass): the minimal R2000 file's LINE and a line/line/circle/arc/polyline export both read back as
  one explicit chain (`nolinks` 0, first prev null, each next = the following entity, last next
  null); the export still imports with the same entity counts. Mutation check: with the helper
  disabled both cases fail on `nolinks == 0` and the links.
- Build: `./dev/build` clean. `./dev/test`: 2011/2018 pass; the 7 failures are the headless
  transcripts already failing on beta (issue233, issue402-offset-ucs, regression-58-offset,
  req068-surface-selection, req087-feature-line-modify, req313-solid-isolines,
  req313-solid-primitives), none touch DWG. All `[dwg]`/`[dxf]`/`[libredwg]` cases pass in both
  test binaries.
- Oracle (AutoCAD 2027 `accoreconsole /i`, run by hand, not in CI): all **214** DWGs the headless
  transcripts wrote in this test run open with no error (before the fix: ErrorStatus 53, e.g.
  `req071-contour-extract/extracted-a.dwg`). They cover lines, arcs, circles, polylines, text,
  blocks, survey points and the ADR-044 trailer.
- Assumptions: none that affect the spec.
- Technical debt / follow-ups (not in this task): REQ-170 names R2004 as the default but GoSurvey
  writes R2000, and LibreDWG 0.13.4 crashes building an R2004 document from scratch (finding 6);
  the REQ-362 GEODATA write spike can now be re-run; the LibreDWG add-API bug could be reported
  upstream.
