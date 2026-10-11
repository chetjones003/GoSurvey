# TASK-288 — PADSOLID: the earthwork a building pad represents, as solids

- Type:    feat
- Status:  review
- Opened:  2026-10-02
- Owner:   Workshop
- GitHub:  #150 (3D Phase 7), acceptance 5

## Requirement authority

GitHub #150 acceptance 5 — "a solid can be generated from a surface, a boundary and a depth, and its
volume matches the cut/fill computed by the existing surface volume path". REQ-074 (the elevation
query never extrapolates), REQ-131 (a closed ring bounds a volume), REQ-201, REQ-361 (new solids go
on their object layer). **D-2026-10-02-a** records the decisions, taken before any of this was built.

## What it does

`PADSOLID` — select the closed polyline that bounds the pad, then run it. Produces up to **two**
solids: the **cut** (ground above the pad — material to dig out) and the **fill** (ground below it —
material to bring in), each reported with its volume, under one undo step.

It **asks for what it needs**, like every other command with options. Typed bare it prompts for the
boundary if nothing is selected, then sits on `specify pad elevation, or [Surface] <name>` until a
number is given. `S` names the surface and returns to the same prompt; a word that is neither a
number nor an option re-states the prompt rather than ending the command. The surface is remembered,
so the common case — one ground surface, several pads at different levels — is one number each time.

The one-line form `PADSOLID <surface>, <pad elevation>` still works, because a script and a
transcript want it.

The first version took everything on one line and, typed bare, printed a usage line and ended. From
the app that reads as the command doing nothing at all: you type `PADSOLID`, press Enter, and it is
over before you can give it an elevation. Reported from the app on 2026-10-02 and fixed here. The
same report found `PADSOLID` missing from the command **registry**, so autocomplete never offered it
and Enter did nothing — the command tests called the command directly and so never crossed the
registry check that the typing box uses.

The depth is the pad's **finished elevation**, which is how a site plan states it. A thickness below
ground would follow every bump of the hillside and leave no flat floor — a topsoil strip, not a pad.

Cut and fill are never merged. A pad cut into a slope is usually both; a hole and a mound are
different shapes, are billed separately, and either may be absent. One lump spanning both would have
a volume in which the two partly cancel, matching neither.

## The risk, taken first

Nothing else in this kernel builds a solid with thousands of planar faces. The whole feature rests on
that being a *solid* — one the program can validate, measure, tessellate and draw — so that was built
and proven before any command existed:

`brep::MakeHeightFieldSolid` builds the shell between a sampled height field and a flat plane. Top
and bottom are two triangles per cell; walls appear wherever a cell's neighbour is not part of the
pad; every edge is created once and shared, which is what makes it a solid rather than a pile of
triangles. `[padsolid]` in `BrepTests` proves a 40 × 40 pad — 1,600 columns, over 3,000 faces —
validates, tessellates **watertight**, and measures to exact arithmetic.

One thing that had to be measured rather than assumed: the first version passed `+Z` as the top
faces' normal. Flat ground passed; sloping ground failed, because a plane face whose vertices do not
lie on its own plane is not a face and `Validate` says so. Normals are derived from each ring by
Newell's method now.

## Choices made from the code, recorded rather than asked

- **The boundary comes from the selection**, not a typed entity id. An id is what `VOLUMES` takes for
  REQ-131's clip — and it is exactly why that clip has never been exercised end to end: nothing puts
  an id in a user's hands, or in a transcript's. The existing REQ-131 coverage tests only its
  refusals. Selecting the ring you already drew is how every other command takes geometry.
- **~1,600 cells**, far coarser than the volume sampler's 250,000. Every cell becomes four or more
  B-rep faces, so that resolution would be a solid with a million of them. The staircase this leaves
  on the boundary is smaller than the ring's own vertex spacing on any real site.
- **A cell that tapers to nothing** — along the line where the ground meets the pad elevation — is
  left out and counted. A zero-thickness cell contributes zero-area faces and an edge used more than
  twice, which is not a closed solid.

## Tests

- `BrepTests [padsolid]` — six cases on the kernel: a level pad measured exactly (640 cubic units, by
  arithmetic); a sloping one (896, likewise exact, because a plane is integrated exactly by the
  two-triangle split); a fill pad the other way up; a half-masked footprint that still closes, which
  is what makes an L-shaped or clipped pad possible; a pad with no depth refused by name; and the
  1,600-cell case above.
- `PadSolidCommandTests [padsolid]` — six cases on the command, driven through
  `ProcessCommandLineSubmit`. **The acceptance test is the second**: the same ground and the same ring
  measured by two genuinely independent routes — the B-rep's own mass properties and
  `ComputeSurfaceVolume`'s grid integration — and compared.
- `headless.phase7-padsolid-prompted` — the prompted form end to end, through the registry and the
  typing box rather than around them: bare `PADSOLID` asks and **stays running**; `S` names the
  surface and comes back; `banana` keeps the prompt; a number builds the cut and the fill; one UNDO
  takes both; the one-line form and the `PAD` alias still work; a bad surface name still refuses.
- Full suite: 2096/2105. The nine failures are `beta`'s: the seven stale transcripts fixed separately
  on `investigate/stale-3d-tests` (PR #662, not on this branch) and the two intermittent surface
  segfaults filed as issue #663. **None is new.**

### One more failure appeared, and it is the harness, not the program

Adding `PADSOLID` to the command registry made `headless.fuzz-smoke` fail on seed 1. The registry is
where the fuzzer draws its command alphabet, so two new entries shift every later random draw and
seed 1 now generates a different transcript. Verified pre-existing by reverting both command files to
`664dcbf2` (beta), rebuilding `gosurvey_headless`, and re-running the generated transcript: the
identical failure, on identical 215,202-byte files, with none of this work present. The transcript
contains no `PADSOLID`.

The program is right and says so itself:

> MIRROR is still running, so "TEXT" was read as input to it rather than as a command. Press Esc to
> end MIRROR, then type TEXT.
> Nothing to undo.

The undo/redo oracle commits one "anchor" entity, saves, undoes and requires the file to change. Its
own comment says the anchor's job is to be "a GUARANTEED commit" — but it guarantees only that the
*coordinates* are plain, never that **no command is still active**. A randomly drawn command left
running swallows the anchor's input, nothing is committed, and `EXPECT DIFFERENTFILE` becomes
unsatisfiable. Narrowed to twelve lines: `CMD MIRROR` with no `ESC`, then the anchor. One `ESC` ahead
of the anchor closes it. Fixed separately, not here — it is the harness, and this branch is a
command. It will bite the next person who adds any command to the registry.

### Why the acceptance is a relative tolerance

The phase says "within REQ-101". REQ-101 is ±0.002 ft — a **length**. These are two different
integrations of the same shape producing a **volume**, and holding them to a length tolerance would be
the wrong instrument. REQ-131 already sets the precedent for this exact kind of check: its own
analytical fixture is stated "within a stated relative tolerance of 1%". That is what is used, and
why, written into the test file itself.

## Not in scope

- **An exact pad boundary.** The footprint is a staircase of cells rather than the ring itself.
  Clipping TIN triangles to a possibly-concave ring, and splitting them on the cut/fill contour, is a
  general polygon-clipping job and its own increment; this one reuses the grid so the volume agrees
  with the tool that already measures it.
- Side slopes (battering the walls out to daylight), which is what turns a pad into a grading.
- A `PADSOLID` ribbon button — the command is typed, as `VOLUMES` and `EXTRACT` are.
