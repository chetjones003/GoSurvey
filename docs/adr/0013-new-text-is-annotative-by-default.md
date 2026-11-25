# New text is annotative by default

- Status: Accepted
- Date: 2026-11-25

## Context

Since D-2026-11-06-a, a non-annotative TEXT or MTEXT is sized by the drawing plot scale (Drawing Settings), and only annotative objects follow the annotation scale on the status bar. A new text style had Annotative unticked, so new text took the plot scale. In issue #1667 the owner drew an MTEXT with the status bar at 1" = 5' and got text sized for the 1" = 50' plot scale: 6.25 model units instead of 0.625, ten times what they expected.

## Decision

New TEXT and MTEXT is annotative unless its text style is explicitly set non-annotative. A style with no Annotative setting counts as annotative. The Text Style dialog shows it ticked. With no active style, new text is annotative. The owner chose this on 2026-11-25 in issue #1667.

The D-2026-11-06-a rule stands for objects that are not annotative: they keep the plot scale. Existing text keeps its own Annotative flag, and no drawing changes size on load.

## Consequences

New text, and the labels that commands stamp with the active text style, follow the status-bar annotation scale. That covers multileader, volume-report, surface and position-marker labels. To make plot-scale text, a user unticks Annotative on the text style or on the object. An annotative MTEXT stores its frame at the drawing scale and draws it at the annotation scale (issue #1667), so a box dragged at any scale wraps the same way when shown at that scale.
