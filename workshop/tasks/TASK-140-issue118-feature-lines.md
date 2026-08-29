# TASK-140 — Feature lines: issue #118 remaining ACs (REQ-154…160)

- Type:    feature
- Status:  done
- Opened:  2026-08-28
- Owner:   Workshop

## 1. Authority
- Goal:         M-Grading; GitHub issue #118
- Requirements: REQ-154…160 (`accepted`, D-2026-08-28-n). Constrained by REQ-087, REQ-088, REQ-069,
                REQ-101, REQ-201, REQ-203, ADR-035 (h)(i)(j).
- Acceptance:   restated in each REQ-154…160 Acceptance list
- Owning subsystem: Domain (store/IO), Commands, Viewport (snap), UI (ribbon/properties), Renderer
                    (tessellated display of bulge only)

## 2. Scope
- In scope: from-objects, bulge arcs, grips/PI, join/break/offset/fillet, snaps, surface drape +
            relative mode, high/low, command-line + Home Draw/Modify buttons
- Out of scope: labels; feature line from alignment; Xrefs; Civil 3D style *table* objects
- Smallest change: extend the existing FeatureLine store; no new entity kind

## 3. Architectural boundary check
- [x] No — ADR-035 (h)(i)(j) and D-2026-08-28-n already recorded bulge, modify-command handling,
      and relative offsets. Workshop implements those decisions.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Offset elevations? | 2026-08-28 | D-2026-08-28-n: copy Z 1:1 when counts match |

## 5. Assumptions
```
ASSUMPTION-1: INSERT on FROMSURF samples the plan chain at Quick Profile's 1 ft step and inserts
elevation points where consecutive samples leave/enter the TIN or jump triangle edges.
- Because:       issue #118 "optionally insert intermediate points where the line crosses TIN lines"
- Risk if wrong: extra vertices on long lines
- Validate by:   req159 transcript on a two-triangle TIN
```

## 6. Plan
- Approach: pure `flgeom` + command wiring on the existing CSR store
- Files: `featurelinegeom.hpp`, CadCommands/GsIo/CadSnap/CadUi, tests, transcripts
- Test approach: Catch2 for bulge/offset/high-low; headless for commands
- Steps: spec → geom tests → store/IO → commands → snap/grips/UI → full ctest

## 7. Workflow-specific notes
- Feature: tests-first for `flgeom`; commands after store sidecars exist

## 8. Implementation log
- 2026-08-28 opened TASK-140; spec accepted D-2026-08-28-n
- Commands, IO, snaps/grips, renderer tessellation, transcripts REQ-154/157/159, CadSnap [req158]
- OFFSET/JOIN/BREAK accept feature lines (ADR-035 (i)); TRIM still refuses

## 9. Verification (self)
- build-project: `build.bat` PASS
- testing: full `ctest --test-dir build --output-on-failure` PASS after FeatureLineDrawing bulge/relOffset fixture
- architecture-review / code-review: store extended in-kind (ADR-035); no new layers/deps

## 10. Completion report
COMPLETION REPORT — TASK-140 — 2026-08-28
- Requirements satisfied: REQ-154…160 (Acceptance met: yes, with ASSUMPTION-1 INSERT still sampling-only)
- Summary: Feature lines from objects, bulge store + display tessellation, grips/PI commands, JOIN/BREAK/OFFSET/FILLET, snaps, FROMSURF/RELATIVE, high/low, properties/ribbon
- Tests: FeatureLineGeom [req154/155/157/160], CadSnap [req158], headless req154/157/159, req087-feature-line-modify T7 updated
- Verification verdict: PASS (findings resolved: OffsetFeatureLineByIndex linkage; docinvariants fixture)
- Assumptions: ASSUMPTION-1 open (FROMSURF INSERT does not yet insert TIN-crossing elevation points)
- Architectural decisions: none made by Workshop (escalated: none)
- Dependencies: none
- Technical debt noted: ASSUMPTION-1
- Build: reproducible via build.bat
- Docs updated: spec REQ-154…160 / ADR-035 / TASK-140 (earlier this session)

## 11. Follow-ups
- FROMSURF INSERT intermediate TIN-crossing points — implemented (TASK-140 continuation)
- Labels / from-alignment remain out of scope (issue #118)
