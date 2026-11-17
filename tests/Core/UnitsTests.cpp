#include <GoSurvey/Core/Units.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("INSUNITS convert to meters with the selected foot definition") {
  CHECK(GoSurvey::Core::MetersPerInsUnit(1, false) == Approx(0.0254));
  CHECK(GoSurvey::Core::MetersPerInsUnit(2, true) == Approx(1200.0 / 3937.0));
  CHECK(GoSurvey::Core::MetersPerInsUnit(2, false) == Approx(0.3048));
  CHECK(GoSurvey::Core::MetersPerInsUnit(4, false) == Approx(0.001));
  CHECK(GoSurvey::Core::MetersPerInsUnit(6, false) == Approx(1.0));
}

TEST_CASE("Unknown and unitless INSUNITS codes convert to zero meters") {
  CHECK(GoSurvey::Core::MetersPerInsUnit(0, false) == 0.0);
  CHECK(GoSurvey::Core::MetersPerInsUnit(3, false) == 0.0);
  CHECK(GoSurvey::Core::MetersPerInsUnit(1000, false) == 0.0);
}
