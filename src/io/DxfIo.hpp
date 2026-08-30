#pragma once

#include <string>
#include <vector>

struct AppCommandState;

/// DXF interchange via GNU LibreDWG (REQ-170 / ADR-041). ASCII and binary DXF as LibreDWG decodes
/// and encodes them. Model-space LINE/CIRCLE/ARC/ELLIPSE/LWPOLYLINE/POLYLINE/TEXT/MTEXT/POINT/INSERT
/// (exploded) map into the GoSurvey domain; other classes are named in the log.
bool ImportDxfFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
bool ExportDxfFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
