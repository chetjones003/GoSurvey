# TASK-1061 — RAWSET command/domain and ordering

Authority: accepted REQ-452, D-2026-10-22-j, approved TASK-1039 plan; GitHub #1061.

Branch starts at integration tip `12dc9d60`. Scope is Commands and Traverse Domain:
add raw records/provenance, parse/serialize validation and command projection with
stable IDs, station references, dependency ordering, diagnostics and scoped overrides.
No Qt UI, file IO, solver changes or dependencies. Public parser/serializer tests
are the user-approved seam. Implement one failing behavioral test and minimal fix
at a time, then run the existing traverse tests and Release build.

Plan verification: PASS against the accepted grammar and existing downward layer
boundary. RAWSET is independent project data, not a reduced observation; it must
not silently add averaged measurements to the solver. Source metadata is parsed
as provenance, never as a measurement. File persistence remains #1062.

## Implementation

Task branch: `feat/rawset-domain-order-1061` in the dedicated task worktree.
The integration branch is local-only; fetching its remote ref returned "couldn't
find remote ref". The local integration tip is authoritative for this handoff.

- `TraverseData::rawSets` stores distinct `TraverseRawSet` records, referencing
  `rawSources` by opaque source ID and reusing `TraverseMeasSet` face values.
  This is independent of legacy `TraverseLeg::rawSets` importer/reduction detail.
- The existing `Parse` / `Serialize(TraverseData, weights)` seam reads/writes the
  accepted RAWSET grammar and JSON-string source declarations. Face values use
  round-trip double precision. Invalid records fail atomically with physical line
  numbers, including unknown sources declared later in the document.
- `Project` produces one Commands-owned `CommandNode` per directive, setup flags,
  station references/anchors, stable opaque IDs and attached node metadata.
  It validates node metadata; callers use `Parse` for measurement validation.
- `Order` derives station/leg/setup dependencies, preserves stable source order
  for unrelated ties, and identifies competing station definitions and strongly
  connected cyclic portions (including self dependencies). Known origin/control
  points take precedence over closing-leg providers.
- Complete, distinct manual ranks resolve only the diagnosed portion. Partial or
  duplicate ranks do not resolve it, and ambiguity ranks cannot reverse existing
  dependencies. `Serialize(CommandDocument)` keeps comments and node metadata
  attached to their command blocks and emits the derived order. Canvas coordinates
  and group membership do not affect that order.

No UI, file persistence, importer, reduction or solver implementation changed.
The integration's IO/UI owners can consume these public value types and command
APIs in #1062/#1063. No new library or manifest change was needed; JSON uses the
repository's existing nlohmann dependency.

## TDD evidence

The user-approved seam was the public command/domain parser and serializer.
Observed red → green slices: unknown RAWSET directive; discarded trailing fields;
missing projection API; lost opaque identity/metadata; absent cycle/ambiguity
diagnostics; false closed-loop ambiguity; generated-ID collision; distinct anchors
for leading-zero numeric IDs; spaced option-prefix handling; punctuation in source
IDs; physical line numbers with prefixed directives; self-dependency diagnostics.
Each failing slice was followed by its implementation and a passing focused run.
Additional coverage checks F1-only, F2-only and dual-face fidelity, distinct files
sharing station pairs/set numbers, malformed tuples/IDs/numbers/source declarations,
atomic failures, unresolved anchors, metadata rejection, scoped override round trips,
and layout-independent ordering.

## Verification

- Focused RAWSET/projection tests: **PASS**, 16 cases / 222 assertions.
- All `TraverseTests.cpp` tests (`GoSurveyTests.exe -# [#TraverseTests]`): **PASS**,
  139 cases / 1,035 assertions.
- Release application/tests/helper build (`.\build.bat`): **PASS**, including
  final application relink. No compiler warnings observed.
- Full Release CTest (`ctest --preset ninja-release --parallel 6`): **PASS**,
  191/191 checks, 94.76 seconds. Includes domain shards, six hidden-desktop Qt
  shards, headless transcripts/fuzz smoke, output layout, deployment and parity.
  The earlier sequential full run also passed 191/191 in 420.85 seconds.
- After the full run, negative-test assertions were strengthened to require the
  intended RAWSET/source diagnostic rather than any parse failure. The test
  target was rebuilt and both focused/all-traverse runs above passed again.
- Architecture/code self-review: value ownership, downward dependencies, public
  seam tests and scope boundaries checked. No blocking findings. No new dependency.
- No performance improvement or large-import UI performance claim is made. The
  integration's full graph/IO scale acceptance remains in #1064 as planned.
- UI screenshot review: not applicable; no visible Qt surface changed.

Logs live under `build/artifacts/rawset-*` and are not committed. Verification
verdict: **PASS**. No blockers, new assumptions or technical debt identified in
this ticket's scope. The latest local integration tip remains `12dc9d60`.
`git merge --no-edit feat/node-measurement-editor-1059` returned `Already up to
date.` at handoff. Issue #1061 stays open for the integration PR to close.
