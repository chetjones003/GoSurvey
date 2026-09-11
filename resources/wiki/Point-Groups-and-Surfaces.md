# Point Groups and Surfaces

A **point group** is a named rule that selects survey points. A **surface** is a TIN triangulated
from one or more point groups. Groups feed surfaces, so build them in that order.

---

## Point Groups

**Ribbon:** Survey → Groups

A group stores a **rule**, never a member list. Membership is recomputed from the current points
every time it is asked for — so a point imported after the group was defined joins it with no edit,
and a deleted point leaves nothing behind.

### Creating a group

Press **New point group**. Give it a name — names are unique within the drawing, and an empty name
is refused (*"Point group name cannot be empty…"*, *"A point group named … already exists — rename
refused."*).

### The rule

> **Include points matching ANY of:**

The criteria combine as a **union (OR)**. A point joins the group if it matches **any** filled-in
criterion.

| Criterion | What it matches |
|---|---|
| **Point numbers** | Ranges and singles, e.g. `1-500, 1200, 1400-1450`. Reversed ranges are normalised |
| **Description matches** | Wildcard against the point's **description** |
| **Raw description matches** | Wildcard against the point's **raw field code** |
| **Group picks** | Points picked by hand in the drawing. Always members, whatever the other criteria say |

**Wildcards** are case-insensitive: `*` matches any run of characters including none, `?` matches
exactly one, everything else is literal. Case-insensitivity is deliberate — field codes are typed
in the field, and `eg` meaning something different from `EG` would be a trap.

![Point Groups](wiki-img:08-point-groups.png)

*A group keyed on **Raw description matches** = `EG`, so office edits to the description cannot
break it.*

> **An empty rule matches nothing, not every point.** The dialog says so:
> *"No criteria set — this group contains 0 points."* Treating "no filter" as "match everything" is
> how a whole drawing ends up in a surface by accident.

### Working with a group

| Control | What it does |
|---|---|
| **Add selection** | Adds the currently selected points to the group as explicit picks — *"Added N point(s) to …"* |
| **Clear picks** / **Clear group picks** | Removes the explicit picks, leaving the rule criteria |
| **Delete** | Deletes the group |
| Live count | The dialog reports `Matches N of M points.` as you type |

### Description versus raw description

This is the reason both fields exist.

A crew codes a point `EG`. The office expands the description to `EXISTING GROUND — SW CORNER`. A
group keyed on **description** stops matching. A group keyed on **raw description** keeps matching,
because the raw code is never rewritten by an edit to the description.

**Key surface-building groups on raw description.**

Points saved before raw descriptions existed have an empty raw field; those fall back to the
description rather than being skipped.

---

## Surfaces

**Ribbon:** Survey → Surfaces

> *A surface is built from point groups — create one in Survey ▸ Groups first.*

A surface is a **TIN** — a triangulation of the points in the groups you choose.

### Creating a surface

1. Open **Surfaces** and press **New from group…**
2. Name the surface. Names are unique; an empty name is refused.
3. Under **Build from point groups:**, tick the groups to include. The dialog reports the total —
   `N point(s) selected.`
4. Press **Create**.

**At least three points are required.** With fewer, the dialog says
`N point(s) selected — need at least 3.` and Create is unavailable.

![Create Surface dialog](wiki-img:09b-surfaces-panel.png)

*The Create Surface dialog — choose **TIN surface**, name, style, and layer. After **OK**, assign
point groups in the Surfaces manager.*

![Surface Style dialog](wiki-img:09-surfaces.png)

*The Surface Style dialog — Display tab — controlling contour colours, triangle visibility, and
other surface display components.*

### The surface list

Open **Surfaces** from the Survey ribbon or Toolspace to see every surface, its point/triangle
counts, elevation range, and the groups it was built from. **Surface Properties → Statistics**
reports the same numbers in full.

![Surface Properties — Statistics](wiki-img:09c-surfaces-list.png)

*Surface Properties for a built TIN — 500 points, 982 triangles, elevation 103.52 to 136.62, with
plan area and slope statistics.*

| Element | Meaning |
|---|---|
| `%d points, %d triangles.` | What the surface was built from |
| `Elevation %.2f to %.2f (%.2f range).` | The elevation range covered |
| **Built from:** | The groups feeding it. A group that has since been deleted shows as `(missing)` |
| `Not built.` | The surface exists but has never been triangulated |

### Rebuilding

**Rebuild** re-triangulates from the **current** points in the listed groups.

> *Rebuilds from the current points in the groups above.*

Because groups are rules, importing more points that match a group and then pressing **Rebuild** is
all it takes to bring a surface up to date. Nothing needs re-selecting.

### Renaming and deleting

**Rename surface** and **Delete surface** are on the right of the panel. Both are confirmed in the
command log.

### Displaying a surface

A surface is drawn as its **triangle edges** — a wireframe mesh over the points.

> **Visual style does not shade a TIN.** Setting **Visual style** to `SHADED` has **no effect on
> surfaces**: they render as triangle edges in all three styles. `SHADED` fills *imported meshes*
> (`IMPORTMODEL`) and *hatches*, not TIN surfaces. Lit surfaces are a planned feature, not a
> current one — see [[Known Limitations]].

To read a surface in three dimensions, **orbit it**. Type `ORBIT` (`3DO`) and drag: the elevation
differences become obvious as the mesh tilts, which is the check that catches a bad shot or a spike
in the triangulation. `SURFELEV` gives you the numbers.

![Orbited TIN surface](wiki-img:11-surface-3d.png)

*The same surface after `ORBIT`. The ViewCube (top right) shows the view is no longer plan — major
and minor contours, survey points, and the Toolspace surface definition are all visible.*

See [[Views and Navigation]].

---

## Reading elevations off a surface — `SURFELEV` (`SE`)

**Ribbon:** Inquiry → Elev/Grade **Command:** `SURFELEV`, `SE`

```
SURFELEV — pick a point for its surface elevation; pick a second for grade. ESC cancels.
SURFELEV: Pick a point for its surface elevation | ESC cancel
SURFELEV: Second point for grade | ESC stops after the elevation
```

- **First pick** — reports the interpolated elevation on **every surface covering that point**.
- **Second pick** — reports the grade, slope, and distances between the two.

A pick that falls **outside** a surface says so rather than extrapolating a number you should not
trust.

With no surfaces in the drawing the command refuses to start:
`SURFELEV — there are no surfaces in the drawing. Build one from a point group first.`

---

## A worked example

**Goal:** a ground surface from field shots, excluding the control points.

1. **Survey → Groups → New point group.** Name it `EG`.
2. Set **Raw description matches** to `EG*`. The dialog reports how many points match.
3. **Survey → Surfaces → New from group…** Name it `Existing Ground`.
4. Tick the `EG` group and press **Create**. The panel reports the point and triangle counts and
   the elevation range.
5. Type `ORBIT` and drag to tilt the view — a 3/4 angle exposes spikes and bad shots that a plan
   view hides.
6. Use `SURFELEV` to spot-check elevations against known shots.

Later, after importing a second day of shots coded `EG`:

7. **Survey → Surfaces**, select `Existing Ground`, press **Rebuild**. The new points are already
   in the group.

---

## Common problems

| Problem | Cause | Solution |
|---|---|---|
| `New from group…` is unavailable | No point groups exist | Create one first in **Survey → Groups** |
| A group matches 0 points | Every criterion is empty, or the wildcard is wrong | An empty rule matches nothing by design. Try `*EG*` rather than `EG` |
| A group stopped matching after description edits | The rule is keyed on **description** | Key it on **Raw description matches** |
| Create is greyed out | Fewer than three points selected | Add more points or more groups |
| The surface is stale after an import | Surfaces are not rebuilt automatically | Press **Rebuild** |
| A group shows `(missing)` under Built from | The group was deleted after the surface was built | Recreate the group, or rebuild from a different one |
| `SURFELEV` refuses to start | No surfaces in the drawing | Build one first |
| `SURFELEV` says a pick is off the surface | The point is outside the TIN | It will not extrapolate. Add shots, or pick inside |

---

## Related

[[Survey Points]] · [[Views and Navigation]] · [[Inquiry Commands]] · [[Traverse Editor]]
