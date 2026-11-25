#include "CadCommandsInternal.hpp"
#include "CadColor.hpp"
#include "CadLinetype.hpp"
#include <GoSurvey/Geometry/spline.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Object isolation (REQ-084 (d) / ADR-034)
// ---------------------------------------------------------------------------

namespace {

/// The attributes of a selected entity, for every type that carries them — solids and pipe runs
/// included. One accessor for both the const and mutable walks, so a read and an edit can never
/// disagree about which stores are covered (the same shape as \ref AttrsForKind).
template <typename StateT>
auto* AttrsOfSelected(StateT& st, const SelectedEntity& e) {
  using T = SelectedEntity::Type;
  using Attr = std::remove_reference_t<decltype(st.drawing.userLineAttrs[0])>;
  if (e.index < 0)
    return static_cast<Attr*>(nullptr);
  const size_t i = static_cast<size_t>(e.index);
  auto at = [i](auto& v) -> Attr* { return i < v.size() ? &v[i] : nullptr; };
  switch (e.type) {
  case T::LineSeg:      return at(st.drawing.userLineAttrs);
  case T::Circle:       return at(st.drawing.userCircleAttrs);
  case T::Polyline:     return at(st.drawing.userPolylineAttrs);
  case T::Arc:          return at(st.drawing.userArcAttrs);
  case T::Ellipse:      return at(st.drawing.userEllAttrs);
  case T::Annotation:   return at(st.drawing.cadAnnotationAttrs);
  case T::FilledRegion: return at(st.drawing.cadFilledRegionAttrs);
  case T::Mesh:         return at(st.drawing.cadMeshAttrs);
  case T::PointCloud:   return at(st.drawing.cadPointCloudAttrs);  // REQ-171
  case T::FeatureLine:  return at(st.drawing.featureLineAttrs);  // REQ-087 — layer, colour, and its stable id
  // REQ-068 / ADR-036 (a). A surface has carried an EntityAttributes since the store was written —
  // it simply had no SelectedEntity::Type to be reached through, and no EntityKind, so its `id` was
  // never assigned. Adding it here is what gives a surface REQ-084 isolation gating and Properties
  // layer/colour, both inherited rather than re-implemented.
  case T::Surface:      return at(st.drawing.cadSurfaceAttrs);
  case T::Table:        return at(st.drawing.cadTableAttrs);
  case T::PositionMarker: return at(st.drawing.cadPositionMarkerAttrs);  // REQ-359
  case T::Multileader:    return at(st.drawing.cadMultileaderAttrs);     // REQ-367
  case T::Spline:         return at(st.drawing.cadSplineAttrs);          // REQ-443
  case T::Point:          return at(st.drawing.cadPointAttrs);           // REQ-456
  case T::BlockRef:     return at(st.drawing.cadBlockRefAttrs);
  case T::Solid:        return at(st.drawing.cadSolidAttrs);    // REQ-352
  case T::PipeRun:      return at(st.drawing.cadPipeRunAttrs);  // REQ-352
  // Survey points, PDF underlays and the section plane carry no EntityAttributes.
  default:              return static_cast<Attr*>(nullptr);
  }
}

} // namespace

const EntityAttributes* CadEditableAttrsForSelected(const AppCommandState& st, const SelectedEntity& e) {
  return AttrsOfSelected(st, e);
}

const EntityAttributes* CadEntityAttrsForSelected(const AppCommandState& st, const SelectedEntity& e) {
  // Isolation (REQ-084) does not cover solids or pipe runs — CollectIsolatableIds never lists them,
  // and a pipe run has no id — so they stay out of this accessor rather than half-join isolation.
  if (e.type == SelectedEntity::Type::Solid || e.type == SelectedEntity::Type::PipeRun)
    return nullptr;
  return AttrsOfSelected(st, e);
}

namespace {

/// Writes one attribute field on every selected entity that carries attributes, as ONE undo step
/// taken only when something actually changes (REQ-352). Returns how many entities changed. With
/// \p lineStyleOnly, only the types \ref CadLineStyleApplies names are written and the rest are
/// counted into \p skipped (REQ-356).
template <typename V, typename FieldFn>
int ApplyAttrFieldToSelection(AppCommandState& st, const V& value, const char* undoLabel, FieldFn field,
                              bool lineStyleOnly = false, int* skipped = nullptr) {
  if (skipped)
    *skipped = 0;
  EnsureAttrCounts(st);
  bool pushed = false;
  int changed = 0;
  for (const SelectedEntity& e : st.selection) {
    EntityAttributes* a = AttrsOfSelected(st, e);
    if (lineStyleOnly && (!a || !CadLineStyleApplies(e.type))) {
      if (skipped)
        ++*skipped;
      continue;
    }
    if (!a || field(*a) == value)
      continue;
    if (!pushed) {
      PushUndoSnapshot(st, undoLabel);
      pushed = true;
    }
    field(*a) = value;
    ++changed;
  }
  if (changed > 0)
    BumpCadGpuCache(st);
  return changed;
}

} // namespace

int CadApplyLayerToSelection(AppCommandState& st, const std::string& layer) {
  if (layer.empty())
    return 0;
  const int n = ApplyAttrFieldToSelection(st, layer, "Change layer",
                                          [](EntityAttributes& a) -> std::string& { return a.layer; });
  if (n > 0)
    SyncDrawingLayerTableWithGeometry(st);  // a typed new name becomes a layer row
  return n;
}

int CadApplyColorToSelection(AppCommandState& st, const std::string& color) {
  if (color.empty())
    return 0;
  return ApplyAttrFieldToSelection(st, color, "Change color",
                                   [](EntityAttributes& a) -> std::string& { return a.color; });
}

