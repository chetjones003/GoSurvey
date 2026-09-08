# TASK-226 — issue #375: GoSurveyTests crashes with 0xC0000409 (MSVC built without /EHsc)

- Type:    bug fix
- Status:  done — PR (fix/issue375-msvc-ehsc)
- Opened:  2026-09-08
- Owner:   Workshop
- GitHub:  #375

## 1. Authority

- REQ-200 / D-2026-08-31-b — vendored, reproducible Windows build; `build.bat` + `CMakePresets.json`
  are authoritative.
- REQ-324 note in `CMakeLists.txt:68-75` already documents this exact hazard: a first configure with
  `-DCMAKE_CXX_FLAGS=...` REPLACES CMake's MSVC seed (`/DWIN32 /D_WINDOWS /EHsc /GR`) and silently
  drops `/EHsc`, "which broke exception-using tests with a stack-buffer-overrun crash".

## 2. Problem — expected vs. actual

Expected: `ctest` passes; a thrown C++ exception is caught by its handler.

Actual: `GoSurveyTests.exe "a missing or corrupt store loads as an empty list"` terminates with exit
`0xC0000409` (100% reproducible in isolation, "intermittent" across a full randomised `ctest` run).
30+ historical `AppCrash_GoSurveyTests.ex_*` WER reports; also seen as a "pre-existing failure" note
in TASK-224 / PR #428 and PR #374.

## 3. Root cause (verified in-session)

The checked-out `build/` tree has `CMAKE_CXX_FLAGS:STRING=` (empty) in `CMakeCache.txt`. A fresh
configure produces `/DWIN32 /D_WINDOWS /EHsc`. The tree was configured once with an empty/overridden
`CMAKE_CXX_FLAGS` (the REQ-324 `/analyze` experiment), and `build.bat` only re-runs configure when
`build.ninja` is missing, so the poisoned cache survived every incremental build.

Without `/EHsc`, MSVC omits unwind data. `ReadRaw` in `src/io/RecentDrawings.cpp:37-60` calls
`f >> j` on `"{ this is not json ]["`; `nlohmann::json::parse_error` is thrown, finds no reachable
handler, and hits `std::terminate` → `abort` → `__fastfail` → exit `0xC0000409` in `ucrtbase.dll`.
The `catch (...)` at line 58 never runs. Every `catch` in the binary was equally affected.

Why the reporter's standalone repro didn't crash: they compiled it with a normal `cl` invocation
that included `/EHsc`.

## 4. Fix (smallest correct)

`CMakeLists.txt`, immediately after the MSVC policy block: if `MSVC` and `CMAKE_CXX_FLAGS` does not
already contain `/EH` (resp. `/GR`), `string(APPEND ...)` the flag. Restores the two flags no matter
how `CMAKE_CXX_FLAGS` was seeded; a normally-configured tree already has them and is unchanged
(the guard is a no-op). Covers our targets and the vendored ones (Catch2, Dear ImGui) that also
rely on exceptions/RTTI. No behaviour change to a healthy build.

## 5. Files

- `CMakeLists.txt` — the `if(MSVC)` `/EHsc` + `/GR` restoration block (comment references #375).

## 6. Architectural-boundary check

Build-system only. No source, dependency, or SPEC change. Reinforces the REQ-324 hazard note that
was already in this file.

## 7. Verification

- Reconfigured the previously-poisoned `build/` tree with `cmake --preset ninja-release`; compile
  commands now carry `/EHsc /GR`.
- `GoSurveyTests.exe "a missing or corrupt store loads as an empty list"` → `All tests passed`,
  exit 0 (was `0xC0000409`, 20/20).
- Full `cmake --build build` + `ctest` — see PR / completion note.

## 8. Result

**PASS.** Crash eliminated at the root; issue #375 closed.
