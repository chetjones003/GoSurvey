# TASK-205 — Import ACIS 3D-solid blocks from .dwg/.dxf (REQ-320, GitHub issue #299)

## Requirement authority

- REQ-320 (import an ACIS `3DSOLID` block, analytic primitives, SAT only), depending on REQ-313 /
  ADR-045 (the analytic `brep::Surface` kinds this maps onto) and REQ-300 (no vendored ACIS/geometry
  kernel).
- ADR-051 records the four scope decisions this task needed before any code: SAT vs. SAB (a), which
  analytic surfaces ship first (b), the further plane/cylinder/cone-full-revolve-only cut within that
  (b-1, added after code review caught a partial-revolve span defect — see Revisions below), the
  face-boundary mismatch with the kernel's rectangular `Face` model and why general trimmed faces are
  a separate issue (#302) rather than this task's to solve (c), and refuse-never-approximate (d).
- Two follow-up issues were filed for the scope this ADR explicitly defers: **#300** (spline/blend/
  swept surfaces) and **#301** (SAB binary ACIS). A third, **#302**, tracks the kernel extension
  (general trimmed-boundary faces) issue #299's own scoping work surfaced as a prerequisite gap for a
  *future* increment, not this one.

## Files / subsystems affected

- `src/util/AcisSatParser.{hpp,cpp}` (new) — the SAT record tokenizer and topology-to-`brep::Solid`
  builder. Pure and dependency-free (no LibreDWG, no GL), same ADR-002 pressure as `brep` itself.
- `src/io/LibreDwgCad.cpp` — `ImportAcisSolid`, wired into `ImportObject`'s `DWG_TYPE__3DSOLID` case.
- `src/util/cadblock.hpp`, `src/commands/CadBlocks.cpp` — `CadBlockContent::solids`/`solidAttrs`, so a
  solid-bearing block round-trips through `BLOCKIMPORT`/`INSERT`/`WBLOCK` the way `meshes` already does
  (closes the same round-trip gap #284 fixed for 2D geometry, for *any* solid block, ACIS-derived or
  native).
- `CMakeLists.txt` — new sources added to `gosurvey_domain` and to `GoSurveyTests`.
- `tests/AcisSatParserTests.cpp` (new).

## Implementation approach

ACIS already gives an explicit topology graph (body → lump → shell → face → loop → coedge → edge →
vertex → point), so this is a **graph translation** into `brep::Solid`, not a primitive-recognition
problem — unlike, say, recognizing a Boolean-result shape from a mesh. The parser:

1. Tokenizes SAT text into records (skips the 3 mandatory ACIS header lines, splits the rest on `#`,
   resolves `$N` pointers to 0-based record positions).
2. Walks the one supported body/lump/shell, gathering ACIS vertex/edge records into a deduplicated
   `brep::Solid` (dedup by ACIS record identity — the same vertex record used by two faces is the same
   vertex).
3. Maps `plane-surface` directly to `SurfaceKind::Plane` (arbitrary loop, no restriction — the kernel
   already tessellates a simple polygon of line/arc edges for a plane).
4. Maps `cone-surface` (which also encodes a cylinder as its zero-half-angle case) to
   `SurfaceKind::Cylinder`/`Cone`, but **only recognizes the full-revolve loop shape** — two full-circle
   rim edges, no seam. Because a full revolve's two rims share no vertex with each other, and
   `brep::Validate`'s ring-closure check requires consecutive loop edges to share a vertex, the importer
   synthesizes one connecting `Line` edge (used once each direction) — the same device the kernel's own
   `MakeCylinder` uses with its two seam lines, with no effect on the analytic area/volume (those
   integrate the surface in closed form from `uStart`/`uEnd`, never from the loop's shape).
5. Refuses everything else by name: SAB (checked in `LibreDwgCad.cpp` before the parser is even called,
   via `Dwg_Entity__3DSOLID::version`), `spline-surface`/blend/sweep, `sphere-surface`/`torus-surface`,
   a partial (seam/arc/seam/arc) cylinder/cone revolve, a non-rectangular planar hole configuration, a
   multi-lump body, a wire/sheet (non-solid) body, and any malformed record — each with a message naming
   the specific record or face (REQ-201).
6. No exceptions (the project builds with them disabled): every parsing step returns `bool` and writes
   to an `error_` member on first failure, mirroring `brep::Make*`'s `Problem* outWhy` pattern.

`LibreDwgCad.cpp`'s `ImportAcisSolid` refuses a non-identity placement transform (a `3DSOLID` reached
through a rotated/scaled nested `INSERT`) — transforming an imported solid's surface/edge frames
consistently is out of scope this increment; the primary case (a block *definition's own* `3DSOLID`,
imported directly by `BLOCKIMPORT`) always reaches this code with an identity transform.

## Test approach

`tests/AcisSatParserTests.cpp` — pure unit tests against hand-authored SAT fixtures (no real vendor SAT
corpus is available; ADR-051 explicitly sanctions this). Covers: a plain cylinder (2 planar caps + 1
full-revolve cylindrical wall) importing to a valid, correctly-oriented `brep::Solid` whose volume
matches the closed-form `pi r^2 h` exactly; an empty stream; an unsupported surface kind
(`spline-surface`) refused by name; `sphere-surface` refused as a recognized-but-deferred fast-follow;
a wire body refused; a malformed record refused without crashing.

No DWG-level (LibreDWG round-trip) test was added — authoring a synthetic `3DSOLID` entity via
LibreDWG's write API in a test is significantly more involved than the existing DWG tests (which only
exercise GoSurvey's own exporter, and GoSurvey does not write `3DSOLID` on export). The
`LibreDwgCad.cpp` wiring (13 lines: one dispatch case plus `ImportAcisSolid`) was reviewed by hand
instead. A follow-up could add a synthetic-DWG fixture if the risk is judged to warrant it.

## Architectural-boundary check

- `AcisSatParser` depends only on `brep.hpp`/`ray3d.hpp`/`ucs.hpp` — no LibreDWG, no GL, no
  `AppCommandState` — so it lives in `gosurvey_domain`'s pure-kernel tier alongside `brep.cpp`, and is
  covered by `GoSurveyTests` (which also links no LibreDWG/GL) rather than `GoSurveySnapTests`.
- `LibreDwgCad.cpp` is the only file that bridges LibreDWG's `Dwg_Entity__3DSOLID` to the parser; the
  parser itself has no LibreDWG dependency (it takes a plain `std::string`).
- No SPEC was changed to make the implementation pass — the reverse happened during review: a defect
  (partial-revolve u-span silently defaulting to a full revolve's `[0, 2*pi)`) was found, and the SPEC
  (ADR-051, REQ-320) was narrowed to match what was actually verified, with the defect's code path
  refused by name instead.

## Revisions

- 2026-09-05 — initial implementation (plane/cylinder/cone, full-revolve only, SAT only). A
  code-review pass on this same change caught that a first draft recognized a **partial** cylinder/cone
  revolve loop shape but always set its `u`-span to the full revolve's `[0, 2*pi)` regardless — a real
  defect (silent area/volume over-reporting) that `brep::Validate` cannot catch (it checks topology, not
  a face's span against its loop's actual geometric extent). Fixed by narrowing scope rather than
  patching the formula under time pressure: the partial-revolve shape is now refused by name and left
  as an explicit fast-follow, and ADR-051/REQ-320 were revised to match. A second review finding (this
  task file did not exist before code was written, per CLAUDE.md's mandatory plan-before-code step) is
  addressed by this file's own retroactive creation — recorded here rather than silently omitted.
