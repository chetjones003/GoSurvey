# Civil 3D grading tools: historical expansion research

> **Historical snapshot.** This report records the repository state inspected on 2026-11-02 (`feat/issue-813`, HEAD `ce018da7`). It predates the accepted four-stage scope and its Stage 1/2 implementation. Do not use its capability table, open-decision notes, or recommendations as current status. Current authority and implementation status are in [the grading toolkit specification](../../spec/grading-toolkit.md), [project decisions](../../spec/project.md), and [Stage 2 task](../../workshop/tasks/TASK-grading-toolkit-stage2.md). The optimizer issue #813 remains separately scoped.

Research date: 2026-11-02. **Research and proposed scope only:** this report itself does not approve behavior or change the specification. Its recommendations describe the state before later decisions. The notes-only investigation ran no builds or tests.

## Current capability and authority

Read `CLAUDE.md`, `AGENTS.md`, and the user's interaction preferences. Existing authority is accepted **REQ-398**, **REQ-404**, and **ADR-070**. REQ-398 now includes #997's persistent editable definition; “baked” means explicitly rebuilt, not uneditable. REQ-404's historical revision text still says groups are unimplemented, but current code and TASK-998 demonstrate implementation. Do not use that historical sentence as the capability inventory.

Evidence anchors (line numbers refer to this checkout):

| Anchor | Current source evidence |
| --- | --- |
| G1 | `spec/requirements.md:10998`: REQ-398 surface targets, separate cut/fill slopes, open-line side selection, persistent definition, explicit rebuild and failure preservation. `spec/requirements.md:12387`: REQ-404 group surfaces and existing volume engine. `spec/architecture.md:4804`: ADR-070, ordinary surfaces and baked membership snapshots. |
| G2 | `src/commands/CadEntities.hpp:1126`: `CadGradingDefinition` stores baseline/surface stable IDs, cut/fill runs and side. No target-kind, distance, elevation, criteria reference or station range. `CadFeatureLineInfo` attaches this optional definition to an ordinary feature line. |
| G3 | `src/commands/CadCommands.cpp:23954`: selecting a result restores its inputs for editing; a result is not interpreted as a fresh baseline. `:24023` onward computes daylight; `:24189` onward prepares affected group updates; `:24234` onward replaces the result in place. `:24525` onward handles cut/fill/surface/side options. |
| G4 | `src/commands/CadEntities.hpp:1181`: member IDs plus baked quadrilateral strips on an ordinary `CadSurface`. `src/commands/CadCommands.cpp:2979` resolves these into constraints and clip-loop union; `:3649` excludes groups from ordinary automatic rebuilding. `:24325` onward implements `GRADINGGROUP name[, Add/Remove/List]`, atomic transfers and rebuilds. `:24353` points users to `VOLUMES ground, group`. |
| G5 | `src/commands/CadCommands.cpp:24794`, `:24814`, `:24852`, `:24889`, `:24911`, `:24983`: point elevation, grade ahead/back, whole-line raise/lower, insert elevation point, delete elevation point. `:46326` documents the corresponding `FLELEV` syntax. These are existing tools, not proposed replacements. |
| G6 | `src/commands/CadCommands.cpp:27971` erases feature-line geometry and metadata; `:28247` routes selected feature lines through it. It does not remove grading-group membership or its baked strips. Generic ERASE therefore exists, but is not equivalent to Civil 3D's group-aware Delete Grading. |
| G7 | `src/qt/RibbonCatalog.hpp:24`, `:27`, `:121`, `:144`: four grading entries launch `GRADING`. `:113`: Edit Elevations entry. `src/qt/EditorRibbon.cpp:51` marks `@editElevations` as awaiting a native dialog and `:319` disables such actions; `:652` only sets the shared window flag. Command-side `FLELEVEDIT` likewise sets a flag (`CadCommands.cpp:46355`); no Qt table implementation was found. `src/qt/EditorProperties.cpp` has no grading-definition-specific editor. |
| G8 | `src/io/GsIo.cpp:1569` writes grading inputs; `:1976` writes baked member strips; `:2335` validates membership; `:3136` and `:3522` restore them. DWG uses the established payload, as recorded in `workshop/tasks/TASK-998-grading-groups.md:15`. `tests/QtEditorTests.cpp:7965` and `:7988` cover missing sources; `:8013` covers grading ribbon routes. `workshop/tasks/TASK-998-grading-groups.md:139` records full-suite verification. |

## Tool comparison

Civil 3D references below are Autodesk documentation, primarily 2026. “Absent” means absent from the inspected grading definition, command workflow and Qt surfaces; it does not mean the geometry could never be constructed manually.

