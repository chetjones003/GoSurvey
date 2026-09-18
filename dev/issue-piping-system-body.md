## Goal

Introduce a **Piping System** — a first-class 3D modeling domain where the user defines **pipe runs** (size, pressure class, and a 3D path), sees the pipe solid follow the cursor as they route, and has **fittings inserted automatically** (90°/45° elbows, tees, reducers, flanges, valves, etc.) from a **GoSurvey-native fittings library** keyed by pipe size and class.

This is the logical next step after #475 (3D block INSERT, connection points, bundled fittings library). Static solid blocks + named connection ports are the foundation; this issue adds the **routing intelligence** and the **authoring toolchain** needed to grow the library inside GoSurvey.

## Why a new issue (not more #475 work)

#475 delivered *placement* — insert a solid fitting at a 3D point, orient it, snap connector-to-connector, browse `resources/blocks/fittings/`. It deliberately stopped short of:

- A **piping object** / **pipe run** entity distinct from a loose INSERT or raw solid
- A **piping network** container that owns runs, segments, and placed fittings as one editable system
- **Automatic fitting selection** from turn angle, elevation change, branch topology, size/class rules
- **Block-editor capabilities** needed to author a full vendor-grade library (metadata, multi-port semantics, export workflow, in-editor previews)

Civil 3D / Plant 3D catalog parts are not portable (#369). GoSurvey must own its library — modeled in BEDIT, tagged with connection ports, exported to the fittings folder.

## User story (target experience)

1. User starts **Create Pipe Run** (ribbon: *Pipe Network*, currently NYI).
2. Chooses **nominal size** (e.g. `4in`) and **pressure class** (e.g. `CS150`).
3. Clicks in 3D to define the run path; a **rubber-band pipe preview** (cylinder sweep along the pending segment) follows the cursor.
4. On each click/commit:
   - A straight **pipe segment** is added.
   - When direction or elevation changes beyond a threshold, the system **inserts the correct elbow** (90° vs 45°) from the library, oriented and connector-snapped.
   - At branches, **tees / crosses** are chosen automatically.
5. User can place **manual fittings** (flange, valve, reducer) from the library; they snap to the run via connection ports.
6. The whole run remains editable as a **Piping System** object (move vertex, change size, swap fitting) without exploding to loose geometry.

## Scope — expect sub-issues / increments

### Track A — Block editor prerequisites (library authoring)

Before the routing engine can consume a library at scale, BEDIT must support building and maintaining fittings:

| Increment | Deliverable |
|-----------|-------------|
| A1 | **Fitting metadata** on block definitions: part type (elbow-90, flange, valve, tee, …), nominal size, pressure class, optional part number — serialized in block JSON and editable in BEDIT |
| A2 | **Connection port roles + engagement** — extend `CadBlockConnection` with role (`inlet` / `outlet` / `branch`), optional compatibility tag, and **engagement length** (how far pipe slides into the port); visual gizmo in BEDIT; list/edit in palette |
| A3 | **Library export workflow** — `WBLOCK` / dedicated command to write a BEDIT block to `resources/blocks/fittings/` (or user library path) with metadata sidecar |
| A4 | **In-editor solid authoring polish** — reliable SAT import inside BEDIT, boolean ops (UNION/SUBTRACT) in block edit session, 3D orbit + face pick ergonomics for `BCONNECT` |
| A5 | **Library browser grouping** — INSERT / library pane filters by size, class, part type; preview shows connection ports |

*These increments unblock the user modeling flanges, elbows, valves, etc. entirely inside GoSurvey.*

### Track B — Piping object & pipe-run routing

| Increment | Deliverable |
|-----------|-------------|
| B1 | **`CadPipeRun` entity** — 3D path (vertices + segment records), nominal size, pressure class, layer/style; persists in `.gs` IO; renders as swept pipe solid |
| B2 | **Interactive routing command** — click-to-add vertices, rubber-band preview solid, undo, osnap to existing connections / faces |
| B3 | **`CadPipingSystem` container** — owns one or more pipe runs + references to placed fitting INSERTs; network-level selection and properties |
| B4 | **Catalog lookup** — map `(size, class, partType)` → block definition name in bundled/user library; refuse with named message when no match |
| B5 | **Auto-fitting at bends** — detect turn angle between segments; insert 90° or 45° elbow block with connector snap; cut pipe segments to fitting engagement length |
| B6 | **Auto-fitting at elevation / branch** — vertical risers, offset transitions, tee/cross at branch nodes |
| B7 | **Manual fitting placement on run** — pick run + station, choose library part, snap via BCONNECT ports |
| B8 | **Edit operations** — grip-edit run vertex, change size/class (refit or warn), replace fitting, split/merge runs |

### Track C — Later (explicitly out of first epic)

- Parametric/regenerating fittings (Civil 3D Parts Catalog model — see #369, ADR-026)
- Hydraulic / pressure-drop analysis
- Isometric or ortho piping drawings from the network
- Import of Plant 3D / Civil 3D pipe networks from DWG
- Clash detection between pipe runs

## Architectural notes (for SPEC / ADR before coding)

- **Piping owns topology; blocks own geometry.** Fittings remain block INSERTs with connection ports (#475). The piping system stores graph edges (pipe segments) and node records (fitting refs + auto-insert rules), not duplicated B-reps.
- **Pipe segment solids** likely generated via sweep/extrude along segment axis (reuse REQ-314 solid pipeline) or cached B-rep — ADR choice: linked vs materialised on edit.
- **Auto-fitting rules** need a recorded decision table (angle thresholds, engagement lengths, reducer rules when size changes) — product decision, not guesswork.
- Ribbon **Pipe Network** button exists as NYI placeholder (`CadUi.cpp` Create Design section).

## Dependencies / related

- **#475** (closed) — 3D INSERT, `CadBlockConnection`, `BCONNECT`, fittings library pane, block units
- **#369** — Civil 3D catalog parts have no portable geometry; GoSurvey-native library is required
- **#147 / REQ-314** — solid modeling (sweep, booleans) for pipe segments and in-BEDIT authoring
- **#318 / ADR-049** — 3D osnap / face pick for routing and connection placement
- **#120** — broader 3D modeling umbrella

## Suggested first increment (recommendation)

**A1 + A2** (fitting metadata + connection roles) — smallest step that lets the user start modeling and tagging library parts while the routing engine is specced. Parallel spec work for B1/B2 can proceed once metadata schema is agreed.

## Acceptance criteria (epic-level)

- [ ] User can author a fitting in BEDIT (solid + multiple tagged connection ports + metadata) and export it to the fittings library
- [ ] Library browser finds the part by size, class, and type
- [ ] User can create a pipe run with size/class, route in 3D with live preview, and get straight pipe segments
- [ ] System auto-inserts at least 90° elbows and tees from the library at direction/branch changes
- [ ] Manual flange/valve placement snaps to run connection ports
- [ ] Piping system round-trips in save/load without losing topology or fitting associations

## Recorded decisions (D-2026-09-12)

1. **Pressure class** — fixed enum for now: **`CS150`** and **`CS300`**. Library lookup and UI pickers use these exact tags (not free text).
2. **Engagement length** — **metadata cutback**. Each connection port (or part default) stores how far pipe engages into the fitting; when auto-inserting a fitting, pipe segments are shortened by that amount. No overlap-then-boolean trim as the primary path.
3. **Networks per drawing** — **multiple named piping networks** in one drawing. Each network owns its runs and fitting refs independently.
4. **Units UX** — **pipe size labels in NPS inches** (`4in`, `6in`, …) for display, catalog tags, and pickers; **run geometry and distances in drawing units (feet)**. The engine converts NPS → OD in feet when building pipe solids. Block-editor geometry stays in feet; `nominalSize` on connections remains an inch label for matching, not a raw foot value.
