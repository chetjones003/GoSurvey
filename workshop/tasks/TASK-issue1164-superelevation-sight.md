# TASK-issue1164 — Superelevation and sight-distance checks

**Requirement authority:** REQ-522 (accepted 2026-11-09, D-2026-11-09-e; GitHub #1164). Replaces the REQ-502 stubs.
Builds on REQ-502, REQ-480, REQ-481, REQ-491.

**Constraints:** no new dependencies; no Qt control change (the criteria column is the existing text); no regional tables.

## Files and subsystems

| Area | Files |
| --- | --- |
| Domain (pure) | `src/alignment/AlignmentDesign.hpp`, `src/alignment/AlignmentDesign.cpp` (two formulas, two checks, criteria text) |
| Report | `src/alignment/AlignmentReport.cpp` (stub lines replaced by assumption lines) |
| Commands | `src/commands/CadCommands_Alignment.cpp` (`SET ... FRICTION|CLEARANCE`, usage) |
| Persistence | `src/io/GsIo.cpp` (`sideFriction` and `sightClearance`, written only when > 0) |
| Tests | `tests/AlignmentDesignTests.cpp` (REQ-522 cases; REQ-502 report test updated to the new text) |
| Docs | `resources/wiki/Alignments.md`, `resources/wiki/Commands/alignmentcheckset.md` |

## Implementation approach

- `RequiredSuperelevation(V mph, R ft, f) = V_fps² / (32.174 R) − f`.
- `AvailableSightDistance(R, L, m) = min(2 R acos(1 − clamp(m, 0, R) / R), L)`.
- `CheckDesign` adds both checks inside the existing Arc branch, at the highest design speed over the span (REQ-502 AC2).
  Each check is off when its limit or input is zero.
- Findings flow through the existing `CheckDesign` callers (layout, LIST, report, markers), so no new consumer is needed.

## Test approach

- Analytical: V = 60 mph, R = 1000, f = 0.10 → e = 0.1407; R = 500, m = 25 → S = 317.56; arc cap at 174.53 for a 20° curve.
- Boundary: maximum equal to the required rate at display precision passes; clearance equal to R caps at arc length.
- Off states: no speed, zero maximum, no clearance. Refusal of negative values. Save/reload and omitted-when-zero JSON.

## Architectural-boundary check

Domain stays free of Qt, GsIo and commands. The new formulas are pure functions in `AlignmentDesign`. Persistence stays
in `GsIo`. No new abstraction (one concrete use each).

## Assumptions and technical debt

- Design speed is mph; distances are feet; the drawing's `'` suffix is assumed (existing).
- Sight beyond the curve's ends is not counted (conservative); S > L formula not implemented.
- Ribbon "Superelevation" and "Sight Distance" buttons remain disabled ("not implemented yet").
- Civil 3D parity comparison: waived by the user for this issue; no Qt control changed.
