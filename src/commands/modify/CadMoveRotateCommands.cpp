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

double RotateDeltaFromReferenceAndNewSegment(double refX1, double refY1, double refX2, double refY2,
                                             double newX1, double newY1, double newX2, double newY2) {
  const double thetaRef = std::atan2(refY2 - refY1, refX2 - refX1);
  const double thetaNew = std::atan2(newY2 - newY1, newX2 - newX1);
  return thetaNew - thetaRef;
}


using GoSurvey::Document::EntityOp;
using GoSurvey::Document::SimilarityTransform;

// Public API (declared in `CadCommands.hpp`): REQ-060 requires that a gizmo drag and the equivalent
// typed command produce the same coordinates, so both call this.
void ApplyTranslationToSelection(AppCommandState& st, float dx, float dy, float dz,
                                std::vector<std::string>& log) {
  TransformSelection(st, SimilarityTransform::Translation({dx, dy, dz}), EntityOp::Move, "MOVE",
                     /*duplicate=*/false, log);
}

bool ParseAngleDegreesInternal(const std::string& raw, double* degreesOut) {
  std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  std::string low = StringUtil::toLowerAsciiCopy(s);
  bool neg = false;
  if (!low.empty() && low[0] == '-') {
    neg = true;
    low = StringUtil::trimCopy(low.substr(1));
  }
  if (!low.empty() && low[0] == '+')
    low = StringUtil::trimCopy(low.substr(1));
  double deg = 0.0;
  double min = 0.0;
  double sec = 0.0;
  size_t pd = low.find('d');
  if (pd != std::string::npos) {
    if (!(std::istringstream(low.substr(0, pd)) >> deg))
      return false;
    std::string rest = low.substr(pd + 1);
    size_t pm = rest.find('m');
    if (pm != std::string::npos) {
      if (!(std::istringstream(rest.substr(0, pm)) >> min))
        return false;
      std::string rest2 = rest.substr(pm + 1);
      size_t ps = rest2.find('s');
      if (ps != std::string::npos) {
        if (!(std::istringstream(rest2.substr(0, ps)) >> sec))
          return false;
      } else {
        if (!(std::istringstream(rest2) >> sec))
          sec = 0.0;
      }
    } else {
      if (!(std::istringstream(rest) >> min))
        min = 0.0;
    }
    *degreesOut = (neg ? -1.0 : 1.0) * (deg + min / 60.0 + sec / 3600.0);
    return true;
  }
  if (!(std::istringstream(low) >> deg))
    return false;
  *degreesOut = neg ? -deg : deg;
  return true;
}

bool ParseAngleDegreesInternal(const std::string& raw, float* degreesOut) {
  double deg = 0.0;
  if (!ParseAngleDegreesInternal(raw, &deg))
    return false;
  *degreesOut = static_cast<float>(deg);
  return true;
}

/// Peel an optional third component off a typed point, returning the `X,Y` part and the elevation.
///
/// The same shape `ParseSolidBasePoint` uses, and for the same reason: the shared 2D parser reads
/// two numbers and ignores the rest, so `1,2,3` would otherwise land silently at (1,2) with the
/// elevation dropped on the floor. Strict about the field count for the same reason.
///
/// An omitted Z is zero and `*outHasZ` is false, so every existing drawing, transcript and habit
/// behaves exactly as it did (REQ-322 item 4).


void ApplyRotationAboutUcsZ(AppCommandState& st, double bx, double by, double bz, double rad,
                            std::vector<std::string>& log) {
  // ROTATE turns the selection about an axis through the base point parallel to the active UCS Z
  // (REQ-329). In plan view, or under any UCS merely rotated in plan, that axis is world Z.
  const ucs::Ucs u = CadActiveUcsStorage(st);
  const ray3d::Vec3 axisUnit = CadWorkPlaneIsWorldXy(st)
                                   ? ray3d::Vec3{0.0, 0.0, 1.0}
                                   : ray3d::Normalize(ray3d::Vec3{u.zAxis.x, u.zAxis.y, u.zAxis.z});
  ApplyRotationAboutAxis(st, {bx, by, bz}, axisUnit, rad, log);
}

void ApplyRotationAboutAxis(AppCommandState& st, const ray3d::Vec3& axisPoint,
                            const ray3d::Vec3& axisUnit, double rad, std::vector<std::string>& log) {
  const SimilarityTransform xf = SimilarityTransform::RotationAboutAxis(axisPoint, axisUnit, rad);
  TransformSelection(st, xf, xf.isPlanarZ() ? EntityOp::RotatePlan : EntityOp::Rotate3d, "ROTATE",
                     /*duplicate=*/false, log);
}

void StartMoveCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // REQ-035: move viewports
    StartPaperMoveCopyViewports(st, /*copy=*/false, log);
    return;
  }
  ClearPendingViewportZoom(st);
  const bool haveSel = !st.selection.empty() || !st.selectedSurveyPointIndices.empty();
  const auto initialPhase =
      haveSel ? stepengine::ModifyPhase::NeedBase : stepengine::ModifyPhase::PickSelection;
  stepengine::BeginSession(st, std::make_unique<stepengine::MoveCopySession>(false, initialPhase), true);
  if (haveSel)
    log.push_back("MOVE — specify base point (click or type X,Y). ESC to cancel.");
  else
    log.push_back(kSelectObjectsPrompt);
}

void StartCopyCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // REQ-035: copy viewports
    StartPaperMoveCopyViewports(st, /*copy=*/true, log);
    return;
  }
  ClearPendingViewportZoom(st);
  const bool haveSel = !st.selection.empty() || !st.selectedSurveyPointIndices.empty();
  const auto initialPhase =
      haveSel ? stepengine::ModifyPhase::NeedBase : stepengine::ModifyPhase::PickSelection;
  stepengine::BeginSession(st, std::make_unique<stepengine::MoveCopySession>(true, initialPhase), true);
  if (haveSel)
    log.push_back("COPY — specify base point (click or type X,Y). ESC to cancel.");
  else
    log.push_back(kSelectObjectsPrompt);
}

void StartRotateCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // REQ-037: rotate paper geometry
    if (st.selectedPaperEntities.empty()) {
      log.push_back("ROTATE — select paper object(s) first.");
      return;
    }
    st.active = AppCommandState::Kind::None;  // paper-space edit ops are not a model command
    st.paperRotatePhase = 1;
    log.push_back("ROTATE — specify base point (click or type X,Y). ESC to cancel.");
    return;
  }
  ClearPendingViewportZoom(st);
  st.pendingSurveyDupIsRotate = false;
  const bool haveSel = !st.selection.empty() || !st.selectedSurveyPointIndices.empty();
  const auto initialPhase =
      haveSel ? stepengine::RotatePhase::NeedBase : stepengine::RotatePhase::PickSelection;
  stepengine::BeginSession(st, std::make_unique<stepengine::RotateSession>(initialPhase), true);
  if (haveSel)
    log.push_back("ROTATE — specify base point (click or type X,Y). ESC to cancel.");
  else
    log.push_back(kSelectObjectsPrompt);
}
