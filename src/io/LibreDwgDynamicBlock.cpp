#include "LibreDwgDynamicBlock.hpp"

#include "CadCommands.hpp"
#include "LibreDwgCad.hpp"

#include <cassert>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#if defined(__cplusplus) && !defined(restrict)
#define restrict
#endif
extern "C" {
#include <dwg.h>
#include <dwg_api.h>
}

extern "C" void dwg_set_next_objhandle(Dwg_Object* obj);
extern "C" void dwg_resolve_objectrefs_silent(Dwg_Data* dwg);

namespace {

constexpr BITCODE_BL kBeMajor = 27;
constexpr BITCODE_BL kBeMinor = 52;
constexpr BITCODE_BL kEvalMajor = 31;
constexpr BITCODE_BL kEvalMinor = 31;

void InitBlock2PtParameterFields(BITCODE_BL** propStates) {
  assert(propStates != nullptr);
  if (*propStates == nullptr)
    *propStates = static_cast<BITCODE_BL*>(std::calloc(4, sizeof(BITCODE_BL)));
}

void InitBlockParamValueSet(Dwg_Data* dwg, Dwg_BLOCKPARAMVALUESET& vs) {
  assert(dwg != nullptr);
  if (vs.desc == nullptr)
    vs.desc = dwg_add_u8_input(dwg, "");
  vs.num_valuelist = 0;
  vs.valuelist = nullptr;
}

int EnsureObjectClass(Dwg_Data* dwg, const char* dxfname, const char* cppname) {
  assert(dwg != nullptr && dxfname != nullptr && cppname != nullptr);
  if (dwg->dwg_class == nullptr)
    return -1;
  for (BITCODE_BS i = 0; i < dwg->num_classes; ++i) {
    const char* name = dwg->dwg_class[i].dxfname;
    if (name != nullptr && std::strcmp(name, dxfname) == 0)
      return static_cast<int>(dwg->dwg_class[i].number);
  }
  return dwg_add_class(dwg, dxfname, cppname, "ObjectDBX Classes", false);
}

std::uint64_t ObjectHandleValue(const Dwg_Object* obj) {
  return obj != nullptr ? obj->handle.value : 0u;
}

std::uint64_t BlockHeaderHandle(Dwg_Object_BLOCK_HEADER* hdr) {
  if (hdr == nullptr)
    return 0;
  int err = 0;
  const Dwg_Object* o = dwg_obj_generic_to_object(hdr, &err);
  return ObjectHandleValue(o);
}

void SetOwnerToBlock(Dwg_Object* obj, Dwg_Data* dwg, std::uint64_t blockHandle) {
  if (obj == nullptr || obj->tio.object == nullptr || blockHandle == 0)
    return;
  obj->tio.object->ownerhandle = dwg_add_handleref(dwg, 4, blockHandle, obj);
}

void InitEvalExpr(Dwg_EvalExpr* e, int nodeId, double numValue) {
  assert(e != nullptr);
  std::memset(e, 0, sizeof(*e));
  e->parentid = -1;
  e->major = kEvalMajor;
  e->minor = kEvalMinor;
  e->value_code = 40;
  e->value.num40 = numValue;
  e->nodeid = nodeId;
}

Dwg_Object* AppendObjectShell(Dwg_Data* dwg, Dwg_Object_Type fixedtype, const char* objName, const char* dxfName,
                              int classNumber, std::uint64_t blockHandle) {
  assert(objName != nullptr && dxfName != nullptr);
  const BITCODE_BL idx = dwg->num_objects;
  const int added = dwg_add_object(dwg);
  if (added > 0)
    return nullptr;
  if (added < 0)
    dwg_resolve_objectrefs_silent(dwg);
  Dwg_Object* obj = &dwg->object[idx];
  obj->supertype = DWG_SUPERTYPE_OBJECT;
  obj->fixedtype = fixedtype;
  obj->type = static_cast<BITCODE_BS>(classNumber);
  obj->dxfname = (dwg->opts & DWG_OPTS_IN) ? _strdup(dxfName) : const_cast<char*>(dxfName);
  obj->name = (dwg->opts & DWG_OPTS_IN) ? _strdup(objName) : const_cast<char*>(objName);
  obj->tio.object = static_cast<Dwg_Object_Object*>(std::calloc(1, sizeof(Dwg_Object_Object)));
  if (obj->tio.object == nullptr)
    return nullptr;
  obj->tio.object->objid = obj->index;
  obj->tio.object->dwg = dwg;
  dwg_set_next_objhandle(obj);
  SetOwnerToBlock(obj, dwg, blockHandle);
  return obj;
}

const CadBlockParameter* FindParameter(const CadBlockDefinition& def, std::string_view paramName) {
  for (const CadBlockParameter& p : def.parameters) {
    if (CadBlockEqCi(p.name, paramName))
      return &p;
  }
  return nullptr;
}

const CadBlockAction* FindFirstAction(const CadBlockDefinition& def, CadBlockActionKind kind,
                                      std::string_view paramName) {
  for (const CadBlockAction& a : def.actions) {
    if (a.kind == kind && CadBlockEqCi(a.paramName, paramName))
      return &a;
  }
  return nullptr;
}

Dwg_Object* AppendFlipParameter(Dwg_Data* dwg, std::uint64_t blockHandle, const CadBlockParameter& param,
                                const CadBlockAction& action, int nodeId) {
  const int classNumber = EnsureObjectClass(dwg, "BLOCKFLIPPARAMETER", "AcDbBlockFlipParameter");
  if (classNumber < 0)
    return nullptr;
  Dwg_Object* obj =
      AppendObjectShell(dwg, DWG_TYPE_BLOCKFLIPPARAMETER, "BLOCKFLIPPARAMETER", "BLOCKFLIPPARAMETER", classNumber,
                        blockHandle);
  if (obj == nullptr)
    return nullptr;
  auto* pp = static_cast<Dwg_Object_BLOCKFLIPPARAMETER*>(std::calloc(1, sizeof(Dwg_Object_BLOCKFLIPPARAMETER)));
  if (pp == nullptr)
    return nullptr;
  obj->tio.object->tio.BLOCKFLIPPARAMETER = pp;
  pp->parent = obj->tio.object;
  InitEvalExpr(&pp->evalexpr, nodeId, static_cast<double>(param.value >= 0.5f ? 1.0 : 0.0));
  pp->name = dwg_add_u8_input(dwg, param.name.c_str());
  pp->be_major = kBeMajor;
  pp->be_minor = kBeMinor;
  pp->show_properties = 1;
  pp->chain_actions = 1;
  pp->def_basept.x = static_cast<double>(action.originX);
  pp->def_basept.y = static_cast<double>(action.originY);
  pp->def_basept.z = 0.0;
  pp->def_endpt.x = pp->def_basept.x + static_cast<double>(action.dirX);
  pp->def_endpt.y = pp->def_basept.y + static_cast<double>(action.dirY);
  pp->def_endpt.z = 0.0;
  pp->flip_label = dwg_add_u8_input(dwg, param.name.c_str());
  pp->flip_label_desc = dwg_add_u8_input(dwg, "Flip");
  pp->base_state_label = dwg_add_u8_input(dwg, "Not Flipped");
  pp->flipped_state_label = dwg_add_u8_input(dwg, "Flipped");
  pp->def_label_pt.x = pp->def_basept.x;
  pp->def_label_pt.y = pp->def_basept.y + 0.5;
  pp->def_label_pt.z = 0.0;
  InitBlock2PtParameterFields(&pp->prop_states);
  return obj;
}

Dwg_Object* AppendFlipAction(Dwg_Data* dwg, std::uint64_t blockHandle, const CadBlockParameter& param,
                             const CadBlockAction& action, int nodeId) {
  const int classNumber = EnsureObjectClass(dwg, "BLOCKFLIPACTION", "AcDbBlockFlipAction");
  if (classNumber < 0)
    return nullptr;
  Dwg_Object* obj =
      AppendObjectShell(dwg, DWG_TYPE_BLOCKFLIPACTION, "BLOCKFLIPACTION", "BLOCKFLIPACTION", classNumber, blockHandle);
  if (obj == nullptr)
    return nullptr;
  auto* act = static_cast<Dwg_Object_BLOCKFLIPACTION*>(std::calloc(1, sizeof(Dwg_Object_BLOCKFLIPACTION)));
  if (act == nullptr)
    return nullptr;
  obj->tio.object->tio.BLOCKFLIPACTION = act;
  act->parent = obj->tio.object;
  InitEvalExpr(&act->evalexpr, nodeId, 0.0);
  act->name = dwg_add_u8_input(dwg, (param.name + "Action").c_str());
  act->be_major = kBeMajor;
  act->be_minor = kBeMinor;
  act->display_location.x = static_cast<double>(action.originX);
  act->display_location.y = static_cast<double>(action.originY);
  act->display_location.z = 0.0;
  act->conn_pts[0].code = 1;
  act->conn_pts[0].name = dwg_add_u8_input(dwg, "DistanceType");
  act->conn_pts[1].code = 2;
  act->conn_pts[1].name = dwg_add_u8_input(dwg, "FlipState");
  return obj;
}

Dwg_Object* AppendFlipGrip(Dwg_Data* dwg, std::uint64_t blockHandle, const CadBlockAction& action, int nodeId) {
  const int classNumber = EnsureObjectClass(dwg, "BLOCKFLIPGRIP", "AcDbBlockFlipGrip");
  if (classNumber < 0)
    return nullptr;
  Dwg_Object* obj =
      AppendObjectShell(dwg, DWG_TYPE_BLOCKFLIPGRIP, "BLOCKFLIPGRIP", "BLOCKFLIPGRIP", classNumber, blockHandle);
  if (obj == nullptr)
    return nullptr;
  auto* grip = static_cast<Dwg_Object_BLOCKFLIPGRIP*>(std::calloc(1, sizeof(Dwg_Object_BLOCKFLIPGRIP)));
  if (grip == nullptr)
    return nullptr;
  obj->tio.object->tio.BLOCKFLIPGRIP = grip;
  grip->parent = obj->tio.object;
  InitEvalExpr(&grip->evalexpr, nodeId, 0.0);
  grip->name = dwg_add_u8_input(dwg, "FlipGrip");
  grip->be_major = kBeMajor;
  grip->be_minor = kBeMinor;
  grip->bg_location.x = static_cast<double>(action.originX);
  grip->bg_location.y = static_cast<double>(action.originY);
  grip->bg_location.z = 0.0;
  grip->orientation.x = static_cast<double>(action.dirX);
  grip->orientation.y = static_cast<double>(action.dirY);
  grip->orientation.z = 0.0;
  return obj;
}

Dwg_Object* AppendLinearParameter(Dwg_Data* dwg, std::uint64_t blockHandle, const CadBlockParameter& param,
                                  const CadBlockAction& action, int nodeId) {
  const int classNumber = EnsureObjectClass(dwg, "BLOCKLINEARPARAMETER", "AcDbBlockLinearParameter");
  if (classNumber < 0)
    return nullptr;
  Dwg_Object* obj =
      AppendObjectShell(dwg, DWG_TYPE_BLOCKLINEARPARAMETER, "BLOCKLINEARPARAMETER", "BLOCKLINEARPARAMETER", classNumber,
                        blockHandle);
  if (obj == nullptr)
    return nullptr;
  auto* lp = static_cast<Dwg_Object_BLOCKLINEARPARAMETER*>(std::calloc(1, sizeof(Dwg_Object_BLOCKLINEARPARAMETER)));
  if (lp == nullptr)
    return nullptr;
  obj->tio.object->tio.BLOCKLINEARPARAMETER = lp;
  lp->parent = obj->tio.object;
  InitEvalExpr(&lp->evalexpr, nodeId, static_cast<double>(param.value));
  lp->name = dwg_add_u8_input(dwg, param.name.c_str());
  lp->be_major = kBeMajor;
  lp->be_minor = kBeMinor;
  lp->show_properties = 1;
  lp->chain_actions = 1;
  lp->def_basept.x = static_cast<double>(action.originX);
  lp->def_basept.y = static_cast<double>(action.originY);
  lp->def_basept.z = 0.0;
  const double len = std::hypot(static_cast<double>(action.dirX), static_cast<double>(action.dirY));
  const double ux = len > 1.e-9 ? static_cast<double>(action.dirX) / len : 1.0;
  const double uy = len > 1.e-9 ? static_cast<double>(action.dirY) / len : 0.0;
  lp->def_endpt.x = lp->def_basept.x + ux;
  lp->def_endpt.y = lp->def_basept.y + uy;
  lp->def_endpt.z = 0.0;
  lp->distance_name = dwg_add_u8_input(dwg, "Distance");
  lp->distance_desc = dwg_add_u8_input(dwg, "Distance");
  lp->distance = static_cast<double>(param.value);
  lp->value_set.minimum = static_cast<double>(param.minValue);
  lp->value_set.maximum = static_cast<double>(param.maxValue);
  lp->value_set.increment = 1.0;
  InitBlock2PtParameterFields(&lp->prop_states);
  InitBlockParamValueSet(dwg, lp->value_set);
  return obj;
}

Dwg_Object* AppendLinearGrip(Dwg_Data* dwg, std::uint64_t blockHandle, const CadBlockAction& action, int nodeId) {
  const int classNumber = EnsureObjectClass(dwg, "BLOCKLINEARGRIP", "AcDbBlockLinearGrip");
  if (classNumber < 0)
    return nullptr;
  Dwg_Object* obj =
      AppendObjectShell(dwg, DWG_TYPE_BLOCKLINEARGRIP, "BLOCKLINEARGRIP", "BLOCKLINEARGRIP", classNumber, blockHandle);
  if (obj == nullptr)
    return nullptr;
  auto* grip = static_cast<Dwg_Object_BLOCKLINEARGRIP*>(std::calloc(1, sizeof(Dwg_Object_BLOCKLINEARGRIP)));
  if (grip == nullptr)
    return nullptr;
  obj->tio.object->tio.BLOCKLINEARGRIP = grip;
  grip->parent = obj->tio.object;
  InitEvalExpr(&grip->evalexpr, nodeId, 0.0);
  grip->name = dwg_add_u8_input(dwg, "LinearGrip");
  grip->be_major = kBeMajor;
  grip->be_minor = kBeMinor;
  grip->bg_location.x = static_cast<double>(action.originX) + static_cast<double>(action.dirX);
  grip->bg_location.y = static_cast<double>(action.originY) + static_cast<double>(action.dirY);
  grip->bg_location.z = 0.0;
  grip->orientation.x = static_cast<double>(action.dirX);
  grip->orientation.y = static_cast<double>(action.dirY);
  grip->orientation.z = 0.0;
  return obj;
}

Dwg_Object* AppendStretchAction(Dwg_Data* dwg, std::uint64_t blockHandle, const CadBlockParameter& param,
                                const CadBlockAction& action, int nodeId) {
  const int classNumber = EnsureObjectClass(dwg, "BLOCKSTRETCHACTION", "AcDbBlockStretchAction");
  if (classNumber < 0)
    return nullptr;
  Dwg_Object* obj = AppendObjectShell(dwg, DWG_TYPE_BLOCKSTRETCHACTION, "BLOCKSTRETCHACTION", "BLOCKSTRETCHACTION",
                                      classNumber, blockHandle);
  if (obj == nullptr)
    return nullptr;
  auto* act = static_cast<Dwg_Object_BLOCKSTRETCHACTION*>(std::calloc(1, sizeof(Dwg_Object_BLOCKSTRETCHACTION)));
  if (act == nullptr)
    return nullptr;
  obj->tio.object->tio.BLOCKSTRETCHACTION = act;
  act->parent = obj->tio.object;
  InitEvalExpr(&act->evalexpr, nodeId, 0.0);
  act->name = dwg_add_u8_input(dwg, (param.name + "Stretch").c_str());
  act->be_major = kBeMajor;
  act->be_minor = kBeMinor;
  act->display_location.x = static_cast<double>(action.originX);
  act->display_location.y = static_cast<double>(action.originY);
  act->display_location.z = 0.0;
  act->conn_pts[0].code = 1;
  act->conn_pts[0].name = dwg_add_u8_input(dwg, "Distance");
  act->conn_pts[1].code = 2;
  act->conn_pts[1].name = dwg_add_u8_input(dwg, "Stretch");
  act->num_pts = 2;
  act->pts = static_cast<BITCODE_2RD*>(std::calloc(2, sizeof(BITCODE_2RD)));
  if (act->pts != nullptr) {
    act->pts[0].x = static_cast<double>(action.originX) - 0.5;
    act->pts[0].y = static_cast<double>(action.originY) - 0.5;
    act->pts[1].x = static_cast<double>(action.originX) + static_cast<double>(action.dirX) + 0.5;
    act->pts[1].y = static_cast<double>(action.originY) + static_cast<double>(action.dirY) + 0.5;
  }
  act->num_hdls = 0;
  act->num_codes = 0;
  return obj;
}

bool AppendEvaluationGraph(Dwg_Data* dwg, std::uint64_t blockHandle, const std::vector<Dwg_Object*>& nodes) {
  if (nodes.empty())
    return false;
  const int classNumber = EnsureObjectClass(dwg, "ACAD_EVALUATION_GRAPH", "AcDbEvalGraph");
  if (classNumber < 0)
    return false;
  Dwg_Object* obj =
      AppendObjectShell(dwg, DWG_TYPE_EVALUATION_GRAPH, "EVALUATION_GRAPH", "ACAD_EVALUATION_GRAPH", classNumber,
                        blockHandle);
  if (obj == nullptr)
    return false;
  auto* graph = static_cast<Dwg_Object_EVALUATION_GRAPH*>(std::calloc(1, sizeof(Dwg_Object_EVALUATION_GRAPH)));
  if (graph == nullptr)
    return false;
  obj->tio.object->tio.EVALUATION_GRAPH = graph;
  graph->parent = obj->tio.object;
  graph->major = kBeMajor;
  graph->minor = kBeMinor;
  graph->has_graph = 1;
  graph->first_nodeid = 1;
  graph->first_nodeid_copy = 1;
  graph->num_nodes = static_cast<BITCODE_BL>(nodes.size());
  graph->num_edges = 0;
  graph->edges = nullptr;
  graph->nodes = static_cast<Dwg_EVAL_Node*>(std::calloc(nodes.size(), sizeof(Dwg_EVAL_Node)));
  if (graph->nodes == nullptr) {
    graph->num_nodes = 0;
    return false;
  }
  for (size_t i = 0; i < nodes.size(); ++i) {
    Dwg_EVAL_Node& node = graph->nodes[i];
    node.parent = graph;
    node.id = static_cast<BITCODE_BL>(i);
    node.edge_flags = 32;
    node.nextid = (i + 1 < nodes.size()) ? static_cast<BITCODE_BLd>(static_cast<int>(i) + 1) : -1;
    node.evalexpr = dwg_add_handleref(dwg, 5, ObjectHandleValue(nodes[i]), obj);
    node.node[0] = -1;
    node.node[1] = -1;
    node.node[2] = -1;
    node.node[3] = -1;
    node.active_cycles = 0;
  }
  return true;
}

bool AppendPurgePreventer(Dwg_Data* dwg, std::uint64_t blockHandle) {
  const int classNumber =
      EnsureObjectClass(dwg, "ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION", "AcDbDynamicBlockPurgePreventer");
  if (classNumber < 0)
    return false;
  Dwg_Object* obj = AppendObjectShell(dwg, DWG_TYPE_DYNAMICBLOCKPURGEPREVENTER, "DYNAMICBLOCKPURGEPREVENTER",
                                      "ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION", classNumber, blockHandle);
  if (obj == nullptr)
    return false;
  auto* pp = static_cast<Dwg_Object_DYNAMICBLOCKPURGEPREVENTER*>(std::calloc(1, sizeof(Dwg_Object_DYNAMICBLOCKPURGEPREVENTER)));
  if (pp == nullptr)
    return false;
  obj->tio.object->tio.DYNAMICBLOCKPURGEPREVENTER = pp;
  pp->parent = obj->tio.object;
  pp->flag = 0;
  pp->block = dwg_add_handleref(dwg, 5, blockHandle, obj);
  return pp->block != nullptr;
}

bool ExportFlipChain(Dwg_Data* dwg, std::uint64_t blockHandle, const CadBlockDefinition& def) {
  const CadBlockParameter* param = nullptr;
  for (const CadBlockParameter& p : def.parameters) {
    if (p.kind == CadBlockParamKind::Flip) {
      param = &p;
      break;
    }
  }
  if (param == nullptr)
    return false;
  const CadBlockAction* action = FindFirstAction(def, CadBlockActionKind::Flip, param->name);
  if (action == nullptr)
    return false;
  std::vector<Dwg_Object*> nodes;
  if (Dwg_Object* o = AppendFlipParameter(dwg, blockHandle, *param, *action, 1))
    nodes.push_back(o);
  if (Dwg_Object* o = AppendFlipAction(dwg, blockHandle, *param, *action, 2))
    nodes.push_back(o);
  if (Dwg_Object* o = AppendFlipGrip(dwg, blockHandle, *action, 3))
    nodes.push_back(o);
  if (nodes.size() < 3)
    return false;
  if (!AppendEvaluationGraph(dwg, blockHandle, nodes))
    return false;
  return AppendPurgePreventer(dwg, blockHandle);
}

[[nodiscard]] float LinearExportDistance(const CadBlockDefinition& def, const CadBlockParameter& param,
                                         const std::vector<CadBlockRef>* refs) {
  if (refs != nullptr) {
    std::optional<float> unified;
    for (const CadBlockRef& r : *refs) {
      if (!CadBlockEqCi(r.defName, def.name))
        continue;
      const float v = CadBlockParamValue(r, def, param.name);
      if (!unified.has_value())
        unified = v;
      else if (*unified != v)
        return param.value;
    }
    if (unified.has_value())
      return *unified;
  }
  return param.value;
}

CadBlockParameter LinearParamForExport(const CadBlockParameter& param, float distance) {
  CadBlockParameter out = param;
  out.value = distance;
  return out;
}

bool ExportLinearStretchChain(Dwg_Data* dwg, std::uint64_t blockHandle, const CadBlockDefinition& def,
                              const std::vector<CadBlockRef>* refs) {
  const CadBlockParameter* param = nullptr;
  for (const CadBlockParameter& p : def.parameters) {
    if (p.kind == CadBlockParamKind::Linear) {
      param = &p;
      break;
    }
  }
  if (param == nullptr)
    return false;
  const CadBlockAction* action = FindFirstAction(def, CadBlockActionKind::Stretch, param->name);
  if (action == nullptr)
    return false;
  const CadBlockParameter exportParam =
      LinearParamForExport(*param, LinearExportDistance(def, *param, refs));
  std::vector<Dwg_Object*> nodes;
  if (Dwg_Object* o = AppendLinearParameter(dwg, blockHandle, exportParam, *action, 1))
    nodes.push_back(o);
  if (Dwg_Object* o = AppendStretchAction(dwg, blockHandle, *param, *action, 2))
    nodes.push_back(o);
  if (Dwg_Object* o = AppendLinearGrip(dwg, blockHandle, *action, 3))
    nodes.push_back(o);
  if (nodes.size() < 3)
    return false;
  if (!AppendEvaluationGraph(dwg, blockHandle, nodes))
    return false;
  return AppendPurgePreventer(dwg, blockHandle);
}

}  // namespace

