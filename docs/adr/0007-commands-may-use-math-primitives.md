# ADR-0007: Commands may use Math primitives

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Commands uses `ray3d` and `ucs` for geometric calculations. The system map omitted Math from
Commands' allowed dependencies even though the audited code already depended on these pure
primitives. Keeping the headers under `src/util/` hid that dependency from the public-header ratchet.

## Decision

Commands may depend on the public `GoSurvey::Math` interface for general mathematical primitives.
Math remains independent of Commands and domain policy. Command-specific transforms and behavior
remain owned by Commands.

## Consequences

- Commands includes Math through `<GoSurvey/Math/...>` public headers.
- Math has no dependency on Commands; the dependency graph remains acyclic.
- The dependency ratchet enforces this edge and rejects private Math header includes.
