# GoSurvey system map

Status: draft target architecture and file mapping. This is the architecture reference for agents and checks; the source tree has not yet been moved. Mapping ambiguity is recorded in `system-map.json` and must be resolved during the audit before enforcement.

## Dependency rules

Dependencies point only downward through the tier order shown in the diagram, with two explicit ordering rules. Within Foundation (T0), the dependency order is Core → Math → Events → UI, where each item may depend on items to its left; Debug may depend on Core only. Within Domain (T2), peer dependencies are allowed only through public headers and must be acyclic. The only Domain peer edges allowed by this draft are Survey → Document, Terrain → Document, Terrain → Survey, and Pdf → Document; all other Domain dependencies must point downward to Kernel or Foundation. No other same-tier dependency is allowed. Platform backends implement lower-tier interfaces and are selected by GoSurveyEditor composition/startup; engine systems do not depend on platform implementations. `GoSurveyEditor` may use Qt and OpenGL backends.

## Decision records

The rationale behind the architecture boundaries is recorded in these ADRs:

- [System layout and tiers](../adr/0001-system-layout-and-tiers.md)
- [Toolkit-neutral UI interfaces](../adr/0002-toolkit-neutral-ui-interfaces.md)
- [Renderer frontend and device boundary](../adr/0003-renderer-frontend-and-device-boundary.md)
- [Public includes and private system sources](../adr/0004-public-includes-and-private-sources.md)
- [Dependency ratchet and file-size gate](../adr/0005-dependency-ratchet-and-file-size-gate.md)
- [System-qualified C++ namespaces](../adr/0006-system-qualified-cpp-namespaces.md)

## Systems

| System | Tier | Responsibility | Does not own | Allowed dependencies |
|---|---|---|---|---|
| Core | T0 Foundation | Provides shared application services such as logging, settings, units, and formatting. | Geometry algorithms, domain entities, UI widgets, platform APIs. | None |
| Math | T0 Foundation | Provides general vectors, matrices, rays, and coordinate math primitives. | Domain policy, geometry models, rendering. | Core |
| Events | T0 Foundation | Defines toolkit-neutral input and document-change events. | Event loop implementations, widgets, command behavior. | Core, Math |
| UI | T0 Foundation | Defines toolkit-neutral window, dialog, progress, clipboard, and timer interfaces. | Concrete widgets and toolkit implementations. | Core, Events |
| Debug | T0 Foundation | Provides profiling, tracing, and invariant diagnostics. | Product workflows and domain state. | Core |
| Geometry | T1 Kernel | Provides reusable 2D/3D curves, NURBS, B-rep, and mesh algorithms. | Document ownership, survey policy, GPU resources. | Math, Core |
| Document | T2 Domain | Owns drawing entities, layers, blocks, tables, text, undo, and selection state. | GPU-shaped caches, import/export formats, widget state. | Geometry, Math, Core, Events |
| Survey | T2 Domain | Owns survey points, traverses, alignments, and coordinate systems. | UI presentation, file formats, rendering. | Geometry, Math, Core, Document |
| Terrain | T2 Domain | Owns terrain surfaces, TINs, contours, volumes, and daylight models. | Viewport rendering, file parsing, UI. | Geometry, Math, Core, Document, Survey |
| PointCloud | T2 Domain | Owns point-cloud storage, indexing, and cache policy. | Point-file parsing, viewport rendering, UI. | Math, Core |
| Pdf | T2 Domain | Owns PDF underlay annotation, measurement, comparison, and scale model. | PDF library integration, widgets, rendering backend. | Geometry, Math, Core, Document |
| Renderer | T3 Services | Converts model state into backend-neutral draw lists and owns render caches and device interfaces. | Domain model state and concrete graphics API implementation. | Geometry, Math, Core, Document, Survey, Terrain, PointCloud, Pdf |
| IO | T3 Services | Reads and writes drawing and exchange formats, including DXF, DWG, glTF, STL, SAT, point files, and StarNet. | Domain ownership, UI, OS services. | Geometry, Math, Core, Document, Survey, Terrain, PointCloud, Pdf |
| Network | T3 Services | Provides map tiles, update checks, and telemetry behind replaceable interfaces. | UI presentation and OS transport implementation. | Core, Events |
| Commands | T4 Application | Coordinates command registration and user-requested operations. | Persistent domain data, toolkit widgets, GPU APIs. | Renderer, IO, Network, Document, Survey, Terrain, PointCloud, Pdf, UI, Events, Core |
| Interaction | T4 Application | Coordinates snapping, grips, previews, picking, and transient user interaction. | Persistent document ownership, concrete widgets, GPU APIs. | Commands, Renderer, Document, Survey, Terrain, PointCloud, Geometry, Math, Events, UI, Core |
| Qt | Platform backend | Implements UI and event interfaces using Qt. | Domain rules, command behavior, application ownership. | Core, Events, UI |
| OpenGL | Platform backend | Implements renderer device interfaces using OpenGL. | Draw-list policy and domain state. | Core, Math, Renderer |
| Windows | Platform backend | Implements OS paths, HTTP, crash reporting, process and native dialog services. | Domain behavior and cross-platform interfaces. | Core, UI, Network |
| Headless | Platform backend | Implements null render and scripted UI services for tests and batch operation. | Product UI and domain behavior. | Core, Events, UI, Renderer |
| GoSurveyEditor | Application | Composes the engine and owns startup, windows, dialogs, ribbon, panels, and hotkeys. | Engine domain logic and backend implementation details beyond composition. | Commands, Interaction, Renderer, IO, Network, Qt, OpenGL, Windows |

