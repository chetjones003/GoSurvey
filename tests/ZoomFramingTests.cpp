// Zoom framing (REQ-122 / GitHub issue #88).
//
// Issue #88's ZOOMEXTENTS acceptance list is almost entirely a claim about ONE piece of arithmetic:
// given a world rectangle and a viewport aspect, what camera shows it? Margin, aspect handling, edge
// clipping, degenerate extents and NaN safety are all decided in `zoomframing::FrameWorldRect` and
// nowhere else, which is why that math was hoisted into a header — `ProcessPendingViewportZoom`
// itself is unreachable from any test target (it early-returns on `fbW <= 0`, and the headless
// driver models no framebuffer; TASK-113 DEBT-1).
//
// Every assertion below is written against the VISIBLE RECT, not against the internals: the camera
// shows `halfH = 50 / zoom` and `halfW = halfH * aspect` about `(panX, panY)`, which is
// `Camera::orthoHalfH`'s own documented relationship. Testing the visible rect is what makes these
// tests about the user-facing guarantee ("is my drawing on screen, with room around it?") rather
// than about the formula that happens to produce it.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

#include "ZoomFraming.hpp"

using Catch::Approx;

namespace {

struct View {
  double cx = 0.;
  double cy = 0.;
  double halfW = 0.;
  double halfH = 0.;
};

// The rect the camera actually shows, derived the way the renderer derives it.
View VisibleRect(double panX, double panY, float zoom, float aspect) {
  View v;
  v.cx = panX;
  v.cy = panY;
  v.halfH = static_cast<double>(zoomframing::kOrthoHalfHRef) / static_cast<double>(zoom);
  v.halfW = v.halfH * static_cast<double>(aspect);
  return v;
}

bool Frame(double mnX, double mxX, double mnY, double mxY, float aspect, View* out) {
  double panX = 12345.;  // sentinel: a refusal must leave these untouched
  double panY = -6789.;
  float zoom = 3.5f;
  const bool ok = zoomframing::FrameWorldRect(mnX, mxX, mnY, mxY, aspect, &panX, &panY, &zoom);
  if (ok && out)
    *out = VisibleRect(panX, panY, zoom, aspect);
  return ok;
}

}  // namespace

TEST_CASE("Framing centres the viewport on the drawing extents", "[zoom][extents]") {
  View v;
  REQUIRE(Frame(100., 400., -50., 250., 16.f / 9.f, &v));
  CHECK(v.cx == Approx(250.));  // 0.5 * (100 + 400)
  CHECK(v.cy == Approx(100.));  // 0.5 * (-50 + 250)
}

TEST_CASE("The whole drawing fits inside the viewport, at any aspect", "[zoom][extents]") {
  // Both orientations at three aspects: wide, square and tall. A formula that quietly assumes a
  // square viewport passes the square case and clips one of the other two.
  const float aspects[] = {2.5f, 1.f, 0.4f};
  const double rects[][4] = {
      {0., 450., 0., 350.},      // wider than tall
      {0., 40., 0., 900.},       // much taller than wide
      {-1000., -900., 20., 40.}, // away from the origin, wide
  };
  for (float aspect : aspects) {
    for (const auto& r : rects) {
      View v;
      REQUIRE(Frame(r[0], r[1], r[2], r[3], aspect, &v));
      CHECK(r[0] >= v.cx - v.halfW);
      CHECK(r[1] <= v.cx + v.halfW);
      CHECK(r[2] >= v.cy - v.halfH);
      CHECK(r[3] <= v.cy + v.halfH);
    }
  }
}

TEST_CASE("A margin is left on every side, so geometry never touches an edge", "[zoom][extents]") {
  View v;
  REQUIRE(Frame(0., 450., 0., 350., 16.f / 9.f, &v));
  // The binding axis carries exactly kMarginFraction of slack, half of it per side; the other axis
  // carries at least that much. Expressed as a fraction of the visible span so it reads as "how much
  // of the screen is empty", which is what the acceptance condition is about.
  const double freeX = 1.0 - (450. - 0.) / (2. * v.halfW);
  const double freeY = 1.0 - (350. - 0.) / (2. * v.halfH);
  CHECK(freeX >= Approx(static_cast<double>(zoomframing::kMarginFraction)).margin(1e-9));
  CHECK(freeY >= Approx(static_cast<double>(zoomframing::kMarginFraction)).margin(1e-9));
  // ...and one of the two is exactly the margin, so the fit is not sloppier than it claims.
  CHECK(std::min(freeX, freeY) == Approx(static_cast<double>(zoomframing::kMarginFraction)));
}

TEST_CASE("The aspect ratio decides which axis binds", "[zoom][extents]") {
  // The same square rect in a wide viewport is height-bound; in a tall viewport it is width-bound.
  // If aspect were ignored, both would produce the same zoom.
  View wide;
  View tall;
  REQUIRE(Frame(0., 100., 0., 100., 4.f, &wide));
  REQUIRE(Frame(0., 100., 0., 100., 0.25f, &tall));
  CHECK(wide.halfH == Approx(100. / (2. * (1. - zoomframing::kMarginFraction))));
  CHECK(tall.halfW == Approx(100. / (2. * (1. - zoomframing::kMarginFraction))));
  CHECK(wide.halfH < tall.halfH);  // the tall viewport must pull back to fit the width
}

