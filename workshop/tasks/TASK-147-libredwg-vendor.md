# TASK-147 — Vendor LibreDWG on MSVC + Ninja

- Type:    feature
- Status:  implement
- Opened:  2026-08-29
- Owner:   workshop

## 1. Authority
- Goal:         File Format Specs §6 increment 1 (native CAD codec)
- Requirements: REQ-170 (accepted)
- Constraints:  CON-07 / REQ-200 (pin, artifacts in build tree); MSVC + Ninja
- Acceptance (this increment, not full REQ-170):
  - LibreDWG is compiled with this repo's `cl` + Ninja and **statically linked** into `GoSurvey`
  - a version-tag probe works in-process (no ODA / AutoCAD)
  - a tiny R2004 DWG can be written and read back in-process
- Owning subsystem: IO (codec) + Build

## 2. Scope
- In scope: FetchContent pin, static `libredwg`, thin `src/io/LibreDwg.*`, Catch2 smoke, GPL note in installer license
- Out of scope: mapping into CAD stores; replacing `DxfIo` / converter File Import; R2018 write
- Smallest change: link the library and prove write/read of one LINE

## 3. Architectural boundary check
- [x] No — ADR-041 / D-2026-08-29-g already chose LibreDWG and static-or-dynamic GPL link

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| — | none; static link matches “binaries built into GoSurvey” | — | — |

## 5. Assumptions
```
ASSUMPTION-1: Pin GNU LibreDWG 0.13.3 (97c7225596c17430b82fd0161e7eff6beb5b1034)
- Because: latest 0.13.x annotated release; 0.14 tags are rolling snapshots
- Risk if wrong: MSVC compile fail → bump pin
- Validate by: ninja-release configure + libredwg.obj compile
```

## 6. Plan
- Approach: FetchContent + upstream CMake (`LIBREDWG_LIBONLY`, static, no `/WX`), alias `LibreDWG::libredwg`, wrap `dwg_new_Document` / `dwg_write_file` / `dwg_read_file`
- Files: `cmake/LibreDwg.cmake`, `CMakeLists.txt`, `src/io/LibreDwg.hpp/.cpp`, `tests/LibreDwgTests.cpp`, `installer/License.txt`
- Test: happy = R2004 write then header reads as AC1018 / R2004; failure = missing path does not succeed
- Steps: as listed

## 8. Implementation log
- 2026-08-29 worktree `GoSurvey-libredwg` on `feat/libre-dwg` from `origin/master` (4966686)
- 2026-08-29 File Import/Export DXF+DWG is LibreDWG only (`LibreDwgCad.cpp`); in-tree pair parser / `DxfEntityEmit` removed. Save is R2000. DXF write goes through an intermediate DWG because `dwg_write_dxf` on a `dwg_add_*` document emits an empty ENTITIES section. Converter remains for 3D tessellation only. LibreDWG ASCII is not byte-stable across round-trip; headless SAMEFILE oracles that assumed the old writer now check entity survival.
