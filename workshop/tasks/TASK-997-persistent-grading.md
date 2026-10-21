# TASK-997 — Persistent editable grading from a feature line

- Type: feature
- Status: verify
- Authority: REQ-398 as extended by D-2026-10-20-f (GitHub issue #997, sub-issue of #802). The
  user chose persistent grading objects over the baked-only first increment. Historical TASK-289 and
  the `req371-grading-daylight` transcript still say REQ-371; current REQ-371 is unrelated.
- Constraints: ADR-028 consequences (no new store or entity kind), REQ-076 stable ids, ADR-044 DWG
  payload, REQ-074 slope wording and no extrapolation, REQ-101 tolerance.
- Glossary: no GLOSSARY.md exists; uses REQ-398's baseline, cut, fill, target surface, daylight line.

## Plan

- Domain: `CadGradingDefinition` (baseline id, surface id, cut run, fill run, side) as an optional
  field on `CadFeatureLineInfo`. Feature-line info already rides in undo snapshots, document
  switching and the internal payload, so no new plumbing.
- IO: `GsIo` writes/reads an optional `grading` object per feature-line info row. Absent or unusable
  (no baseline id, non-positive slope) loads as an ordinary feature line. DWG reopen reads the
  ADR-044 trailer, so the definition survives DWG save/reopen.
- Commands: GRADING on a selected (or picked) grading result enters edit mode
  (`gradingEditId`), restoring the stored inputs. Enter rebuilds the same line in place
  (`ReplaceFeatureLineVertices`), keeping id, layer and name, with one undo step. Missing baseline:
  reported, command ends, nothing changes. Missing surface: reported, no fallback to another
  surface until the user types S. Failed calculation or Escape: nothing changes.
- UI: the four Qt Grading ribbon controls (CgdGrading, CdGrading, SvyGrading, TsGrade) start
  GRADING. Grading Optimization stays a placeholder (#813).
- Out of scope: automatic re-grading on source edits, grading groups/volumes (#998), optimization.

## Tests (tests/QtEditorTests.cpp)

- definition stored on create, with hand-computed 15 ft offset (5 ft fill at 3:1);
- edit rebuilds in place (10 ft at 2:1), id/layer/name kept, undo restores geometry and definition;
- Escape and a failed rebuild leave geometry and definition unchanged;
- DWG save/reopen keeps the definition and the result can be rebuilt after reopen;
- payload without `grading` (pre-#997) and with a broken definition load as ordinary lines;
- deleted baseline reported and left intact; deleted surface asks rather than substitutes;
- the four ribbon controls are live and start create and edit; screenshots under build/artifacts.

## Boundary check

Domain/Commands/IO/UI ownership unchanged; no dependency, global, entity kind or store added.

## Evidence

See the PR for build and test results.
