# ADR-0005: Ratchet dependency violations and gate oversized files

- **Status:** Accepted (amended 2026-11-21: no GoSurvey file may stay grandfathered)
- **Date:** 2026-11-12

## Context

The existing code contains dependency violations and very large files. Enforcing a zero-violation baseline immediately would block incremental work, while allowing the baseline to grow would let the architecture degrade. Large files make review and future ownership moves difficult.

## Decision

Check source includes against the system map and keep a baseline of existing violations that may only shrink. A change that removes a violation also removes it from the baseline. CI blocks new dependency violations and any source file over 3,000 lines; about 1,500 lines is the soft target. Keep the checker testable with fixture trees, including baseline growth and file-size boundary cases.

### Amendment (2026-11-21): the file-size baseline goes to zero

The file-size baseline is temporary, not a permanent exception list. Every GoSurvey file must come
under 3,000 lines, including tests, IO, Geometry, Renderer and Qt. Only vendored code under
`third_party/` is exempt. That folder is the checker's only exclusion today, and it stays that way.

Each grandfathered file is tracked to removal by one umbrella issue (#1580). Splits follow the module seam
that file's owning system will use (ADR-0011, ADR-0012), never arbitrary numbered parts. The PR that
removes the last baseline entry deletes these:

- `.github/file-size-baseline.json`;
- the grandfather and rename-transfer logic in `.github/scripts/check_file_size.py`;
- the `--allow-growth` option and every per-PR exception in `.github/workflows/pr-checks.yml`.

After that PR, the rule is only this: any file outside `third_party/` over 3,000 lines fails.

## Alternatives rejected

- **Require zero violations immediately:** this makes the initial migration impractical and encourages a big-bang rewrite.
- **Allow baseline updates in either direction:** this provides no protection against regression.
- **Use only a soft file-size guideline:** it cannot stop a god file from growing further.

## Consequences

- The repository can start enforcing boundaries before all legacy code has moved.
- Progress is measurable, and removed debt cannot silently return.
- CI needs a maintained baseline and a reliable system map as inputs.
- The hard size gate requires exceptional generated or vendored files to be handled explicitly by checker policy, not by silently weakening the general limit.
