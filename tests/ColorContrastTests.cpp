#include <catch2/catch_test_macros.hpp>

#include "commands/ColorContrast.hpp"

// REQ-048 refinement: white/black are background-adaptive for legibility; other colors are untouched.
TEST_CASE("Near-white flips to black on a light background (REQ-048)", "[color]") {
  float r = 1.f, g = 1.f, b = 1.f;
  AdaptWhiteBlackToBackground(&r, &g, &b, /*backgroundIsLight=*/true);
  REQUIRE(r == 0.f);
  REQUIRE(g == 0.f);
  REQUIRE(b == 0.f);
}

TEST_CASE("Near-black flips to white on a dark background (REQ-048)", "[color]") {
  float r = 0.f, g = 0.f, b = 0.f;
  AdaptWhiteBlackToBackground(&r, &g, &b, /*backgroundIsLight=*/false);
  REQUIRE(r == 1.f);
  REQUIRE(g == 1.f);
  REQUIRE(b == 1.f);
}

TEST_CASE("No flip when the color already contrasts the background (REQ-048)", "[color]") {
  // White on a dark background stays white; black on a light background stays black.
  float wr = 1.f, wg = 1.f, wb = 1.f;
  AdaptWhiteBlackToBackground(&wr, &wg, &wb, /*backgroundIsLight=*/false);
  REQUIRE(wr == 1.f);

  float kr = 0.f, kg = 0.f, kb = 0.f;
  AdaptWhiteBlackToBackground(&kr, &kg, &kb, /*backgroundIsLight=*/true);
  REQUIRE(kr == 0.f);
}

TEST_CASE("Mid and saturated colors are left unchanged (REQ-048)", "[color]") {
  // Red — well away from white/black — is untouched on either background.
  float r = 1.f, g = 0.f, b = 0.f;
  AdaptWhiteBlackToBackground(&r, &g, &b, true);
  REQUIRE(r == 1.f);
  REQUIRE(g == 0.f);
  REQUIRE(b == 0.f);

  // A light gray below the near-white threshold is not flipped.
  float lr = 0.7f, lg = 0.7f, lb = 0.7f;
  AdaptWhiteBlackToBackground(&lr, &lg, &lb, true);
  REQUIRE(lr == 0.7f);
}
