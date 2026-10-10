// Tests for configurable angle DISPLAY formatting (AngleFormat.hpp, REQ-021).
// The parity case is the regression guard: at default settings the new formatter
// must reproduce the pre-feature bearing output (DMS, °/'/" symbols, 1-decimal
// seconds). Pure compute — no UI/GL (ADR-002).

#include <catch2/catch_test_macros.hpp>

#include "util/AngleFormat.hpp"

namespace {
const char* DEG = "\xc2\xb0";  // UTF-8 degree sign, as the app emits it
}

TEST_CASE("Default settings reproduce the pre-feature DMS bearing", "[angle][parity]") {
    AngleDisplaySettings def;  // DegMinSec, precision 1, clockwise, base north
    REQUIRE(FormatBearing(0.0, def)   == std::string("0") + DEG + "0'0.0\"");
    REQUIRE(FormatBearing(45.5, def)  == std::string("45") + DEG + "30'0.0\"");
    REQUIRE(FormatBearing(90.0, def)  == std::string("90") + DEG + "0'0.0\"");
    REQUIRE(FormatBearing(359.5, def) == std::string("359") + DEG + "30'0.0\"");
}

TEST_CASE("Decimal degrees honors precision", "[angle]") {
    AngleDisplaySettings s; s.type = AngleDisplayType::DecimalDegrees; s.precision = 4;
    REQUIRE(FormatBearing(45.1234567, s) == std::string("45.1235") + DEG);
    s.precision = 0;
    REQUIRE(FormatBearing(45.6, s) == std::string("46") + DEG);
}

TEST_CASE("Surveyor's units render the correct quadrant", "[angle]") {
    AngleDisplaySettings s; s.type = AngleDisplayType::Surveyor; s.precision = 0;
    REQUIRE(FormatBearing(45.0,  s) == std::string("N 45") + DEG + "0'0\" E");
    REQUIRE(FormatBearing(135.0, s) == std::string("S 45") + DEG + "0'0\" E");
    REQUIRE(FormatBearing(225.0, s) == std::string("S 45") + DEG + "0'0\" W");
    REQUIRE(FormatBearing(315.0, s) == std::string("N 45") + DEG + "0'0\" W");
}

TEST_CASE("Direction and base angle shift the displayed value", "[angle]") {
    AngleDisplaySettings s; s.type = AngleDisplayType::DecimalDegrees; s.precision = 2;
    // Base = East (90° CW from north), clockwise: a north-pointing bearing reads 270.
    s.baseDeg = 90.0; s.clockwise = true;
    REQUIRE(FormatBearing(90.0, s)  == std::string("0.00") + DEG);   // east is now 0
    REQUIRE(FormatBearing(0.0, s)   == std::string("270.00") + DEG); // north is 270 CW from east
    // Counter-clockwise from north base: east (90) reads 270.
    s.baseDeg = 0.0; s.clockwise = false;
    REQUIRE(FormatBearing(90.0, s)  == std::string("270.00") + DEG);
}

TEST_CASE("Swept angle folds to [0,180] and ignores quadrant", "[angle]") {
    AngleDisplaySettings s;  // DMS, precision 1
    REQUIRE(FormatSweptAngle(45.5, s) == std::string("45") + DEG + "30'0.0\"");
    REQUIRE(FormatSweptAngle(270.0, s) == std::string("90") + DEG + "0'0.0\"");
    s.type = AngleDisplayType::Surveyor;  // falls back to DMS for a swept angle
    REQUIRE(FormatSweptAngle(30.0, s) == std::string("30") + DEG + "0'0.0\"");
}
