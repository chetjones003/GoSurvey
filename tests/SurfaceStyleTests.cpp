// REQ-070 — the surface style table (`commands/SurfaceStyle.hpp`, ADR-036 (d)).
//
// Three of REQ-070's five acceptance conditions are decided entirely in this header, before any
// geometry is generated:
//   * "a surface whose style was deleted falls back to a default style rather than failing to draw";
//   * "a legacy `.gs` loads unchanged" — every surface in one carries an EMPTY styleName, which must
//     take the same fallback path as a deleted one;
//   * "a major interval that is not a whole multiple of the minor interval is rejected with a
//     specific message rather than producing mis-labelled contours".
//
// The last one is asserted on the MESSAGE, not merely on the rejection. A bare `false` satisfies the
// letter of the condition and leaves the user with an interval that will not take and no way to find
// out why, which is the outcome REQ-201 exists to forbid.

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>
#include <vector>

#include "commands/SurfaceStyle.hpp"

namespace {

/// A style that is deliberately not "Standard", so a fallback that returns the right *value* by
/// accident cannot pass a test that meant to check WHICH row came back.
SurfaceStyle NamedStyle(const std::string& name) {
  SurfaceStyle s = SurfaceStyles::StandardSurfaceStyle();
  s.name = name;
  s.minorIntervalFt = 1.0;
  s.majorIntervalFt = 5.0;
  s.triangles.visible = false;
  return s;
}

/// True when \p msg names both intervals, so a rejection tells the user what it rejected rather than
/// only that it rejected something.
bool Mentions(const std::string& msg, const std::string& needle) {
  return msg.find(needle) != std::string::npos;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The table: what exists, and what an unresolved name resolves to
// ---------------------------------------------------------------------------------------------

TEST_CASE("A new drawing's style table is exactly Standard", "[surface][req070][style]") {
  const auto styles = SurfaceStyles::DefaultSurfaceStyles();
  REQUIRE(styles.size() == 1);
  CHECK(styles[0].name == SurfaceStyles::kStandardName);
}

TEST_CASE("Standard opens a surface as a contour map, not a triangle mesh",
          "[surface][req070][style]") {
  // The Civil 3D default, chosen by the user on 2026-08-21: border plus both contour sets, triangles
  // OFF. The triangulation is the model; the contours are what a topo plan is for.
  //
  // Pinned rather than left implicit because it is a DELIBERATE change of appearance for drawings
  // that already contain a surface — before REQ-070 a surface drew as its triangle edges and nothing
  // else. A later reader who assumed that was an oversight and "fixed" it would silently undo a
  // decision, and this case is what stops them.
  const SurfaceStyle s = SurfaceStyles::StandardSurfaceStyle();
  CHECK_FALSE(s.triangles.visible);
  CHECK(s.border.visible);
  CHECK(s.minorContour.visible);
  CHECK(s.majorContour.visible);
  CHECK_FALSE(s.points.visible);  // the source points are already drawn as survey points

  // And the pair it ships with must satisfy the rule the editor will enforce, or a brand-new drawing
  // would open in a state its own dialog refuses.
  std::string why = "seeded";
  CHECK(SurfaceStyles::IntervalsCompatible(s.minorIntervalFt, s.majorIntervalFt, &why));
  CHECK(why.empty());
}

TEST_CASE("EnsureStandard inserts once and is idempotent", "[surface][req070][style]") {
  std::vector<SurfaceStyle> styles{NamedStyle("Contours 1ft")};

  SurfaceStyles::EnsureStandard(styles);
  REQUIRE(styles.size() == 2);
  CHECK(styles.front().name == SurfaceStyles::kStandardName);  // inserted at the front

  SurfaceStyles::EnsureStandard(styles);
  CHECK(styles.size() == 2);  // a second call adds nothing
}

TEST_CASE("EnsureStandard does not overwrite an edited Standard", "[surface][req070][style]") {
  // A user who edits Standard and reopens the drawing must get their edit back, not the built-in
  // defaults reapplied over it. This is the difference between "guarantee the row exists" and
  // "reset the row", and the loader calls this on every open.
  std::vector<SurfaceStyle> styles = SurfaceStyles::DefaultSurfaceStyles();
  styles[0].minorIntervalFt = 0.5;
  styles[0].triangles.visible = false;

  SurfaceStyles::EnsureStandard(styles);
  REQUIRE(styles.size() == 1);
  CHECK(styles[0].minorIntervalFt == 0.5);
  CHECK_FALSE(styles[0].triangles.visible);
}

TEST_CASE("A surface resolves the style it names", "[surface][req070][style]") {
  std::vector<SurfaceStyle> styles = SurfaceStyles::DefaultSurfaceStyles();
  styles.push_back(NamedStyle("Contours 1ft"));

  const SurfaceStyle* got = SurfaceStyles::Resolve(styles, "Contours 1ft");
  REQUIRE(got != nullptr);
  CHECK(got->name == "Contours 1ft");
  CHECK(got->minorIntervalFt == 1.0);
}

TEST_CASE("A deleted style falls back to Standard rather than failing to draw",
          "[surface][req070][style]") {
  // REQ-070's acceptance, stated verbatim. The surface keeps its styleName — it is not rewritten
  // behind the user's back — and simply draws with the default until the name resolves again.
  std::vector<SurfaceStyle> styles = SurfaceStyles::DefaultSurfaceStyles();

  const SurfaceStyle* got = SurfaceStyles::Resolve(styles, "Deleted By Someone");
  REQUIRE(got != nullptr);
  CHECK(got->name == SurfaceStyles::kStandardName);
}

TEST_CASE("An empty style name resolves to Standard", "[surface][req070][style]") {
  // This is the legacy case, and it is the one that runs on EVERY surface in EVERY drawing written
  // before this field existed. If it returned nullptr, those surfaces would stop drawing on the day
  // the feature shipped — the exact opposite of "a legacy `.gs` loads unchanged".
  const auto styles = SurfaceStyles::DefaultSurfaceStyles();

  const SurfaceStyle* got = SurfaceStyles::Resolve(styles, "");
  REQUIRE(got != nullptr);
  CHECK(got->name == SurfaceStyles::kStandardName);
}

TEST_CASE("Resolving against an empty table yields nothing rather than a dangling read",
          "[surface][req070][style]") {
  // The table is never empty in a live drawing — SyncDrawingLayerTableWithGeometry guarantees
  // Standard on every route in. Asserted anyway, because "cannot happen" is how a null deref gets
  // written, and the display pass dereferences whatever this returns.
  const std::vector<SurfaceStyle> empty;
  CHECK(SurfaceStyles::Resolve(empty, "") == nullptr);
  CHECK(SurfaceStyles::Resolve(empty, "Anything") == nullptr);
}

TEST_CASE("A table with no Standard still resolves to something drawable",
          "[surface][req070][style]") {
  // Belt and braces for the same reason: the fallback chain ends at the first row, not at nullptr,
  // so a table that somehow lost Standard still draws.
  const std::vector<SurfaceStyle> styles{NamedStyle("Only One")};
  const SurfaceStyle* got = SurfaceStyles::Resolve(styles, "Missing");
  REQUIRE(got != nullptr);
  CHECK(got->name == "Only One");
}

// ---------------------------------------------------------------------------------------------
// The interval rule — REQ-070's fourth acceptance condition
// ---------------------------------------------------------------------------------------------

TEST_CASE("A major interval that is a whole multiple of the minor is accepted",
          "[surface][req070][style]") {
  std::string why = "seeded";
  CHECK(SurfaceStyles::IntervalsCompatible(2.0, 10.0, &why));
  CHECK(why.empty());  // an accepted pair leaves no stale message behind for a caller to display

  CHECK(SurfaceStyles::IntervalsCompatible(1.0, 5.0, &why));
  CHECK(SurfaceStyles::IntervalsCompatible(5.0, 5.0, &why));  // 1x is a whole multiple
  CHECK(SurfaceStyles::IntervalsCompatible(0.5, 2.0, &why));
  CHECK(SurfaceStyles::IntervalsCompatible(100.0, 500.0, &why));
}

TEST_CASE("The multiple test survives intervals that are inexact in binary",
          "[surface][req070][style]") {
  // The reason the rule is a ratio with a tolerance and not `std::fmod(major, minor) == 0`:
  // std::fmod(0.5, 0.1) is 0.09999999999999995, so an exact test rejects a 0.1/0.5 pair that is
  // obviously correct — and a surveyor working in tenths would be told their intervals are invalid
  // with no way to satisfy the rule.
  std::string why;
  CHECK(SurfaceStyles::IntervalsCompatible(0.1, 0.5, &why));
  CHECK(SurfaceStyles::IntervalsCompatible(0.1, 1.0, &why));
  CHECK(SurfaceStyles::IntervalsCompatible(0.2, 0.6, &why));
  CHECK(SurfaceStyles::IntervalsCompatible(0.3, 0.9, &why));
}

TEST_CASE("A major interval that is not a whole multiple is rejected with a specific message",
          "[surface][req070][style]") {
  // The acceptance condition, and the message is half of it. A rejection that says only "invalid"
  // leaves the user guessing at which of the two numbers is wrong.
  std::string why;
  REQUIRE_FALSE(SurfaceStyles::IntervalsCompatible(2.0, 7.0, &why));

  CHECK_FALSE(why.empty());
  CHECK(Mentions(why, "7"));  // the value it rejected
  CHECK(Mentions(why, "2"));  // the value it rejected it against
  // And what WOULD work, so the message is actionable: 7 sits between 6 and 8.
  CHECK(Mentions(why, "6"));
  CHECK(Mentions(why, "8"));
}

TEST_CASE("A rejection message carries no floating-point noise", "[surface][req070][style]") {
  // The message is read by a person. "0.5" and "2", never "0.500000" or "5e-01" — the number the
  // user typed, in the form they typed it.
  std::string why;
  REQUIRE_FALSE(SurfaceStyles::IntervalsCompatible(0.5, 1.25, &why));
  CHECK(Mentions(why, "1.25"));
  CHECK(Mentions(why, "0.5"));
  CHECK_FALSE(Mentions(why, "e-"));
  CHECK_FALSE(Mentions(why, "0.500000"));
}

TEST_CASE("A major interval smaller than the minor is rejected on its own terms",
          "[surface][req070][style]") {
  // Reported as the ordering mistake it is rather than as a failed multiple test: "10 is not a whole
  // multiple of 2" would be a true statement about the wrong problem when the user has swapped two
  // fields.
  std::string why;
  REQUIRE_FALSE(SurfaceStyles::IntervalsCompatible(10.0, 2.0, &why));
  CHECK(Mentions(why, "at least as large"));
}

TEST_CASE("A non-positive or non-finite interval is rejected", "[surface][req070][style]") {
  std::string why;
  CHECK_FALSE(SurfaceStyles::IntervalsCompatible(0.0, 10.0, &why));
  CHECK(Mentions(why, "minor"));
  CHECK_FALSE(SurfaceStyles::IntervalsCompatible(-2.0, 10.0, &why));
  CHECK_FALSE(SurfaceStyles::IntervalsCompatible(2.0, 0.0, &why));
  CHECK(Mentions(why, "major"));
  CHECK_FALSE(SurfaceStyles::IntervalsCompatible(2.0, -10.0, &why));

  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  CHECK_FALSE(SurfaceStyles::IntervalsCompatible(nan, 10.0, &why));
  CHECK_FALSE(SurfaceStyles::IntervalsCompatible(2.0, nan, &why));
  CHECK_FALSE(SurfaceStyles::IntervalsCompatible(inf, 10.0, &why));
  CHECK_FALSE(SurfaceStyles::IntervalsCompatible(2.0, inf, &why));
}

TEST_CASE("IntervalsCompatible tolerates a null message pointer", "[surface][req070][style]") {
  // The check is also useful as a plain predicate — a guard on the display path has no message to
  // show — so refusing without somewhere to put the reason must not crash.
  CHECK(SurfaceStyles::IntervalsCompatible(2.0, 10.0, nullptr));
  CHECK_FALSE(SurfaceStyles::IntervalsCompatible(2.0, 7.0, nullptr));
}

// ---------------------------------------------------------------------------------------------
// Value equality — the display cache's staleness key rides on it
// ---------------------------------------------------------------------------------------------

TEST_CASE("Style equality notices a change in any component or interval",
          "[surface][req070][style]") {
  // The display-geometry cache compares the RESOLVED STYLE to decide whether generated contours are
  // stale (ADR-036 (e)). An operator== that missed a field would leave the old contours on screen
  // after an edit — REQ-070's "two surfaces sharing a style both change when the style is edited",
  // failing silently and looking like the edit never took.
  const SurfaceStyle base = SurfaceStyles::StandardSurfaceStyle();

  CHECK(base == SurfaceStyles::StandardSurfaceStyle());

  auto differs = [&](void (*mutate)(SurfaceStyle&)) {
    SurfaceStyle s = base;
    mutate(s);
    return s != base;
  };

  CHECK(differs([](SurfaceStyle& s) { s.name = "Other"; }));
  CHECK(differs([](SurfaceStyle& s) { s.minorIntervalFt = 1.0; }));
  CHECK(differs([](SurfaceStyle& s) { s.majorIntervalFt = 20.0; }));
  CHECK(differs([](SurfaceStyle& s) { s.triangles.visible = !s.triangles.visible; }));
  CHECK(differs([](SurfaceStyle& s) { s.border.color = "Red"; }));
  CHECK(differs([](SurfaceStyle& s) { s.majorContour.linetype = "DASHED"; }));
  CHECK(differs([](SurfaceStyle& s) { s.minorContour.lineweightMm = 0.7f; }));
  CHECK(differs([](SurfaceStyle& s) { s.points.visible = !s.points.visible; }));

  // REQ-072's analysis fields are part of the SAME staleness key, and for the same reason: a band
  // edited without the cache noticing leaves the surface painted in its old colours, which reads as
  // the edit never having taken — and the legend beside it would then be describing colours that are
  // no longer on screen.
  CHECK(differs([](SurfaceStyle& s) { s.analysisMode = SurfaceAnalysisMode::Elevation; }));
  CHECK(differs([](SurfaceStyle& s) { s.slopeArrowsOn = true; }));
  CHECK(differs([](SurfaceStyle& s) { s.bands.push_back(SurfaceBand{100.0, "Red"}); }));
  CHECK(differs([](SurfaceStyle& s) { s.arrowBands.push_back(SurfaceBand{25.0, "Blue"}); }));

  // Both halves of a band, not just how many there are: recolouring a band and moving its edge are
  // the two edits a user actually makes on the Analysis tab, and neither changes the band COUNT.
  SurfaceStyle banded = base;
  banded.bands = {SurfaceBand{100.0, "Red"}, SurfaceBand{110.0, "Blue"}};

  SurfaceStyle recoloured = banded;
  recoloured.bands[1].color = "Green";
  CHECK(recoloured != banded);

  SurfaceStyle moved = banded;
  moved.bands[1].upperBound = 111.0;
  CHECK(moved != banded);
}

TEST_CASE("A style starts with analysis off, and that is the legacy state too",
          "[surface][req072][style]") {
  // REQ-072's "turning banding off restores the style's plain display unchanged" is satisfied by
  // making OFF the state a style begins in. A `.gs` written before REQ-072 existed carries none of
  // these keys, and the reader seeds each style from this same default — so an old drawing opens
  // displaying exactly what it displayed before, without the reader needing a legacy branch.
  const SurfaceStyle standard = SurfaceStyles::StandardSurfaceStyle();

  CHECK(standard.analysisMode == SurfaceAnalysisMode::None);
  CHECK(standard.bands.empty());
  CHECK_FALSE(standard.slopeArrowsOn);
  CHECK(standard.arrowBands.empty());

  for (const SurfaceStyle& s : SurfaceStyles::DefaultSurfaceStyles()) {
    INFO("style " << s.name);
    CHECK(s.analysisMode == SurfaceAnalysisMode::None);
    CHECK(s.bands.empty());
  }
}