## Target architecture diagram

```mermaid
flowchart TB
  editor(["`**GoSurveyEditor** — application\nmain() · windows · dialogs · ribbon · panels · hotkeys\nthe composition root`"])
  subgraph T4["GoSurvey engine · Application systems"]
    direction LR
    commands["`**Commands**\nregistry · command families`"]
    interaction["`**Interaction**\nsnap · grips · previews · picking`"]
  end
  subgraph T3["GoSurvey engine · Services"]
    direction LR
    renderer["`**Renderer**\ncamera · draw lists · caches · device interfaces`"]
    io["`**IO**\nDXF · DWG · glTF · STL · SAT · point files · StarNet`"]
    network["`**Network**\nmap tiles · updates · telemetry`"]
  end
  subgraph T2["GoSurvey engine · Domain"]
    direction LR
    document["`**Document**\nentities · layers · blocks · tables · text · undo · selection`"]
    survey["`**Survey**\npoints · traverse · alignment · coordinate systems`"]
    terrain["`**Terrain**\nTIN · contours · volumes · daylight`"]
    pointcloud["`**PointCloud**\nstorage · octree · cache`"]
    pdf["`**Pdf**\nannotate · measure · compare · scale`"]
  end
  subgraph T1["GoSurvey engine · Kernel"]
    geometry["`**Geometry**\ngeom2d · curves · NURBS · B-rep · mesh`"]
  end
  subgraph T0["GoSurvey engine · Foundation"]
    core["`**Core**\nlogging · settings · units · formatting`"]
    math["`**Math**\nvectors · matrices · rays · UCS`"]
    events["`**Events**\ninput · document changes`"]
    ui["`**UI** (interfaces)\nwindow · dialogs · progress · clipboard`"]
    debug["`**Debug**\nprofiling · traces · invariants`"]
  end
  subgraph PLATFORM["Platform — swappable backends"]
    pqt["`**Qt**\nUI + Events`"]
    pgl["`**OpenGL**\nrender device`"]
    pwin["`**Windows**\npaths · HTTP · crash · dialogs`"]
    phead["`**Headless**\nnull device · test dialogs`"]
  end
  editor --> T4
  T4 --> T3
  T3 --> T2
  T2 --> T1
  T1 --> T0
  T0 -. implemented by .-> PLATFORM
```

