## Summary

- Adds **REQ-372** (issue **#624** materials slice): diffuse RGB material appearance on 3D mesh/solid hosts + R2007+ DWG `MATERIAL` / `ACAD_MATERIAL` interop, in four phased increments.
- Records decision **D-2026-10-05-c** in `spec/project.md`.
- Updates `docs/dwg-feature-gaps.md` — materials no longer a SPEC GAP; lights/sun still are.

## Test plan

- [x] Spec-only change — no code tests.
- [ ] Implementation PR(s) will use `[issue624][req372]` tests per REQ-372 acceptance.

## Notes

Visual styles remain **REQ-371** (shipped). Lights/sun need a separate REQ when scheduled.
