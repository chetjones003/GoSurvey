#pragma once

#include <string>
#include <vector>

struct AppCommandState;

/// Replaces CAD geometry from ASCII DXF (model space only: skips paper space group 67 and VIEWPORT; expands INSERT;
/// LINE/LWPOLYLINE/POLYLINE (VERTEX/SEQEND)/CIRCLE/ARC/ELLIPSE/POINT/TEXT/MTEXT/DIMENSION/HATCH/ACAD_TABLE). Survey database untouched.
bool ImportDxfFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);

/// Writes drawing CAD geometry as ASCII DXF (R2004-style minimal sections).
bool ExportDxfFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
