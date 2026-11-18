#pragma once

#include <string>

// In-process GNU LibreDWG (REQ-170 increment 1 / ADR-041).
// Mapping into Cad stores is later; this seam only proves the library is linked.

/// Package version string compiled into LibreDWG (e.g. "0.13.4"), never empty on a successful link.
const char* LibreDwgPackageVersion();

struct _dwg_struct;

/// Issue #590: gives every entity of every block explicit prev/next links (R13-R2000 `nolinks` = 0).
/// LibreDWG's add API leaves `nolinks` = 1 ("my neighbours are handle - 1 and handle + 1") on the
/// last entity it added to a block, which claims a next entity that does not exist; AutoCAD then
/// refuses the file with eDwgCRCDoesNotMatch. Call after the last dwg_add_* and before encoding.
void LibreDwgLinkBlockEntities(_dwg_struct* dwg);

/// Writes a R2000 DWG (AC1015) containing one model-space LINE from (0,0,0) to (10,0,0).
/// Writes a minimal one-LINE DWG at the given LibreDWG version (R2000 through R2018).
bool LibreDwgWriteMinimalAtVersion(int libreDwgVersionType, const char* pathUtf8);
bool LibreDwgWriteMinimalR2000(const char* pathUtf8);
bool LibreDwgWriteMinimalR2004(const char* pathUtf8);

/// Reads a DWG and returns LibreDWG's version name (e.g. "R2004"). Empty on missing file or decode error.
std::string LibreDwgReadVersionName(const char* pathUtf8);
