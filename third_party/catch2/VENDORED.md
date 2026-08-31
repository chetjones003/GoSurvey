# catch2 — vendored (amalgamated)

- Upstream: https://github.com/catchorg/Catch2
- Pin: `v3.5.2`
- Form: the AMALGAMATED distribution — `catch_amalgamated.hpp` + `catch_amalgamated.cpp`
  (from `extras/`), not the modular library. `Catch2::Catch2WithMain` in CMake is a
  small static lib built from the `.cpp` (it defines `main()`).
- `catch2/**.hpp` are SHIMS that forward to `../catch_amalgamated.hpp`, so test files
  can keep `#include <catch2/catch_test_macros.hpp>` etc. Add a shim if a test needs a
  path not listed.
- `Catch.cmake` + `CatchAddTests.cmake` (also from `extras/`) drive `catch_discover_tests`.
- Refresh: download the two `extras/catch_amalgamated.*` + the two `extras/Catch*.cmake`
  for the new tag; keep/extend the shims.
