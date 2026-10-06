# TASK — REQ-372 materials (issue #624)

- Branch: `feat/issue624-materials-req372-inc4` (increment 4)
- Authority: **REQ-372**, D-2026-10-05-c, GitHub #624
- Spec-only PR: `docs/req372-materials-624`

## Implementation order (when scheduled)

1. **Inc 1** — Import: resolve `MATERIAL` diffuse → shaded mesh/solid colour override; REQ-201 log for map-only.
2. **Inc 2** — Export R2007+: hand-built `MATERIAL` + `ACAD_MATERIAL` on #611 mesh hosts; 3DSOLID
   entity material deferred (LibreDWG encode corrupts DWG — subentity DXF 331 follow-up).
3. **Inc 3** — `.gs` additive material name + diffuse override (`EntityAttributesToJson` / import name).
4. **Inc 4** — Round-trip tests `[issue624][req372]`; `#614` loss for 3DSOLID entity material gap; 3DSOLID import + mesh export RT.

## Verification (future)

- `./dev/build`, `GoSurveySnapTests.exe "[issue624][req372]"`