bool CadBlockDefinitionNeedsDynamicDwgExport(const CadBlockDefinition& def) {
  if (def.dynamicAnonymous || def.name.empty())
    return false;
  if (def.parameters.empty() && def.visibilityStates.empty())
    return false;
  return true;
}

bool WriteGoSurveyDynamicBlockObjects(Dwg_Data* dwg, Dwg_Object_BLOCK_HEADER* blockHdr, const CadBlockDefinition& def,
                                      const std::vector<CadBlockRef>* refsForParamValues,
                                      std::vector<std::string>& log) {
  if (dwg == nullptr || blockHdr == nullptr || !CadBlockDefinitionNeedsDynamicDwgExport(def))
    return false;
  const std::uint64_t blockHandle = BlockHeaderHandle(blockHdr);
  if (blockHandle == 0)
    return false;

  bool wrote = false;
  if (ExportLinearStretchChain(dwg, blockHandle, def, refsForParamValues)) {
    log.push_back("CAD export — wrote dynamic-block linear/stretch objects for \"" + def.name + "\" (REQ-369).");
    wrote = true;
  } else if (ExportFlipChain(dwg, blockHandle, def)) {
    log.push_back("CAD export — wrote dynamic-block flip objects for \"" + def.name + "\" (REQ-369).");
    wrote = true;
  } else if (!def.parameters.empty()) {
    log.push_back("CAD export — block \"" + def.name +
                  "\" has parameters/actions with no supported R2004+ dynamic DWG encoder yet (REQ-369).");
  }
  if (wrote)
    blockHdr->block_scaling = 1;
  return wrote;
}

