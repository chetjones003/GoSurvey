// REQ-399 - extract coordinates from a PDF region into survey points or circles. Pure matching,
// no PDF, no window.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pdf/PdfCoordExtract.hpp"

#include <algorithm>

using namespace pdfview;

namespace {

DimText T(const std::string& s, float x0, float y0, float x1, float y1) { return {s, x0, y0, x1, y1}; }

} // namespace

TEST_CASE("PdfCoordExtract single monument block") {
  // REQ-399 clause 4a: "BRASS CAP CONTROL" / N:303267.23 / E:1887764.63 / EL. 99.35
  std::vector<DimText> texts = {
      T("BRASS CAP CONTROL", 10, 90, 150, 100),
      T("N:303267.23", 10, 75, 100, 85),
      T("E:1887764.63", 10, 60, 100, 70),
      T("EL. 99.35", 10, 45, 100, 55),
  };
  ExtractResult r = ExtractCoordinates(texts, 0, 0, 200, 120);
  REQUIRE(r.status == ExtractStatus::Ok);
  REQUIRE(r.candidates.size() == 1);
  const CoordCandidate& c = r.candidates[0];
  CHECK(c.northing == Catch::Approx(303267.23).margin(1e-6));
  CHECK(c.easting == Catch::Approx(1887764.63).margin(1e-6));
  REQUIRE(c.HasElevation());
  CHECK(*c.elevation == Catch::Approx(99.35).margin(1e-6));
  CHECK(c.description == "BRASS CAP CONTROL");
  CHECK(c.IsSurveyPoint());
  CHECK_FALSE(c.pointNumber.has_value());
}

TEST_CASE("PdfCoordExtract bare northing easting pair") {
  std::vector<DimText> texts = {
      T("N303401.741", 10, 80, 100, 90),
      T("E1887631.271", 10, 65, 100, 75),
  };
  ExtractResult r = ExtractCoordinates(texts, 0, 0, 200, 120);
  REQUIRE(r.status == ExtractStatus::Ok);
  REQUIRE(r.candidates.size() == 1);
  const CoordCandidate& c = r.candidates[0];
  CHECK(c.northing == Catch::Approx(303401.741).margin(1e-6));
  CHECK(c.easting == Catch::Approx(1887631.271).margin(1e-6));
  CHECK_FALSE(c.HasElevation());
  CHECK_FALSE(c.HasDescription());
  CHECK_FALSE(c.IsSurveyPoint()); // REQ-399 clause 6: bare pair becomes a circle, not a point
}

TEST_CASE("PdfCoordExtract point table with header columns") {
  // Header: Point # | Description | Elevation | Northing | Easting
  std::vector<DimText> texts = {
      T("Point #", 0, 100, 40, 110),
      T("Description", 45, 100, 100, 110),
      T("Elevation", 105, 100, 150, 110),
      T("Northing", 155, 100, 210, 110),
      T("Easting", 215, 100, 270, 110),

      T("1", 10, 85, 20, 95),
      T("SIDEWALK", 45, 85, 100, 95),
      T("100.12", 115, 85, 145, 95),
      T("303398.59", 160, 85, 205, 95),
      T("1887382.33", 220, 85, 265, 95),

      T("2", 10, 70, 20, 80),
      T("SIDEWALK", 45, 70, 100, 80),
      T("99.94", 115, 70, 145, 80),
      T("303388.42", 160, 70, 205, 80),
      T("1887394.51", 220, 70, 265, 80),
  };
  ExtractResult r = ExtractCoordinates(texts, 0, 0, 300, 120);
  REQUIRE(r.status == ExtractStatus::Ok);
  REQUIRE(r.candidates.size() == 2);

  CHECK(r.candidates[0].pointNumber.value_or(-1) == 1);
  CHECK(r.candidates[0].description == "SIDEWALK");
  CHECK(r.candidates[0].elevation.value_or(0) == Catch::Approx(100.12).margin(1e-6));
  CHECK(r.candidates[0].northing == Catch::Approx(303398.59).margin(1e-6));
  CHECK(r.candidates[0].easting == Catch::Approx(1887382.33).margin(1e-6));
  CHECK(r.candidates[0].IsSurveyPoint());

  CHECK(r.candidates[1].pointNumber.value_or(-1) == 2);
  CHECK(r.candidates[1].northing == Catch::Approx(303388.42).margin(1e-6));
  CHECK(r.candidates[1].easting == Catch::Approx(1887394.51).margin(1e-6));
}

TEST_CASE("PdfCoordExtract table skips a non-numeric footer row") {
  std::vector<DimText> texts = {
      T("Point #", 0, 100, 40, 110),
      T("Northing", 45, 100, 100, 110),
      T("Easting", 105, 100, 160, 110),
      T("1", 10, 85, 20, 95),
      T("303398.59", 50, 85, 95, 95),
      T("1887382.33", 110, 85, 155, 95),
      T("POINT TABLE TO BE REVISED", 10, 70, 150, 80),
  };
  ExtractResult r = ExtractCoordinates(texts, 0, 0, 300, 120);
  REQUIRE(r.status == ExtractStatus::Ok);
  REQUIRE(r.candidates.size() == 1);
  CHECK(r.candidates[0].pointNumber.value_or(-1) == 1);
}

