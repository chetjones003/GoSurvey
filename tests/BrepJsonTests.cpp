// `.gs` fidelity for B-rep solids (REQ-313 / REQ-314 / REQ-315), through exactly the serializer the
// saver runs (`gsio::SolidToJson` / `gsio::SolidFromJson` in BrepJson.hpp).
//
// The acceptance conditions this pins:
//   - a solid round-trips with vertex / edge / face counts identical and volume / area within a
//     relative 1e-6 (REQ-315);
//   - a solid with no NURBS face serializes exactly as a version-3 build would — the encoding is
//     additive, so its JSON is byte-for-byte independent of this change;
//   - a version-4 file with a malformed NURBS patch is refused by the loader, not loaded.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "io/BrepJson.hpp"
#include "util/brep.hpp"

#include <string>
#include <vector>

using Catch::Approx;

namespace {

constexpr double kPi = 3.14159265358979323846;

ucs::Ucs World() { return ucs::Ucs{}; }

ucs::Ucs PlaneAt(double z) {
  ucs::Ucs u;
  u.origin = {0.0, 0.0, z};
  return u;
}

brep::Profile CircleProfile(const ucs::Ucs& plane, double r) {
  brep::Profile pr;
  pr.plane = plane;
  pr.vertices = {ucs::PlaneToWorld(plane, {r, 0.0}), ucs::PlaneToWorld(plane, {-r, 0.0})};
  brep::ProfileEdge e;
  e.arc = true;
  e.centre = plane.origin;
  e.sweep = kPi;
  pr.edges = {e, e};
  return pr;
}

void RequireRoundTrips(const brep::Solid& s) {
  const nlohmann::json j = gsio::SolidToJson(s);
  brep::Solid back;
  REQUIRE(gsio::SolidFromJson(j, &back));
  REQUIRE(brep::Validate(back) == brep::Problem::Ok);
  REQUIRE(back.vertices.size() == s.vertices.size());
  REQUIRE(back.edges.size() == s.edges.size());
  REQUIRE(back.faces.size() == s.faces.size());

  const brep::MassProperties a = brep::ComputeMassProperties(s);
  const brep::MassProperties b = brep::ComputeMassProperties(back);
  REQUIRE(a.valid);
  REQUIRE(b.valid);
  REQUIRE(b.volume == Approx(a.volume).epsilon(1e-6));
  REQUIRE(b.surfaceArea == Approx(a.surfaceArea).epsilon(1e-6));

  // Written twice, the JSON is identical — the encoding has no hidden state.
  REQUIRE(gsio::SolidToJson(back).dump() == j.dump());
}

}  // namespace

TEST_CASE("An analytic solid round-trips through .gs unchanged", "[brepjson][req313]") {
  brep::Solid box;
  brep::Solid cyl;
  brep::Solid tor;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(World(), 20.0, 10.0, 8.0, &box, &why));
  REQUIRE(brep::MakeCylinder(World(), 4.0, 9.0, &cyl, &why));
  REQUIRE(brep::MakeTorus(World(), 8.0, 2.0, &tor, &why));
  RequireRoundTrips(box);
  RequireRoundTrips(cyl);
  RequireRoundTrips(tor);
}

TEST_CASE("A solid with no NURBS face serializes without the patch key", "[brepjson][req315]") {
  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(World(), 3.0, 3.0, 3.0, &box, &why));
  const std::string dumped = gsio::SolidToJson(box).dump();
  REQUIRE(dumped.find("patch") == std::string::npos);
  REQUIRE(dumped.find("Nurbs") == std::string::npos);
}

TEST_CASE("A swept solid round-trips through .gs with topology and volume intact", "[brepjson][req315]") {
  brep::Solid swept;
  brep::Problem why = brep::Problem::Ok;
  brep::SweepSegment seg;
  seg.arc = true;
  seg.centre = ray3d::Vec3{0, 0, 0};
  seg.normal = ray3d::Vec3{0, 0, 1};
  seg.sweep = kPi;  // a half-turn elbow
  brep::SweepPath arcPath;
  arcPath.points = {ray3d::Vec3{10, 0, 0}, ray3d::Vec3{-10, 0, 0}};  // +X rotated pi about +Z
  arcPath.segments = {seg};

  ucs::Ucs prof;
  REQUIRE(ucs::FromNormal(ray3d::Vec3{10, 0, 0}, ray3d::Vec3{0, 1, 0}, &prof));
  brep::Profile pr;
  pr.plane = prof;
  for (const ray3d::Vec3& lp : {ray3d::Vec3{-1.5, -1.5, 0}, ray3d::Vec3{1.5, -1.5, 0},
                                ray3d::Vec3{1.5, 1.5, 0}, ray3d::Vec3{-1.5, 1.5, 0}})
    pr.vertices.push_back(ucs::UcsToWorld(prof, lp));
  pr.edges.assign(4, brep::ProfileEdge{});

  REQUIRE(brep::Sweep(pr, arcPath, brep::SweepOptions{}, &swept, &why));
  bool sawNurbs = false;
  for (const brep::Face& f : swept.faces)
    if (f.surface.kind == brep::SurfaceKind::Nurbs)
      sawNurbs = true;
  REQUIRE(sawNurbs);
  RequireRoundTrips(swept);
}

