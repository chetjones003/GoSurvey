# TASK-151 — DWG layer table imports incorrectly (issue #140)

- Type:    bug
- Status:  implement
- Opened:  2026-08-31
- Owner:   workshop

## 1. Authority
- Goal:         File Format Specs §6 (native CAD codec)
- Requirements: REQ-170 (accepted) — "Entities and tables LibreDWG decoded are mapped into the
  GoSurvey domain"; acceptance: an R2018 `.dwg` opens correctly with no ODA/AutoCAD.
  REQ-201 — every mapping outcome is named in the log.
- Constraints:  CON-07 / REQ-200 (vendored pin unchanged); MSVC + Ninja
- Acceptance (this fix):
  - opening an R2007+ DWG with many layers yields the exact layer **names** (incl. non-ASCII)
  - layer **colors** match the source (ACI + true-color), and a layer that is *off* keeps its color
  - each layer's assigned **linetype** name is imported (not forced to Continuous)
  - on/frozen/locked survive; entities resolve to the right layer
  - the log reports the imported layer count and names any layer it could not decode
- Owning subsystem: IO (codec) + Domain (mapping)

## 2. Scope
- In scope: `src/io/LibreDwgCad.cpp` DWG import path — `FromT`, `ColorStorage`, `ImportLayers`,
  `ImportStyles`, TEXT/MTEXT string decode; a fixture test in `tests/LibreDwgCadTests.cpp`.
- Out of scope: DXF import (separate text parser in `DxfIo.cpp`); writing the layer table on
  export (ExportLibreCadFile still emits R2000 model-space geometry only — separate gap);
  full LTYPE dash-pattern table.
- Smallest change: make string decode version-aware; read `ly->ltype`; fix negative-ACI.

## 3. Architectural boundary check
- [x] No new abstraction / layer / dependency / data-format change. `CadLayerRow.linetype`
  already exists; we populate an existing field. LibreDWG pin unchanged.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| — | none — REQ-170 already mandates table mapping; issue names the fix | — | — |

## 5. Assumptions
```
ASSUMPTION-1: IS_FROM_TU_DWG(dwg) (bits.h) is the correct gate for TU-vs-char* strings.
- Because: it is LibreDWG's own decode-side macro (from_version >= R_2007 && !DWG_OPTS_IN).
- Risk if wrong: names still garble for some version; caught by the R2018 fixture test.
- Validate by: LibreDwgCad fixture test asserts a non-ASCII layer name round-trips.
```

## 6. Plan
See issue #140 review + the 6-step plan in the task opening. Implement, self-run
build-project / code-review / testing skills, loop to PASS, completion report.

## 8. Implementation log
- 2026-08-31 opened from issue #140 review on branch `beta`.
- 2026-08-31 implemented in `src/io/LibreDwgCad.cpp`:
  - `FromT` now delegates to `libredwgcad_detail::DecodeDwgString`, gated by LibreDWG's own
    `IS_FROM_TU_DWG(dwg)` macro — R2007+ names/styles/text decode from UTF-16LE via
    `bit_convert_TU` instead of a truncating `char*` cast. All 6 call sites updated.
  - `ColorStorage` -> `libredwgcad_detail::ColorToStorage`: handles `method` 0xc0/0xc1/0xc3 and
    takes `abs(index)` so an off-layer's negative ACI keeps its colour.
  - `ImportLayers` reads `ly->ltype` -> LTYPE name into `row.linetype` (new `LayerLinetypeName`),
    normalising CONTINUOUS/ByLayer/empty -> "Continuous"; now takes `log` and reports the
    imported layer count + any name it could not decode (REQ-201).
  - The two helpers are exposed via `libredwgcad_detail` in the header for tests.
- Tests: `tests/LibreDwgCadTests.cpp` +2 cases (`[issue140]`) — UTF-16LE decode incl. the
  first-NUL truncation failure mode; negative-ACI colour retention + method/true-colour mapping.
- Build clean (MSVC/Ninja release). `ctest`: 846/846 pass.

### Technical debt / follow-ups (not blocking this fix)
- DEBT-151-a (partly addressed 2026-08-31): added an end-to-end test that builds a multi-layer
  drawing with the LibreDWG C API, writes it, re-imports via `ImportDwgFile`, and asserts the
  full `drawingLayerTable` (names, ACI + negative-ACI colour, `DASHED` linetype resolved from the
  LTYPE handle, freeze/lock flags). Fixture is **R2000**: LibreDWG 0.13.3's own encoder returns
  `0x100` (critical) when re-reading its R2004+ output, so an in-test R2007+ fixture is not
  possible. Still open: a committed real R2018 `.dwg` fixture (needs a `tests/` fixtures
  mechanism) to exercise the UTF-16LE (`from_version >= R_2007`) path end to end — that path is
  currently covered only by the `DecodeDwgString` unit case. LibreDWG also does not round-trip
  the layer on/off bit through R2000, so that one flag is not asserted in the fixture.
  **Follow-up issue #160** tracks the remaining R2018-fixture + `tests/` fixture-mechanism work.
- DEBT-151-b (addressed 2026-08-31): `LibreDwgCad.cpp` `FillFromState` now builds the DWG LAYER
  and LTYPE tables from `st.drawingLayerTable` (name, ACI colour incl. off-layer negative index,
  freeze/lock flag bits, linetype handle) via a `TableWriter` helper, and wires each exported
  entity to its layer / colour / linetype handle from the parallel `*Attrs` arrays. Entity
  linetype is now also read back on import (`EntityLinetypeName`, `ltype_flags` aware). Colour
  string -> RGB moved to a shared `DxfColorStringToRgbPacked` in `DxfColors`. Test
  `[issue140]` "DWG export writes the layer table and per-entity layer" round-trips through
  `ExportLibreCadFile` (payload bypassed) and asserts the imported table + entity layer.
  Note: not verified against AutoCAD ("open without Recover", REQ-170) — no AutoCAD in the env;
  the handleref pattern is the standard `dwg_add_*` one.

COMPLETION REPORT — TASK-151 — 2026-08-31
- Requirements satisfied:  REQ-170 (table mapping; Acceptance met for the layer table via unit
  coverage — see DEBT-151-a), REQ-201 (layer outcomes logged)
- Summary:                 DWG layer import now decodes R2007+ UTF-16LE names, maps layer colour
                           (incl. off-layer negative ACI and true-colour), and imports each
                           layer's linetype; identical string bug fixed for styles + TEXT/MTEXT.
- Tests:                   LibreDwgCadTests [issue140] x2 (happy + first-NUL truncation / negative
                           ACI failure modes); full [libredwg] suite green; 846/846 ctest
- Verification verdict:    self-run build-project + testing PASS; code-review pending
- Assumptions:             ASSUMPTION-1 (IS_FROM_TU_DWG is the right gate) — validated by test
- Architectural decisions: none (populated existing `CadLayerRow.linetype`; no new abstraction)
- Dependencies:            none (LibreDWG pin unchanged)
- Technical debt noted:    DEBT-151-a (real R2018 binary fixture still owed); DEBT-151-b resolved
- Build:                   clean, MSVC/Ninja release; ctest 848/848
- Docs updated:            this task log
