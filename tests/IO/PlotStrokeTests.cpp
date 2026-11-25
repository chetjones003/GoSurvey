// REQ-446: PDF plot lineweights and linetypes.
//
// The width and dash-unit rules are pure (io/PlotStroke.hpp). The dash runs the plot lays come from the same
// tessellator the on-screen viewport uses, so these also pin what PdfPlot strokes for a dashed or continuous line.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "commands/CadLinetype.hpp"
#include "io/src/PlotStroke.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr size_t kStride = 14;  ///< two vertices of xyz + rgba per dash
const float kBlack[4] = {0.f, 0.f, 0.f, 1.f};

/// Total drawn length of the dash runs in a tessellated buffer.
float DrawnLength(const std::vector<float>& v) {
  float total = 0.f;
  for (size_t k = 0; k + kStride - 1 < v.size(); k += kStride) {
    const float dx = v[k + 7] - v[k];
    const float dy = v[k + 8] - v[k + 1];
    total += std::sqrt(dx * dx + dy * dy);
  }
  return total;
}

}  // namespace

TEST_CASE("An unset lineweight plots at the historical 0.5 pt stroke (REQ-446)", "[plotstroke]") {
  using Catch::Matchers::WithinAbs;
  REQUIRE_THAT(plotstroke::WidthPt(-1.f), WithinAbs(0.5, 1e-6));
  REQUIRE(plotstroke::WidthKey(plotstroke::WidthPt(-1.f)) == 50u);
}

TEST_CASE("A 2 mm lineweight plots measurably thicker than the default line (REQ-446 acceptance 1)", "[plotstroke]") {
  using Catch::Matchers::WithinAbs;
  const float w = plotstroke::WidthPt(2.f);  // 2 mm = 5.67 pt
  REQUIRE_THAT(w, WithinAbs(2.f * 72.f / 25.4f, 1e-4));
  REQUIRE(w > 10.f * plotstroke::kDefaultWidthPt);
}

TEST_CASE("A lineweight of 0 mm is a zero-width stroke, not the default (REQ-446)", "[plotstroke]") {
  // 0 mm is an explicit lineweight; only a negative value means "not set".
  REQUIRE(plotstroke::WidthPt(0.f) == 0.f);
}

TEST_CASE("Equal widths share one stroke group, to 1/100 pt (REQ-446)", "[plotstroke]") {
  REQUIRE(plotstroke::WidthKey(0.499f) == plotstroke::WidthKey(0.5f));
  REQUIRE(plotstroke::WidthKey(0.25f * 72.f / 25.4f) != plotstroke::WidthKey(0.5f));
  REQUIRE(plotstroke::WidthKey(0.5f) == 50u);
}

TEST_CASE("The dash unit follows AutoCAD's viewport scale rule (REQ-446)", "[plotstroke]") {
  using Catch::Matchers::WithinAbs;
  // A pattern unit is LTSCALE (1) model units, shrunk by the viewport: at 1 paper inch = 50 model units it is 0.02 in.
  REQUIRE_THAT(plotstroke::DashUnitIn(50.f), WithinAbs(0.02, 1e-6));
  // A finer viewport (fewer model units per paper inch) draws dashes longer on paper.
  REQUIRE(plotstroke::DashUnitIn(10.f) > plotstroke::DashUnitIn(50.f));
  // The sheet itself uses one paper inch per pattern unit at LTSCALE 1.
  REQUIRE_THAT(plotstroke::kSheetDashUnitIn, WithinAbs(1.0, 1e-6));
  // A degenerate scale still yields a positive unit, so the dash tessellator always advances.
  REQUIRE(plotstroke::DashUnitIn(0.f) > 0.f);
}

TEST_CASE("A DASHED line plots with gaps and a CONTINUOUS line stays solid (REQ-446 acceptance 2)", "[plotstroke]") {
  using Catch::Matchers::WithinAbs;
  const float xy[] = {0.f, 0.f, 10.f, 0.f};

  std::vector<float> dashed;
  CadTessellateLinetypeChainVc(xy, 2, 0.f, false, "DASHED", 1.f, kBlack, &dashed);
  REQUIRE(dashed.size() / kStride > 1);  // several dashes, so there are gaps between them
  REQUIRE(DrawnLength(dashed) < 10.f - 1.f);
  REQUIRE(DrawnLength(dashed) > 0.f);

  std::vector<float> solid;
  CadTessellateLinetypeChainVc(xy, 2, 0.f, false, "Continuous", 1.f, kBlack, &solid);
  REQUIRE(solid.size() == kStride);  // one segment covering the whole line
  REQUIRE_THAT(DrawnLength(solid), WithinAbs(10.0, 1e-4));
}

