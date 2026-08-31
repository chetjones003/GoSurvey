# libredwg — vendored (headers + prebuilt static lib)

- Upstream: https://github.com/LibreDWG/libredwg
- Pin: tag `0.13.3` (peeled commit 97c7225596c17430b82fd0161e7eff6beb5b1034)
- REQ-170 / ADR-041. Product licence is GPL-3.0-or-later because of this dependency.

## What is here
- `include/*.h` — the public headers (`dwg.h`, `dwg_api.h`).
- `src/*.h` — the internal headers `src/io/LibreDwgCad.cpp` includes (`bits.h`,
  `out_dxf.h`, and their transitive `common.h` / `dec_macros.h` / …) PLUS the
  generated `config.h` (from `src/cmakeconfig.h.in`, MSVC x64 Release configuration).
- `lib/win-x64/libredwg.lib` — prebuilt MSVC x64 **Release**, `/MD`, from the 0.13.3
  tag with: LIBREDWG_LIBONLY=ON, DISABLE_WERROR=ON, ENABLE_LTO=OFF,
  LIBREDWG_DISABLE_JSON=ON, LIBREDWG_DISABLE_WRITE=OFF, BUILD_SHARED_LIBS=OFF.

## Debt
`ninja-debug` links this Release `/MD` lib (no `libredwgd.lib`). LibreDWG is pure C
with an allocator-neutral API, so this works; expect `LNK4099` (no PDB) on Debug.
Add `lib/win-x64/libredwgd.lib` if a real Debug build of the codec is needed.

## Refresh / rebuild the .lib
1. `git clone --branch 0.13.3 --depth 1 https://github.com/LibreDWG/libredwg`
2. Configure with the flags above (see the old `cmake/LibreDwg.cmake` in git history,
   removed in the D-2026-08-31-b commit) + `-A x64` / Ninja + MSVC, Release.
3. `cmake --build . --config Release` → copy `libredwg.lib` here.
4. Copy `include/*.h`, `src/*.h`, and the build tree's generated `src/config.h`.
5. Rebuild GoSurvey + run ctest (the `LibreDwg*Tests` cases exercise the codec).
