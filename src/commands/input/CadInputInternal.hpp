#include "CadCommandsInternal.hpp"
#include <cmath>
#include <string>
#include <vector>

static float NormalizeBearingDegreesCwNorth(float deg) {
  deg = std::fmod(deg, 360.f);
  if (deg < 0.f)
    deg += 360.f;
  return deg;
}

static bool ParseBearingCwNorthStringWithOptionalDelta(const std::string& raw, float* bearingCombinedDegOut,
                                                       std::vector<std::string>& log) {
  std::string work = StringUtil::trimCopy(raw);
  if (work.empty())
    return false;
  float bear = 0.f;
  float delta = 0.f;
  bool hasDelta = false;
  std::string bearOnly = work;

  const size_t sp = work.rfind(' ');
  if (sp != std::string::npos && sp + 1 < work.size()) {
    std::string tail = StringUtil::trimCopy(work.substr(sp + 1));
    std::string head = StringUtil::trimCopy(work.substr(0, sp));
    if (!tail.empty() && (tail[0] == '+' || tail[0] == '-')) {
      if (!ParseAngleDegreesInternal(tail, &delta)) {
        log.push_back("Invalid adjustment — use +90 / -45 (decimal or DMS).");
        return false;
      }
      hasDelta = true;
      bearOnly = head;
    }
  }

  if (!hasDelta) {
    for (size_t k = 1; k < work.size(); ++k) {
      if (work[k] != '+' && work[k] != '-')
        continue;
      std::string head = StringUtil::trimCopy(work.substr(0, k));
      std::string tail = StringUtil::trimCopy(work.substr(k));
      if (head.empty() || tail.empty())
        continue;
      if (!ParseAngleDegreesInternal(head, &bear))
        continue;
      if (!ParseAngleDegreesInternal(tail, &delta))
        continue;
      *bearingCombinedDegOut = NormalizeBearingDegreesCwNorth(bear + delta);
      return true;
    }
  }

  if (!ParseAngleDegreesInternal(bearOnly, &bear)) {
    log.push_back("Could not parse bearing — decimal degrees or DMS (° clockwise from north).");
    return false;
  }
  *bearingCombinedDegOut = NormalizeBearingDegreesCwNorth(bear + (hasDelta ? delta : 0.f));
  return true;
}

static void CommitSegmentAnglePickLock(AppCommandState& st, std::vector<std::string>& log) {
  using SAP = AppCommandState::SegmentAnglePickPhase;
  const float br = NormalizeBearingDegreesCwNorth(st.segmentPickDraftBearingDeg);
  const float theta = MathAngleRadFromBearingCwNorthDeg(br);
  st.segmentLockUx = std::cos(theta);
  st.segmentLockUy = std::sin(theta);
  st.segmentAngleLockActive = true;
  st.segmentAnglePickPhase = SAP::Idle;
  char buf[144];
  std::snprintf(buf, sizeof(buf),
                "Bearing locked %.6g° clockwise from north — distance (+/-) or click on ray (A clears).",
                static_cast<double>(br));
  log.push_back(buf);
}