std::string CadSelectionLayer(const AppCommandState& st) {
  std::string shared;
  for (const SelectedEntity& e : st.selection) {
    const EntityAttributes* a = AttrsOfSelected(st, e);
    if (!a)
      continue;
    const std::string layer = a->layer.empty() ? std::string("0") : a->layer;
    if (shared.empty())
      shared = layer;
    else if (shared != layer)
      return kCadSelectionLayerVaries;
  }
  return shared;
}

void CadRibbonPickLayer(AppCommandState& st, const std::string& layer, std::vector<std::string>& log) {
  if (layer.empty())
    return;
  if (CadSelectionLayer(st).empty()) {
    st.drawing.currentLayer = layer;
    SyncDrawingLayerTableWithGeometry(st);
    return;
  }
  const int n = CadApplyLayerToSelection(st, layer);
  log.push_back("LAYER — " + std::to_string(n) + " object(s) moved to layer \"" + layer + "\".");
}


// ---------------------------------------------------------------------------
// CHPROP / MATCHPROP / LAYMCUR and the current colour (REQ-356, GitHub issue #575)
// ---------------------------------------------------------------------------

bool CadLineStyleApplies(SelectedEntity::Type t) {
  using T = SelectedEntity::Type;
  switch (t) {
  case T::LineSeg:
  case T::Circle:
  case T::Arc:
  case T::Ellipse:
  case T::Spline:  // REQ-443
  case T::Point:   // REQ-456
  case T::Polyline:
  case T::Annotation:
  case T::Table:
    return true;
  default:
    return false;
  }
}

int CadApplyLinetypeToSelection(AppCommandState& st, const std::string& linetype, int* skipped) {
  if (linetype.empty()) {
    if (skipped)
      *skipped = 0;
    return 0;
  }
  return ApplyAttrFieldToSelection(
      st, linetype, "Change linetype", [](EntityAttributes& a) -> std::string& { return a.linetype; },
      true, skipped);
}

int CadApplyLineweightToSelection(AppCommandState& st, float mm, int* skipped) {
  const float stored = (mm < 0.f) ? -1.f : mm;
  return ApplyAttrFieldToSelection(
      st, stored, "Change lineweight", [](EntityAttributes& a) -> float& { return a.lineweightMm; }, true,
      skipped);
}

int CadApplyTransparencyToSelection(AppCommandState& st, float transparency, int* skipped) {
  const float stored = (transparency < -0.5f) ? -1.f : std::clamp(transparency, 0.f, 1.f);
  return ApplyAttrFieldToSelection(
      st, stored, "Change transparency", [](EntityAttributes& a) -> float& { return a.transparency; }, true,
      skipped);
}

std::string CadSelectionColor(const AppCommandState& st) {
  std::string shared;
  for (const SelectedEntity& e : st.selection) {
    const EntityAttributes* a = AttrsOfSelected(st, e);
    if (!a)
      continue;
    const std::string color = a->color.empty() ? std::string("ByLayer") : a->color;
    if (shared.empty())
      shared = color;
    else if (!CadColorStorageMatches(shared, color))
      return kCadSelectionColorVaries;
  }
  return shared;
}

void CadRibbonPickColor(AppCommandState& st, const std::string& color, std::vector<std::string>& log) {
  if (color.empty())
    return;
  if (CadSelectionColor(st).empty()) {
    st.drawing.currentColor = color;
    return;
  }
  const int n = CadApplyColorToSelection(st, color);
  log.push_back("COLOR — " + std::to_string(n) + " object(s) set to " + CadColorDisplayLabel(color) + ".");
}

bool IsPropCommandKind(AppCommandState::Kind k) {
  using K = AppCommandState::Kind;
  return k == K::ChProp || k == K::MatchProp || k == K::LayMCur;
}

void EndPropCommand(AppCommandState& st) {
  stepengine::EndSession(st);
  st.selBoxWaitingSecond = false;
  st.selection.clear();
  BumpCadGpuCache(st);  // the pick highlight goes with the selection
}

std::string CadPropCommandPromptText(const AppCommandState& st) {
  using P = stepengine::PropCmdPhase;
  using K = AppCommandState::Kind;
  const auto* sess = st.sessionAs<stepengine::PropCmdSession>();
  const P phase = sess ? sess->phase : P::SelectObjects;
  if (st.active == K::LayMCur)
    return "LAYMCUR — select an object whose layer will become current. ESC cancels.";
  if (st.active == K::MatchProp) {
    if (phase == P::SelectTargets)
      return "MATCHPROP — select destination object(s), Enter when done. ESC cancels.";
    return "MATCHPROP — select source object. ESC cancels.";
  }
  if (st.active != K::ChProp)
    return "";
  switch (phase) {
  case P::WaitProperty:
    return "CHPROP — property to change [Color/LAyer/LType/LWeight] (Enter ends):";
  case P::WaitValue:
    switch (sess ? sess->chPropProperty : stepengine::ChPropProperty::Color) {
    case stepengine::ChPropProperty::Color:
      return "CHPROP — new color [ByLayer/ByBlock/1-255/name/#RRGGBB]:";
    case stepengine::ChPropProperty::Layer:
      return "CHPROP — new layer name:";
    case stepengine::ChPropProperty::Linetype:
      return "CHPROP — new linetype [ByLayer/ByBlock/Continuous/DASHED/HIDDEN/CENTER/PHANTOM/DIVIDE/BORDER]:";
    case stepengine::ChPropProperty::Lineweight:
      return "CHPROP — new lineweight in mm, or ByLayer:";
    }
    return "";
  default: {
    std::string s = "CHPROP — select objects";
    if (!st.selection.empty())
      s += " (" + std::to_string(st.selection.size()) + " selected)";
    return s + ", Enter when done. ESC cancels.";
  }
  }
}

