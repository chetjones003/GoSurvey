# md4c — vendored

- Upstream: https://github.com/mity/md4c
- Pin: `release-0.5.2` (MIT)
- Vendored: 2026-09-10 (D-2026-09-10-d, ADR-056, REQ-336)
- Trimmed: only `md4c.c`, `md4c.h`, and `LICENSE.md` — no md2html, tests, docs, or CMake from upstream.
- Refresh: check out the pin upstream, copy the same three files here, rebuild + run ctest,
  update this file.
