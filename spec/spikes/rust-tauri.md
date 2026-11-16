# REQ-9901 — Rust/Tauri drafting spike

Status: accepted for an isolated, throwaway experiment only, 2026-11-15.
Authority: user's explicit request to build a spike with React components, Tailwind,
daisyUI and enough GoSurvey logic ported to Rust for command entry, LINE, MOVE,
COPY, DELETE, panning and zooming. This does not approve replacing the production app.

Acceptance:
- A runnable Windows Tauri desktop app, with a Vite-served React UI during development.
- React workspace includes ribbon, toolspace, properties, command history/input and viewport.
- Rust owns f64 line entities, selection, command parsing, edit phases and committed mutations.
- LINE accepts typed absolute/relative coordinates and viewport points; MOVE/COPY use
  a selection, base point and destination; DELETE/ERASE removes selected lines.
- Invalid input leaves geometry intact; Escape cancels pending input.
- Middle drag and PAN left drag pan; wheel zoom stays anchored to the cursor; extents fits linework.
- UI changes use Vite HMR independently from Rust rebuilds.
- Clearly identify prototype limitations; inspect and present actual captured UI before completion.

Recorded experiment decisions: user-requested React/Tailwind/daisyUI/Tauri/Vite dependencies
and Cargo/npm lockfiles are permitted for this spike (an experiment-only exception to REQ-300
vendoring; no production policy changes). Use SVG retained line display for this bounded 2D
interaction experiment, not as proof of the eventual CAD GPU renderer. A plain Rust engine
is owned by Tauri state with serialized access; React owns camera and transient presentation.
No shared production AppCommandState and no C++ ABI bridge. GoSurvey line/transform/pan
semantics and ZoomFraming/OrthoConstrain math inform a focused Rust reimplementation.
In-memory document only; no DWG/DXF, projects, 3D or PDF implementation. These remain
outside this explicitly requested command/UI spike. One developed workspace is requested,
so separate speculative layout variants are unnecessary.
