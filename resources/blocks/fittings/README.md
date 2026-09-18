# Pipe fittings library (ACIS `.sat`)

Drop validated 3D fitting solids here. On startup and when INSERT opens, GoSurvey imports each
`.sat` as a **block definition only** — nothing is placed in the drawing until you INSERT it.

Geometry is already in the drawing's model units (feet by default). Use the INSERT dialog **Block
unit** selector if a fitting needs a different insertion scale.