| Tool / workflow | Civil 3D documented behavior | GoSurvey today / expansion opportunity |
| --- | --- | --- |
| Surface grading | Projects from a footprint to terrain; permits open/closed footprints. [Overview][about] | **Supported subset:** feature-line baseline, separate cut/fill slopes and explicit rebuild (G1–G3). No lot-line/site topology parity. |
| Distance target | Ends at a horizontal offset. [Criteria tab][criteria-tab] | **Absent:** add width plus signed grade for shoulders or bench edges (G2). No surface should be required for this target. |
| Absolute elevation target | Ends at a specified elevation. [Criteria tab][criteria-tab] | **Absent:** useful for pond bottoms and berm crest levels (G2). |
| Relative elevation target | Ends at a height/depth measured from the footprint. [Criteria tab][criteria-tab] | **Absent:** useful for a curb reveal or a fixed drop before daylight (G2). This differs from a feature line relative to a terrain surface. |
| Named criteria / sets | Reusable definitions; locked values can propagate to existing gradings, while unlocked defaults need not. [Criteria][criteria] | **Partial:** each result retains its own inputs; no named library/locking (G2–G3). Propose copied presets first. |
| Copy Create | Reuses another grading's criteria and style on a new footprint. [Copy procedure][copy] | **Absent as a grading operation:** selecting a result currently edits it (G3). Requires an explicit copy-inputs mode, not ordinary geometry COPY. |
| Partial footprint regions | Selects start/end points instead of the whole footprint. [Create procedure][create] | **Absent:** one definition covers the baseline's full vertex chain (G2–G3). |
| Transitions | Blends regions or end values over a chosen interval. [Transitions][transitions], [procedure][transition-create] | **Absent:** no station intervals or interpolation definition (G2). Must validate compatible targets; see limitations below. |
| Chained grading / benches | Another grading's daylight line can be the next footprint. [Overview][about], [create procedure][create] | **Partial geometry substrate:** results are feature lines, but normal selection reopens the parent definition (G3). No explicit “grade from result edge” workflow or dependent-chain rebuild policy. |
| Infill | Adds a face in an enclosed void without a projection criterion. [Infill][infill] | **Absent:** groups contain side strips, deliberately excluding the closed pad interior (G4; TASK-998 plan). Add an explicit bounded interior face, not convex-hull filling. |
| Edit Grading / Grading Editor | Edits criteria and region endpoints; locked inputs are read-only. [Editor][editor] | **Partial:** `GRADING` edits stored slopes/target/side in place (G3). No region endpoints or dedicated Qt editor. |
| Delete Grading | Removes the grading and its group contribution. [Tools][tools] | **Partial:** ERASE deletes the feature line, but leaves baked group contribution/membership (G6). A group-aware deletion action needs approved semantics. |
| Change Grading Group | Moves a grading to a selected group. [Tools][tools] | **Supported by command:** Add transfers membership, Remove detaches it, List inspects it (G4). Add discoverable Qt controls. |
| Feature-line elevation editing | Table supports elevations, grades, raising/lowering, flattening, point edits and elevations from a surface. [Elevation editor][elevation-editor] | **Partial, substantial command support:** SET/GRADEAHEAD/GRADEBACK/RAISE/INSERT/DELETE already exist (G5). Qt table is pending (G7); selected-row operations, flatten and surface assignment need separate assessment/specification. |
| Creation palette / properties | Creation toolbar selects context; individual properties show group, style and criteria information. [Tools][tools], [Properties][properties] | **Partial entry points:** four ribbon routes and command prompts exist (G7). Propose an actual Qt palette plus grading/group properties, with target-specific fields and failures visible. |
| Group surface / detached surface | Supports dynamic group surfaces and independent snapshots. [Surfaces][surfaces] | **Partial:** normal TIN group surface exists (G4); there is no grading-specific detached-surface workflow. Baked group inputs still retain membership; they are not a detached snapshot. |
| Classic Grading Volume Tools | Displays volumes, raises/lowers whole group or selection, and iteratively approaches a net target. [Volume toolbar][volumes] | **Partial:** existing `VOLUMES` comparison is available (G4). No group raise/lower or auto-balance workflow; individual baseline RAISE is not equivalent. |
| Projection / surface quality utilities | Criteria include search-order and corner-conflict policies; group tessellation adds definition between footprint and edge. [Criteria tab][criteria-tab], [Surfaces][surfaces] | **Partial:** existing daylight search and vertex normals plus constrained group strips (G3–G4). No user-facing matching policy/tessellation controls; do not claim geometric parity. |

## Documented limitations and product boundaries

