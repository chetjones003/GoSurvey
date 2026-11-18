# GoSurvey Rust workspace

Port of GoSurvey to Rust and Tauri, tracked by issue #1403. The Qt/C++ app in the repo root is unchanged and stays releasable until cutover.

## Layers

Dependencies point strictly downward. Each layer crate depends only on the layer directly below it.

| Crate | Rank | Layer |
| --- | --- | --- |
| `gs-foundation` | 0 | Core services, math, events, UI interfaces, debugging, crash reporting |
| `gs-kernel` | 1 | Geometry: 2D geometry, curves, NURBS, B-rep, meshes |
| `gs-domain` | 2 | Drawing, Project, Survey, Terrain, PointCloud, PDF |
| `gs-services` | 3 | Renderer, IO, Network |
| `gs-application` | 4 | Command registry and interaction |
| `gs-editor` | 5 | Tauri shell and ui-sdk front end |

`layer-check` is the gate. It is a tool, not a layer, and it is not checked for direction.

## Commands

Run from this directory:

```
cargo test --workspace      # includes the layer gate
cargo clippy --workspace --all-targets -- -D warnings
cargo fmt --all -- --check
```

The gate test (`layer-check/tests/gate.rs`) fails if any layer crate depends on a crate at the same or a higher rank, and it checks the real workspace graph through `cargo metadata`.

Build output goes to `rust/target/`, which is git-ignored.
