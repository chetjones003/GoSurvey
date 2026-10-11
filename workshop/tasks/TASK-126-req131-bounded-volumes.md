# TASK-126 — Bounded volume clip on the existing sampler (REQ-131)

- Type:    feature
- Status:  implement
- Opened:  2026-08-27
- Owner:   workshop

## 1. Authority
- Goal:         M-Surfaces-119
- Requirements: REQ-131 (accepted D-2026-08-27-a)
- Constraints:  ADR-039 (i); CON-06; no volume-surface entity
- Acceptance:
  - the 5 ft × 1 acre fixture matches 21,780 ft³ within 1%;
  - a clip that misses both surfaces reports zero and says there is no overlap inside the clip;
  - omitting the clip preserves today's full-overlap behaviour.
- Owning subsystem: util (surfacevolume), Commands, UI

## 2. Scope
- In scope: optional closed-polyline clip on `ComputeSurfaceVolume`; `VOLUMES` third argument; dashboard clip picker / `VOLDASH CLIP`.
- Out of scope: volume-surface entity; watersheds (Phase 3).
- Smallest change: skip sample cells whose centres fall outside the ring.

## 3. Architectural boundary check
- [x] No — ADR-039 (i) already names a clip on the existing sampler.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | 1 acre × 5 ft is 217,800 ft³, but Acceptance says 21,780 ft³ (806.67 yd³) | 2026-08-27 | Follow the cubic number: 5 ft × 4,356 ft² (0.1 acre / 66×66 ft square). The "1-acre" wording is treated as a slip vs the stated yards. |

## 5. Assumptions
```
ASSUMPTION-1: Clip ring is sampled in TIN local XY (polyline world minus document origin).
- Because: ComputeSurfaceVolume already samples CadTin local verts.
- Risk if wrong: clip misses the surface at large origin.
- Validate by: Catch2 with origin-free fixtures; command transcript with a drawn polyline.
```

## 6. Plan
- Approach: optional clip ring on ComputeSurfaceVolume; cell centres outside TinPointInPolygon contribute nothing.
- Files: surfacevolume.*, CadCommands.*, CadUi_VolumeDashboard.cpp, SurfaceVolumeTests, headless transcript.
- Test: 66×66×5 ft → 21,780 within 1%; miss clip → overlapped false; no clip → existing tests unchanged.

## 8. Implementation log
- 2026-08-27 opened after Phase 1 commit e44b344.
- 2026-08-27 implemented: optional clip ring on ComputeSurfaceVolume; VOLUMES / VOLDASH CLIP / dashboard picker. Catch2 21780 ft3 fixture; req073 clip-id refusals.
