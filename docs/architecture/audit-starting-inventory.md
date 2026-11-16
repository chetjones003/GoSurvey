# Architecture audit starting inventory

Captured at the start of final-map issue #1238, after audits #1220–#1237 and before resolving the final ownership decisions. These are the source-tree metrics used to assess the carve-out baseline.

- Source files: 451
- CadCommands.hpp include fan-in: 65
- Include graph: 451 files, 1048 edges
- Dependency cycles: 0
- Qt includes outside src/qt: 0
- OpenGL use outside src/render: 4

## Largest source files

| File | Bytes | Lines |
|---|---:|---:|
| `src/commands/CadCommands.cpp` | 2519789 | 52819 |
| `src/util/brep.cpp` | 954252 | 19848 |
| `src/commands/CadCommands.hpp` | 591871 | 9089 |
| `src/io/LibreDwgCad.cpp` | 474354 | 9338 |
| `src/io/DxfIo.cpp` | 297331 | 6688 |
| `src/io/GsIo.cpp` | 291273 | 6000 |
| `src/commands/CadCommands_Alignment.cpp` | 281126 | 5801 |
| `src/render/ViewportRenderer.cpp` | 221356 | 4256 |
| `src/qt/EditorWindow.cpp` | 204333 | 3694 |
| `src/qt/PdfWindow.cpp` | 191395 | 3296 |
| `src/commands/CadBlocks.cpp` | 180777 | 4307 |
| `src/qt/EditorViewport.cpp` | 147637 | 2729 |
| `src/util/AcisSatParser.cpp` | 139781 | 3189 |
| `src/viewport/CadSnap.cpp` | 139589 | 2720 |
| `src/qt/EditorDialogs.cpp` | 137072 | 2291 |
| `src/qt/EditorRibbon.cpp` | 121541 | 2082 |
| `src/viewport/TransformPreview.cpp` | 117256 | 2239 |
| `src/util/brep.hpp` | 116839 | 1717 |
| `src/commands/CadCommands_DrawInquiry.cpp` | 96336 | 2320 |
| `src/commands/CadEntities.hpp` | 96141 | 1727 |

## Source lines by folder

| Folder | Lines |
|---|---:|
| `src/alignment` | 5933 |
| `src/commands` | 89714 |
| `src/core/include/GoSurvey/Core` | 82 |
| `src/core/src` | 128 |
| `src/font` | 473 |
| `src/geo` | 1001 |
| `src/io` | 41045 |
| `src/pdf` | 8773 |
| `src/platform` | 4714 |
| `src/qt` | 36227 |
| `src/render` | 5762 |
| `src/survey` | 1571 |
| `src/telemetry` | 222 |
| `src/traverse` | 7374 |
| `src/ui` | 1116 |
| `src/update` | 794 |
| `src/util` | 40481 |
| `src/viewport` | 7237 |

## Baseline notes

This report is a snapshot, not a target or a new pass/fail threshold. The final map and dependency ratchet baseline remain provisional until the owner approves the decisions recorded for #1238.
