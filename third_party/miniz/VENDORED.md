# miniz — vendored

- Upstream: https://github.com/richgel999/miniz
- Pin: release `3.0.2` (amalgamated `miniz.c` + `miniz.h`, MIT; library version string 11.0.2)
- Vendored: 2026-10-05 (D-2026-10-05-i, ADR-066, REQ-380)
- Trimmed: only `miniz.c`, `miniz.h` and `LICENSE` — no examples, docs or CMake from upstream.
- Refresh: download the pin's `miniz-<tag>.zip` release asset, copy the same three files here, rebuild +
  run ctest, update this file.
