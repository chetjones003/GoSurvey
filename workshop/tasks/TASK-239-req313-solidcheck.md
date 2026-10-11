# TASK-239 — SOLIDCHECK, and the kernel split it turned out to need after all

> **Read §7 first.** Sections 2-6 record a conclusion that was WRONG: they argue no kernel change
> was needed. A code review disproved it the same day and §7 is the correction. The earlier sections
> are kept because how the wrong answer was reached is the useful part.

- Type:    feat (amendment to an accepted requirement)
- Status:  review
- Opened:  2026-09-09
- Owner:   Workshop
- GitHub:  #149 acceptance 7 (3D Phase 6 — Analysis), step 5 of 6.

## 1. Authority

- **REQ-313 / ADR-045** — the kernel and its validity contract. **Amended here** (D-2026-09-09-j)
  to pin that each fault is reported apart, and to add the command.
- **REQ-201** — refuse with a stated reason; report a fault rather than silently repairing it.
- Constraints: CON-06 smallest change. It turned out to be smaller than planned.

## 2. Problem — and the premise that was wrong

#149 acceptance 7: *"Solid validation detects non-manifold, unclosed and inconsistently oriented
solids."*

The phase gameplan budgeted kernel work for this, on the strength of an earlier probe (P3) that
broke a box four ways and saw **two** of the faults come back as the same message,
`"A face boundary does not close."` The plan read: *"Split the two colliding `Problem` values."*

**Reading `Validate` before starting said otherwise.** It already had:

```
Problem::EdgeNotUsedTwice             "The surface is not closed: an edge does not bound exactly
                                       two faces."
Problem::EdgeOrientationInconsistent  "Two faces disagree about which way an edge runs."
```

Two distinct values, two distinct messages. So a second probe (**P6**) was written to find out which
was wrong — the validator or the fixtures.

**The fixtures were.** `Validate` checks ring closure — each edge use ending where the next begins —
**before** it tallies edge uses, so a fixture that disturbs a loop's ring never reaches the tally and
reports `LoopNotClosed` whatever fault it was built to test. P3 got both of its two wrong, in two
different ways:

| P3's fixture | why it never reached the check it was aiming at |
|---|---|
| flip `reversed` on every use of one loop | reverses each edge's direction of travel without reversing the **sequence**, so consecutive uses stop meeting — the ring breaks |
| append an extra use to another face's loop | breaks that loop's ring too |

Rebuilt so the ring still closes — **reverse the order AND the flags** for the orientation fault,
**duplicate a whole FACE rather than a loop** for the non-manifold one — P6 gets:

```
inconsistently oriented   -> Two faces disagree about which way an edge runs.
non-manifold              -> The surface is not closed: an edge does not bound exactly two faces.
unclosed                  -> The solid's topology refers to a face, edge or vertex that does not exist.
degenerate                -> An edge has no length.
```

Four faults, four reasons. **There was nothing to split, and no kernel change was made.**

(One intermediate fixture is worth recording too: duplicating a loop onto an *existing* face makes
that face an outer boundary plus an identical "hole", so its area collapses and `DegenerateFace`
fires first — a third way to write a fixture that tests something other than what it claims.)

## 3. What was actually missing, and what was built

A way for a user to ask. `SOLIDCHECK` — the selection, or the whole drawing when nothing is
selected; read-only, no undo entry, nothing repaired.

**It asks two questions, and that is the one design decision in this slice.** `brep::Validate` asks
whether the topology holds up. `brep::SelfIntersects` asks the separate geometric question that
`Validate` deliberately answers Ok: a torus whose tube is wider than its ring is legitimate topology
and draws correctly, but its surface encloses part of space twice, so `ComputeMassProperties`
declines its volume, area and centroid.

A check reporting only the first would call that solid **sound**, and leave the user to discover from
a Properties panel that has gone blank that nothing can be measured about it — with no statement
anywhere of why. So both are asked, and the self-intersecting case gets its own line and its own
tally.

## 4. Tests

**`BrepTests [req149]`, 5 cases** — and the fixtures *are* the subject, so each asserts the
**specific** `Problem`, never merely that validation failed:

| case | what it pins |
|---|---|
| inconsistently oriented | `EdgeOrientationInconsistent`, from a loop reversed in order **and** flags |
| non-manifold | `EdgeNotUsedTwice`, from a duplicated **face** |
| unclosed vs degenerate | each names its own reason, and the two differ |
| all four pairwise distinct | the assertion P3 could not make |
| self-intersecting torus | `Validate` says Ok, `SelfIntersects` says true, mass properties decline |

**`headless.req313-solidcheck`** covers the command: empty drawing, whole-drawing check, the
self-passing torus, and a selection narrowing the scope — plus that nothing is drawn and no undo
entry is made.

A note carried in the transcript's header, because it cost a debugging round: the driver's `ESC`
verb cancels a running command and does **not** clear a selection, so a box selection persists and
every later `SOLIDCHECK` would silently narrow to it. The selection case is therefore last.

## 5. Assumptions

None. The premise was re-measured rather than assumed, which is the whole story of this task.

## 6. Verification result

- **build-project** — clean release build, MSVC/Ninja, no new warnings.
- **architecture-review** — no kernel change at all; one command function, one registry entry, one
  header declaration. No new layer, dependency or abstraction.
