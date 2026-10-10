#include <catch2/catch_test_macros.hpp>

#include "CadCommands.hpp"

TEST_CASE("PickCadEntityByDepth prefers highest Z in plan view", "[pick-disambiguation]") {
  std::vector<CadPickCandidate> cands;
  cands.push_back({SelectedEntity{SelectedEntity::Type::LineSeg, 0}, 1.0, 0.0});
  cands.push_back({SelectedEntity{SelectedEntity::Type::LineSeg, 1}, 1.0, 5.0});
  cands.push_back({SelectedEntity{SelectedEntity::Type::Circle, 0}, 0.5, 2.0});

  SelectedEntity out{};
  REQUIRE(PickCadEntityByDepth(cands, &out, nullptr));
  REQUIRE(out.type == SelectedEntity::Type::LineSeg);
  REQUIRE(out.index == 1);
}

TEST_CASE("PickCadEntityByDepth prefers nearest ray t when orbited", "[pick-disambiguation]") {
  ray3d::Ray ray{};
  ray.origin = ray3d::Vec3{0.0, 0.0, 10.0};
  ray.dir = ray3d::Vec3{0.0, 0.0, -1.0};

  std::vector<CadPickCandidate> cands;
  cands.push_back({SelectedEntity{SelectedEntity::Type::Arc, 0}, 1.0, 8.0});
  cands.push_back({SelectedEntity{SelectedEntity::Type::Arc, 1}, 1.0, 3.0});

  SelectedEntity out{};
  REQUIRE(PickCadEntityByDepth(cands, &out, &ray));
  REQUIRE(out.index == 1);
}
