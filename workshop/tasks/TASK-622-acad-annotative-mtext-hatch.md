# TASK-622 — AcadAnnotative EED for MTEXT, HATCH, MULTILEADER

## Authority

- GitHub issue **#622** (annotative scaling epic; slice after PR #675)
- REQ-110 (proposed sketch; viewport/plot rescale behavior already largely shipped)

## Goal

Extend native DWG **AcadAnnotative** / **AnnotativeData** EED export (and hatch import) to entity types that already support annotative display in GoSurvey but only had partial DWG markers:

- **MTEXT** — had R2018 `is_not_annotative` only
- **HATCH** — had path flag `0x200` only
- **MULTILEADER** — had `is_annotative` only

Parity with TEXT / INSERT (#675): `WriteAnnotativeEntityEed` on export; hatch also reads `ImportEedMarksAnnotative`.

## Out of scope (issue stays open)

- Per-scale visibility / annotation context blobs
- DIMENSION annotative in LibreDWG where fields are missing

## Files

- `src/io/LibreDwgCad.cpp`
- `tests/LibreDwgCadTests.cpp`

## Tests

- Acad-only DWG round-trip for annotative MTEXT and HATCH after stripping GOSURVEY `annotative` EED
- Existing `[issue622]` suite green

## Verification

- `./dev/build`
- `./dev/test` (LibreDwgCad + related)
