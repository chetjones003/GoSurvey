#include "CadCommandsInternal.hpp"
#include "inquiry/CadInquiryInternal.hpp"
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

void StartIdPointCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("ID — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = K::IdPoint;
  log.push_back("ID — specify point (click in drawing or type X,Y). UCS = World. ESC cancels.");
}


void StartSurveyInverseCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using SIP = AppCommandState::SurveyInversePhase;
  if (st.active != K::None) {
    log.push_back("INVERSE — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = K::SurveyInverse;
  st.surveyInversePhase = SIP::WaitFrom;
  log.push_back(
      "INVERSE — first point (World X=Easting, Y=Northing); then second. "
      "Result: ΔE, ΔN, horizontal distance, bearing D°M'S\" and decimal ° clockwise from north. ESC cancels.");
}

void StartDistCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using DP = AppCommandState::DistPhase;
  if (st.active != K::None) {
    log.push_back("DIST — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = K::Dist;
  st.distPhase = DP::WaitFrom;
  log.push_back(
      "DIST — first point (pick, snap, or type X,Y); then second (pick, snap, type X,Y, or "
      "@dx,dy for direct-distance entry). Result: dX, dY, dZ, and slope (3D) distance. ESC cancels.");
}
