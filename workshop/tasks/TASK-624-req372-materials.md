# TASK — REQ-372 materials (issue #624)

- Branch: (implementation TBD) `feat/issue624-materials-req372`
- Authority: **REQ-372**, D-2026-10-05-c, GitHub #624
- Spec-only PR: `docs/req372-materials-624`

## Implementation order (when scheduled)

1. **Inc 1** — Import: resolve `MATERIAL` diffuse → shaded mesh/solid colour override; REQ-201 log for map-only.
2. **Inc 2** — Export R2007+: hand-built `MATERIAL` + `ACAD_MATERIAL` on #611/#612 hosts.
3. **Inc 3** — `.gs` additive material name + diffuse override.
4. **Inc 4** — Round-trip tests `[issue624][req372]`; `#614` loss lines.

## Verification (future)

- `./dev/build`, `GoSurveySnapTests.exe "[issue624][req372]"`
