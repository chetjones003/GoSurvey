# SURFACEVISIBILITY

**Command:** `SURFACEVISIBILITY` — aliases: `SURFVIS`
**Category overview:** [[Point Groups and Surfaces]]

Visibility analysis on a surface with a built TIN.

| Usage | Result |
|---|---|
| `SURFACEVISIBILITY <surface>, LOS, eyeX, eyeY, eyeHeight, targetX, targetY, targetHeight` | Line of sight: visible, or the first obstruction |
| `SURFACEVISIBILITY <surface>, ZVI, observerX, observerY, eyeHeight, targetHeight, radius` | Zone of visual influence: visible, partly visible and hidden cells |

Heights cannot be negative. ZVI radius is 0 to 10000.

---

## Related

- [[Point Groups and Surfaces]]
- [[Command Reference]]
