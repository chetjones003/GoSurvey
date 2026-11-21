# Glossary

Domain and architecture terms used in GoSurvey code, issues and ADRs. Add a term when a decision
names it; keep each entry to what a reader needs to use the word correctly.

## Drawing data

**Entity store**: The Document-owned holder of a drawing's CAD entities (lines, arcs, polylines,
solids, blocks and the rest), the layer table, and the text, dimension and multileader styles.
`CadEntityStore` is the block-capable core; `CadDrawingEntityStore` adds drawing-only entity kinds.
Both are in `GoSurvey/Document/cadblock.hpp`. Block capture copies the core, while drawing content
inherits the extension. Document owns how entities are held, compacted and copied; Commands decides
what to change. See
[ADR-0010](docs/adr/0010-drawing-content-and-entity-store.md).
_Avoid_: geometry arrays, flat arrays.

**Drawing content**: The Commands-owned value holding everything a drawing saves and the user
edits: the entity store plus survey, terrain, PDF and paper-layout data, and the "current"
settings (current layer, color, text style). Undo frames, per-tab documents and the clipboard each hold one.
Excludes view state, app preferences, selection and the entity-id counter. See
[ADR-0010](docs/adr/0010-drawing-content-and-entity-store.md).
_Avoid_: geometry snapshot (for the content itself; the undo frame wraps it).

**Selection**: The transient set of picked entities. Not saved, not undone, and not part of
drawing content. Interaction owns selection intent.

**Entity-kind operation**: A Document-owned operation implemented once per entity kind: transform,
duplicate, bounding box, snap candidates and hit distance. Every kind declares whether it supports
each operation, or refuses it with a stated reason. Commands combines these operations with the kinds
other systems own, such as survey points and PDF underlays. See
[ADR-0011](docs/adr/0011-entity-kind-operations.md).
_Avoid_: per-command transform loop.

**Similarity transform**: The one transform entity-kind operations accept: rotation about any axis,
uniform scale, translation and reflection across a plane, computed in `double`. It cannot express
shear or non-uniform scale. See [ADR-0011](docs/adr/0011-entity-kind-operations.md).
_Avoid_: affine matrix (it allows shear and non-uniform scale).

## Commands

**Command module**: One file per command or small family under `src/commands/<family>/`. It declares
the command's name, aliases, description, steps, completion and cancel behaviour, and its own state.
The registry built from command modules is the only command list. See
[ADR-0012](docs/adr/0012-command-modules-and-step-engine.md).

**Step engine**: The shared Commands code that drives a command module's steps. It owns click,
Enter, Esc and dynamic-input routing, prompt text and the select-objects box for every command. See
[ADR-0012](docs/adr/0012-command-modules-and-step-engine.md).
_Avoid_: per-command click/Enter/Esc handler.
