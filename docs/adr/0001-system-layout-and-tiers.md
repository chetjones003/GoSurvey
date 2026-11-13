# ADR-0001: Organize the engine into systems and tiers

- **Status:** Accepted
- **Date:** 2026-11-12

## Context

GoSurvey has grown without enforceable ownership boundaries. Shared headers and mixed-purpose folders make changes risky, while the current application combines domain logic with toolkit and graphics APIs. The architecture refactor needs a stable way to locate code and constrain dependencies while allowing behavior-preserving, incremental moves.

## Decision

Organize the toolkit-neutral GoSurvey engine into named systems arranged in tiers: Foundation, Kernel, Domain, Services, and Application. Keep `GoSurveyEditor` above the engine and swappable Qt, OpenGL, Windows, and Headless implementations below the engine's platform interfaces. Dependencies flow downward only. Domain peers may depend on one another only through public interfaces, and their graph must remain acyclic.

## Alternatives rejected

- **Keep the current folder structure:** it does not establish ownership or dependency boundaries.
- **Split only by technical layer:** broad layers would still mix unrelated responsibilities and leave ownership unclear.
- **Move everything in one rewrite:** it would make behavior changes hard to isolate and keep the application releasable.

## Consequences

- Every concept has a named owning system and an allowed dependency direction.
- The system map is the authority for responsibilities and permitted dependencies.
- The refactor can proceed in small, behavior-preserving changes while the application remains releasable.
- Cross-system work must use public interfaces; introducing cycles is prohibited.