Solid arrows indicate permitted dependencies by tier; dotted arrows indicate implementation of interfaces by platform backends. Foundation uses the explicit internal order above; Domain uses only the four named public-header peer edges. The table is authoritative for exact allowed edges.

## Mapping format

`system-map.json` is the machine-readable draft. It maps each current `src` folder to a target system, except mixed `src/util`, which is enumerated file by file. Every enumerated file is marked `ambiguous` and has no guessed target until the audit determines ownership. New source paths must be added as folders or files before this draft can be used as an enforcement baseline.

## Architecture inventory

Run `python3 tools/architecture_inventory.py` from the repository root to write a readable
report and JSON metrics to `build/artifacts/architecture-inventory/`. The scanner does not
compile the project. It scans C/C++ files under `src/` only; folder line counts include files
directly in each folder. Include lookup uses the including file's folder, `src/`, each direct
subfolder of `src/` (matching the project's flat include paths), and the repository root. It
reports the largest source files, per-folder line counts, include fan-in for `CadCommands.hpp`,
the source include graph and its cycles, Qt includes outside `src/qt`, and OpenGL code or headers
outside `src/render`. Use `--root` to scan another checkout and `--output-dir` to choose a
different report directory. Fixture coverage is in
`python3 -m unittest tools.test_architecture_inventory`.

## Dependency ratchet

Run `python3 tools/architecture_dependencies.py` to classify source includes against the system
map and compare current violations with `dependency-baseline.json`. The PR check also scans the
base branch and rejects baseline entries that are not present there, so the baseline can only
shrink. New violations fail the check; removed violations also fail until their entries are
removed from the baseline. Unmapped source files and includes are reported for mapping work.
Generate an updated baseline deliberately with
`python3 tools/architecture_dependencies.py --write-baseline`. Fixture coverage is in
`python3 -m unittest tools.test_architecture_dependencies`. The PR workflow runs this check on
every pull request, including docs-only changes.

## Architecture metrics history

The `architecture-metrics` workflow (`.github/workflows/architecture-metrics.yml`) runs on every push
to `beta`. It records a snapshot for every beta commit that has a system map and no snapshot yet, at
`snapshots/<commit>.json` on the `metrics` branch, so merge commits also record the feature-branch
commits they bring in: largest source file, violation count, `CadCommands.hpp` include fan-in, dependency cycles,
the share of domain tests that build without Qt (zero until systems own tests), and the violation
and file-size baselines. Snapshots carry no timestamp and are never rewritten; a second run for the
same commit is a no-op, and a conflicting snapshot fails the run. Concurrent publishers retry on top
of the latest `metrics` history. Build one snapshot locally with
`python3 tools/architecture_snapshot.py build --root . --commit <full-sha> --output <file>`.
Backfill a commit by running the workflow manually with its full SHA; manual runs work only once
the workflow file is on `master`, because GitHub reads `workflow_dispatch` from the default branch.
The push run after a merge already covers the beta head. Commits before
`docs/architecture/system-map.json` existed are skipped. Fixture coverage is in
`python3 -m unittest tools.test_architecture_snapshot`.

## File-size gate

The `PR gate / file size` job in `.github/workflows/pr-checks.yml` runs
`.github/scripts/check_file_size.py` on every PR, including docs-only ones. Tracked C/C++ sources (excluding `third_party/`) over 3,000 lines fail unless
they are grandfathered in `.github/file-size-baseline.json`. A grandfathered file may not grow, and
it must lower its entry when it shrinks or remove the entry once it is at or under 3,000 lines. The
PR's baseline is checked against the base branch's baseline, so a PR cannot add or raise entries.
Renaming or moving a grandfathered file always fails the gate: the old entry is then missing, and
the new path is an over-limit file that cannot be added to the baseline. The only ways through are
to shrink the file to 3,000 lines or fewer, or to change the gate itself. To regenerate the
baseline from a checkout of `beta`, run
`python .github/scripts/check_file_size.py --write-baseline .github/file-size-baseline.json`.
Fixture coverage is in `python -m unittest discover -s .github/scripts -p "test_*.py"`.
