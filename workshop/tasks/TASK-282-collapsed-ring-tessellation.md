# TASK-282 — a collapsed ring tessellates as one triangle, not a degenerate pair

- Type:    fix
- Status:  review
- Opened:  2026-09-28
- Owner:   Workshop
- GitHub:  none — found on `beta` by its own suite

## Requirement authority

REQ-313 (solid display), REQ-101. No requirement changed: `RequireMeshWatertight` already stated the
rule this restores.

## Why

`beta`'s own test `Tessellation agrees with the analytic figures and winds outward` was failing:

```
cone
cracked (non-manifold) triangle edges: 256 of 768
```

Found while rebasing the section stack onto `beta`, and confirmed **not** ours by building clean
`beta` in a separate worktree and reproducing it there. It arrived with the recent tessellation work
(the batch that fixed the bolt-hole and NURBS-loft cracks).

## What it actually was

Measured, not inferred. A diagnostic over the failing cone reported:

- 3 faces — one plane cap, two `Cone` half-faces (`u` 0→π and π→2π) — 256 triangles each;
- the bad edges used **4** times, not 1 or 3;
- every one of them running from a base-rim point to the apex.

Four uses is the tell. It is not a crack (that reads as 1) and not two faces meeting badly (3). The
conical band emits **two** triangles per segment. On a sharp cone the whole upper ring is the apex —
one point repeated — so the second triangle of every segment has two coincident corners. A zero-area
sliver draws nothing, which is why it was never seen; but its two "different" edges are the *same*
rim-to-apex edge, so that edge is counted twice, giving 2 + 2 = 4.

The arithmetic closes exactly: 128 segments per half-face → 128 slivers per face → 256 degenerate
triangles, matching the 256 flagged edges one for one. **Half of a sharp cone's side mesh was junk.**

Fixing the cone then exposed the **sphere** at 512 of 97536, which the cone had been masking — `REQUIRE`
aborts the case at the first failure, and the cone is tested first. Same mechanism at the poles, where
every longitude meets at one point.

## What changed

`src/util/brep.cpp`, in `Tessellate`:

- **Conical band** (`Cylinder`/`Cone`) — the ring positions are kept alongside their mesh indices, and
  a segment whose upper or lower ring has collapsed emits only the triangle that is not degenerate.
- **Spherical / toroidal grid** — the same test on each cell's two `v`-edges. A torus has no pole and
  is unaffected.
- Emission **order** is unchanged, so a band with no collapsed ring — every cylinder, every frustum,
  every torus — produces the identical index buffer it always has.

The collapse test is geometric (positions compared against an epsilon scaled to the surface), not a
check for `radius2 == 0`, so it also covers a cut cone whose kept range happens to reach the apex.

## Result

A sharp cone: **768 → 512** triangles, every edge used exactly twice.

## Tests

- `BrepTests [collapsedring]` — the cone's apex and the sphere's poles carry no degenerate triangle
  and are watertight and outward-wound; a frustum, a cylinder and a torus are untouched (which is
  what says the collapse test is not trimming real geometry); and the apex mesh still measures the
  analytic cone volume, because the slivers enclosed nothing.
- The new test was run against the unfixed build and fails there on both shapes, 256 of 768 and
  512 of 97536.
- `beta`'s own `[req313]` tessellation test now passes.
- Full suite: 1812/1819 — `beta`'s 7 headless failures, unchanged.

## Checked rather than assumed

`headless.req313-solid-isolines` and `headless.req313-solid-primitives` are two of those 7 and both
touch solids, so both were run against an **unfixed rebuild** as well as a fixed one: identical
failures, same step and same numbers (`SOLIDEDGESEGS expected 102, got 514`; a `.dwg` that should lack
solids containing them). Neither is affected by this change. The first attempt at that comparison was
invalid — stashing the source does not rebuild the binary — and was redone properly.

## Not in scope

- The apex is still pushed once per segment, so the vertex count is unchanged (769 for the test cone)
  even though the triangle count halves. Sharing one vertex is a separate, purely-size change.
- The 7 pre-existing `beta` headless failures.