namespace {

std::string ImportTvToString(const Dwg_Data* dwg, BITCODE_T t) {
  (void)dwg;
  if (t == nullptr)
    return {};
  return std::string(t);
}

bool ObjectOwnedByBlockHandle(const Dwg_Object* obj, std::uint64_t blockHandle) {
  if (obj == nullptr || blockHandle == 0 || obj->supertype != DWG_SUPERTYPE_OBJECT || obj->tio.object == nullptr)
    return false;
  const Dwg_Object_Object* oo = obj->tio.object;
  if (oo->ownerhandle == nullptr)
    return false;
  return oo->ownerhandle->absolute_ref == blockHandle;
}

void ImportLinearParameter(const Dwg_Data* dwg, const Dwg_Object_BLOCKLINEARPARAMETER* lp, CadBlockDefinition& def) {
  if (lp == nullptr)
    return;
  CadBlockParameter p;
  p.kind = CadBlockParamKind::Linear;
  p.name = ImportTvToString(dwg, lp->name);
  if (p.name.empty())
    p.name = ImportTvToString(dwg, lp->distance_name);
  if (p.name.empty())
    return;
  p.value = static_cast<float>(lp->distance);
  p.minValue = static_cast<float>(lp->value_set.minimum);
  p.maxValue = static_cast<float>(lp->value_set.maximum);
  if (p.maxValue < p.minValue)
    p.maxValue = p.minValue + 1.e6f;
  def.parameters.push_back(std::move(p));
}

void ImportStretchAction(const Dwg_Data* dwg, const Dwg_Object_BLOCKSTRETCHACTION* act,
                         const CadBlockDefinition& def, CadBlockDefinition& outDef) {
  (void)dwg;
  if (act == nullptr || def.parameters.empty())
    return;
  std::string paramName = def.parameters.front().name;
  for (const CadBlockParameter& p : def.parameters) {
    if (p.kind == CadBlockParamKind::Linear) {
      paramName = p.name;
      break;
    }
  }
  CadBlockAction a;
  a.kind = CadBlockActionKind::Stretch;
  a.paramName = paramName;
  a.originX = static_cast<float>(act->display_location.x);
  a.originY = static_cast<float>(act->display_location.y);
  if (act->num_pts >= 2 && act->pts != nullptr) {
    const double dx = act->pts[1].x - act->pts[0].x;
    const double dy = act->pts[1].y - act->pts[0].y;
    const double len = std::hypot(dx, dy);
    if (len > 1.e-9) {
      a.dirX = static_cast<float>(dx / len);
      a.dirY = static_cast<float>(dy / len);
    }
  } else {
    a.dirX = 1.f;
    a.dirY = 0.f;
  }
  a.threshold = 0.f;
  outDef.actions.push_back(std::move(a));
}

void ImportFlipParameter(const Dwg_Data* dwg, const Dwg_Object_BLOCKFLIPPARAMETER* pp, CadBlockDefinition& def) {
  if (pp == nullptr)
    return;
  CadBlockParameter p;
  p.kind = CadBlockParamKind::Flip;
  p.name = ImportTvToString(dwg, pp->name);
  if (p.name.empty())
    p.name = ImportTvToString(dwg, pp->flip_label);
  if (p.name.empty())
    return;
  p.value = pp->evalexpr.value.num40 >= 0.5 ? 1.f : 0.f;
  def.parameters.push_back(std::move(p));
}

void ImportFlipAction(const Dwg_Data* dwg, const Dwg_Object_BLOCKFLIPACTION* act, const CadBlockDefinition& def,
                      CadBlockDefinition& outDef) {
  (void)dwg;
  if (act == nullptr)
    return;
  std::string paramName;
  for (const CadBlockParameter& p : def.parameters) {
    if (p.kind == CadBlockParamKind::Flip) {
      paramName = p.name;
      break;
    }
  }
  if (paramName.empty())
    return;
  CadBlockAction a;
  a.kind = CadBlockActionKind::Flip;
  a.paramName = paramName;
  a.originX = static_cast<float>(act->display_location.x);
  a.originY = static_cast<float>(act->display_location.y);
  a.dirX = 1.f;
  a.dirY = 0.f;
  outDef.actions.push_back(std::move(a));
}

}  // namespace

