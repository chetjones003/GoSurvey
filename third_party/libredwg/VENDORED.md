# libredwg — GoSurvey's own copy, built from source

- Upstream: https://github.com/LibreDWG/libredwg
- Base: tag `0.14` (commit d9468ae948b8f07a08efa756c19f8916052358c0)
- Decision: D-2026-09-30-d / ADR-041 (h). GoSurvey maintains this copy: it is upstream's library
  plus the changes listed below. REQ-170 / ADR-041. Product licence is GPL-3.0-or-later because of
  this dependency; this directory is the corresponding source.

## What is here
- `src/`, `include/` — every file upstream tracks under those two directories at the base tag
  (the codepage tables `src/codepages/` included), plus `src/config.h`: the header upstream's CMake
  generates for MSVC x64 (`-DLIBREDWG_LIBONLY=ON -DLIBREDWG_DISABLE_JSON=ON
  -DLIBREDWG_DISABLE_WRITE=OFF -DBUILD_SHARED_LIBS=OFF`), committed so nothing is configured.
- `programs/my_stat.h`, the one upstream header outside `src/` the library includes (from `dwg.c`).
- `COPYING`, `AUTHORS`, `README`, `NEWS` from upstream.
- Not copied: upstream's `test/`, `examples/`, `doc/`, the rest of `programs/`, `bindings/` and
  build system.
  GoSurvey builds only the library, with its own target in the top-level `CMakeLists.txt`
  (same sources and flags as upstream's library-only build; 0.14 adds `logging.c`,
  `decode2.c`, `encode2.c`).

## GoSurvey's changes to upstream
Each change is its own commit. Keep this list current.

| # | Files | What and why | Offered upstream |
|---|-------|--------------|------------------|
| 1 | `src/dwg2.spec` (GEODATA) | The R2000–R2007 (class version 1) GEODATA layout read a second `unknown_b` bit after `zero2` that AutoCAD does not write, so LibreDWG misread AutoCAD's GEODATA and AutoCAD refused one LibreDWG wrote (`eDwgCRCDoesNotMatch`). Found by decoding an AutoCAD-saved R2000 GEODATA bit by bit (REQ-362 item 3, TASK-299). | not yet |
| 2 | `src/encode.c` (`in_postprocess_SEQEND`) | INSERT attribute chains: use `absolute_ref` when rebuilding `first_attrib`/`last_attrib`, not `handleref.value` after offset encoding (issue #606). | not yet |
| 3 | `src/dwg_api.c` (`dwg_add_ATTRIB`) | Re-resolve INSERT `Dwg_Object*` after `API_ADD_ENTITY` may grow `dwg->object[]` (issue #605 heap corruption on 3+ attribs). | not yet |
| 4 | `src/dwg_api.c`, `include/dwg_api.h` | `dwg_add_MLEADERSTYLE`, `dwg_add_MULTILEADER`, `dwg_add_MULTILEADER_branches` for R2010+ native multileader DWG export (issue #619). | not yet |
| 5 | `src/objects.in`, `src/objects.c`, `src/dwg.c` | MLEADERSTYLE marked STABLE; dictionary lookup uses `description` as style name (issue #619). | not yet |
| 6 | GoSurvey `CMakeLists.txt` (`DEBUG_CLASSES`) | Keep `ACAD_MATERIAL` / MATERIAL objects on DWG encode (REQ-372 export); upstream `fixup_NOD` strips them by default. | not yet |
| 7 | `src/dwg_api.c` (`dwg_add_POLYLINE_PFACE`) | Last face vertex `prev_entity` links to the previous owned vertex, not `handle-1` (issue #663). | not yet |
| 8 | `src/out_dxf.c`, `src/common.h`, `src/dwg_spec_shared.h`, `src/dwg.spec` | `dwg_convert_SAB_to_SAT1` reads **ASM BinaryFile** (ShapeManager) streams: **15-byte** ASM magic skip (`BinaryFile4`), RecordTable→SAT `$` remap (tag 12), `body` record boundary, `num_blocks` fix, SAB tag-4/21 plain ints, drop dangling `encr_sat_data` alias; `free_3dsolid` off-by-one (`i < num_blocks`); `CAN_ACIS_IN_DS_DATA` (REQ-320 increment 2, TASK-734, issue #786). | not yet |
| 9 | `src/acds.spec` | AcDs ACIS/ASM records bind to their `3DSOLID` through the index — `datidx` slot (`segment + 48 + offset`: size, 1, `RLL` handle, `RL` local offset) and record (`segment + objdata_algn_offset * 16 + local offset`: `RL` length, bytes) — instead of handing the n-th blob found in the section to the n-th pending handle, which gave 27 of 89 solids in a Plant 3D drawing a neighbour's body. Records of other schemas (preview PNGs) and non-3DSOLID handles are skipped (D-2026-10-08-a, issue #786). | not yet |
| 10 | `src/out_dxf.c` (`SAT_boolean`) | The face logical counter (sense / sides / containment) resets at every record identifier; it used to carry from one face to the next when records with no logicals sat between them, naming a `forward single` face `out forward` (issue #786). | not yet |
| 11 | `src/out_dxf.c` (`SAB_RD1`) | SAB → SAT writes doubles with `%.17g` (exact round trip), not `%g`'s six digits, which made full-circle edges read back as 6.28318 and closed solids fail exact closure checks (issue #786). | not yet |
| 12 | `src/decode.c`, `src/decode_r2007.c` (class section) | The class record's `dwg_version` / `maint_version` are read as `BL` (as the R13–R2000 reader and `Dwg_Class` already have them), not `BS`: a value ≥ 256 mis-framed every later class, so R2018 Plant 3D classes lost their entity flag and decoded as objects (D-2026-10-08-b, issue #786). The encoder still writes `BS`, identical for values < 256. | not yet |

## Moving to a newer upstream release
A recorded decision (ADR-041 (h)). Replace `src/` and `include/` with the new tag's tracked files,
regenerate `src/config.h` with upstream's CMake using the flags above, re-apply every change in the
table, update the version in `CMakeLists.txt` (`GOSURVEY_LIBREDWG_VERSION`), then rebuild and run
ctest (the `[libredwg]` / `[dwg]` / `[dxf]` cases exercise the codec).
