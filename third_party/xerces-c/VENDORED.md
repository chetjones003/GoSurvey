# Xerces-C++ — vendored (headers + prebuilt static lib)

- Upstream: https://github.com/apache/xerces-c
- Pin: `31b4b3a06105dcd607db9fda9d1883ad7e489bfe` (v3.3.0 branch tip; the `v3.3.0` tag object itself
  does not resolve to a commit in this mirror, so the branch tip is pinned by commit hash instead)
- Apache-2.0. Required by `third_party/libe57format` (REQ-172, ADR-042) — libE57Format uses it to
  parse the E57 XML section. Not used directly by any GoSurvey source file.

## What is here
- `include/` — the full public Xerces-C++ header tree (`xercesc/...`).
- `lib/win-x64/xerces-c_3.lib` — prebuilt MSVC x64 **Release**, static (`/MD`, `MultiThreadedDLL`),
  from the pinned commit with: `BUILD_SHARED_LIBS=OFF`, `network-accessor=winsock`,
  `transcoder=windows`, `message-loader=inmemory` (the in-tree Windows-native options — no ICU,
  no libcurl, no external message catalog files to ship).

## Refresh / rebuild the .lib
1. `git clone https://github.com/apache/xerces-c` (or checkout the pinned commit above).
2. From an MSVC x64 Developer Prompt (`vcvars64.bat`):
   ```
   cmake -G Ninja -S xerces-c -B xerces-build ^
     -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF ^
     -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL ^
     -Dnetwork-accessor=winsock -Dtranscoder=windows -Dmessage-loader=inmemory
   ninja -C xerces-build
   cmake --install xerces-build --prefix xerces-install
   ```
3. Copy `xerces-install/include/*` here, and `xerces-install/lib/xerces-c_3.lib` to
   `lib/win-x64/xerces-c_3.lib`.
4. Rebuild GoSurvey + run ctest.

## Debt
No Debug (`/MDd`) build vendored — Debug links this Release `/MD` lib, same as LibreDWG's
precedent (`third_party/libredwg/VENDORED.md`). Expect `LNK4099` (no PDB), not a functional issue.
