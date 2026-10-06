#pragma once

// REQ-355 (GitHub issue #564 section 8, D-2026-09-28-k) — the Modeling ribbon tab's buttons as plain
// data. Each button is the text a user would type: the tab submits `command` through the command
// line, so a click and the typed command are one path, and tests/ModelingRibbonTests.cpp runs every
// entry through that same path. Icons are names in resources/icons (the library set already has one
// per command). No ImGui here, so the table is testable headless.

namespace modelingribbon {

struct Button {
  const char* id;       // ImGui id, unique across the tab
  const char* label;    // text beside the icon
  const char* command;  // what the click submits, exactly as typed
  const char* iconName; // resources/icons/<iconName>.png
  const char* tooltip;
};

struct Section {
  const char* title;
  const Button* buttons;
  int count;
};

inline constexpr Button kPrimitives[] = {
    {"##ModBox", "Box", "BOX", "Box", "Box — a rectangular solid.\nCommand bar: BOX"},
    {"##ModWedge", "Wedge", "WEDGE", "Wedge", "Wedge — a solid with one sloped face.\nCommand bar: WEDGE"},
    {"##ModCone", "Cone", "CONE", "Cone", "Cone — a cone or truncated cone.\nCommand bar: CONE"},
    {"##ModCylinder", "Cylinder", "CYLINDER", "Cylinder", "Cylinder — a round solid.\nCommand bar: CYLINDER"},
    {"##ModSphere", "Sphere", "SPHERE", "Sphere", "Sphere — a ball of a given radius.\nCommand bar: SPHERE"},
    {"##ModTorus", "Torus", "TORUS", "Torus", "Torus — a ring-shaped solid.\nCommand bar: TORUS"},
    {"##ModPyramid", "Pyramid", "PYRAMID", "Pyramid", "Pyramid — a pyramid or frustum.\nCommand bar: PYRAMID"},
    {"##ModPolysolid", "Polysolid", "POLYSOLID", "Polysolid",
     "Polysolid — a wall swept along picked points.\nCommand bar: POLYSOLID"},
};

inline constexpr Button kCreate[] = {
    {"##ModExtrude", "Extrude", "EXTRUDE", "Extrude",
     "Extrude — push a closed profile into a solid.\nCommand bar: EXTRUDE"},
    {"##ModRevolve", "Revolve", "REVOLVE", "Revolve",
     "Revolve — turn a closed profile about an axis.\nCommand bar: REVOLVE"},
    {"##ModSweep", "Sweep", "SWEEP", "Sweep", "Sweep — drag a profile along a path.\nCommand bar: SWEEP"},
    {"##ModLoft", "Loft", "LOFT", "Loft", "Loft — a solid through two or more profiles.\nCommand bar: LOFT"},
};

inline constexpr Button kBooleans[] = {
    {"##ModUnion", "Union", "UNION", "Union", "Union — combine two solids into one.\nCommand bar: UNION"},
    {"##ModSubtract", "Subtract", "SUBTRACT", "Subtract",
     "Subtract — cut the second solid out of the first.\nCommand bar: SUBTRACT"},
    {"##ModIntersect", "Intersect", "INTERSECT", "Intersect",
     "Intersect — keep only what two solids share.\nCommand bar: INTERSECT"},
    {"##ModSlice", "Slice", "SLICE", "Slice", "Slice — cut solids with a plane.\nCommand bar: SLICE"},
};

inline constexpr Button kEdit[] = {
    {"##ModFillet", "Fillet", "FILLET", "Fillet_Edge",
     "Fillet — round a solid edge (or a corner between two curves).\nCommand bar: FILLET"},
    {"##ModChamfer", "Chamfer", "CHAMFER", "Chamfer_Edge",
     "Chamfer — bevel a solid edge (or a corner between two curves).\nCommand bar: CHAMFER"},
    {"##ModSection", "Section", "SECTION", "Section_Plane_to_Block",
     "Section — the cross-section of solids by a plane, as a closed polyline.\nCommand bar: SECTION"},
    {"##ModSectionPlane", "Section Plane", "SECTIONPLANE", "Section_Plane",
     "Section Plane — place the section plane on a solid's flat face.\nCommand bar: SECTIONPLANE"},
};

inline constexpr Button kModify3d[] = {
    {"##Mod3dMove", "3D Move", "3DMOVE", "3D_Move", "3D Move — move with the 3D gizmo.\nCommand bar: 3DMOVE"},
    {"##Mod3dRotate", "3D Rotate", "3DROTATE", "3D_Rotate",
     "3D Rotate — rotate with the 3D gizmo.\nCommand bar: 3DROTATE"},
    {"##Mod3dScale", "3D Scale", "3DSCALE", "Scale-001",
     "3D Scale — scale uniformly with the 3D gizmo.\nCommand bar: 3DSCALE"},
    {"##ModMove", "Move", "MOVE", "move", "Move — by base point and offset; solids too.\nCommand bar: MOVE"},
    {"##ModCopy", "Copy", "COPY", "copy", "Copy — by base point and offset; solids too.\nCommand bar: COPY"},
    {"##ModArray", "Array", "ARRAY", "array",
     "Array — rectangular or polar copies; solids too.\nCommand bar: ARRAY"},
};

// Piping's own buttons. PIPERUN is not here: it has its size dropdown beside it and starts at that
// size without asking (StartPipeRunAtCurrentSize), so it is drawn by hand in CadUi.cpp. PIPEFIT's
// command is a prefix — the button opens a menu of kPipeFitPartTypes, each item appending one.
inline constexpr Button kPiping[] = {
    {"##ModPipeFit", "Fitting", "PIPEFIT", "Insert_Block",
     "Fitting — splice a fitting into the selected pipe run.\nCommand bar: PIPEFIT <part type>"},
    {"##ModPipeSplit", "Split", "PIPESPLIT", "Break_at_Point",
     "Split — split the selected pipe run at a point.\nCommand bar: PIPESPLIT"},
    {"##ModPipeJoin", "Join", "PIPEJOIN", "join", "Join — join the selected pipe runs.\nCommand bar: PIPEJOIN"},
};

// PIPEFIT's part types, in the order its own usage message lists them.
inline constexpr const char* kPipeFitPartTypes[] = {"elbow-90", "elbow-45", "tee", "cross", "reducer",
                                                    "flange", "valve", "coupling", "cap"};

template <int N>
constexpr Section MakeSection(const char* title, const Button (&b)[N]) {
  return Section{title, b, N};
}

inline constexpr Section kSections[] = {
    MakeSection("Primitives", kPrimitives), MakeSection("Create", kCreate),
    MakeSection("Booleans", kBooleans),     MakeSection("Edit", kEdit),
    MakeSection("3D Modify", kModify3d),    MakeSection("Piping", kPiping),
};

inline constexpr const char* kPipeRunCommand = "PIPERUN";

} // namespace modelingribbon