- **Transitions are not unrestricted.** Autodesk Support says transitions cannot connect different target surfaces; different absolute target elevations are also treated as different targets. This is a support article rather than a version-specific 2026 guarantee; its searchable excerpt supplied the limitation, while its opened page exposed no readable body. Preserve that qualification. Recommend initially allowing only explicitly compatible target pairs. [Autodesk Support][transition-limit]
- **Criteria editing has consequences.** Civil 3D restricts changes to Target/Projection on criteria already in use. Its locked-value propagation is more than saving a preset. GoSurvey should not accidentally acquire automatic propagation through a criteria library. [Criteria tab][criteria-tab], [Criteria][criteria]
- **Surface dependencies matter.** Civil 3D disallows pasting a grading surface into its own target. Classic grading-volume controls need a dynamic group surface and a volume base; finer tessellation trades time for detail. These are reference constraints, not approved changes to GoSurvey's explicit-rebuild design. [Surfaces][surfaces]
- **Classic volume balancing is not Grading Optimization.** The former changes grading elevations toward a net earthwork target. [Volume toolbar][volumes] The separate Grading Optimization tool solves with surface constraints and objectives, including drainage, earthwork and smoothness, then returns a solution to the drawing. [Optimization overview][optimization], [Options][optimization-options] This report does not specify #813 or relabel classic auto-balance as that product.

## Recommended expansion sequence — proposed, not approved

1. **First phase: complete everyday pad/shoulder creation.** Add distance + signed grade, absolute elevation + slope, and footprint-relative elevation + slope targets while preserving surface daylight. Add explicit infill for a closed pad interior. Deliver these through a small Qt creation/edit palette with target-specific fields, group selection, a properties summary and explicit Rebuild. Example: draw a sloping pad edge, create a fixed-width shoulder, or project to a chosen pond-bottom elevation; add the interior face and compare the completed design with existing ground. Continue using the existing group TIN and volume engine. Until chaining is approved, describe these as alternative targets from a chosen baseline, not an automatically linked multi-stage bench.
2. **Reuse and editing phase.** Add Copy Create and named presets whose values are copied into the result; expose group transfers and group-aware deletion. Port the existing elevation services into a Qt table before inventing parallel editing logic. Then consider selected-row raise/lower, flatten and one-shot surface elevations. Example: apply the same shoulder settings around several pads, then adjust baseline grades without redrawing. Copied presets are a recommendation; linked criteria/locking require a later decision.
3. **Regional and bench phase.** Add station-based start/end ranges, compatible-target transitions, and explicit “use result edge as next baseline.” Example: a steep slope near a property boundary transitions to a flatter slope, or a drop leads to a bench and then surface daylight. Define interval ownership, closed-line wrap, sampling, seams and ordered rebuild before coding. A manually rebuilt chain is a viable first scope; live dependency propagation is not assumed.
4. **Utilities and quality phase.** Add a detached design-surface snapshot, group properties including useful quality controls, clear missing/stale-source status, and deliberate corner/search policies. Separately scope classic group raise/lower and optional auto-balance if requested. Keep optimization #813 a distinct decision after creation/editing workflows are settled.

## Decisions needed before implementation

Parent review recommendation: include group-aware deletion and visible group transfer controls in
the first palette increment, alongside the additional targets and explicit infill. This brings
the existing ERASE/baked-contribution mismatch into the same user workflow instead of leaving a
grading visibly deleted while its earthwork remains in the group. This is proposed behavior;
REQ-404 does not currently specify deletion semantics. Recommend the broader toolkit in the four
stages above, with explicit rebuild and copied presets retained initially. User scope decision pending.

These were specification gaps at the time of this research and have since been resolved for the accepted stages noted above.

- **How updates happen:** current approval retains inputs but rebuilds only when asked. Recommend keeping that rule for the first phase. Automatic source updates, preset propagation or chained rebuilds change how designs react to edits and require a recorded change to the relevant accepted decisions. A visible “inputs changed; rebuild needed” status is itself proposed behavior, not something the current baked group machinery guarantees.
- **What a new target means:** approve valid target/projection combinations, signed grades, units, upward/downward choices, zero-grade/equal-elevation cases, and what happens when no solution exists. Changing the current “surface required” guard (`CadCommands.cpp:24474`) is necessary for non-surface targets. Do not approximate an elevation target by manufacturing a hidden terrain surface.
- **What fills a pad:** approve infill membership and boundary ownership, holes, nonplanar boundary triangulation and conflicting elevations. The current member payload holds four-vertex strips; an interior polygon does not fit by merely toggling a flag. Preserve disjoint regions and genuine voids. Explicit infill adds measurable design area, so volumes must state the included footprint.
- **What persists and remains editable:** approve an additive target definition with legacy surface-target defaults, finite-value validation, source IDs, optional ranges, and infill representation. Extend the existing DWG payload and undo/document model; decide the documented losses for formats that cannot retain those definitions. Exported geometry alone must not be called Civil 3D grading-object interoperability.
- **Where responsibility lives:** recommend shared geometry/command services plus thin Qt widgets, current surface build inputs and existing volume calculations. Decide whether richer region/infill definitions still belong on feature-line/group metadata before introducing a new entity kind, store or dependency graph. ADR-070 already rules out a second grading volume engine.
- **Deletion, direct edits and chains:** decide whether Delete Grading removes its group contribution atomically, what ERASE should do, and what happens to downstream gradings when a source disappears. Also decide whether generic edits to a result are permitted, invalidate its definition, or must go through grading inputs. Current ordinary-feature-line representation does not imply Civil 3D's controlled daylight-edge editing semantics. [Projection grading][projection]
- **UI scope and acceptance:** approve the palette/properties/elevation-table scope and future numerical, persistence, cancel/undo and failure criteria through SPEC → VERIFICATION → WORKSHOP. Actual Qt implementation will require screenshots of meaningful states and user approval under repository governance. No screenshots or approvals are needed for this research note.