namespace {

/// Refuses a property command outside model space: REQ-352's edit reaches model-space selections
/// only, and a paper-space object is out of REQ-356's scope (item 7).
bool PropCommandModelSpaceOnly(AppCommandState& st, AppCommandState::Kind k, std::vector<std::string>& log) {
  if (st.activeSpaceIndex == kModelSpaceIndex)
    return true;
  log.push_back(std::string(AppCommandState::KindName(k)) + " — works in model space only.");
  return false;
}

void BeginPropCommand(AppCommandState& st, AppCommandState::Kind k) {
  if (IsPropCommandKind(st.active)) {
    st.activeSession.reset();
    st.active = AppCommandState::Kind::None;  // restarting keeps the held selection
  }
  ClearPendingViewportZoom(st);
  stepengine::BeginSession(st, std::make_unique<stepengine::PropCmdSession>(k), /*recordAsLastCommand=*/true);
}

/// LAYMCUR's whole action on the selection it has. Refuses (and clears the pick) a selection on
/// more than one layer; nothing carrying a layer is not an answer yet, so it keeps asking.
void LayMCurFromSelection(AppCommandState& st, std::vector<std::string>& log) {
  const std::string layer = CadSelectionLayer(st);
  if (layer.empty()) {
    st.selection.clear();
    BumpCadGpuCache(st);
    log.push_back("LAYMCUR — that object has no layer. " + CadPropCommandPromptText(st));
    return;
  }
  if (layer == kCadSelectionLayerVaries) {
    st.selection.clear();
    BumpCadGpuCache(st);
    log.push_back("LAYMCUR — the selected objects are on more than one layer; the current layer is "
                  "unchanged. Select one object.");
    return;
  }
  st.drawing.currentLayer = layer;
  SyncDrawingLayerTableWithGeometry(st);
  log.push_back("LAYMCUR — \"" + layer + "\" is now the current layer.");
  EndPropCommand(st);
}

/// MATCHPROP's source step on the selection it has: exactly one object carrying attributes.
void MatchPropTakeSource(AppCommandState& st, std::vector<std::string>& log) {
  auto* sess = st.sessionAs<stepengine::PropCmdSession>();
  if (!sess)
    return;
  const EntityAttributes* a = st.selection.size() == 1 ? AttrsOfSelected(st, st.selection.front()) : nullptr;
  if (a == nullptr) {
    log.push_back(st.selection.size() > 1 ? "MATCHPROP — select ONE source object."
                                          : "MATCHPROP — that object has no properties to match.");
    st.selection.clear();
    BumpCadGpuCache(st);
    return;
  }
  sess->matchPropSource = *a;
  sess->matchPropSourceHasLineStyle = CadLineStyleApplies(st.selection.front().type);
  st.selection.clear();
  BumpCadGpuCache(st);
  sess->phase = stepengine::PropCmdPhase::SelectTargets;
  log.push_back(std::string("MATCHPROP — matching Layer, Color") +
                (sess->matchPropSourceHasLineStyle ? ", Linetype, Lineweight" : "") + ".");
  log.push_back(CadPropCommandPromptText(st));
}

/// MATCHPROP's destination step: the source's layer and colour onto every picked object, and its
/// linetype / lineweight where both carry one — one undo step per pick (REQ-356 item 2).
void MatchPropApplyToSelection(AppCommandState& st, std::vector<std::string>& log) {
  auto* sess = st.sessionAs<stepengine::PropCmdSession>();
  if (!sess)
    return;
  EnsureAttrCounts(st);
  const EntityAttributes& src = sess->matchPropSource;
  bool pushed = false;
  int changed = 0;
  for (const SelectedEntity& e : st.selection) {
    EntityAttributes* a = AttrsOfSelected(st, e);
    if (!a)
      continue;
    EntityAttributes next = *a;
    next.layer = src.layer;
    next.color = src.color;
    if (sess->matchPropSourceHasLineStyle && CadLineStyleApplies(e.type)) {
      next.linetype = src.linetype;
      next.lineweightMm = src.lineweightMm;
    }
    if (next.layer == a->layer && next.color == a->color && next.linetype == a->linetype &&
        next.lineweightMm == a->lineweightMm)
      continue;
    if (!pushed) {
      PushUndoSnapshot(st, "Match properties");
      pushed = true;
    }
    *a = next;
    ++changed;
  }
  st.selection.clear();
  if (changed > 0)
    SyncDrawingLayerTableWithGeometry(st);
  BumpCadGpuCache(st);
  log.push_back("MATCHPROP — " + std::to_string(changed) + " object(s) changed.");
}

/// CHPROP's value step: parse, apply (one undo step), report. False when the value is refused.
bool ChPropApplyValue(AppCommandState& st, const std::string& value, std::vector<std::string>& log) {
  const auto* sess = st.sessionAs<stepengine::PropCmdSession>();
  if (!sess)
    return false;
  using PP = stepengine::ChPropProperty;
  int changed = 0;
  int skipped = 0;
  std::string what;
  switch (sess->chPropProperty) {
  case PP::Color: {
    std::string storage;
    if (!CadColorStorageFromTyped(value, &storage)) {
      log.push_back("CHPROP — \"" + value + "\" is not a color (ByLayer, ByBlock, 1-255, a color name or #RRGGBB).");
      return false;
    }
    changed = CadApplyColorToSelection(st, storage);
    what = "color " + CadColorDisplayLabel(storage);
    break;
  }
  case PP::Layer: {
    const CadLayerRow* row = FindDrawingLayerRowCi(st, value);
    if (row == nullptr) {
      log.push_back("CHPROP — layer \"" + value + "\" does not exist; nothing changed.");
      return false;
    }
    const std::string name = row->name;  // copied: the edit may resync the table
    changed = CadApplyLayerToSelection(st, name);
    what = "layer \"" + name + "\"";
    break;
  }
  case PP::Linetype: {
    const char* match = nullptr;
    for (const char* lt : kEntityLinetypeStorage)
      if (CadLinetypeNameEqCi(value, lt))
        match = lt;
    if (match == nullptr) {
      log.push_back("CHPROP — linetype \"" + value + "\" is not one GoSurvey offers; nothing changed.");
      return false;
    }
    changed = CadApplyLinetypeToSelection(st, match, &skipped);
    what = std::string("linetype ") + match;
    break;
  }
  case PP::Lineweight: {
    float mm = 0.f;
    bool ok = false;
    if (StringUtil::toLowerAsciiCopy(value) == "bylayer") {
      mm = -1.f;
      ok = true;
    } else {
      char* end = nullptr;
      const float v = std::strtof(value.c_str(), &end);
      if (end != value.c_str() && *end == '\0' && v >= 0.f) {
        for (float preset : kEntityLineweightMmPresets)
          if (preset >= 0.f && std::fabs(preset - v) < 1e-4f) {
            mm = preset;
            ok = true;
          }
      }
    }
    if (!ok) {
      log.push_back("CHPROP — \"" + value + "\" is not a lineweight (ByLayer, or 0 to 2.11 mm from the standard list).");
      return false;
    }
    changed = CadApplyLineweightToSelection(st, mm, &skipped);
    char buf[32];
    std::snprintf(buf, sizeof(buf), mm < 0.f ? "ByLayer" : "%.2f mm", mm);
    what = std::string("lineweight ") + buf;
    break;
  }
  }
  std::string msg = "CHPROP — " + what + " set on " + std::to_string(changed) + " object(s)";
  if (skipped > 0)
    msg += "; " + std::to_string(skipped) + " skipped (linetype and lineweight do not apply to them)";
  log.push_back(msg + ".");
  return true;
}

/// The CHPROP property keyword: the full word or AutoCAD's capitalised abbreviation.
bool ChPropParseProperty(const std::string& word, stepengine::ChPropProperty* out) {
  using PP = stepengine::ChPropProperty;
  const std::string w = StringUtil::toLowerAsciiCopy(word);
  if (w == "c" || w == "color" || w == "colour")
    *out = PP::Color;
  else if (w == "la" || w == "layer")
    *out = PP::Layer;
  else if (w == "lt" || w == "ltype" || w == "linetype")
    *out = PP::Linetype;
  else if (w == "lw" || w == "lweight" || w == "lineweight")
    *out = PP::Lineweight;
  else
    return false;
  return true;
}

} // namespace

void StartChPropCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (!PropCommandModelSpaceOnly(st, AppCommandState::Kind::ChProp, log))
    return;
  BeginPropCommand(st, AppCommandState::Kind::ChProp);
  if (!st.selection.empty()) {
    if (auto* sess = st.sessionAs<stepengine::PropCmdSession>())
      sess->phase = stepengine::PropCmdPhase::WaitProperty;  // a held selection is the pick
  }
  log.push_back(CadPropCommandPromptText(st));
}

void StartMatchPropCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (!PropCommandModelSpaceOnly(st, AppCommandState::Kind::MatchProp, log))
    return;
  BeginPropCommand(st, AppCommandState::Kind::MatchProp);
  if (st.selection.size() == 1 && AttrsOfSelected(st, st.selection.front()) != nullptr) {
    MatchPropTakeSource(st, log);  // a held single object is the source
    return;
  }
  st.selection.clear();  // several held objects cannot be one source
  BumpCadGpuCache(st);
  log.push_back(CadPropCommandPromptText(st));
}

void StartLayMCurCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (!PropCommandModelSpaceOnly(st, AppCommandState::Kind::LayMCur, log))
    return;
  BeginPropCommand(st, AppCommandState::Kind::LayMCur);
  if (!st.selection.empty()) {
    LayMCurFromSelection(st, log);  // a held selection is used at once
    return;
  }
  log.push_back(CadPropCommandPromptText(st));
}

void CadPropCommandSelectionChanged(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using P = stepengine::PropCmdPhase;
  auto* sess = st.sessionAs<stepengine::PropCmdSession>();
  if (st.selection.empty() || !sess)
    return;
  if (st.active == K::LayMCur)
    LayMCurFromSelection(st, log);
  else if (st.active == K::MatchProp && sess->phase == P::SelectObjects)
    MatchPropTakeSource(st, log);
  else if (st.active == K::MatchProp && sess->phase == P::SelectTargets)
    MatchPropApplyToSelection(st, log);
  // CHPROP accumulates until Enter, as MOVE / COPY do (REQ-121).
}

