# Pipe fittings library (`.sat`, `.dwg`, `.dxf`)

Drop validated 3D fitting solids here. On startup and when INSERT opens, GoSurvey lists each
`.sat`, `.dwg` and `.dxf` as a **block definition only** — nothing is placed in the drawing until
you INSERT it, or pick it from the Pipe Fittings palette (REQ-350).

Geometry is already in the drawing's model units (feet by default). Use the INSERT dialog **Block
unit** selector if a fitting needs a different insertion scale.

## Metadata sidecars

A part is only findable *by what it is* if it carries a same-stem `.json` sidecar. `LIBEXPORT`
writes one automatically; a part dropped in by hand needs one written by hand:

```json
{
  "partType": "flange",
  "nominalSize": "2in",
  "pressureClass": "CS150",
  "partNumber": "optional"
}
```

- `partType` — one of `elbow-90`, `elbow-45`, `tee`, `cross`, `reducer`, `flange`, `valve`,
  `coupling`, `cap`, `nozzle`, `other`. Anything else reads as "not a piping part".
- `nominalSize` — an NPS inch label, e.g. `2in`, `1.5in`. This is what the Pipe Fittings palette
  matches against the run being routed, so a part with no size never appears while routing.
- `pressureClass` — `CS150` or `CS300`. **Omit it** when the real class is unknown: an untagged part
  matches a run of either class, whereas a wrong tag hides the part from half of them.

A part with no sidecar still lists in the INSERT library pane — it just has no type, size or class,
so nothing can filter it and the Pipe Fittings palette cannot show it under a category.
