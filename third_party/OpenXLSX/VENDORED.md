# OpenXLSX

- **Upstream:** https://github.com/troldal/OpenXLSX
- **Pin:** tag `v0.5.1`, commit `2eb596ac69d5ef8c1a1be1de8ee481b5ce39baea` (2026-06-15).
- **License:** BSD-3-Clause (`LICENSE.md`).
- **Form:** `headers/` and `sources/` copied unmodified from `OpenXLSX/` in the pinned commit.
- **Added for:** GitHub issue #804 (REQ-467 Extract Data, REQ-468 Link Data). Approved as D-2026-10-30-a.
- **Local files (not upstream):**
  - `generated/OpenXLSX-Exports.hpp` replaces the header upstream CMake generates; export macros are empty for a static build.
  - `shims/miniz_zip.h` forwards to the repo's vendored `third_party/miniz/miniz.h`, which already contains the zip API Zippy uses.
- **Build flags:** the OpenXLSX sources get `/analyze- /W0` in `CMakeLists.txt` (the same treatment as pugixml). The `msvc-analyze` job builds with `/WX`, and upstream's `XLComments.cpp` has an unused variable (C4101) that would fail it. The sources are not edited.
- **Dependencies:** pugixml (repo copy, `third_party/pugixml`, v1.14) and miniz (`third_party/miniz`). No other copies are bundled.
- **Update:** replace `headers/` and `sources/` from a new tag, keep the two local files, and rerun the OpenXLSX tests.
