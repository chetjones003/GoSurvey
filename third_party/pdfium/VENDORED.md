# pdfium — vendored

- Upstream: https://github.com/bblanchon/pdfium-binaries
- Pin: `chromium/7857`
- Vendored: 2026-08-31 (D-2026-08-31-b, issue #142)
- Trimmed: docs, tests, examples, `.git`, CI config — only what the build compiles/links.
- Refresh: check out the pin upstream, copy the same file set here, rebuild + run ctest,
  update this file.
