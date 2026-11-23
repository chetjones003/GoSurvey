#include "CadCommandsInternal.hpp"
#include "engine/StepEngine.hpp"
#include "CadCoordinateFrame.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static void CommitIdPointAt(AppCommandState& st, float lx, float ly, std::vector<std::string>& log) {
  double wx = 0.;
  double wy = 0.;
  CadCoord::WorldFromLocal(st, lx, ly, &wx, &wy);
  const int p = st.displayLinearPrecision;
  const double wz = static_cast<double>(CadCommitElevation(st));
  const ray3d::Vec3 shown = ucs::WorldToUcs(st.activeUcs, {wx, wy, wz});
  char buf[256];
  std::snprintf(buf, sizeof(buf), "ID — UCS (%s)  X = %s  Y = %s  Z = %s", CadUcsIsWorld(st) ? "World" : "current",
                FormatLinear(shown.x, p).c_str(), FormatLinear(shown.y, p).c_str(),
                FormatLinear(shown.z, p).c_str());
  log.push_back(buf);
  stepengine::EndSession(st);
}

static void CommitSurveyInverseSecondPoint(AppCommandState& st, float x2, float y2, std::vector<std::string>& log) {
  auto* inv = st.sessionAs<stepengine::SurveyInverseSession>();
  if (!inv)
    return;
  const float de = x2 - inv->fromX;
  const float dn = y2 - inv->fromY;
  const float horiz = std::hypot(de, dn);
  if (horiz < 1e-10f) {
    log.push_back("INVERSE — horizontal distance is zero; pick a different second point.");
    return;
  }
  const float theta = std::atan2(dn, de);
  const float brg = BearingCwNorthDegFromMathAngleRad(theta);
  const std::string brgStr = FormatBearing(static_cast<double>(brg), CadAngleDisplaySettings(st));
  const int p = st.displayLinearPrecision;
  char buf[512];
  std::snprintf(buf, sizeof(buf), "INVERSE — ΔE = %s  ΔN = %s  horiz dist = %s  bearing = %s",
                FormatLinear(static_cast<double>(de), p).c_str(), FormatLinear(static_cast<double>(dn), p).c_str(),
                FormatLinear(static_cast<double>(horiz), p).c_str(), brgStr.c_str());
  log.push_back(buf);
  stepengine::EndSession(st);
}

/// Below this horizontal separation DIST reports a pair as vertical and declines to state a grade
/// (REQ-105 as amended, D-2026-09-09-f).
///
/// The VALUE is REQ-074's, deliberately: `SURFELEV` already calls two picks closer than this "the
/// same location" and refuses a grade for them, and a user who picks the same two points in both
/// commands must not be told a grade exists in one and not the other. The NAME is DIST's own,
/// because REQ-074 spells the condition `kTinPlanEpsilon` — a threshold that means "the TIN builder
/// treats these as one shot" — and DIST has nothing to do with a TIN. Depending on that name would
/// be borrowing a predicate whose name describes another subsystem's concern, which is how a
/// redefinition upstream silently changes a branch here.
///
/// Note it is five times REQ-101's +/-0.002 ft: between the two, DIST calls a pair vertical while
/// their plan separation is still measurable. That is the conservative end, and agreeing with
/// SURFELEV was preferred over agreeing with the coordinate tolerance — see D-2026-09-09-f, which
/// records the tension rather than pretending it is not there.
static constexpr double kDistVerticalRunFt = 0.01;

/// REQ-105: DIST — reports delta X/Y/Z and slope (true 3D) distance in the active UCS, the same
/// frame ID reports in (CommitIdPointAt) so the numbers match what the user would type back in,
/// and then the four numbers a surveyor reads together: horizontal distance, vertical difference,
/// grade and run:rise (REQ-105 as amended for GitHub #149 acceptance 1).
///
/// The grade goes on its OWN log line, worded exactly as `SURFELEV`'s (REQ-074) rather than
/// appended to the first: DIST's established line already says "slope dist" for the 3D distance,
/// and "slope 1.67:1" for the run:rise ratio beside it would be two different meanings of the same
/// word in one sentence. Two lines also leave the accepted line byte-identical, so this amendment
/// only adds.
///
/// \p lx1,ly1 and \p lx2,ly2 are LOCAL X/Y (the same frame CommitIdPointAt's lx/ly parameters are
/// in), converted to world here; \p wz1,wz2 are already WORLD Z (as returned by
/// CadCommitElevation), matching CommitIdPointAt's convention.
static void CommitDistSecondPoint(AppCommandState& st, float lx1, float ly1, float wz1, float lx2, float ly2, float wz2,
                                   std::vector<std::string>& log) {
  double wx1 = 0., wy1 = 0., wx2 = 0., wy2 = 0.;
  CadCoord::WorldFromLocal(st, lx1, ly1, &wx1, &wy1);
  CadCoord::WorldFromLocal(st, lx2, ly2, &wx2, &wy2);
  const ray3d::Vec3 p1 = ucs::WorldToUcs(st.activeUcs, {wx1, wy1, static_cast<double>(wz1)});
  const ray3d::Vec3 p2 = ucs::WorldToUcs(st.activeUcs, {wx2, wy2, static_cast<double>(wz2)});
  const double dx = p2.x - p1.x;
  const double dy = p2.y - p1.y;
  const double dz = p2.z - p1.z;
  const double slope = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (slope < 1e-10) {
    log.push_back("DIST — distance is zero; pick a different second point.");
    return;
  }
  const int p = st.displayLinearPrecision;
  char buf[512];
  std::snprintf(buf, sizeof(buf), "DIST — dX = %s  dY = %s  dZ = %s  slope dist = %s", FormatLinear(dx, p).c_str(),
                FormatLinear(dy, p).c_str(), FormatLinear(dz, p).c_str(), FormatLinear(slope, p).c_str());
  log.push_back(buf);

  const double run = std::hypot(dx, dy);
  const double rise = dz;
  if (run < kDistVerticalRunFt) {
    std::snprintf(buf, sizeof(buf), "DIST — vertical: horiz %s  vert %s. No grade.",
                  FormatLinear(run, p).c_str(), FormatLinear(rise, p).c_str());
  } else if (std::abs(rise) < 1e-9) {
    std::snprintf(buf, sizeof(buf), "DIST — level (0.00%%)  horiz %s  vert %s", FormatLinear(run, p).c_str(),
                  FormatLinear(rise, p).c_str());
  } else {
    std::snprintf(buf, sizeof(buf), "DIST — grade %.2f%%  slope %.2f:1  horiz %s  vert %s", rise / run * 100.0,
                  run / std::abs(rise), FormatLinear(run, p).c_str(), FormatLinear(rise, p).c_str());
  }
  log.push_back(buf);

  stepengine::EndSession(st);
}