TEST_CASE("A single point frames at a usable scale, not an unusable one", "[zoom][extents]") {
  // The regression this whole requirement exists for. The old 1e-5 degeneracy pad framed a point at
  // zoom ~4.6e6 — a view a fifth of a thousandth of a unit tall. The floor must produce a view on
  // the order of the drawing's working scale instead.
  double panX = 0.;
  double panY = 0.;
  float zoom = 0.f;
  REQUIRE(zoomframing::FrameWorldRect(75., 75., -20., -20., 1.5f, &panX, &panY, &zoom));
  CHECK(panX == Approx(75.));
  CHECK(panY == Approx(-20.));
  const View v = VisibleRect(panX, panY, zoom, 1.5f);
  // Absolute bounds, deliberately NOT expressed in terms of kMinFrameSpan: a test written against
  // the constant passes for any value of it, including the 1e-5 that made this unusable.
  CHECK(2. * v.halfH >= 0.5);
  CHECK(zoom < 200.f);  // the old behaviour was ~9.2e6 here
}

TEST_CASE("Very small and identical-coordinate extents are floored to the minimum span",
          "[zoom][extents]") {
  // A hair-length line, a zero-height row of objects, and a pair of coincident objects — issue #88's
  // three degenerate examples. Each must end up showing at least the minimum span on the short axis
  // while still being centred on the content.
  struct Case {
    double mnX, mxX, mnY, mxY;
  };
  const Case cases[] = {
      {0., 1e-6, 0., 1e-6},   // a hair-length diagonal
      {10., 60., 5., 5.},     // zero height, real width
      {-3., -3., -3., -3.},   // identical coordinates
  };
  for (const Case& c : cases) {
    View v;
    REQUIRE(Frame(c.mnX, c.mxX, c.mnY, c.mxY, 1.6f, &v));
    CHECK(v.cx == Approx(0.5 * (c.mnX + c.mxX)));
    CHECK(v.cy == Approx(0.5 * (c.mnY + c.mxY)));
    // Absolute, for the reason given above: at least half a world unit visible on each axis.
    CHECK(2. * v.halfH >= 0.5);
    CHECK(2. * v.halfW >= 0.5);
  }
}

TEST_CASE("A real drawing is not affected by the minimum-span floor", "[zoom][extents]") {
  // The floor must be invisible above it: a 450 x 350 site frames exactly as it did before.
  View v;
  REQUIRE(Frame(0., 450., 0., 350., 16.f / 9.f, &v));
  // 16:9 is wide enough that the HEIGHT binds: 350 / 2(1 - margin).
  const double expectHalfH = 350. / (2. * (1. - zoomframing::kMarginFraction));
  CHECK(v.halfH == Approx(expectHalfH));
}

TEST_CASE("Non-finite extents are refused and the camera is left untouched", "[zoom][extents]") {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  const double huge = 1e308;

  struct Case {
    double mnX, mxX, mnY, mxY;
    float aspect;
  };
  const Case cases[] = {
      {nan, 10., 0., 10., 1.5f},
      {0., nan, 0., 10., 1.5f},
      {0., 10., nan, 10., 1.5f},
      {0., 10., 0., nan, 1.5f},
      {-inf, 10., 0., 10., 1.5f},
      {0., inf, 0., 10., 1.5f},
      {-huge, huge, 0., 10., 1.5f},  // both finite, their difference is not
      {0., 10., 0., 10., nan},       // a NaN aspect (a viewport mid-resize)
      {0., 10., 0., 10., 0.f},       // a zero-width viewport
  };
  for (const Case& c : cases) {
    double panX = 12345.;
    double panY = -6789.;
    float zoom = 3.5f;
    CHECK_FALSE(zoomframing::FrameWorldRect(c.mnX, c.mxX, c.mnY, c.mxY, c.aspect, &panX, &panY, &zoom));
    // The point of refusing: the caller's existing camera survives, rather than becoming NaN.
    CHECK(panX == 12345.);
    CHECK(panY == -6789.);
    CHECK(zoom == 3.5f);
  }
}

TEST_CASE("Every accepted rect produces finite camera values", "[zoom][extents]") {
  // The complement of the refusal test: across the full range of shapes the app can hand it, the
  // written camera is always finite and always inside the zoom clamp.
  const double spans[] = {1e-9, 1e-3, 1., 1e3, 1e6, 1e12};
  const float aspects[] = {0.05f, 1.f, 20.f};
  for (double s : spans) {
    for (float aspect : aspects) {
      double panX = 0.;
      double panY = 0.;
      float zoom = 0.f;
      REQUIRE(zoomframing::FrameWorldRect(-s, s, -0.5 * s, 0.5 * s, aspect, &panX, &panY, &zoom));
      CHECK(std::isfinite(panX));
      CHECK(std::isfinite(panY));
      CHECK(std::isfinite(zoom));
      CHECK(zoom > 0.f);
      CHECK(zoom <= 1.e9f);
    }
  }
}

TEST_CASE("An inverted rect frames the same view as the ordered one", "[zoom][extents]") {
  // ZOOM WINDOW's corners arrive in whatever order the user dragged them.
  View ordered;
  View flipped;
  REQUIRE(Frame(10., 90., 20., 60., 1.4f, &ordered));
  REQUIRE(Frame(90., 10., 60., 20., 1.4f, &flipped));
  CHECK(flipped.cx == Approx(ordered.cx));
  CHECK(flipped.cy == Approx(ordered.cy));
  CHECK(flipped.halfH == Approx(ordered.halfH));
}

TEST_CASE("Null out-parameters are refused rather than dereferenced", "[zoom][extents]") {
  double pan = 0.;
  float zoom = 1.f;
  CHECK_FALSE(zoomframing::FrameWorldRect(0., 10., 0., 10., 1.f, nullptr, &pan, &zoom));
  CHECK_FALSE(zoomframing::FrameWorldRect(0., 10., 0., 10., 1.f, &pan, nullptr, &zoom));
  CHECK_FALSE(zoomframing::FrameWorldRect(0., 10., 0., 10., 1.f, &pan, &pan, nullptr));
}
