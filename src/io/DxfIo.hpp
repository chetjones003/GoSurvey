#pragma once

#include <string>
#include <vector>

struct AppCommandState;

/// Replaces CAD geometry from ASCII DXF (model space only: skips paper space group 67 and VIEWPORT; expands INSERT;
/// LINE/LWPOLYLINE/POLYLINE (VERTEX/SEQEND)/CIRCLE/ARC/ELLIPSE/POINT/TEXT/MTEXT/DIMENSION/HATCH/ACAD_TABLE).
bool ImportDxfFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);

/// Writes CAD geometry as ASCII DXF (AC1032): HEADER/CLASSES/TABLES/BLOCKS/ENTITIES/OBJECTS aligned with the
/// ObjectARX DXF Reference (empty CLASSES; minimal OBJECTS; *Model_Space / *Paper_Space block names; symbol tables;
/// ByBlock/ByLayer linetypes; owner 330 to *Model_Space; survey points as POINT with $PDMODE X when present;
/// AcDbText/AcDbMText group 50 radians; extrusion 210–230).
bool ExportDxfFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