void ImportDynamicBlockDefinitionFromDwg(const Dwg_Data* dwg, const Dwg_Object* blockHeaderObj,
                                         CadBlockDefinition& def) {
  if (dwg == nullptr || blockHeaderObj == nullptr || def.dynamicAnonymous)
    return;
  if (!def.parameters.empty() && !def.actions.empty())
    return;
  const std::uint64_t blockHandle = ObjectHandleValue(blockHeaderObj);
  if (blockHandle == 0)
    return;

  CadBlockDefinition imported;
  imported.name = def.name;
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    const Dwg_Object* o = &dwg->object[i];
    if (!ObjectOwnedByBlockHandle(o, blockHandle))
      continue;
    if (o->fixedtype == DWG_TYPE_BLOCKLINEARPARAMETER && o->tio.object != nullptr &&
        o->tio.object->tio.BLOCKLINEARPARAMETER != nullptr)
      ImportLinearParameter(dwg, o->tio.object->tio.BLOCKLINEARPARAMETER, imported);
    else if (o->fixedtype == DWG_TYPE_BLOCKFLIPPARAMETER && o->tio.object != nullptr &&
             o->tio.object->tio.BLOCKFLIPPARAMETER != nullptr)
      ImportFlipParameter(dwg, o->tio.object->tio.BLOCKFLIPPARAMETER, imported);
  }
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    const Dwg_Object* o = &dwg->object[i];
    if (!ObjectOwnedByBlockHandle(o, blockHandle))
      continue;
    if (o->fixedtype == DWG_TYPE_BLOCKSTRETCHACTION && o->tio.object != nullptr &&
        o->tio.object->tio.BLOCKSTRETCHACTION != nullptr)
      ImportStretchAction(dwg, o->tio.object->tio.BLOCKSTRETCHACTION, imported, imported);
    else if (o->fixedtype == DWG_TYPE_BLOCKFLIPACTION && o->tio.object != nullptr &&
             o->tio.object->tio.BLOCKFLIPACTION != nullptr)
      ImportFlipAction(dwg, o->tio.object->tio.BLOCKFLIPACTION, imported, imported);
  }
  if (imported.parameters.empty())
    return;
  if (def.parameters.empty())
    def.parameters = std::move(imported.parameters);
  if (def.actions.empty())
    def.actions = std::move(imported.actions);
}

