# ADR-0008: Geometry exposes its kernel algorithms through a public interface

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Geometry algorithms lived under `src/util/`, while boundary tracing and fillet/chamfer solvers were
embedded in Commands headers. Consumers therefore included implementation paths, and the dependency
ratchet could not distinguish the reusable kernel from command policy. Several editor panels also
consume public geometry values and algorithms.

## Decision

Geometry-owned headers live under `src/geometry/include/GoSurvey/Geometry/`, with implementation
sources under `src/geometry/src/`. Geometry exposes the planar boundary tracer and fillet/chamfer
construction algorithms from that public include tree. Commands and GoSurveyEditor may depend on
Geometry's public interface. Qt remains a platform adapter and does not depend on Geometry.

Geometry itself may depend only on Math and Core. Geometry does not own document entities,
coordinate-system policy, exchange formats, interaction policy, or render caches. Geometry tests
link Geometry, Math/Core as needed, and Catch2 without Qt, OpenGL, or the application domain library.

## Consequences

- Cross-system consumers include Geometry through `<GoSurvey/Geometry/...>` headers.
- Commands owns selection, command options, and entity mutation; Geometry owns the resulting
  geometry calculations.
- Survey owns coordinate-system transforms and their public headers. Network owns Web Mercator tile
  math. Core owns drawing-unit conversion shared by model and editor code.
- The dependency ratchet requires zero remaining Geometry-related entries in its baseline.
- Reclassifying editor source files in the machine map can expose older cross-system private-header
  violations owned by the separate UI boundary work.
