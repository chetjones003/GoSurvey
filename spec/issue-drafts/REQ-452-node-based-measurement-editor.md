# Node-Based Measurement Editor

## Problem Statement

Surveyors can enter and adjust traverses through command text, but long command files and imported raw observation sets make the network relationships difficult to create, inspect, and organize. A text edit and a visual understanding of the same traverse are separate workflows, and the current command language does not preserve each raw F1/F2 measurement set as an editable command record.

## Solution

Add a Graph tab beside Editor in the Qt least-squares adjustment workspace. The text editor and graph represent one shared project and stay synchronized in both directions. Command nodes show measurement values; station anchors and wires show survey relationships. Imported observations appear as individually identifiable set nodes, grouped by occupied/from station and target station. The graph provides automatic layout, manual organization, visual groups, properties editing, deterministic command ordering, and persistence through `.gsdat` and `.gstrav`.

## User Stories

1. As a surveyor, I want to switch between Editor and Graph without changing projects, so that I can work in the representation that suits the task.
2. As a surveyor, I want every supported GSTC command and option represented by a node, so that the graph remains a complete view of the command file.
3. As a surveyor, I want project-wide commands shown in a labeled Setup area, so that they are easy to distinguish from station measurements.
4. As a surveyor, I want command nodes to show their key values, so that I can understand a network without opening every properties panel.
5. As a surveyor, I want a full properties editor for each command node, so that I can edit every value supported by that command.
6. As a surveyor, I want graph edits to update the command text, so that visual editing produces a usable GSTC document.
7. As a surveyor, I want text edits to update the graph, so that the editor remains the authoritative alternative way to work.
8. As a surveyor, I want creating or deleting a command node to create or delete its corresponding command, so that the graph and text do not diverge.
9. As a surveyor, I want wires to show survey relationships and station fields to stay synchronized with those wires, so that a changed connection updates the actual measurement references.
10. As a surveyor, I want one anchor for each referenced station ID, so that the network topology is visible even when a station has no command node of its own.
11. As a surveyor, I want unresolved station references to remain visible and be marked, so that incomplete data can be found and corrected.
12. As a surveyor, I want invalid command text retained while the last valid graph is marked stale and the parse error is shown, so that an incomplete edit never destroys my work or appears current.
13. As a surveyor, I want each raw observation measurement set represented by one node with its available F1/F2 readings, so that face data stays together without losing the distinction between sets.
14. As a surveyor, I want raw observation nodes to show their source file, set number, and face details, so that I can trace a result back to its imported measurement.
15. As a surveyor, I want raw sets grouped by occupied/from station to target station across files, so that repeated and cross-file observations are easy to compare.
16. As a surveyor, I want those raw-set groups to collapse and expand, so that large imports remain navigable.
17. As a surveyor, I want each available F1/F2 reading preserved as an explicit GSTC `RAWSET` record, so that converting or editing raw observations never silently reduces them to averages.
18. As a surveyor, I want edits to affect the project copy while original raw files stay unchanged, so that the source record remains intact.
19. As a surveyor, I want command order derived from station and command relationships, so that generated text is valid without depending on node positions.
20. As a surveyor, I want otherwise unrelated commands ordered by stable source order, so that regenerated command text is predictable.
21. As a surveyor, I want ambiguous and cyclic ordering relationships identified and manually overridable, so that I can resolve cases where dependencies do not give one valid order.
22. As a surveyor, I want manual ordering overrides saved with the project, so that resolved ordering survives closing and reopening.
23. As a surveyor, I want to move nodes without changing command order, so that visual organization does not alter solver inputs.
24. As a surveyor, I want labeled visual frames that can organize related nodes without affecting the solver, so that large networks can be arranged by work area or purpose.
25. As a surveyor, I want the first graph layout arranged automatically, so that I can inspect a new project without arranging every node manually.
26. As a surveyor, I want to move nodes and group frames, save their positions, and restore them when reopening, so that my visual organization persists.
27. As a surveyor, I want reset and arrange actions, so that I can recover from a cluttered layout or produce a fresh overview.
28. As a surveyor, I want graph layout stored in parse-ignored comments in `.gsdat`, so that layout stays with editable command text without becoming a solver command.
29. As a surveyor, I want optional layout and source fields in `.gstrav`, so that the native traverse file preserves the same graph and provenance state.
30. As a surveyor, I want older version-1 `.gstrav` files to remain readable, so that this feature does not invalidate existing traverse projects.

## Implementation Decisions

