#pragma once

#include <string>
#include <vector>

struct _dwg_struct;

/// Reset per-import counters (call once at the start of DWG import).
void DwgAnnotContextImportBegin();

/// Scan \p dwg for AutoCAD annotation context objects (issue #688 / REQ-384).
void DwgAnnotContextImportScan(const _dwg_struct* dwg);

/// Append REQ-201 import summary when context objects were present.
void DwgAnnotContextImportAppendLog(std::vector<std::string>& log);

/// Count decoded `*_OBJECTCONTEXTDATA` and `CONTEXTDATAMANAGER` objects (tests / diagnostics).
int DwgAnnotContextCountObjects(const _dwg_struct* dwg);

/// Test helper: append a minimal `MTEXTOBJECTCONTEXTDATA` object (LibreDWG has no public writer).
bool DwgTestAddBareMtextContextObject(_dwg_struct* dwg);