- **code-review** — the command follows `CadReportSolids`' shape, which it sits beside.
- **dependency-audit** — none added.
- **performance-review** — one `Validate` and one `SelfIntersects` per solid, on a command the user
  invokes by hand.
- **testing** — full suite **1441/1441 green**, 5 new unit cases and 1 new transcript.

COMPLETION REPORT — TASK-239 — 2026-09-09
- Requirements satisfied:  REQ-313 as amended (D-2026-09-09-j); GitHub #149 acceptance 7
- Summary:                 SOLIDCHECK asks two questions; the planned kernel split was unnecessary
- Tests:                   5 unit cases + 1 transcript; 1424/1424
- Verification verdict:    PASS
- Assumptions:             none
- Architectural decisions: D-2026-09-09-j — including the decision NOT to make a planned change
- Dependencies:            none added
- Technical debt noted:    none
- Build:                   reproducible, clean on Windows/MSVC
- Docs updated:            REQ-313 acceptance + revisions, a traceability row, D-2026-09-09-j,
                           this task log

---

## 7. CORRECTION, same day — the collision was real after all (D-2026-09-09-k)

**Everything above §6 that says "no kernel change was needed" is wrong.** A code review of the PR
disproved this task's central claim, and re-probing confirmed the review. The correction is recorded
here rather than by rewriting the sections above, because how the wrong answer was reached is more
useful than a tidy log.

### What was actually true

`Validate`'s edge-use tally read:

```cpp
const int total = forwardUses[i] + reverseUses[i];
if (total != 2)
  return Problem::EdgeNotUsedTwice;
```

**One value for two different faults** — `total < 2` is an edge bounding one face (the shell is
**open**), `total > 2` is an edge bounding three (the surface is **not a manifold**) — and its text
says *"The surface is not closed"*, which is simply wrong for the second. So a non-manifold solid was
reported, through the SOLIDCHECK this task added, as an unclosed one.

The enum's own comment said so the whole time: *"A non-manifold **or** open shell: an edge bounding
one face, **or three**."* It was read during this task and not registered.

### Why the corrected probe missed it

P6 replaced P3's fixtures and got the orientation case right. Its "unclosed" fixture was careless in
a **new** way: it popped the face's index from `shells[0].faces` and left the `Face` in `s.faces`.
That is an **orphan face**, and `Validate` checks "every face belongs to exactly one shell" *before*
the edge tally, so the fixture returned `IndexOutOfRange` and never built an unclosed shell at all.

P6's own output said as much — *"refers to a face, edge or vertex that does not exist"* is not an
unclosed shell — and it was quoted verbatim into this log, the requirement and the PR description
without being read.

**So there are TWO checks standing between a fixture and the edge tally**, not one:

| trap | what a naive fixture reports instead | the fix |
|---|---|---|
| ring closure | `LoopNotClosed` | reverse a loop's **order** as well as its `reversed` flags |
| face ownership | `IndexOutOfRange` (orphan face) | pop the face from **both** `faces` and the shell |

### The part that should have caught it

**A correct fixture already existed in the same file**, ~300 lines above the new ones:
`Validate rejects broken topology` → *"a missing face leaves edges bounding only one face"*, which
pops from both and asserted `EdgeNotUsedTwice`. Reading the existing validation tests before writing
new ones would have produced both the right fixture and the collision immediately. Instead new
fixtures were written from scratch, and one of the new cases duplicated a pre-existing one verbatim.

### What changed

- **Kernel**: `Problem::ShellOpenAtEdge` and `Problem::EdgeNonManifold`, each with its own text.
  `EdgeNotUsedTwice` is kept unrenamed for FILLET/CHAMFER, which raise it as a precondition where
  "exactly two" genuinely is the condition, and where the existing wording is right.
- **Tests**: the unclosed fixture pops from both; every fault is asserted **by name**; a new case
  pins the orphan-face trap so it cannot be mistaken for an unclosed shell again; the duplicate
  orientation case was removed in favour of the pre-existing one; both new values were added to the
  every-reason-has-its-own-text case, which is the guard that catches exactly this class of bug.
  Three pre-existing tessellation tests moved from `EdgeNotUsedTwice` to `ShellOpenAtEdge`.
- **Command**: a selection holding no solids now says so instead of silently widening to the whole
  drawing (the review's finding 5).
- **Spec**: REQ-313's clause, its traceability row and D-2026-09-09-k all state the corrected
  position, and the rule is strengthened — **do not accept pairwise distinctness as evidence**, since
  four unrelated faults are pairwise distinct too, which is precisely how the wrong claim passed its
  own test.

### Verification after the correction

Full suite **1441/1441 green**. The three tessellation tests that changed value are the proof the
split reaches real call sites rather than only the new cases.

COMPLETION REPORT — TASK-239 — 2026-09-09 (superseding §6)
- Requirements satisfied:  REQ-313 as amended (D-2026-09-09-j **as corrected by -k**); #149 acc. 7
- Summary:                 the kernel split WAS needed; SOLIDCHECK ships on top of it
- Tests:                   corrected fixtures, each fault asserted by name, orphan-face trap pinned
- Verification verdict:    PASS
- Assumptions:             none — the premise was re-measured twice, and wrong the first time
- Architectural decisions: D-2026-09-09-k, reversing -j
- Technical debt noted:    none
- Docs updated:            REQ-313 clause + traceability row, D-2026-09-09-k, this section