namespace {

bool ParamKindHasNoDwgEncoder(CadBlockParamKind kind) {
  switch (kind) {
    case CadBlockParamKind::Polar:
    case CadBlockParamKind::Rotation:
    case CadBlockParamKind::Move:
    case CadBlockParamKind::Lookup:
    case CadBlockParamKind::Visibility:
      return true;
    case CadBlockParamKind::Linear:
    case CadBlockParamKind::Flip:
      return false;
  }
  return true;
}

const CadBlockParameter* FirstLinearParameter(const CadBlockDefinition& def) {
  for (const CadBlockParameter& p : def.parameters) {
    if (p.kind == CadBlockParamKind::Linear)
      return &p;
  }
  return nullptr;
}

bool BlockWouldExportLinearStretch(const CadBlockDefinition& def) {
  const CadBlockParameter* param = FirstLinearParameter(def);
  if (param == nullptr)
    return false;
  return FindFirstAction(def, CadBlockActionKind::Stretch, param->name) != nullptr;
}

bool BlockWouldExportFlip(const CadBlockDefinition& def) {
  for (const CadBlockParameter& p : def.parameters) {
    if (p.kind != CadBlockParamKind::Flip)
      continue;
    if (FindFirstAction(def, CadBlockActionKind::Flip, p.name) != nullptr)
      return true;
  }
  return false;
}

bool InsertParamValuesConflict(const CadBlockDefinition& def, std::string_view paramName,
                               const std::vector<CadBlockRef>& refs) {
  std::optional<float> unified;
  for (const CadBlockRef& r : refs) {
    if (!CadBlockEqCi(r.defName, def.name))
      continue;
    const float v = CadBlockParamValue(r, def, paramName);
    if (!unified.has_value())
      unified = v;
    else if (*unified != v)
      return true;
  }
  return false;
}

size_t CountRefsForDef(const CadBlockDefinition& def, const std::vector<CadBlockRef>& refs) {
  size_t n = 0;
  for (const CadBlockRef& r : refs) {
    if (CadBlockEqCi(r.defName, def.name))
      ++n;
  }
  return n;
}

}  // namespace

