# Glossary

Domain terms for GoSurvey. Define a term here once; code, issues and ADRs use it exactly as written.

## Project

**Project file**
The small marker file of a project. It holds the project's identity (ID and name), its folder layout, its settings and its list of tracked files. It never holds survey points.

**Point database**
The one store of a project's survey points. Every drawing in the project reads and writes this same copy, so a point edited in one drawing is the same point in every other drawing of the project.

**Source drawing**
The drawing that created a survey point. A point created in a drawing that has never been saved is tagged with that drawing's name instead.

**Point visibility rules**
A drawing's rules for which of the project's points it shows (for example, a point group). The rules belong to the drawing, not to the project file or the point database.

## Coordinates

**World coordinates**
The state-plane coordinates shared by every drawing in a project. A drawing keeps its own points relative to its document origin and converts at the boundary.

**Bearing**
A direction in the drawing plane measured clockwise from north. The Kernel owns bearing, translation and geometry calculations; command code parses input and manages interaction state.

## Engine

**Engine**
The Application-layer object that owns the open Drawing and accepts commands for it. The front end never changes a Drawing directly.

**Command seam**
The boundary where the front end sends a command and reads back the Engine's status and a snapshot. Tests drive the seam, not the internals.

**Command**
A named action the user invokes, such as LINE. A command may request additional input before it finishes.

**Command session**
A Command that is still collecting input and retains the context needed to continue, such as LINE between points.

**Command input parser**
The Application module that turns command-line text into named commands, coordinate inputs, bearings and distances. It parses syntax; geometric calculations belong to the Kernel.

**Command input event**
Normalized user intent delivered by the Editor to the Application, such as submitted text, a drawing-plane point pick or cancellation. It contains no platform keyboard or pointer event.

**Command prompt**
The message describing what the active Command currently accepts as input.

**Engine status**
Whether the Engine can accept commands, and the version of the engine. The front end shows it as "Engine ready".

**Drawing snapshot**
What the front end sees of the open Drawing: its name, entity count and line geometry. It is a read-only copy, never the Drawing itself.

**Untitled drawing**
The empty Drawing a new Engine opens with, named "Untitled" and holding no entities.
