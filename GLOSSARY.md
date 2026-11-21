# Glossary

Domain and architecture terms used in GoSurvey code, issues and ADRs. Add a term when a decision
names it; keep each entry to what a reader needs to use the word correctly.

## Drawing data

**Entity store**: The Document-owned holder of a drawing's CAD entities (lines, arcs, polylines,
solids, blocks and the rest), the layer table, and the text, dimension and multileader styles.
The block-capable entity collections are `CadEntityStore` (`GoSurvey/Document/CadEntityStore.hpp`):
a block definition's content and drawing content both hold them, so a block copy is one assignment.
Owns how entities are held, compacted and copied; Commands decides what to change. See
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
