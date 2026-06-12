// Regression tests for centralized display-precision formatting (NumFormat.hpp).
// Guards the single source of truth that unifies user-facing coordinate/length
// readouts; before this helper existed, each readout hard-coded its own decimal
// count (status bar 3, dimensions/properties 4, ID/INVERSE/survey 6), which is
// the bug this fix resolves. Pure compute — no UI/GL (ADR-002).

#include <catch2/catch_test_macros.hpp>

#include "util/NumFormat.hpp"

TEST_CASE("FloatFmt builds a printf token for the requested precision", "[numformat]") {
    REQUIRE(DisplayFloatFmt(0) == "%.0f");
    REQUIRE(DisplayFloatFmt(4) == "%.4f");
    REQUIRE(DisplayFloatFmt(6) == "%.6f");
}

TEST_CASE("FormatLinear rounds to the requested decimal places", "[numformat]") {
    // Same stored value renders at different precisions on demand — this is what
    // makes every readout honor one configurable setting.
    const double v = 1.23456789;
    REQUIRE(FormatLinear(v, 4) == "1.2346");
    REQUIRE(FormatLinear(v, 6) == "1.234568");
    REQUIRE(FormatLinear(v, 0) == "1");
    REQUIRE(FormatLinear(2.0, 4) == "2.0000");
}

TEST_CASE("Out-of-range precision is clamped, not crashed", "[numformat]") {
    REQUIRE(DisplayPrecisionClamp(-3) == 0);
    REQUIRE(DisplayPrecisionClamp(99) == 12);
    REQUIRE(FormatLinear(1.0, 99) == FormatLinear(1.0, 12));
}