bool HandlePropCommandTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using P = stepengine::PropCmdPhase;
  auto* sess = st.sessionAs<stepengine::PropCmdSession>();
  if (!sess)
    return false;
  const std::string line = StringUtil::trimCopy(lineIn);
  const std::string verb = AppCommandState::KindName(st.active);

  if (st.active == K::LayMCur || (st.active == K::MatchProp && sess->phase == P::SelectObjects)) {
    if (line.empty()) {
      log.push_back(verb + " — nothing picked; ended.");
      EndPropCommand(st);
    } else {
      log.push_back(CadPropCommandPromptText(st));
    }
    return true;
  }
  if (st.active == K::MatchProp) {  // SelectTargets
    if (line.empty()) {
      log.push_back("MATCHPROP — done.");
      EndPropCommand(st);
    } else {
      log.push_back(CadPropCommandPromptText(st));
    }
    return true;
  }

  // CHPROP
  switch (sess->phase) {
  case P::SelectObjects:
    if (!line.empty()) {
      log.push_back(CadPropCommandPromptText(st));
    } else if (st.selection.empty()) {
      log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
    } else {
      sess->phase = P::WaitProperty;
      log.push_back(CadPropCommandPromptText(st));
    }
    return true;
  case P::WaitProperty: {
    if (line.empty()) {
      log.push_back("CHPROP — done.");
      EndPropCommand(st);
      return true;
    }
    std::istringstream is(line);
    std::string word;
    is >> word;
    if (!ChPropParseProperty(word, &sess->chPropProperty)) {
      log.push_back("CHPROP — \"" + word + "\" is not a property. " + CadPropCommandPromptText(st));
      return true;
    }
    std::string value;
    std::getline(is, value);
    value = StringUtil::trimCopy(value);
    if (value.empty()) {
      sess->phase = P::WaitValue;
      log.push_back(CadPropCommandPromptText(st));
      return true;
    }
    (void)ChPropApplyValue(st, value, log);  // `COLOR red` on one line
    log.push_back(CadPropCommandPromptText(st));
    return true;
  }
  case P::WaitValue:
    if (!line.empty() && !ChPropApplyValue(st, line, log)) {
      log.push_back(CadPropCommandPromptText(st));  // refused: ask again for the same property
      return true;
    }
    sess->phase = P::WaitProperty;  // applied, or Enter = keep and ask for another property
    log.push_back(CadPropCommandPromptText(st));
    return true;
  case P::SelectTargets:
    break;
  }
  return true;
}

bool CadSelectedEntityHidden(const AppCommandState& st, const SelectedEntity& e) {
  if (st.hiddenEntityIds.empty())
    return false;
  const EntityAttributes* a = CadEntityAttrsForSelected(st, e);
  return a && CadEntityIdHidden(&st.hiddenEntityIds, a->id);
}

namespace {

/// Ids of the entity types isolation covers, in one place so "everything" and "the selection"
/// can never disagree about what the set of isolatable objects is.
void CollectIsolatableIds(const AppCommandState& st, std::vector<std::uint64_t>* out) {
  auto take = [out](const std::vector<EntityAttributes>& v) {
    for (const EntityAttributes& a : v)
      if (a.id != 0)
        out->push_back(a.id);
  };
  take(st.drawing.userLineAttrs);
  take(st.drawing.userCircleAttrs);
  take(st.drawing.userPolylineAttrs);
  take(st.drawing.userArcAttrs);
  take(st.drawing.userEllAttrs);
  take(st.drawing.cadAnnotationAttrs);
  take(st.drawing.cadFilledRegionAttrs);
  take(st.drawing.cadMeshAttrs);
  take(st.drawing.cadPointCloudAttrs);
  take(st.drawing.cadSurfaceAttrs);
  take(st.drawing.cadTableAttrs);
  take(st.drawing.cadBlockRefAttrs);
}

/// Ids of the current selection that isolation can act on, plus how many picks it had to skip
/// (survey points / PDF underlays), so the caller can say so rather than silently dropping them.
void CollectSelectedIsolatableIds(const AppCommandState& st, std::vector<std::uint64_t>* out, int* outSkipped) {
  *outSkipped = 0;
  for (const SelectedEntity& e : st.selection) {
    const EntityAttributes* a = CadEntityAttrsForSelected(st, e);
    if (a && a->id != 0)
      out->push_back(a->id);
    else
      ++*outSkipped;
  }
  *outSkipped += static_cast<int>(st.selectedSurveyPointIndices.size());
}

void SortUniqueIds(std::vector<std::uint64_t>* v) {
  std::sort(v->begin(), v->end());
  v->erase(std::unique(v->begin(), v->end()), v->end());
}

/// Isolation hides objects, so leaving them selected would keep grips and the modify commands
/// pointed at geometry the user can no longer see.
void DropHiddenFromSelection(AppCommandState& st) {
  auto& sel = st.selection;
  sel.erase(std::remove_if(sel.begin(), sel.end(),
                           [&](const SelectedEntity& e) { return CadSelectedEntityHidden(st, e); }),
            sel.end());
}

void ReportSkippedPicks(int skipped, std::vector<std::string>& log) {
  if (skipped > 0)
    log.push_back("  (" + std::to_string(skipped) +
                  " selected item(s) skipped — survey points and PDF underlays are not isolatable.)");
}

} // namespace

void IsolateSelectedObjects(AppCommandState& st, std::vector<std::string>& log) {
  EnsureEntityIds(st);  // an entity created this frame has id 0 until the sweep runs
  std::vector<std::uint64_t> keep;
  int skipped = 0;
  CollectSelectedIsolatableIds(st, &keep, &skipped);
  if (keep.empty()) {
    log.push_back("ISOLATEOBJECTS — nothing isolatable is selected; nothing hidden.");
    ReportSkippedPicks(skipped, log);
    return;
  }
  SortUniqueIds(&keep);

  std::vector<std::uint64_t> all;
  CollectIsolatableIds(st, &all);

  st.hiddenEntityIds = CadIsolationHiddenSet(std::move(all), keep);  // sorted, as the gates require
  BumpCadGpuCache(st);
  log.push_back("ISOLATEOBJECTS — " + std::to_string(keep.size()) + " object(s) isolated, " +
                std::to_string(st.hiddenEntityIds.size()) + " hidden.");
  ReportSkippedPicks(skipped, log);
}

