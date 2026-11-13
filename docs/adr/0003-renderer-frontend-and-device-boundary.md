# ADR-0003: Separate Renderer frontend from graphics devices

- **Status:** Accepted
- **Date:** 2026-11-12

## Context

Drawing data and render caches shaped around OpenGL tie document and command state to one graphics API. Supporting another API would then require changes throughout the engine, and rendering behavior is difficult to test without a GPU.

## Decision

Make Renderer a backend-neutral frontend that turns the model into draw lists and owns render caches. Define graphics device interfaces and a factory at the boundary. Keep OpenGL calls in the OpenGL platform backend, selected at application startup. Provide a Headless null device that records draw calls for tests. The document stores model data, not GPU-shaped buffers.

## Alternatives rejected

- **Keep OpenGL calls in Renderer and callers:** this preserves the current API coupling.
- **Create a second graphics backend now:** no second backend is needed to establish and verify the seam.
- **Store device-formatted geometry in the document:** that makes domain data depend on a rendering implementation.

## Consequences

- Renderer can be exercised without a GPU through the null device.
- A future graphics API can implement the device interfaces without changing domain systems.
- Shader cross-compilation remains deferred until another backend is undertaken; shaders stay GLSL for now.
- Draw-list and device contracts become an explicit boundary that must not leak API-specific types upward.
