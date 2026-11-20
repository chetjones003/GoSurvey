# ADR-0004: Separate public includes from private system sources

- **Status:** Accepted
- **Date:** 2026-11-12

## Context

Systems need clear public interfaces, but current headers are broadly visible and allow callers to reach into implementation details. Naming an owning system in include paths also makes dependencies legible at call sites.

## Decision

Each system keeps public headers in a system-qualified include tree (`GoSurvey/<System>/…`) and private sources and headers in a separate source tree. Other systems may include only the public tree. Build each system as its own CMake library with only its allowed dependencies; platform backends expose small public factory headers to the application.

## Alternatives rejected

- **One shared include directory for all headers:** visibility does not distinguish supported interfaces from implementation details.
- **Rely on code review alone:** accidental private-header dependencies would remain compilable.
- **Expose every header publicly for convenience:** this would make internals a de facto contract and impede changes.

## Consequences

- Include paths state which system owns an interface.
- Compiler and library boundaries can reject private-header and forbidden-library dependencies.
- Moving a declaration between public and private status requires updating its callers deliberately.
- The application can link the engine through the umbrella `GoSurvey::Engine` target.
