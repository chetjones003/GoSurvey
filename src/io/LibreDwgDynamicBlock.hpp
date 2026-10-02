#pragma once

#include "util/cadblock.hpp"

#include <string>
#include <vector>

struct _dwg_struct;
typedef struct _dwg_struct Dwg_Data;
struct _dwg_object_BLOCK_HEADER;
typedef struct _dwg_object_BLOCK_HEADER Dwg_Object_BLOCK_HEADER;

/// REQ-369 increment 3 — write native dynamic-block objects for GoSurvey-authored definitions.
/// Returns true when at least one supported parameter/action chain was encoded (R2004+ only).
bool WriteGoSurveyDynamicBlockObjects(Dwg_Data* dwg, Dwg_Object_BLOCK_HEADER* blockHdr, const CadBlockDefinition& def,
                                      std::vector<std::string>& log);

[[nodiscard]] bool CadBlockDefinitionNeedsDynamicDwgExport(const CadBlockDefinition& def);
