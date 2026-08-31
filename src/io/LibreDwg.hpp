#pragma once

#include <string>

// In-process GNU LibreDWG (REQ-170 increment 1 / ADR-041).
// Mapping into Cad stores is later; this seam only proves the library is linked.

/// Package version string compiled into LibreDWG (e.g. "0.13.3"), never empty on a successful link.
const char* LibreDwgPackageVersion();

/// Writes a R2000 DWG (AC1015) containing one model-space LINE from (0,0,0) to (10,0,0).
/// LibreDWG 0.13.3's encoder documents R2000 as the supported write target; R2004 encode
/// is still planned upstream (`dwgwrite --help`).
bool LibreDwgWriteMinimalR2000(const char* pathUtf8);

/// Reads a DWG and returns LibreDWG's version name (e.g. "R2004"). Empty on missing file or decode error.
std::string LibreDwgReadVersionName(const char* pathUtf8);