CadBlockDynamicExportLossCounts ComputeCadBlockDynamicExportLossCounts(const AppCommandState& st) {
  CadBlockDynamicExportLossCounts out;
  for (const CadBlockDefinition& def : st.blockDefs) {
    if (!CadBlockDefinitionNeedsDynamicDwgExport(def))
      continue;
    bool hasVisibility = !def.visibilityStates.empty();
    if (!hasVisibility) {
      for (const CadBlockParameter& p : def.parameters) {
        if (p.kind == CadBlockParamKind::Visibility) {
          hasVisibility = true;
          break;
        }
      }
    }
    if (!hasVisibility) {
      for (const CadBlockAction& a : def.actions) {
        if (a.kind == CadBlockActionKind::Visibility) {
          hasVisibility = true;
          break;
        }
      }
    }
    if (hasVisibility)
      ++out.visibilityBlockDefs;

    size_t linearCount = 0;
    for (const CadBlockParameter& p : def.parameters) {
      if (ParamKindHasNoDwgEncoder(p.kind))
        ++out.unsupportedParameters;
      if (p.kind == CadBlockParamKind::Linear)
        ++linearCount;
    }
    if (linearCount > 1)
      out.extraLinearParameters += linearCount - 1;

    const bool linearStretch = BlockWouldExportLinearStretch(def);
    const bool flip = BlockWouldExportFlip(def);
    if (linearStretch && !flip)
      ++out.stretchWithoutEntityLinks;

    const CadBlockParameter* linear = FirstLinearParameter(def);
    if (linear != nullptr && linearStretch &&
        InsertParamValuesConflict(def, linear->name, st.cadBlockRefs))
      out.insertParamConflicts += CountRefsForDef(def, st.cadBlockRefs);
  }
  return out;
}
