# SURFCROSSBL

**Command:** `SURFCROSSBL`
**Category overview:** [[Point Groups and Surfaces]]

Lists or resolves places where two breaklines on a surface cross at different elevations.

| Usage | Effect |
|---|---|
| `SURFCROSSBL <surface>` | List unresolved crossings |
| `SURFCROSSBL <surface>, RESOLVE, x, y, <choice>` | Resolve the crossing at x, y |

Choice: `HIGHER`, `LOWER`, `AVERAGE`, `TRIM-A`, `TRIM-B`, or an elevation. The decision is stored on
the surface; the source breaklines are not changed.

---

## Related

- [[Point Groups and Surfaces]]
- [[Command Reference]]