void HideSelectedObjects(AppCommandState& st, std::vector<std::string>& log) {
  EnsureEntityIds(st);
  std::vector<std::uint64_t> hide;
  int skipped = 0;
  CollectSelectedIsolatableIds(st, &hide, &skipped);
  if (hide.empty()) {
    log.push_back("HIDEOBJECTS — nothing isolatable is selected; nothing hidden.");
    ReportSkippedPicks(skipped, log);
    return;
  }
  const size_t before = st.hiddenEntityIds.size();
  st.hiddenEntityIds.insert(st.hiddenEntityIds.end(), hide.begin(), hide.end());
  SortUniqueIds(&st.hiddenEntityIds);
  DropHiddenFromSelection(st);
  BumpCadGpuCache(st);
  log.push_back("HIDEOBJECTS — " + std::to_string(st.hiddenEntityIds.size() - before) +
                " object(s) hidden (" + std::to_string(st.hiddenEntityIds.size()) + " hidden in total).");
  ReportSkippedPicks(skipped, log);
}

void EndObjectIsolation(AppCommandState& st, std::vector<std::string>& log) {
  if (st.hiddenEntityIds.empty()) {
    log.push_back("UNISOLATEOBJECTS — nothing was hidden.");
    return;
  }
  const size_t n = st.hiddenEntityIds.size();
  st.hiddenEntityIds.clear();
  BumpCadGpuCache(st);
  log.push_back("UNISOLATEOBJECTS — " + std::to_string(n) + " object(s) shown.");
}