TEST_CASE("PdfCoordExtract no selectable text reports the scanned-document message") {
  std::vector<DimText> texts; // nothing in the region
  ExtractResult r = ExtractCoordinates(texts, 0, 0, 100, 100);
  REQUIRE(r.status == ExtractStatus::NoText);
  CHECK(r.message.find("scanned") != std::string::npos);
  CHECK(r.message.find("OCR") != std::string::npos);
  CHECK(r.candidates.empty());
}

TEST_CASE("PdfCoordExtract text with no northing or easting is reported unparsed") {
  std::vector<DimText> texts = {T("RIGHT OF WAY LINE", 10, 10, 100, 20)};
  ExtractResult r = ExtractCoordinates(texts, 0, 0, 200, 50);
  REQUIRE(r.status == ExtractStatus::Unparsed);
  CHECK(r.candidates.empty());
}

TEST_CASE("PdfCoordExtract finds several separate bare-pair callouts in one dragged region") {
  // A dense structural sheet: four separate N/E callouts (corners of a dragged region) plus unrelated
  // dimension/elevation callout text scattered between them (e.g. "EL 101'-6"", "HELICAL PILE(TYP)"),
  // exactly like a user dragging a box over a whole busy plan instead of one tight callout.
  std::vector<DimText> texts = {
      // top-left pair
      T("N303401.741", 10, 490, 95, 500),
      T("E1887261.271", 10, 478, 95, 488),
      // unrelated filler well away from any pair
      T("EL 101'-6\"", 200, 470, 260, 480),
      T("HELICAL PILE(TYP)", 150, 380, 260, 390),
      T("4'-3\"", 300, 420, 330, 430),
      // top-right pair
      T("N303413.338", 500, 490, 585, 500),
      T("E1887672.742", 500, 478, 585, 488),
      // bottom-left pair
      T("N303372.448", 10, 40, 95, 50),
      T("E1887639.462", 10, 28, 95, 38),
      // bottom-right pair
      T("N303384.046", 500, 40, 585, 50),
      T("E1887680.934", 500, 28, 585, 38),
  };
  ExtractResult r = ExtractCoordinates(texts, 0, 0, 600, 520);
  REQUIRE(r.status == ExtractStatus::Ok);
  REQUIRE(r.candidates.size() == 4);
  std::vector<double> norths;
  for (const auto& c : r.candidates) {
    norths.push_back(c.northing);
    CHECK_FALSE(c.HasElevation()); // the "EL 101'-6"" dimension text must not attach to any of them
  }
  std::sort(norths.begin(), norths.end());
  CHECK(norths[0] == Catch::Approx(303372.448).margin(1e-3));
  CHECK(norths[1] == Catch::Approx(303384.046).margin(1e-3));
  CHECK(norths[2] == Catch::Approx(303401.741).margin(1e-3));
  CHECK(norths[3] == Catch::Approx(303413.338).margin(1e-3));
}

TEST_CASE("PdfCoordExtract ignores dense unrelated annotation text near real pairs") {
  // A busy structural sheet: four scattered N/E pairs (as on the real test sheet) surrounded by long
  // run-on dimension/annotation callouts that must never be read as a fifth pair, an elevation, or get
  // glued onto a real pair's description.
  std::vector<DimText> texts = {
      T("N303385.313", 10, 900, 95, 910),
      T("E1887572.524", 10, 888, 95, 898),
      T("BLDG COLUMN & (P2) C L HELICAL PILES 2'-2 1/2\" C L HELICAL PILES 5'-3 3/8\" EL. 101'-0\"", 150, 860, 500, 870),

      T("N303401.741", 300, 900, 385, 910),
      T("E1887631.271", 300, 888, 385, 898),

      T("N303429.767", 600, 900, 685, 910),
      T("E1887731.488", 600, 888, 685, 898),
      T("T.O. RAIL EL 100'-0\"", 400, 700, 500, 710),
      T("HELICAL PILE(TYP)", 450, 650, 560, 660),

      T("N303356.020", 10, 100, 95, 110),
      T("E1887580.716", 10, 88, 95, 98),
  };
  ExtractResult r = ExtractCoordinates(texts, 0, 0, 800, 950);
  REQUIRE(r.status == ExtractStatus::Ok);
  REQUIRE(r.candidates.size() == 4);
  for (const auto& c : r.candidates) {
    CHECK_FALSE(c.HasElevation());
    CHECK_FALSE(c.HasDescription());
  }
}

TEST_CASE("PdfCoordExtract only reads text whose box lies inside the picked region") {
  std::vector<DimText> texts = {
      T("N:303267.23", 10, 75, 100, 85),
      T("E:1887764.63", 10, 60, 100, 70),
      T("N:999999.00", 10, 200, 100, 210), // outside the region below
      T("E:111111.00", 10, 215, 100, 225),
  };
  ExtractResult r = ExtractCoordinates(texts, 0, 0, 200, 100);
  REQUIRE(r.status == ExtractStatus::Ok);
  REQUIRE(r.candidates.size() == 1);
  CHECK(r.candidates[0].northing == Catch::Approx(303267.23).margin(1e-6));
}
