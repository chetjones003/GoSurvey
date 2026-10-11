#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "commands/CadDimStroke.hpp"
#include "commands/PaperSpace.hpp"

#include <cmath>

static CadAnnotation MakeAlignedDim() {
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::DimAligned;
  a.dimExt1X = 0.f;
  a.dimExt1Y = 0.f;
  a.dimExt2X = 100.f;
  a.dimExt2Y = 0.f;
  a.dimSignedOffset = 20.f;
  a.insX = 50.f;
  a.insY = 25.f;
  a.rotationRad = 0.f;
  a.plottedHeightInches = 0.10f;
  a.text = "100.00";
  return a;
}

static CadAnnotation MakeLinearDim() {
  CadAnnotation a = MakeAlignedDim();
  a.kind = CadAnnotation::Kind::DimLinear;
  a.dimLinearVertical = false;
  a.dimExt1Y = 5.f;
  a.dimExt2Y = 15.f;
  return a;
}

static CadAnnotation MakeAngularDim() {
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::DimAngular;
  a.dimAngVertexX = 0.f;
  a.dimAngVertexY = 0.f;
  a.dimExt1X = 10.f;
  a.dimExt1Y = 0.f;
  a.dimExt2X = 0.f;
  a.dimExt2Y = 10.f;
  a.dimSignedOffset = 8.f;
  a.insX = 6.f;
  a.insY = 6.f;
  a.plottedHeightInches = 0.10f;
  a.text = "90d";
  return a;
}

static Viewport MakeVp(float scale, double cx, double cy) {
  Viewport vp;
  vp.paperXIn = 1.f;
  vp.paperYIn = 1.f;
  vp.paperWIn = 10.f;
  vp.paperHIn = 8.f;
  vp.modelCenterX = cx;
  vp.modelCenterY = cy;
  vp.scaleModelPerPaperIn = scale;
  return vp;
}

TEST_CASE("Aligned dimension strokes include extensions, dim line, arrows, and label", "[dim-viewport]") {
  CadDimStrokeParams p;
  p.modelUnitsPerPlottedInch = 50.f;
  CadDimWorldStrokes s;
  REQUIRE(CadDimBuildWorldStrokes(MakeAlignedDim(), p, &s));
  REQUIRE(s.ok);
  int nExt = 0, nDim = 0, nArr = 0;
  for (const CadDimWorldSeg& g : s.segs) {
    if (g.kind == CadDimWorldSeg::Kind::Extension)
      ++nExt;
    else if (g.kind == CadDimWorldSeg::Kind::DimLine)
      ++nDim;
    else
      ++nArr;
  }
  REQUIRE(nExt == 2);
  REQUIRE(nDim >= 1);
  REQUIRE(s.arrows.size() == 2);
  REQUIRE(s.labelX == Catch::Approx(50.f));
  REQUIRE(s.labelY == Catch::Approx(25.f));
}

TEST_CASE("Linear and angular dimensions produce strokes", "[dim-viewport]") {
  CadDimStrokeParams p;
  CadDimWorldStrokes lin, ang;
  REQUIRE(CadDimBuildWorldStrokes(MakeLinearDim(), p, &lin));
  REQUIRE(CadDimBuildWorldStrokes(MakeAngularDim(), p, &ang));
  REQUIRE(lin.segs.size() >= 3);
  REQUIRE(ang.segs.size() >= 3);
  int angExt = 0, angDim = 0;
  for (const CadDimWorldSeg& g : ang.segs) {
    if (g.kind == CadDimWorldSeg::Kind::Extension)
      ++angExt;
    else if (g.kind == CadDimWorldSeg::Kind::DimLine)
      ++angDim;
  }
  REQUIRE(angExt == 2);
  REQUIRE(angDim >= 1);
}

TEST_CASE("Degenerate dimension builds no strokes", "[dim-viewport]") {
  CadAnnotation a = MakeAlignedDim();
  a.dimExt2X = 0.f;
  CadDimWorldStrokes s;
  REQUIRE_FALSE(CadDimBuildWorldStrokes(a, CadDimStrokeParams{}, &s));
  REQUIRE_FALSE(s.ok);
}

TEST_CASE("Model dimension maps through a viewport like linework", "[dim-viewport]") {
  const Viewport vp = MakeVp(50.f, 50.0, 10.0);
  CadDimWorldStrokes s;
  REQUIRE(CadDimBuildWorldStrokes(MakeAlignedDim(), CadDimStrokeParams{}, &s));
  float px = 0.f, py = 0.f;
  ModelToPaperIn(vp, static_cast<double>(s.labelX), static_cast<double>(s.labelY), &px, &py);
  REQUIRE(px == Catch::Approx(6.f));  // center x 1+5, label at model 50 = center
  REQUIRE(py == Catch::Approx(5.3f)); // center y 1+4=5, +15 model units / 50 = +0.3
}

