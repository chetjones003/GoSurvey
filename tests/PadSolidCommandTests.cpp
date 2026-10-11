// GitHub #150 (3D Phase 7) — PADSOLID: the earthwork a building pad represents, as solids.
//
// The acceptance this file exists for is the one the phase states: a solid generated from a surface,
// a boundary and a depth, whose volume MATCHES the cut/fill the existing surface-volume path reports.
// Both numbers are computed here from the same ground and the same ring, by two independent routes —
// the B-rep's own mass properties, and `ComputeSurfaceVolume`'s grid integration — and compared.
//
// They are compared on a RELATIVE tolerance, not REQ-101. REQ-101 is +/-0.002 ft, a length; these are
// volumes produced by two different integrations of the same shape, and REQ-131 already sets the
// precedent for this kind of check (its own analytical fixture is stated "within 1%").

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "surfacevolume.hpp"

using Catch::Approx;

namespace {

/// Ground as an exact plane, z = base + slope * x, triangulated over [0, size] x [0, size].
std::shared_ptr<CadTin> PlaneGround(double size, double base, double slope) {
  auto tin = std::make_shared<CadTin>();
  tin->vertsXyz = {
      0.0,  0.0,  base,
      size, 0.0,  base + slope * size,
      size, size, base + slope * size,
      0.0,  size, base,
  };
  tin->indices = {0, 1, 2, 0, 2, 3};
  return tin;
}

/// A flat surface at `z`, over the same footprint — what the pad floor is, as a surface the existing
/// volume path can compare against.
std::shared_ptr<CadTin> FlatGround(double size, double z) {
  auto tin = std::make_shared<CadTin>();
  tin->vertsXyz = {0.0, 0.0, z, size, 0.0, z, size, size, z, 0.0, size, z};
  tin->indices = {0, 1, 2, 0, 2, 3};
  return tin;
}

void AddSurface(AppCommandState& st, const std::string& name, std::shared_ptr<CadTin> tin) {
  CadSurface s;
  s.name = name;
  s.tin = std::move(tin);
  st.cadSurfaces.push_back(std::move(s));
  st.cadSurfaceAttrs.push_back(EntityAttributes{});
}

/// A closed rectangular polyline, selected, as the pad boundary.
void AddSelectedBoundary(AppCommandState& st, double x0, double y0, double x1, double y1) {
  if (st.userPolylineOffsets.empty())
    st.userPolylineOffsets.push_back(0);
  const double xs[4] = {x0, x1, x1, x0};
  const double ys[4] = {y0, y0, y1, y1};
  for (int i = 0; i < 4; ++i) {
    st.userPolylineVerts.push_back(xs[i]);
    st.userPolylineVerts.push_back(ys[i]);
    st.userPolylineVerts.push_back(0.0);
  }
  st.userPolylineOffsets.push_back(static_cast<int>(st.userPolylineVerts.size() / 3));
  st.userPolylineClosed.push_back(1);
  st.userPolylineAttrs.push_back(EntityAttributes{});
  EnsureEntityIds(st);
  st.selection.clear();
  st.selection.push_back({SelectedEntity::Type::Polyline,
                          static_cast<int>(st.userPolylineAttrs.size()) - 1});
}

void Type(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  std::vector<char> buf(line.begin(), line.end());
  buf.push_back('\0');
  ProcessCommandLineSubmit(buf.data(), static_cast<int>(buf.size()), st, log);
}

bool LogHas(const std::vector<std::string>& log, const std::string& needle) {
  for (const std::string& l : log)
    if (l.find(needle) != std::string::npos)
      return true;
  return false;
}

double TotalSolidVolume(const AppCommandState& st) {
  double v = 0.0;
  for (const auto& sp : st.cadSolids) {
    if (!sp)
      continue;
    const brep::MassProperties mp = brep::ComputeMassProperties(*sp);
    if (mp.valid)
      v += mp.volume;
  }
  return v;
}

}  // namespace

TEST_CASE("A pad under level ground measures the prism it cuts", "[padsolid][issue150][phase7]") {
  // Level ground at 110, pad floor at 100, a 20 x 20 boundary: 20 * 20 * 10 = 4,000 cubic units, by
  // arithmetic rather than by agreement with another approximation.
  AppCommandState st;
  AddSurface(st, "EG", PlaneGround(100.0, 110.0, 0.0));
  AddSelectedBoundary(st, 20.0, 20.0, 40.0, 40.0);
  std::vector<std::string> log;

  Type(st, "PADSOLID EG, 100", log);

  INFO("last log line: " << (log.empty() ? std::string("(none)") : log.back()));
  REQUIRE(st.cadSolids.size() == 1u);
  CHECK(LogHas(log, "cut solid created"));
  CHECK(TotalSolidVolume(st) == Approx(4000.0).epsilon(0.01));
}

