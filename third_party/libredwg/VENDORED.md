# libredwg — GoSurvey's own copy, built from source

- Upstream: https://github.com/LibreDWG/libredwg
- Base: tag `0.13.4` (commit e3774bd4020fcfebb68150361db74b8b34d170fe)
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
  (same 21 sources and flags as upstream's library-only build).

## GoSurvey's changes to upstream
Each change is its own commit. Keep this list current.

| # | Files | What and why | Offered upstream |
|---|-------|--------------|------------------|
| — | — | none yet | — |

## Moving to a newer upstream release
A recorded decision (ADR-041 (h)). Replace `src/` and `include/` with the new tag's tracked files,
regenerate `src/config.h` with upstream's CMake using the flags above, re-apply every change in the
table, update the version in `CMakeLists.txt` (`GOSURVEY_LIBREDWG_VERSION`), then rebuild and run
ctest (the `[libredwg]` / `[dwg]` / `[dxf]` cases exercise the codec).
