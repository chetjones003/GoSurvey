#include "CadCommandsInternal.hpp"
#include "modify/CadModifyInternal.hpp"
#include "modify/CadModifySessions.hpp"
#include "engine/StepEngine.hpp"
#include "CadBlocks.hpp"
#include "CadCoordinateFrame.hpp"
#include <GoSurvey/Geometry/spline.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using GoSurvey::Document::EntityOp;
using GoSurvey::Document::SimilarityTransform;

void ApplyUniformScaleAboutBase(AppCommandState& st, float bx, float by, float bz, float sc,
                                std::vector<std::string>& log) {
  // SCALE is uniform in X, Y and Z about the base point, in plan view too, as AutoCAD's is (issue
  // #1577). REQ-060's gizmo calls this as well, so the two cannot disagree.
  if (!(sc > 0.f) || !std::isfinite(sc))
    return;
  TransformSelection(st, SimilarityTransform::UniformScale({bx, by, bz}, sc), EntityOp::Scale, "SCALE",
                     /*duplicate=*/false, log);
}


bool CadRotatePreviewTheta(const AppCommandState& cmd, float curX, float curY, double* outThetaRad) {
  const auto* rs = cmd.sessionAs<stepengine::RotateSession>();
  if (!rs || !outThetaRad)
    return false;
  using RP = stepengine::RotatePhase;
  if (rs->phase == RP::NeedAngleOrReference) {
    // RotateSession::onViewportPick: clockwise from north in the work plane through the base point.
    double dx = static_cast<double>(curX) - rs->baseX;
    double dy = static_cast<double>(curY) - rs->baseY;
    if (!CadWorkPlaneIsWorldXy(cmd)) {
      // The cursor on the work plane (no pick ray here). The pick resolves its ray onto the plane
      // through the base point instead, which is the same point unless the base was snapped off the
      // work plane in an orbited view.
      const ucs::Point2D loc = ucs::WorldToPlane(CadWorkPlaneAnchoredAt(cmd, rs->baseX, rs->baseY, rs->baseZ),
                                                 {curX, curY, cmd.uiCursorWorldZ});
      dx = loc.x;
      dy = loc.y;
    }
    *outThetaRad = -std::atan2(dx, dy);
  } else if (rs->phase == RP::AfterReference_WaitAngleOrP) {
    const double thetaRef = std::atan2(rs->refY2 - rs->refY1, rs->refX2 - rs->refX1);
    *outThetaRad = std::atan2(static_cast<double>(curY) - rs->baseY, static_cast<double>(curX) - rs->baseX) - thetaRef;
  } else if (rs->phase == RP::AnglePoints_WaitP2) {
    *outThetaRad = RotateDeltaFromReferenceAndNewSegment(rs->refX1, rs->refY1, rs->refX2, rs->refY2, rs->anglePt1X,
                                                         rs->anglePt1Y, curX, curY);
  } else {
    return false;
  }
  return true;
}

bool CadScalePreviewFactor(const AppCommandState& cmd, float curX, float curY, float* outScale) {
  const auto* ss = cmd.sessionAs<stepengine::ScaleSession>();
  if (!ss || !outScale)
    return false;
  if (ss->phase != stepengine::ModifyPhase::NeedDestination)
    return false;
  using SP = stepengine::ScalePhase;
  if (ss->scalePhase == SP::FactorPick) {
    const float d = std::hypot(curX - ss->baseX, curY - ss->baseY);
    const float s = d / std::max(ss->refDist, 1e-20f);
    *outScale = std::max(s, 1e-6f);
    return true;
  }
  if (ss->scalePhase == SP::NewLength_WaitP2) {
    const float d = std::hypot(curX - ss->newLenP1X, curY - ss->newLenP1Y);
    const float s = d / std::max(ss->refDist, 1e-20f);
    *outScale = std::max(s, 1e-6f);
    return true;
  }
  return false;
}

void CadAnnotationCollectTransformPreviews(const AppCommandState& cmd, float curX, float curY,
                                           std::vector<CadAnnotation>* out) {
  if (!out)
    return;
  out->clear();
  // The same scratch copy the wireframe preview draws (issue #1700): the annotations the commit
  // would leave, transformed by the commit's own operation.
  CadTransformPreviewPlan plan;
  if (!CadActiveTransformPreviewPlan(cmd, curX, curY, &plan) && !CadGizmoDragPreviewPlan(cmd, &plan))
    return;
  CadTransformPreviewScratch scratch;
  BuildCadTransformPreviewScratch(cmd, plan, &scratch, SelectedEntity::Type::Annotation);
  for (const SelectedEntity& e : scratch.entities)
    out->push_back(scratch.drawing.cadAnnotations[static_cast<size_t>(e.index)]);
}


void StartScaleCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // #764: scale paper geometry
    if (st.selectedPaperEntities.empty()) {
      log.push_back("SCALE — select paper object(s) first.");
      return;
    }
    st.active = AppCommandState::Kind::None;  // paper-space edit ops are not a model command
    st.paperScalePhase = 1;
    log.push_back("SCALE — specify base point (click or type X,Y). ESC to cancel.");
    return;
  }
  ClearPendingViewportZoom(st);
  const bool haveSel = !st.selection.empty() || !st.selectedSurveyPointIndices.empty();
  const auto initialPhase =
      haveSel ? stepengine::ModifyPhase::NeedBase : stepengine::ModifyPhase::PickSelection;
  stepengine::BeginSession(st, std::make_unique<stepengine::ScaleSession>(initialPhase), true);
  if (haveSel)
    log.push_back("SCALE — specify base point (click or type X,Y). ESC to cancel.");
  else
    log.push_back(kSelectObjectsPrompt);
}
