# libE57Format — vendored (headers + prebuilt static lib)

- Upstream: https://github.com/asmaloney/libE57Format
- Pin: `1a5f9a830c49c117eef2d64f9522b8ecd7d6e1a4` (default branch tip, 2026-09-17), v3.4.0
- Boost Software License 1.0 (`LICENSE.md`). File Format Specs §5 / ADR-042(d) — REQ-172's E57
  reader/writer. Depends on `third_party/xerces-c` for XML parsing.

## What is here
- `include/*.h` — the public C++ Simple API (`E57SimpleReader.h`, `E57SimpleWriter.h`,
  `E57SimpleData.h`, `E57Exception.h`) plus the lower-level `E57Format.h`. GoSurvey's E57 reader
  should use the **Simple API** (`e57::Reader`/`e57::Data3D`) — it is the intended integration
  surface for "read points out of a scan," not the general E57 node-tree API.
- `lib/win-x64/E57Format.lib` — prebuilt MSVC x64 **Release**, static, `/MD`
  (`MultiThreadedDLL`), from the pinned commit with: `BUILD_SHARED_LIBS=OFF`, `E57_BUILD_TEST=OFF`,
  **`E57_RELEASE_LTO=OFF`** (the default is ON; turned off here because `/GL` object files bloat
  the vendored `.lib` roughly 9x with no benefit unless the final GoSurvey link is also built
  `/LTCG`, which it is not).

## Refresh / rebuild the .lib
1. Build and install Xerces-C first — see `third_party/xerces-c/VENDORED.md`.
2. `git clone https://github.com/asmaloney/libE57Format` (or checkout the pinned commit above).
   The repo has a `test/extern/googletest` submodule; it auto-inits on configure even with
   `E57_BUILD_TEST=OFF` (harmless, just skip using it).
3. From an MSVC x64 Developer Prompt (`vcvars64.bat`):
   ```
   cmake -G Ninja -S libE57Format -B libE57Format-build ^
     -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF ^
     -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DE57_BUILD_TEST=OFF -DE57_RELEASE_LTO=OFF ^
     -DCMAKE_PREFIX_PATH=<path to xerces-install>
   ninja -C libE57Format-build E57Format
   ```
4. Copy `libE57Format/include/*.h` here, and `libE57Format-build/E57Format.lib` to
   `lib/win-x64/E57Format.lib`.
5. Rebuild GoSurvey + run ctest.

## Debt
No Debug (`/MDd`) build vendored — same precedent as LibreDWG/Xerces-C above; expect `LNK4099`,
not a functional issue.
