# LASzip — vendored upstream source, built from source

- Upstream: https://github.com/LASzip/LASzip
- Base: tag `3.5.0` (release published 2025-12-08 by rapidlasso)
- Decision: issue #825 increment 4 (REQ-172's LAZ format — the last of the four formats #825
  commits GoSurvey to). LASzip is the reference implementation of the compression scheme `.laz`
  files use; GoSurvey vendors upstream's source and builds it in-tree, the same way it vendors
  LibreDWG (`third_party/libredwg/VENDORED.md`) rather than linking a prebuilt binary.
- License: **Apache License 2.0** (`LICENSE.txt`, copied from upstream's `COPYING.txt`). This is
  compatible, for GoSurvey's direction of linking (GoSurvey statically links LASzip, not the other
  way around), with GoSurvey's own GPL-3.0-or-later licensing — which is already a consequence of
  vendoring LibreDWG (`spec/file-format-specs.md` §2 item 5) and is unaffected by adding this
  Apache-2.0 dependency alongside it. No top-level `LICENSE` file changes were needed.

## What is here
- `src/` — every `.cpp`/`.hpp` file upstream's `src/CMakeLists.txt` lists under `LASZIP_SOURCES`
  (the arithmetic coder, the point item (de)compressors for LAS 1.0-1.4, `laszip.cpp`'s VLR/codec
  setup, and `laszip_dll.cpp`, which implements the public C API's functions directly).
- `include/laszip/laszip_common.h`, `include/laszip/laszip_api_version.h` — upstream's small
  shared-type/version headers, unmodified.
- `dll/laszip_api.h` — the public C API's declarations (`laszip_create`, `laszip_open_reader`,
  `laszip_read_point`, `laszip_open_writer`, `laszip_write_point`, ... — this is the surface
  `src/io/CadPointCloudLaz.cpp` calls). **`dll/laszip_api.c` is vendored for reference but is NOT
  compiled**: it is upstream's *dynamic-loading* shim (function pointers populated by
  `laszip_load_dll()`, for a consumer that `dlopen`s/`LoadLibrary`s a LASzip shared library at
  runtime) and defines the exact same C symbols `laszip_dll.cpp` already defines directly —
  compiling both into one static library is a duplicate-symbol link error. GoSurvey links
  `laszip_dll.cpp`'s real implementations straight in; there is no DLL boundary, so the loader
  shim has nothing to do.
- `LICENSE.txt` (upstream's `COPYING.txt`), `AUTHORS.txt`, `README.md`, `CHANGES.txt` — from
  upstream, unmodified.
- Not copied: upstream's own CMake/VS project files, `example/`, `docs/`, `design/`, `scripts/`,
  `cmake/`, `tools/`, `data/` (test fixtures), and `unused/` (an old, unused range coder upstream
  itself no longer builds).

## GoSurvey's changes to upstream
None. Every vendored file is upstream's byte-for-byte source — only the *set* of files vendored
(and which ones GoSurvey's own `CMakeLists.txt` compiles) differs from upstream's full tree.

## How GoSurvey builds it
A single static library target (`laszip`, aliased `Laszip::laszip`) in the top-level
`CMakeLists.txt`, compiling exactly `src/CMakeLists.txt`'s `LASZIP_SOURCES` list minus the two
plain headers that list also carries (`mydefs.hpp`, `../include/laszip/laszip_common.h` — headers
need no compile unit). `LASZIP_API_VERSION` is defined (selects `laszip_api.h`'s
`<laszip/...>`-prefixed include form, matching where this vendoring put those two headers) and
propagated `PUBLIC` so any consumer that also includes `laszip_api.h` directly takes the same
branch. No `LASZIP_DYN_LINK` define — `LASZIP_API` expands to nothing, a plain static-link symbol,
never `__declspec(dllexport/dllimport)`.

## Moving to a newer upstream release
Replace `src/`, `include/laszip/`, and `dll/laszip_api.h` (+ `dll/laszip_api.c`, vendored but
unused, for anyone diffing against upstream) with the new tag's versions, re-diff
`src/CMakeLists.txt`'s `LASZIP_SOURCES` list against the top-level `CMakeLists.txt`'s `laszip`
target in case upstream added/removed a source file, bump the tag and
`GOSURVEY_LASZIP_VERSION` above, then rebuild and run ctest (`[laz]` cases — including the
LAS/LAZ cross-check in `tests/CadPointCloudLazTests.cpp` — exercise the codec).