## Primary sources

All links below are direct Autodesk sources. Unless noted, they are Civil 3D 2026 User Guide pages. Findings are paraphrased; no long quotations are reproduced.

[tools]: https://help.autodesk.com/cloudhelp/2026/ENU/Civil3D-UserGuide/files/GUID-E074CD16-90E6-42B6-8B54-2A4B5496E7AE.htm
[about]: https://help.autodesk.com/cloudhelp/2026/ENU/Civil3D-UserGuide/files/GUID-3E75D600-B442-4C78-8DCC-C649E1ED1F87.htm
[criteria]: https://help.autodesk.com/cloudhelp/2026/ENU/Civil3D-UserGuide/files/GUID-6266C918-C677-48E1-BB2C-8CE9860F0B00.htm
[criteria-tab]: https://help.autodesk.com/cloudhelp/2026/ENU/Civil3D-UserGuide/files/GUID-13F6FA40-7BB5-460D-B786-7503C3440004.htm
[create]: https://help.autodesk.com/cloudhelp/2026/ENU/Civil3D-UserGuide/files/GUID-30D7E7C2-4361-47A2-9F28-F5915EB713AB.htm
[copy]: https://help.autodesk.com/cloudhelp/2026/ENU/Civil3D-UserGuide/files/GUID-E500BC72-7DBD-485E-994A-317E23AC24C1.htm
[infill]: https://help.autodesk.com/cloudhelp/2026/ENU/Civil3D-UserGuide/files/GUID-6098710E-EA80-40F7-B3CF-21BCD92CD18E.htm
[transitions]: https://help.autodesk.com/cloudhelp/2026/ENG/Civil3D-UserGuide/files/GUID-78CD9770-EFDD-4300-ABC1-68F1FFB923BA.htm
[transition-create]: https://help.autodesk.com/cloudhelp/2026/ENG/Civil3D-UserGuide/files/GUID-9571204C-C70A-4362-AF01-7B342B490F97.htm
[transition-limit]: https://www.autodesk.com/support/technical/article/caas/sfdcarticles/sfdcarticles/Creating-a-grading-transition-in-Civil-3D-doesn-t-work.html
[editor]: https://help.autodesk.com/cloudhelp/2026/ENU/Civil3D-UserGuide/files/GUID-7143F1FE-0FB9-4E98-8F34-3B4DE56EF562.htm
[elevation-editor]: https://help.autodesk.com/cloudhelp/2026/ENU/Civil3D-UserGuide/files/GUID-CD59A8C3-6A59-4655-9CDB-75ECA6C0D31B.htm
[properties]: https://help.autodesk.com/cloudhelp/2026/ENU/Civil3D-UserGuide/files/GUID-ACC6E83B-A7D4-4816-8894-2AAB6C272B83.htm
[surfaces]: https://help.autodesk.com/cloudhelp/2026/ENU/Civil3D-UserGuide/files/GUID-819B6F99-7829-4E74-BF5C-709C086C3A1F.htm
[volumes]: https://help.autodesk.com/cloudhelp/2026/ENU/Civil3D-UserGuide/files/GUID-27249BE4-A308-41E3-8E7A-D30673E26D9D.htm
[projection]: https://help.autodesk.com/cloudhelp/2024/ENU/Civil3D-BestPractices/files/GUID-A575BA8D-DC37-4DDE-BA9C-3D158A4F4E16.htm
[optimization]: https://help.autodesk.com/cloudhelp/ENG/C3DGO-Help/files/about.html
[optimization-options]: https://help.autodesk.com/cloudhelp/ENG/C3DGO-Help/files/dialog_box_reference/C3DGO_Help_dialog_box_reference_optimization_options_dialog_html.html
