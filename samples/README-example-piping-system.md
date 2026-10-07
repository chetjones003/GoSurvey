# `example-piping-system.dwg`

AutoCAD 2018 Plant 3D / PI piping sample used as the REQ-320 increment 2 regression fixture
(TASK-734). Source: user project drawing renamed on import into the repository.

Expected after increment 2 ships: DWG open imports at least one `brep::Solid` from AcDs-stored ASM
bodies; opening the file must not log only `3DSOLID(empty)` for every solid.