TEST_CASE("A dashed chain keeps its pattern across joints (REQ-446)", "[plotstroke]") {
  using Catch::Matchers::WithinAbs;
  // The same 10-unit length as one chord and as two chords: the dash runs must add up identically, because the
  // plot lays dashes along the whole entity rather than restarting them at every chord.
  const float oneChord[] = {0.f, 0.f, 10.f, 0.f};
  const float twoChords[] = {0.f, 0.f, 4.f, 0.f, 10.f, 0.f};
  std::vector<float> a, b;
  CadTessellateLinetypeChainVc(oneChord, 2, 0.f, false, "DASHED", 1.f, kBlack, &a);
  CadTessellateLinetypeChainVc(twoChords, 3, 0.f, false, "DASHED", 1.f, kBlack, &b);
  REQUIRE_THAT(DrawnLength(b), WithinAbs(DrawnLength(a), 1e-3));
}

TEST_CASE("Clipped dashed chain bounds memory and preserves phase at extreme zoom", "[plotstroke][linetype]") {
  // Simulate zooming in deeply on one bolt hole beside a large CENTER bolt circle (radius 1000,
  // view half-extent 0.001, dashPatScale = 4.5e-5). Without clipping, tessellating the full
  // circumference would emit >100 million dash vertices (>5 GB RAM) and freeze the UI thread.
  constexpr int kSegs = 256;
  constexpr double kR = 1000.0;
  constexpr double kTwoPi = 6.283185307179586;
  std::vector<float> xy((kSegs + 1) * 2);
  for (int i = 0; i <= kSegs; ++i) {
    const double t = kTwoPi * static_cast<double>(i) / static_cast<double>(kSegs);
    // Anchor on the circle's right rim so a tiny slice passes through the origin clip square.
    xy[static_cast<size_t>(i * 2)] = static_cast<float>(kR * std::cos(t) - kR);
    xy[static_cast<size_t>(i * 2 + 1)] = static_cast<float>(kR * std::sin(t));
  }
  std::vector<float> verts;
  CadTessellateLinetypeChainVc(xy.data(), kSegs + 1, 0.f, true, "CENTER", 4.5e-5f, kBlack, &verts, nullptr, 0.002f);
  const size_t segments = verts.size() / kStride;
  REQUIRE(segments > 0);
  REQUIRE(segments < 500);

  // Dash phase across an off-screen lead-in must match the unclipped phase inside the window.
  const float longLine[] = {-50.f, 0.f, 50.f, 0.f};
  std::vector<float> clipped, unclipped;
  CadTessellateLinetypeChainVc(longLine, 2, 0.f, false, "DASHED", 1.f, kBlack, &clipped, nullptr, 2.f);
  CadTessellateLinetypeChainVc(longLine, 2, 0.f, false, "DASHED", 1.f, kBlack, &unclipped);
  REQUIRE(!clipped.empty());
  float unclippedInsideLen = 0.f;
  for (size_t i = 0; i + kStride <= unclipped.size(); i += kStride) {
    const float x0 = std::clamp(unclipped[i], -2.f, 2.f);
    const float x1 = std::clamp(unclipped[i + 7], -2.f, 2.f);
    unclippedInsideLen += std::fabs(x1 - x0);
  }
  REQUIRE_THAT(DrawnLength(clipped), Catch::Matchers::WithinAbs(unclippedInsideLen, 1e-3));
}

TEST_CASE("Linetype tessellator handles extreme ratios, cycle boundaries, and BORDER dots safely",
          "[plotstroke][linetype]") {
  // Unclipped pathological ratio must fall back instead of allocating unbounded memory.
  std::vector<float> segOut;
  CadTessellateLinetypeSegmentVc(0.f, 0.f, 0.f, 1e6f, 0.f, 0.f, "HIDDEN", 1e-5f, kBlack, &segOut);
  REQUIRE(segOut.size() == kStride);

  const float hugeChain[] = {0.f, 0.f, 1e6f, 0.f};
  std::vector<float> chainOut;
  CadTessellateLinetypeChainVc(hugeChain, 2, 0.f, false, "HIDDEN", 1e-5f, kBlack, &chainOut);
  REQUIRE(chainOut.size() == kStride);

  // BORDER includes a zero-length dot element; exact cycle multiples must not read out of bounds.
  std::vector<float> borderOut;
  const float borderLine[] = {0.f, 0.f, 5.25f, 0.f};
  CadTessellateLinetypeChainVc(borderLine, 2, 0.f, false, "BORDER", 1.f, kBlack, &borderOut);
  REQUIRE(borderOut.size() / kStride >= 4);
}
