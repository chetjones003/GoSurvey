# ADR-0005: Ratchet dependency violations and gate oversized files

- **Status:** Accepted
- **Date:** 2026-11-12

## Context

The existing code contains dependency violations and very large files. Enforcing a zero-violation baseline immediately would block incremental work, while allowing the baseline to grow would let the architecture degrade. Large files make review and future ownership moves difficult.

## Decision

Check source includes against the system map and keep a baseline of existing violations that may only shrink. A change that removes a violation also removes it from the baseline. CI blocks new dependency violations and any source file over 3,000 lines; about 1,500 lines is the soft target. Keep the checker testable with fixture trees, including baseline growth and file-size boundary cases.

## Alternatives rejected

- **Require zero violations immediately:** this makes the initial migration impractical and encourages a big-bang rewrite.
- **Allow baseline updates in either direction:** this provides no protection against regression.
- **Use only a soft file-size guideline:** it cannot stop a god file from growing further.

## Consequences

- The repository can start enforcing boundaries before all legacy code has moved.
- Progress is measurable, and removed debt cannot silently return.
- CI needs a maintained baseline and a reliable system map as inputs.
- The hard size gate requires exceptional generated or vendored files to be handled explicitly by checker policy, not by silently weakening the general limit.