TEST_CASE("The pad's volume agrees with the surface volume path", "[padsolid][issue150][phase7]") {
  // Phase 7's own acceptance. Sloping ground, so the answer is not something both routes could get
  // right by accident, and the two integrations are genuinely independent.
  const double size = 100.0;
  const double padZ = 100.0;
  AppCommandState st;
  AddSurface(st, "EG", PlaneGround(size, 100.0, 0.4));  // 100 at x=0 rising to 140 at x=100
  AddSelectedBoundary(st, 20.0, 20.0, 60.0, 70.0);
  std::vector<std::string> log;

  Type(st, "PADSOLID EG, 100", log);
  REQUIRE_FALSE(st.cadSolids.empty());
  const double fromSolid = TotalSolidVolume(st);

  // The same question asked the other way: the ground against a flat surface at the pad elevation,
  // clipped to the same ring.
  const std::vector<std::pair<double, double>> ring = {{20.0, 20.0}, {60.0, 20.0}, {60.0, 70.0}, {20.0, 70.0}};
  const auto ground = PlaneGround(size, 100.0, 0.4);
  const auto flat = FlatGround(size, padZ);
  const SurfaceVolumeResult r =
      ComputeSurfaceVolume(ground->vertsXyz, ground->indices, flat->vertsXyz, flat->indices, nullptr,
                           nullptr, &ring);
  REQUIRE(r.overlapped);
  REQUIRE(r.cutFt3 > 0.0);

  INFO("solid " << fromSolid << " vs volume path " << r.cutFt3);
  CHECK(fromSolid == Approx(r.cutFt3).epsilon(0.01));  // within 1%, the REQ-131 precedent
}

TEST_CASE("A pad that straddles the ground gives a cut AND a fill solid",
          "[padsolid][issue150][phase7]") {
  // The real case: a pad cut into a slope is below ground at one end and above it at the other.
  // They are different shapes and are billed separately, so they are two solids (D-2026-10-02-a).
  AppCommandState st;
  AddSurface(st, "EG", PlaneGround(100.0, 100.0, 0.4));  // 100 -> 140 across x
  AddSelectedBoundary(st, 20.0, 20.0, 80.0, 60.0);       // ground 108 -> 132 across the pad
  std::vector<std::string> log;

  Type(st, "PADSOLID EG, 120", log);  // pad floor halfway up the slope

  CHECK(st.cadSolids.size() == 2u);
  CHECK(LogHas(log, "cut solid created"));
  CHECK(LogHas(log, "fill solid created"));
}

TEST_CASE("PADSOLID refuses clearly when it cannot work", "[padsolid][issue150][phase7]") {
  SECTION("no surface of that name") {
    AppCommandState st;
    AddSurface(st, "EG", PlaneGround(100.0, 110.0, 0.0));
    AddSelectedBoundary(st, 20.0, 20.0, 40.0, 40.0);
    std::vector<std::string> log;
    Type(st, "PADSOLID Nope, 100", log);
    CHECK(LogHas(log, "no surface named"));
    CHECK(st.cadSolids.empty());
  }
  SECTION("nothing selected to bound it") {
    AppCommandState st;
    AddSurface(st, "EG", PlaneGround(100.0, 110.0, 0.0));
    std::vector<std::string> log;
    Type(st, "PADSOLID EG, 100", log);
    CHECK(LogHas(log, "select the closed polyline"));
  }
  SECTION("an elevation that is not a number") {
    AppCommandState st;
    AddSurface(st, "EG", PlaneGround(100.0, 110.0, 0.0));
    AddSelectedBoundary(st, 20.0, 20.0, 40.0, 40.0);
    std::vector<std::string> log;
    Type(st, "PADSOLID EG, banana", log);
    CHECK(LogHas(log, "must be a number"));
    CHECK(st.cadSolids.empty());
  }
  SECTION("ground already at the pad elevation") {
    // Nothing to dig and nothing to bring in. An empty solid would be worse than saying so.
    AppCommandState st;
    AddSurface(st, "EG", PlaneGround(100.0, 100.0, 0.0));
    AddSelectedBoundary(st, 20.0, 20.0, 40.0, 40.0);
    std::vector<std::string> log;
    Type(st, "PADSOLID EG, 100", log);
    CHECK(LogHas(log, "nothing to cut or fill"));
    CHECK(st.cadSolids.empty());
  }
}

TEST_CASE("A pad is one undo step", "[padsolid][issue150][phase7]") {
  AppCommandState st;
  AddSurface(st, "EG", PlaneGround(100.0, 100.0, 0.4));
  AddSelectedBoundary(st, 20.0, 20.0, 80.0, 60.0);
  std::vector<std::string> log;

  Type(st, "PADSOLID EG, 120", log);
  REQUIRE(st.cadSolids.size() == 2u);  // both the cut and the fill
  DoUndo(st, log);
  CHECK(st.cadSolids.empty());  // one step takes BOTH away: one command, one step
}

TEST_CASE("A pad holds up at survey coordinate magnitudes", "[padsolid][issue150][phase7]") {
  // Phase 7: "everything remains stable at survey coordinate magnitudes."
  const double e0 = 2196000.0;
  const double n0 = 1400000.0;
  AppCommandState st;
  auto tin = std::make_shared<CadTin>();
  tin->vertsXyz = {
      e0,         n0,         250.0,
      e0 + 100.0, n0,         260.0,
      e0 + 100.0, n0 + 100.0, 260.0,
      e0,         n0 + 100.0, 250.0,
  };
  tin->indices = {0, 1, 2, 0, 2, 3};
  AddSurface(st, "EG", tin);
  AddSelectedBoundary(st, e0 + 20.0, n0 + 20.0, e0 + 60.0, n0 + 70.0);
  std::vector<std::string> log;

  Type(st, "PADSOLID EG, 250", log);

  INFO("last log line: " << (log.empty() ? std::string("(none)") : log.back()));
  REQUIRE_FALSE(st.cadSolids.empty());
  // Ground runs 252 to 256 across the pad, mean 254, so the cut over 40 x 50 is about 4 * 2000.
  CHECK(TotalSolidVolume(st) == Approx(8000.0).epsilon(0.02));
}
