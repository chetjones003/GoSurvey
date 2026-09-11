# TASK-221 — the sub-object pre-highlight works while FILLET is running (REQ-323)

## The request

User, 2026-09-08, immediately after TASK-220 landed: *"can you now make it so that while in the
fillet command it will highlight the face or edge that you are about to select."*

TASK-220 made `Ctrl`+click *work* mid-command; this makes it **visible**. Without it the user could
select an edge but had no way to know which one a click would take except by taking it — which is
precisely the gap REQ-318 item 14 (D-2026-09-04-b) closed for the idle case and this left open.

## Root cause

One clause in `CadUi.cpp`:

```cpp
const bool blockEntityHover = (cmd.active != AK::None && !trimEntityPick && !extendEntityPick &&
                               !breakEntityPick && !lengthenEntityPick) || …
```

Every hover — the entity highlight, the sub-object pre-highlight and the rollover — hangs off this.
With any command running it is true unless the command is one of four named exceptions, and the
comment above it states the rule those four satisfy: *"Every other command still suppresses hover,
since their clicks mean coordinates rather than objects."*

**FILLET's clicks mean objects.** `ViewportPickPolicy` routes it to `RawEntityPick`, and since
REQ-323 a `Ctrl`+click there names a solid edge. It belonged in that list from the start and was
simply never added — the omission predates the solid fillet, so the 2D fillet has been picking lines
without hover feedback all along too.

## The fix

One term:

```cpp
// FILLET: both phases are entity SEARCHES … it belongs with TRIM/EXTEND/BREAK/LENGTHEN rather than
// with the coordinate-entry commands.
const bool filletEntityPick = cmd.active == AK::Fillet;
```

added to the exception list. Nothing else changed: the sub-object hover, its rollover and the entity
hover were all already correct, they were just unreachable.

**CHAMFER is the identical shape and is deliberately NOT included.** It is `RawEntityPick` too, but
no solid chamfer exists yet, so there is nothing for a sub-object pre-highlight to offer there. One
line when there is — the comment says so, so the next reader does not have to work out whether the
omission was reasoned.

## Verified in the running application

Headless transcripts cannot drive a modifier-held hover, so this was checked by hand:

- `FILLET` with nothing selected, then `Ctrl` held over a top edge → rollover **"Solid edge 4"**;
- moved to a different edge → **"Solid edge 6"**, so it tracks rather than latching;
- moved onto an end face → the face tints **purple** with a purple outline and the rollover reads
  **"Solid face 5"** — the same two-colour scheme the idle pre-highlight uses (D-2026-09-04-b:
  purple for a face, blue for an edge or vertex, so the three kinds are told apart at a glance).

A hovered FACE is not a mistake to suppress: it is what the pick would return there, and clicking it
gets `"FILLET - that is not an edge. Ctrl+click a solid EDGE"` from TASK-220. **What lights up is
what selects**, which is the rule the pre-highlight exists to make true.

## Verification

- **build-project** — PASS.
- **testing** — PASS. `ctest` **1309/1309**, unchanged — this reaches existing behaviour rather than
  adding any.
- **architecture-review** — PASS. One term in an existing predicate; no new state, no new path.
- **code-review** — self-run. The change is small enough that the risk is in what ELSE the term
  unblocks: with `blockEntityHover` false, FILLET also gets the ordinary 2D entity hover when Ctrl is
  **not** held. That is the same feedback TRIM and EXTEND already give while picking objects, and it
  is an improvement for the 2D fillet, but it is a behaviour change beyond the request and is
  recorded here rather than left to be discovered.
- **performance-review** — PASS. The hover pick was already rate-gated by `HoverPickGateShouldRun`
  (~30 Hz, TASK-199); FILLET now shares that budget rather than adding one.

## Not covered by test, stated plainly

- **The hover itself.** No headless verb drives a modifier-held cursor, so nothing here is asserted
  mechanically. The three cases above were checked by driving the application.
- **The 2D fillet's new entity hover.** Reached by the same term, not exercised.

## Technical debt

- **DEBT-1 — CHAMFER.** Same clause, same reasoning, deliberately deferred until a solid chamfer
  exists.
- **DEBT-2 — no hover coverage in the headless harness at all.** Three tasks in a row (TASK-200,
  TASK-220, TASK-221) have now leaned on manual verification for hover behaviour. A verb that sets a
  cursor position and modifier state, then asserts `subObjectHoverValid` and the hovered kind, would
  cover all of them.

---

**DEBT-1 discharged 2026-09-08** by TASK-222 (REQ-331). A solid chamfer now exists, so `CHAMFER`
joined the hover exception list — the one line this task predicted. The predicate's variable was
renamed `filletEntityPick` -> `cornerEntityPick` at the same time, since it now names both.

**Not re-verified by hand, and stated plainly.** The three GUI cases above were checked for FILLET;
the CHAMFER term reaches byte-for-byte the same code with the same command shape, so TASK-222 did not
repeat them. **DEBT-2 therefore stands and is now the gap it was always going to be** — a verb that
sets a cursor position and modifier state, then asserts `subObjectHoverValid` and the hovered kind,
would have made this a one-line test instead of an argument from similarity.