TEST_CASE("Viewport scale changes paper position of the same dimension", "[dim-viewport]") {
  CadDimWorldStrokes s;
  REQUIRE(CadDimBuildWorldStrokes(MakeAlignedDim(), CadDimStrokeParams{}, &s));
  const Viewport a = MakeVp(50.f, 50.0, 25.0);
  const Viewport b = MakeVp(100.f, 50.0, 25.0);
  float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
  ModelToPaperIn(a, s.labelX, s.labelY, &ax, &ay);
  ModelToPaperIn(b, s.labelX, s.labelY, &bx, &by);
  REQUIRE(ax == Catch::Approx(bx));
  REQUIRE(ay == Catch::Approx(by));
  float p50 = 0.f, q50 = 0.f, p100 = 0.f, q100 = 0.f;
  ModelToPaperIn(a, 50.0, 25.0 + 50.0, &p50, &q50);
  ModelToPaperIn(b, 50.0, 25.0 + 50.0, &p100, &q100);
  REQUIRE(std::fabs(q50 - 5.f) == Catch::Approx(1.f));
  REQUIRE(std::fabs(q100 - 5.f) == Catch::Approx(0.5f));
}

TEST_CASE("Two viewports transform the same dimension independently", "[dim-viewport]") {
  CadDimWorldStrokes s;
  REQUIRE(CadDimBuildWorldStrokes(MakeAlignedDim(), CadDimStrokeParams{}, &s));
  Viewport left = MakeVp(50.f, 50.0, 25.0);
  Viewport right = MakeVp(50.f, 50.0, 25.0);
  right.paperXIn = 12.f;
  float lx = 0.f, ly = 0.f, rx = 0.f, ry = 0.f;
  ModelToPaperIn(left, s.labelX, s.labelY, &lx, &ly);
  ModelToPaperIn(right, s.labelX, s.labelY, &rx, &ry);
  REQUIRE(rx - lx == Catch::Approx(11.f));
  REQUIRE(ly == Catch::Approx(ry));
}

TEST_CASE("Dimension segments outside the viewport rect are clipped away", "[dim-viewport]") {
  Viewport vp = MakeVp(50.f, 0.0, 0.0);
  float x0 = 1000.f, y0 = 0.f, x1 = 1100.f, y1 = 0.f;
  float px0 = 0.f, py0 = 0.f, px1 = 0.f, py1 = 0.f;
  ModelToPaperIn(vp, x0, y0, &px0, &py0);
  ModelToPaperIn(vp, x1, y1, &px1, &py1);
  const float vx0 = vp.paperXIn, vy0 = vp.paperYIn;
  const float vx1 = vp.paperXIn + vp.paperWIn, vy1 = vp.paperYIn + vp.paperHIn;
  REQUIRE_FALSE(CadClipSegToRect(vx0, vy0, vx1, vy1, px0, py0, px1, py1));
}

TEST_CASE("Dimension segment crossing the viewport edge is clipped to the rect", "[dim-viewport]") {
  Viewport vp = MakeVp(1.f, 5.0, 5.0);
  float px0 = 0.f, py0 = 0.f, px1 = 0.f, py1 = 0.f;
  ModelToPaperIn(vp, -10.0, 5.0, &px0, &py0);
  ModelToPaperIn(vp, 20.0, 5.0, &px1, &py1);
  const float vx0 = vp.paperXIn, vy0 = vp.paperYIn;
  const float vx1 = vp.paperXIn + vp.paperWIn, vy1 = vp.paperYIn + vp.paperHIn;
  REQUIRE(CadClipSegToRect(vx0, vy0, vx1, vy1, px0, py0, px1, py1));
  REQUIRE(px0 == Catch::Approx(vx0));
  REQUIRE(px1 == Catch::Approx(vx1));
}

TEST_CASE("Pan (model center) moves dimension paper coordinates", "[dim-viewport]") {
  CadDimWorldStrokes s;
  REQUIRE(CadDimBuildWorldStrokes(MakeAlignedDim(), CadDimStrokeParams{}, &s));
  Viewport a = MakeVp(50.f, 50.0, 25.0);
  Viewport b = MakeVp(50.f, 100.0, 25.0);
  float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
  ModelToPaperIn(a, s.labelX, s.labelY, &ax, &ay);
  ModelToPaperIn(b, s.labelX, s.labelY, &bx, &by);
  REQUIRE(ax - bx == Catch::Approx(1.f));  // 50 model units / 50 scale = 1 paper inch
  REQUIRE(ay == Catch::Approx(by));
}

TEST_CASE("TEXT annotations are not dimension strokes", "[dim-viewport]") {
  CadAnnotation t;
  t.kind = CadAnnotation::Kind::Text;
  t.text = "A";
  CadDimWorldStrokes s;
  REQUIRE_FALSE(CadDimBuildWorldStrokes(t, CadDimStrokeParams{}, &s));
}
