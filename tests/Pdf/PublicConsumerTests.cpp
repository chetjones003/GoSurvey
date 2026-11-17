#include <catch2/catch_test_macros.hpp>

#include <GoSurvey/Pdf/Pdf.hpp>

TEST_CASE("GoSurvey Pdf public headers expose the standalone snap API") {
  pdfview::SnapIndex index;
  pdfview::SnapIndex::Pt point;
  REQUIRE_FALSE(index.Nearest(0.f, 0.f, 1.f, point));
}
