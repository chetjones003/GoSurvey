// Issue #917: survey-point label boxes are measured by a callback the frontend installs on
// AppCommandState (Qt text layout), so the shared survey code stays UI-independent. This pins
// the two sides of that seam: the callback is what sizes the box, and with no callback the label
// still gets a sane no-font estimate instead of a crash or a zero-size pick target.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "SurveyPoints.hpp"

namespace {

struct Capture {
  static inline int calls = 0;
  static inline float lastPx = 0.f;
  static inline std::string lastText;
};

void WideMeasurer(float fontPx, const std::string& text, const std::string&, float* w, float* h) {
  ++Capture::calls;
  Capture::lastPx = fontPx;
  Capture::lastText = text;
  *w = 400.f;
  *h = 40.f;
}

const CadAnnotation* LabelOfFirstPoint(const AppCommandState& st) {
  REQUIRE_FALSE(st.surveyPoints.empty());
  const int ix = FindSurveyLabelAnnIndex(st, st.surveyPoints[0]);
  REQUIRE(ix >= 0);
  return &st.cadAnnotations[static_cast<size_t>(ix)];
}

float BoxWidth(const CadAnnotation& a) { return a.boxMaxX - a.boxMinX; }

}  // namespace

TEST_CASE("Survey label measurement: a new state has no measurer installed", "[issue917]") {
  AppCommandState st;
  CHECK(st.measureAnnotationText == nullptr);
}

TEST_CASE("Survey label measurement: without a measurer the label still gets a positive box", "[issue917]") {
  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(TryPlaceSurveyPoint(st, 10.0, 20.0, 5.0, log));
  const CadAnnotation* label = LabelOfFirstPoint(st);
  CHECK(BoxWidth(*label) > 0.f);
  CHECK(label->boxMaxY > label->boxMinY);
}

TEST_CASE("Survey label measurement: the installed measurer sizes the label box", "[issue917]") {
  float fallbackWidth = 0.f;
  {
    AppCommandState st;
    std::vector<std::string> log;
    REQUIRE(TryPlaceSurveyPoint(st, 10.0, 20.0, 5.0, log));
    fallbackWidth = BoxWidth(*LabelOfFirstPoint(st));
  }

  Capture::calls = 0;
  AppCommandState st;
  st.measureAnnotationText = WideMeasurer;
  std::vector<std::string> log;
  REQUIRE(TryPlaceSurveyPoint(st, 10.0, 20.0, 5.0, log));
  const CadAnnotation* label = LabelOfFirstPoint(st);

  CHECK(Capture::calls > 0);
  CHECK(Capture::lastPx > 0.f);
  CHECK(Capture::lastText == label->text);  // measured through the string the renderer draws
  CHECK(BoxWidth(*label) > fallbackWidth);  // the 400 px measurement beat the 8 px estimate
}
