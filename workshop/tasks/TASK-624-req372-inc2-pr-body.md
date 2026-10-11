## Summary

- **REQ-372 increment 2:** Export R2007+ `MATERIAL` / `ACAD_MATERIAL` dictionary entries and attach entity-level material on **mesh/TIN** hosts during DWG write (`DwgExportMaterialContext`, `FillFromState` wiring).
- Vendored LibreDWG: enable `DEBUG_CLASSES` on compile so encode retains `ACAD_MATERIAL` (documented in `third_party/libredwg/VENDORED.md` #6).
- **Deferred:** Entity-level MATERIAL on `3DSOLID` currently corrupts DWG on re-read; solids export without material attachment until encode is fixed (task notes).

## Test plan

- [x] `./dev/test` — `[issue624][req372]` (import diffuse + R2018 mesh MATERIAL export/import round-trip)
- [ ] Broader `[libredwg]` / regression spot-check after merge (optional)

## Links

- Issue #624 / REQ-372
- Depends on inc 1 (#697)
