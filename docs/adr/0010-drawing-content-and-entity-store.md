# ADR-0010: Compose drawing content in Commands; Document owns the entity store

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

The system map makes Document the owner of drawing entities, but the drawing actually lives as about
95 parallel arrays inside `AppCommandState` (`src/commands/CadCommands.hpp`). The same field list is
written out by hand five times: the live state, `DrawingGeometrySnapshot` (undo), `DrawingDocument`
(the per-tab store), `CadClipboard` and `CadBlockContent`. Each copy has its own hand-written copy
functions. Fields left out of one of those lists cause undo entries that restore nothing, values
that leak between open drawings, and comments like "was missing from the tab swap".

The undo snapshot does not hold only CAD entities. It also holds survey points, alignments,
profiles, terrain surfaces, PDF attachments and paper layouts. Survey, Terrain and Pdf depend on
Document, so a single Document-owned type cannot hold their data without creating a cycle.

## Decision

- **Document owns the entity store:** the CAD entity collections, blocks, the layer table, and the
  text, dimension and multileader styles.
- **Commands owns drawing content:** the entity store plus the Survey, Terrain, Pdf and
  paper-layout data that a drawing carries. Drawing content is the single value that undo frames
  and the per-tab document hold. Undo history stays with Commands while it stores drawing content.
- **Drawing content covers all saved, user-edited drawing data**, including the "current" settings
  (current layer, color, text style and similar). View state, app preferences, the entity-id
  counter and session state stay outside it. Disputed fields follow AutoCAD's undo behaviour,
  checked with `accoreconsole.exe`.
- **Selection is transient and is not drawing content.** Interaction owns selection intent, per
  owner decision #59. This supersedes ADR-0008's statement that Commands owns selection. Commands
  keeps owning command options and entity mutation.
- **Migration is behaviour-preserving first.** Phase 1a moves the shared fields into one
  `DrawingContent` value that `AppCommandState` temporarily inherits, so call sites keep compiling.
  Coverage fixes follow in their own change. Partitioning the entity store into Document comes
  later.
- **Document's drawing entity store has a block-capable core and a drawing-only extension.**
  `CadEntityStore` remains the exact store shared by `CadBlockContent` and drawing content.
  `CadDrawingEntityStore` adds drawing-level CAD entities that block definitions do not support.
  `DrawingContent` derives from the extension, while block capture and BEDIT continue copying only
  the shared core. This keeps the move source-compatible and prevents drawing-only objects from
  entering block definitions.
- **The mixed `CadEntities.hpp` declarations follow their owning systems.** Document entity records
  belong in Document; feature-line and TIN/surface records stay Terrain-owned; point-cloud payloads
  stay PointCloud-owned; frame display batches stay Renderer-owned; imported DWG light records are
  IO data. Those non-Document values remain outside `CadDrawingEntityStore` and travel in the
  Commands composition only where a drawing needs them.

## Alternatives rejected

- **One flat drawing struct in Commands:** simpler, but it leaves Document with no entities and
  makes the system map permanently wrong.
- **Put everything in Document:** Survey, Terrain and Pdf data would make Document depend on
  systems that already depend on it.
- **Rename every call site to `st.drawing.…` up front:** a very large diff with no behaviour
  benefit, and it would hide behaviour changes during review.

## Consequences

- Adding a drawing field means editing one struct; undo, tab switching and their copies pick it up.
- `AppCommandState` inheriting `DrawingContent` is a temporary seam. Later phases move access
  behind the entity store interface one entity kind at a time.
- The system map lists selection under Interaction, not Document.

## Amendment (2026-11-21): remove the inheritance seam in one mechanical step

The rejection of an up-front `st.drawing.…` rename above is narrowed, not reversed. The inheritance
seam is removed in a single change that does only what the compiler requires:

- `AppCommandState` stops inheriting `DrawingContent` and holds it as a member, `drawing`.
- Every former inherited member access becomes `st.drawing.<member>` (and `drawing.<member>` inside
  `AppCommandState`). The compiler locates each site; no logic changes.
- The change lands as its own commit, separate from any behaviour change, so review can check that
  the diff is rename-only.

Phase 5's stride-accessor work covers the stride kinds: lines, circles and polyline vertices.
Kinds without stride arithmetic (arcs, ellipses, solids, annotations, block-store arrays) keep direct
member access through `drawing` until a later phase moves them behind the entity store.
The stride kinds go through their accessors, not through `drawing`.

## Amendment (2026-11-21): define CAD geometry clearing at the content boundary

`ClearCadGeometry` replaces model-space CAD content for OPEN and CAD imports. It clears the CAD
entity collections, their per-entity attributes, annotation and viewport scale lists, alignments,
sample lines, profiles, surfaces, and named UCS and view records. Drawing setup (layer and style
tables, units and settings), survey points, feature lines, underlays, and paper layouts stay with
the drawing. The entity ID space and origin reset; application-owned selection, isolation, derived
geometry caches and in-flight surface work are cleared at the application boundary. These rules
preserve the established OPEN/import behavior;
`DrawingContent::clearCadGeometry` owns the content portion so it cannot drift from the value that
undo and tabs copy.
