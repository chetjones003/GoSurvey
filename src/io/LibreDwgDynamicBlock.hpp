#pragma once

#include "util/cadblock.hpp"

#include <string>
#include <vector>

struct AppCommandState;

struct _dwg_struct;
typedef struct _dwg_struct Dwg_Data;
struct _dwg_object;
typedef struct _dwg_object Dwg_Object;
struct _dwg_object_BLOCK_HEADER;
typedef struct _dwg_object_BLOCK_HEADER Dwg_Object_BLOCK_HEADER;

/// REQ-369 increment 3 — write native dynamic-block objects for GoSurvey-authored definitions.
/// Returns true when at least one supported parameter/action chain was encoded (R2004+ only).
bool WriteGoSurveyDynamicBlockObjects(Dwg_Data* dwg, Dwg_Object_BLOCK_HEADER* blockHdr, const CadBlockDefinition& def,
                                      const std::vector<CadBlockRef>* refsForParamValues,
                                      std::vector<std::string>& log);

[[nodiscard]] bool CadBlockDefinitionNeedsDynamicDwgExport(const CadBlockDefinition& def);

/// REQ-369 increment 4 — read decoded BPARAM/BACTION objects owned by a block header into \p def.
void ImportDynamicBlockDefinitionFromDwg(const Dwg_Data* dwg, const Dwg_Object* blockHeaderObj,
                                         CadBlockDefinition& def);

struct CadBlockDynamicExportLossCounts {
  size_t visibilityBlockDefs = 0;
  size_t unsupportedParameters = 0;
  size_t extraLinearParameters = 0;
  size_t insertParamConflicts = 0;
  size_t stretchWithoutEntityLinks = 0;
};

[[nodiscard]] CadBlockDynamicExportLossCounts ComputeCadBlockDynamicExportLossCounts(const AppCommandState& st);
