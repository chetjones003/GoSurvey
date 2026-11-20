# ADR-0006: Use system-qualified C++ namespaces

- **Status:** Accepted
- **Date:** 2026-11-12

## Context

As code moves into systems, folder names and include paths alone do not make ownership visible in declarations and call sites. Namespace changes made during broad moves would also make reviews harder to isolate.

## Decision

Use `GoSurvey::<System>` namespaces for C++ code owned by each system. Introduce a system's namespace in a separate, focused change after that system's files have moved.

## Alternatives rejected

- **Keep legacy or generic namespaces:** these do not communicate system ownership consistently.
- **Rename namespaces during file moves:** combining structural moves and symbol changes makes behavior-preserving reviews harder.
- **Use only file paths to express ownership:** paths do not appear at type and function use sites.

## Consequences

- Declarations and call sites communicate their owning system.
- Namespace work is incremental and can follow each system move.
- Namespace changes affect API call sites and should be kept separate from file moves.