// ---------------------------------------------------------------------------
// QUICKSELECT (issue 05)
// ---------------------------------------------------------------------------
//
// This lived in `src/ui/CadUi.cpp` as a `static` function — 214 lines of pure selection filtering
// on the wrong side of the link boundary. architecture.md says what that costs: the UI layer is
// "what headless does not link", so the whole matrix (2 scopes x 12 object types x 11 properties x
// 5 operators x include/exclude/append) was unreachable from a transcript, and `static` put it out
// of reach of a Catch2 test as well. It was the only Execute* function outside the command layer,
// 1 of 12 — the name followed the convention, the location did not.
//
// The move is a relocation, not a redesign: every input it reads already lives in
// AppCommandState (qsApplyTo / qsObjectType / qsProperty / qsOperator / qsValueBuf / qsIncludeMode
// / qsAppendToExisting) and it touches no UI at all. The panel stays exactly what the driver's
// own IMPORT POINTS comment praises — a form, not logic — and calls this unchanged.
void ExecuteQuickSelect(AppCommandState& cmd, std::vector<std::string>& log) {
  using OT = AppCommandState::QsObjectType;
  using QP = AppCommandState::QsProperty;
  using QO = AppCommandState::QsOperator;
  using QI = AppCommandState::QsInclude;
  using T  = SelectedEntity::Type;

  float numVal = 0.f;
  try { numVal = std::stof(cmd.qsValueBuf); } catch (...) { // NOLINT(bugprone-empty-catch) - non-numeric input; numVal stays 0, string comparison below still applies
  }
  const std::string strVal = cmd.qsValueBuf;

  auto matchStr = [&](const std::string& prop) -> bool {
    switch (cmd.qsOperator) {
    case QO::SelectAll:  return true;
    case QO::Equals:     return prop == strVal;
    case QO::NotEquals:  return prop != strVal;
    default:             return false;
    }
  };
  auto matchNum = [&](float prop) -> bool {
    switch (cmd.qsOperator) {
    case QO::SelectAll:     return true;
    case QO::Equals:        return std::fabs(prop - numVal) < 1e-5f;
    case QO::NotEquals:     return std::fabs(prop - numVal) >= 1e-5f;
    case QO::LessThan:      return prop < numVal;
    case QO::GreaterThan:   return prop > numVal;
    }
    return false;
  };
  auto typeMatches = [&](OT t) -> bool {
    return cmd.qsObjectType == OT::All || cmd.qsObjectType == t;
  };
  auto getAttrs = [&](const SelectedEntity& e) -> const EntityAttributes* {
    switch (e.type) {
    case T::LineSeg:    return (size_t)e.index < cmd.drawing.userLineAttrs.size()       ? &cmd.drawing.userLineAttrs[(size_t)e.index]       : nullptr;
    case T::Circle:     return (size_t)e.index < cmd.drawing.userCircleAttrs.size()     ? &cmd.drawing.userCircleAttrs[(size_t)e.index]     : nullptr;
    case T::Arc:        return (size_t)e.index < cmd.drawing.userArcAttrs.size()        ? &cmd.drawing.userArcAttrs[(size_t)e.index]        : nullptr;
    case T::Ellipse:    return (size_t)e.index < cmd.drawing.userEllAttrs.size()        ? &cmd.drawing.userEllAttrs[(size_t)e.index]        : nullptr;
    case T::Spline:     return e.index >= 0 && (size_t)e.index < cmd.drawing.cadSplineAttrs.size() ? &cmd.drawing.cadSplineAttrs[(size_t)e.index] : nullptr;
    case T::Point:      return e.index >= 0 && (size_t)e.index < cmd.drawing.cadPointAttrs.size() ? &cmd.drawing.cadPointAttrs[(size_t)e.index] : nullptr;
    case T::Polyline:   return (size_t)e.index < cmd.drawing.userPolylineAttrs.size()   ? &cmd.drawing.userPolylineAttrs[(size_t)e.index]   : nullptr;
    case T::Annotation: return (size_t)e.index < cmd.drawing.cadAnnotationAttrs.size()  ? &cmd.drawing.cadAnnotationAttrs[(size_t)e.index]  : nullptr;
    case T::Table:      return (size_t)e.index < cmd.drawing.cadTableAttrs.size()       ? &cmd.drawing.cadTableAttrs[(size_t)e.index]       : nullptr;
    case T::BlockRef:   return (size_t)e.index < cmd.drawing.cadBlockRefAttrs.size()    ? &cmd.drawing.cadBlockRefAttrs[(size_t)e.index]    : nullptr;
    default:            return nullptr;
    }
  };

  auto testEntity = [&](const SelectedEntity& e) -> bool {
    // Type gate
    switch (e.type) {
    case T::LineSeg:  if (!typeMatches(OT::Line))    return false; break;
    case T::Circle:   if (!typeMatches(OT::Circle))  return false; break;
    case T::Arc:      if (!typeMatches(OT::Arc))     return false; break;
    case T::Ellipse:  if (!typeMatches(OT::Ellipse)) return false; break;
    case T::Polyline: if (!typeMatches(OT::Polyline))return false; break;
    case T::Spline:
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.drawing.cadSplines.size() || cmd.qsObjectType != OT::All)
        return false;
      break;
    case T::Point:
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.drawing.cadPoints.size() || cmd.qsObjectType != OT::All)
        return false;
      break;
    case T::Annotation: {
      if ((size_t)e.index >= cmd.drawing.cadAnnotations.size()) return false;
      const auto k = cmd.drawing.cadAnnotations[(size_t)e.index].kind;
      using AK = CadAnnotation::Kind;
      if (cmd.qsObjectType == OT::Text       && k != AK::Text)       return false;
      if (cmd.qsObjectType == OT::Mtext      && k != AK::Mtext)      return false;
      if (cmd.qsObjectType == OT::DimAligned && k != AK::DimAligned) return false;
      if (cmd.qsObjectType == OT::DimLinear  && k != AK::DimLinear)  return false;
      if (cmd.qsObjectType == OT::DimAngular && k != AK::DimAngular) return false;
      if (cmd.qsObjectType != OT::All && cmd.qsObjectType != OT::Text &&
          cmd.qsObjectType != OT::Mtext && cmd.qsObjectType != OT::DimAligned &&
          cmd.qsObjectType != OT::DimLinear && cmd.qsObjectType != OT::DimAngular)
        return false;
      break;
    }
    case T::Table:
      if (cmd.qsObjectType != OT::All)
        return false;
      break;
    case T::BlockRef:
      if (cmd.qsObjectType != OT::All)
        return false;
      break;
    default: return false;
    }
    // Property test
    const EntityAttributes* attrs = getAttrs(e);
    switch (cmd.qsProperty) {
    case QP::Layer:   return attrs ? matchStr(attrs->layer) : (cmd.qsOperator == QO::SelectAll);
    case QP::Color: {
      if (!attrs) return cmd.qsOperator == QO::SelectAll;
      std::string layerColor;
      const CadLayerRow* row = FindDrawingLayerRowCi(cmd, attrs->layer);
      if (row && !row->color.empty())
        layerColor = row->color;
      const std::string effective =
          CadColorEffectiveStorage(attrs->color, layerColor);
      if (cmd.qsOperator == QO::SelectAll)
        return true;
      if (cmd.qsOperator == QO::Equals)
        return CadColorStorageMatches(effective, strVal);
      if (cmd.qsOperator == QO::NotEquals)
        return !CadColorStorageMatches(effective, strVal);
      return false;
    }
    case QP::Length: {
      double len = 0.0;
      if (e.type == T::LineSeg) {
        const size_t k = (size_t)e.index;
        if (k < cmd.drawing.lineCount())
          len = std::hypot(cmd.drawing.line(k).x1 - cmd.drawing.line(k).x0,
                           cmd.drawing.line(k).y1 - cmd.drawing.line(k).y0);
      } else if (e.type == T::Polyline) {
        const int np = (int)cmd.drawing.userPolylineOffsets.size();
        if (e.index >= 0 && e.index + 1 < np) {
          const int sv = cmd.drawing.userPolylineOffsets[(size_t)e.index];
          const int ev = cmd.drawing.userPolylineOffsets[(size_t)e.index + 1];
          for (int vi = sv; vi + 1 < ev; ++vi) {
            const size_t nextVi = (size_t)vi + 1;
            if (nextVi < cmd.drawing.vertexCount())
              len += std::hypot(cmd.drawing.vertex(nextVi).x - cmd.drawing.vertex(vi).x,
                                cmd.drawing.vertex(nextVi).y - cmd.drawing.vertex(vi).y);
          }
        }
      } else if (e.type == T::Spline && e.index >= 0 && static_cast<size_t>(e.index) < cmd.drawing.cadSplines.size()) {
        len = spline::Length(cmd.drawing.cadSplines[static_cast<size_t>(e.index)]);
      }
      return matchNum(static_cast<float>(len));
    }
    case QP::Radius: {
      double r = 0.0;
      if (e.type == T::Spline) return false;
      if (e.type == T::Circle) {
        if ((size_t)e.index < cmd.drawing.circleCount()) r = cmd.drawing.circle((size_t)e.index).r;
      } else if (e.type == T::Arc && (size_t)e.index < cmd.drawing.userArcs.size()) {
        r = cmd.drawing.userArcs[(size_t)e.index].r;
      }
      return matchNum(static_cast<float>(r));
    }
    case QP::Closed:
      if (e.type == T::Polyline && (size_t)e.index < cmd.drawing.userPolylineClosed.size()) {
        const bool closed = cmd.drawing.userPolylineClosed[(size_t)e.index] != 0;
        if (cmd.qsOperator == QO::SelectAll) return true;
        const bool want = (strVal == "Yes" || strVal == "yes" || strVal == "1" || strVal == "true");
        return (cmd.qsOperator == QO::Equals) ? (closed == want) : (closed != want);
      }
      if (e.type == T::Spline && e.index >= 0 && static_cast<size_t>(e.index) < cmd.drawing.cadSplines.size()) {
        const bool closed = cmd.drawing.cadSplines[static_cast<size_t>(e.index)].periodic;
        if (cmd.qsOperator == QO::SelectAll) return true;
        const bool want = (strVal == "Yes" || strVal == "yes" || strVal == "1" || strVal == "true");
        return (cmd.qsOperator == QO::Equals) ? (closed == want) : (closed != want);
      }
      return false;
    case QP::Content:
      if (e.type == T::Annotation && (size_t)e.index < cmd.drawing.cadAnnotations.size())
        return matchStr(cmd.drawing.cadAnnotations[(size_t)e.index].text);
      if (e.type == T::Table && (size_t)e.index < cmd.drawing.cadTables.size()) {
        std::string joined;
        for (const std::string& c : cmd.drawing.cadTables[(size_t)e.index].cells) {
          if (!joined.empty())
            joined += " ";
          joined += c;
        }
        return matchStr(joined);
      }
      return false;
    default: return cmd.qsOperator == QO::SelectAll;
    }
  };

  auto testSurvey = [&](int spi) -> bool {
    if (!typeMatches(OT::SurveyPoint)) return false;
    if ((size_t)spi >= cmd.drawing.surveyPoints.size()) return false;
    const SurveyPoint& sp = cmd.drawing.surveyPoints[(size_t)spi];
    switch (cmd.qsProperty) {
    case QP::Layer:       return matchStr(sp.layer);
    case QP::Id:          return matchNum(static_cast<float>(sp.id));
    // ADR-054 Phase F made these `double` on SurveyPoint; `matchNum` compares against the float
    // parsed from the QUICKSELECT dialog's text box, so the narrowing is at the comparison and is
    // upstream's own (#447/#461), carried across the relocation rather than reverted by it.
    case QP::Elevation:   return matchNum(static_cast<float>(sp.elevation));
    case QP::Easting:     return matchNum(static_cast<float>(sp.easting));
    case QP::Northing:    return matchNum(static_cast<float>(sp.northing));
    case QP::Description: return matchStr(sp.description);
    default:              return cmd.qsOperator == QO::SelectAll;
    }
  };

  const bool exclude = (cmd.qsIncludeMode == QI::Exclude);
  std::vector<SelectedEntity> newCad;
  std::vector<int> newSurvey;

  auto addCad = [&](const SelectedEntity& e) {
    if (testEntity(e) != exclude) newCad.push_back(e);
  };
  auto addSurvey = [&](int spi) {
    if (testSurvey(spi) != exclude) newSurvey.push_back(spi);
  };

  if (cmd.qsApplyTo == AppCommandState::QsApplyTo::EntireDrawing) {
    const int nLines = (int)(cmd.drawing.lineCount());
    for (int i = 0; i < nLines; ++i)  addCad({SelectedEntity::Type::LineSeg, i});
    const int nCirc = (int)cmd.drawing.circleCount();
    for (int i = 0; i < nCirc; ++i)   addCad({SelectedEntity::Type::Circle, i});
    for (int i = 0; i < (int)cmd.drawing.userArcs.size(); ++i)      addCad({SelectedEntity::Type::Arc, i});
    for (int i = 0; i < (int)cmd.drawing.userEllipses.size(); ++i)  addCad({SelectedEntity::Type::Ellipse, i});
    for (int i = 0; i < (int)cmd.drawing.cadSplines.size(); ++i)    addCad({SelectedEntity::Type::Spline, i});
    for (int i = 0; i < (int)cmd.drawing.cadPoints.size(); ++i)     addCad({SelectedEntity::Type::Point, i});
    for (int i = 0; i < (int)cmd.drawing.alignments.size(); ++i)    addCad({SelectedEntity::Type::Alignment, i});  // REQ-480
    const int nPoly = std::max(0, (int)cmd.drawing.userPolylineOffsets.size() - 1);
    for (int i = 0; i < nPoly; ++i)   addCad({SelectedEntity::Type::Polyline, i});
    for (int i = 0; i < (int)cmd.drawing.cadAnnotations.size(); ++i) addCad({SelectedEntity::Type::Annotation, i});
    for (int i = 0; i < (int)cmd.drawing.cadTables.size(); ++i)      addCad({SelectedEntity::Type::Table, i});
    for (int i = 0; i < (int)cmd.drawing.cadBlockRefs.size(); ++i)   addCad({SelectedEntity::Type::BlockRef, i});
    for (int i = 0; i < (int)cmd.drawing.surveyPoints.size(); ++i)   addSurvey(i);
  } else {
    for (const auto& e : cmd.selection)           addCad(e);
    for (int spi : cmd.selectedSurveyPointIndices) addSurvey(spi);
  }

  if (cmd.qsAppendToExisting) {
    for (const auto& e : newCad) {
      if (!std::any_of(cmd.selection.begin(), cmd.selection.end(),
            [&](const SelectedEntity& s){ return s.type == e.type && s.index == e.index; }))
        cmd.selection.push_back(e);
    }
    for (int spi : newSurvey) {
      if (std::find(cmd.selectedSurveyPointIndices.begin(), cmd.selectedSurveyPointIndices.end(), spi)
          == cmd.selectedSurveyPointIndices.end())
        cmd.selectedSurveyPointIndices.push_back(spi);
    }
  } else {
    cmd.selection = std::move(newCad);
    cmd.selectedSurveyPointIndices = std::move(newSurvey);
  }

  EnsureAttrCounts(cmd);
  BumpCadGpuCache(cmd);

  const int total = (int)(cmd.selection.size() + cmd.selectedSurveyPointIndices.size());
  char msg[128];
  std::snprintf(msg, sizeof(msg), "QUICKSELECT — %d item%s selected.", total, total == 1 ? "" : "s");
  log.push_back(msg);
}
void StartQuickSelectCommand(AppCommandState& st, std::vector<std::string>& log) {
  st.showQuickSelectWindow = true;
  log.push_back("QUICKSELECT — filter entities by type and property.");
}