- The Qt adjustment workspace owns the user interaction and presents Editor and Graph as tabs on one project state.
- The graph contains a node for every supported command and option, plus station-ID anchors. Station anchors are not commands.
- Wires represent survey relationships only. They do not encode execution flow; command values remain editable on their nodes.
- Invalid text remains untouched. The graph retains the last valid projection and is visibly marked stale with the parser error.
- Raw observations are editable project data. Each measurement set is a distinct node, containing its available F1/F2 faces and file/set/face provenance. Grouping never merges source observations.
- Raw sets convert to one additive GSTC `RAWSET` record per set. The record identifies occupied/from and target stations, set number, an optional F1 tuple, an optional F2 tuple, and a source ID. A face tuple contains horizontal circle reading, zenith reading, and slope distance; at least one face is required.
- Source IDs are mapped to source paths in parse-ignored metadata. Source files themselves are never modified.
- `.gsdat` metadata comments are ignored by measurement parsing. A node metadata comment immediately precedes its command and carries the command's stable opaque ID, logical canvas position, optional group ID, and optional manual-order value. Group frame metadata carries stable ID, bounds, collapsed state, and label. Source metadata maps source IDs to source paths. These metadata comments stay attached to their command when command blocks are reordered.
- `.gstrav` stores optional raw-source and graph-layout fields. Existing version-1 files load without those fields.
- Dependencies determine command order. Stable source order breaks ties between otherwise unrelated commands. Ambiguous and cyclic portions are reported; manual overrides apply to those portions and persist in metadata.
- Canvas position and visual group membership never change command order or solver inputs. Groups are visual only.
- The graph starts with automatic layout. Users may move nodes, add or rename labeled frames, collapse/expand raw groups, save/reopen layout, reset layout, or arrange again.
- Module boundaries follow the existing architecture: Qt presents and edits; Commands parse, validate, serialize, and derive command order; the traverse Domain owns raw measurement values and provenance; IO reads and writes `.gsdat` and `.gstrav`. No new third-party graph dependency is proposed.
- The approved requirement is REQ-452 and the recorded architecture/data-format decision is D-2026-10-22-j.

## Testing Decisions

- Prefer observable behavior through the highest seam: exercise the Qt adjustment window and widget tree for tab navigation, two-way synchronization, properties editing, node creation/deletion, station wires/anchors, unresolved references, invalid-text stale state, raw grouping, manual order, and layout save/reopen.
- Add focused command/domain/IO round-trip tests where the UI seam cannot establish exact F1/F2 values, provenance, parser rejection behavior, metadata ignoring, deterministic ordering, or old-format compatibility.
- Good tests assert user-visible behavior and serialized outcomes rather than private widget implementation details. Numeric measurement values should compare at the precision required by the persisted representation.
- Cover F1-only, F2-only, and dual-face sets; reject a set with neither face, incomplete tuples, non-finite values, invalid station/set IDs, and unknown source IDs.
- Cover distinct observations from multiple files that share the same station pair, proving grouping does not merge records and source identity survives round trips.
- Cover text-to-graph and graph-to-text edits, invalid text retaining the last valid graph, command dependency ordering, stable source-order ties, ambiguous/cyclic warnings, and manual override persistence.
- Verify `.gsdat` metadata does not create measurements or change solver output, and verify version-1 `.gstrav` loads alongside new-format round trips.
- Prior art includes existing traverse parser/serializer and `.gstrav` round-trip tests, least-squares adjustment tests, and Qt editor tests that exercise real widget trees. Add Qt screenshots for affected graph states and request visual review before completion.

## Out of Scope

- Editing or rewriting original imported raw files.
- Merging distinct raw measurement sets, even if their station pair and values match.
- Changing solver semantics based on graph position, group membership, or wire drawing order.
- Making visual group frames affect command dependencies or survey computations.
- Replacing the command editor or removing direct GSTC text editing.
- Adding an external graph-rendering or graph-editing dependency.
- Changing legacy `.gstrav` version-1 read behavior.

## Further Notes

- The feature brief was confirmed through Q1–Q28. The test seam is Qt widget-tree coverage for full user workflows, with focused parser/IO tests for fidelity and compatibility guarantees.
- REQ-452 is accepted in the project specification. Verification approved the implementation plan in TASK-1039; implementation has not started.
- No glossary file is present in the repository. This spec uses the established traverse terms: station, traverse leg, raw measurement set, F1/F2 face, backsight, foresight, GSTC, `.gsdat`, and `.gstrav`.
