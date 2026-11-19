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