TEST_CASE("A lofted solid with NURBS faces round-trips with topology and volume intact",
          "[brepjson][req315]") {
  brep::Solid loft;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::Loft({CircleProfile(World(), 5.0), CircleProfile(PlaneAt(4.0), 8.0),
                      CircleProfile(PlaneAt(11.0), 3.5)},
                     &loft, &why));

  bool sawNurbs = false;
  for (const brep::Face& f : loft.faces)
    if (f.surface.kind == brep::SurfaceKind::Nurbs)
      sawNurbs = true;
  REQUIRE(sawNurbs);

  RequireRoundTrips(loft);

  // The reloaded patches evaluate to the same points as the originals.
  const nlohmann::json j = gsio::SolidToJson(loft);
  brep::Solid back;
  REQUIRE(gsio::SolidFromJson(j, &back));
  for (std::size_t fi = 0; fi < loft.faces.size(); ++fi) {
    if (loft.faces[fi].surface.kind != brep::SurfaceKind::Nurbs)
      continue;
    const nurbs::Patch& p0 = loft.faces[fi].surface.patch;
    const nurbs::Patch& p1 = back.faces[fi].surface.patch;
    for (double u : {0.1, 0.5, 0.9})
      for (double v : {0.2, 0.8}) {
        const ray3d::Vec3 a = nurbs::Evaluate(p0, nurbs::UMin(p0) + u, nurbs::VMin(p0) + v);
        const ray3d::Vec3 b = nurbs::Evaluate(p1, nurbs::UMin(p1) + u, nurbs::VMin(p1) + v);
        REQUIRE(b.x == Approx(a.x));
        REQUIRE(b.y == Approx(a.y));
        REQUIRE(b.z == Approx(a.z));
      }
  }
}

TEST_CASE("A hand-built general trim loop round-trips through .gs (ADR-052, issue #306)",
          "[brepjson][req306][adr052]") {
  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(World(), 10.0, 10.0, 10.0, &box, &why));

  int fi = -1;
  for (std::size_t i = 0; i < box.faces.size(); ++i) {
    if (box.faces[i].surface.kind == brep::SurfaceKind::Plane && box.faces[i].loops.size() == 1) {
      fi = static_cast<int>(i);
      break;
    }
  }
  REQUIRE(fi >= 0);
  box.faces[static_cast<std::size_t>(fi)].paramLoops = {
      {{0.0, 0.0}, {4.0, 0.0}, {4.0, 2.0}, {2.0, 4.0}, {0.0, 2.0}}};
  REQUIRE(brep::Validate(box) == brep::Problem::Ok);

  const nlohmann::json j = gsio::SolidToJson(box);
  brep::Solid back;
  REQUIRE(gsio::SolidFromJson(j, &back));
  REQUIRE(brep::Validate(back) == brep::Problem::Ok);
  REQUIRE(back.faces[static_cast<std::size_t>(fi)].paramLoops.size() == 1);
  const std::vector<curveisect::Vec2>& poly = back.faces[static_cast<std::size_t>(fi)].paramLoops[0];
  REQUIRE(poly.size() == 5);
  REQUIRE(poly[2].x == Approx(4.0));
  REQUIRE(poly[2].y == Approx(2.0));

  // Written twice, the JSON is identical.
  REQUIRE(gsio::SolidToJson(back).dump() == j.dump());
}

TEST_CASE("A face with an empty general trim loop serializes without the paramLoops key",
          "[brepjson][req306][adr052]") {
  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(World(), 3.0, 3.0, 3.0, &box, &why));
  const std::string dumped = gsio::SolidToJson(box).dump();
  REQUIRE(dumped.find("paramLoops") == std::string::npos);
}

TEST_CASE("A version-4 file with a malformed NURBS patch is refused, not loaded", "[brepjson][req315]") {
  brep::Solid loft;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::Loft({CircleProfile(World(), 5.0), CircleProfile(PlaneAt(6.0), 5.0)}, &loft, &why));
  nlohmann::json j = gsio::SolidToJson(loft);

  // Corrupt the first NURBS patch: a weight of zero is not a usable rational surface.
  for (auto& jf : j["faces"]) {
    if (jf["surface"].contains("patch")) {
      jf["surface"]["patch"]["wts"][0] = 0.0;
      break;
    }
  }
  brep::Solid back;
  REQUIRE_FALSE(gsio::SolidFromJson(j, &back));
}
