// REQ-394: testing a page's scale against dimensions the drawing already states - the value parser, the
// Good / Check / Blunder verdicts, the best fit and its outliers, the user's correction and the opt-in robust
// (weighted least squares) calibration. Pure maths, no PDF, no window.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <catch2/catch_test_macros.hpp>

#include "pdf/PdfAnnotate.hpp"
#include "pdf/PdfScaleCheck.hpp"

#include <cmath>

using namespace pdfview;

namespace {

bool Close(double a, double b, double rel = 1e-9) { return std::fabs(a - b) <= rel * std::max(1.0, std::fabs(b)); }

// 1 pt = 0.5 ft
PageScale HalfFoot() { return ScaleFromCalibration(100.0, 50.0, Unit::Foot); }

ScaleCheck Check(double pts, double stated, Unit unit = Unit::Foot) {
  ScaleCheck c;
  c.x0 = 0.f;
  c.y0 = 0.f;
  c.x1 = static_cast<float>(pts);
  c.y1 = 0.f;
  c.stated = stated;
  c.unit = unit;
  return c;
}

constexpr double kFootM = 0.3048;

} // namespace

TEST_CASE("ParseLength reads what drawings print", "[pdfcheck][req394][issue732]") {
  ParsedLength p;
  std::string why;
  struct Case {
    const char* text;
    double value;
    Unit unit;
    bool hasUnit;
  };
  const Case ok[] = {
      {"43'-0 3/4\"", 43.0625, Unit::Foot, true}, {"43' 0 3/4\"", 43.0625, Unit::Foot, true},
      {"43'-0 3/4", 43.0625, Unit::Foot, true},   {"10'6\"", 10.5, Unit::Foot, true},
      {"10'-6\"", 10.5, Unit::Foot, true},        {"10'", 10.0, Unit::Foot, true},
      {"6 1/2\"", 6.5, Unit::Inch, true},         {"3/4\"", 0.75, Unit::Inch, true},
      {"43.0625 ft", 43.0625, Unit::Foot, true},  {"12.5m", 12.5, Unit::Metre, true},
      {"850 mm", 850.0, Unit::Millimetre, true},  {"43.0625", 43.0625, Unit::Foot, false},
      {"  7  feet ", 7.0, Unit::Foot, true},      {"43\xE2\x80\xB2-0 3/4\xE2\x80\xB3", 43.0625, Unit::Foot, true}, // prime marks
  };
  for (const Case& c : ok) {
    INFO("input: " << c.text);
    REQUIRE(ParseLength(c.text, p, why));
    CHECK(Close(p.value, c.value));
    CHECK(p.hasUnit == c.hasUnit);
    if (c.hasUnit)
      CHECK(p.unit == c.unit);
  }
  for (const char* bad : {"", "   ", "abc", "''", "10'-", "1/0\"", "-5", "-5 ft", "0", "0 ft", "10 furlongs", "43'-0 3/4\"abc",
                          "1/2/3\"", "10'-x\"", "1.2.3", "ft"}) {
    INFO("input: \"" << bad << "\"");
    p = {};
    CHECK_FALSE(ParseLength(bad, p, why));
    CHECK_FALSE(why.empty());
  }
}

TEST_CASE("a check reads measured against stated with a verdict at exactly the limits", "[pdfcheck][req394][issue732]") {
  const CheckLimits lim; // 0.10 % and 0.50 %
  const ScaleCheck c = Check(100.0, 50.0);
  const CheckResult exact = EvaluateCheck(c, HalfFoot(), lim);
  CHECK(Close(exact.measured, 50.0));
  CHECK(std::fabs(exact.diff) < 1e-9);
  CHECK(exact.verdict == Verdict::Good);

  // The doc's own case: stated 43.06, reads 42.98.
  const ScaleCheck far = Check(85.96, 43.06); // 85.96 pt at 0.5 ft/pt = 42.98 ft
  const CheckResult r = EvaluateCheck(far, HalfFoot(), lim);
  CHECK(Close(r.measured, 42.98, 1e-6));
  CHECK(Close(r.diff, -0.08, 1e-6));
  CHECK(Close(r.pct, -0.08 / 43.06 * 100.0, 1e-5));
  CHECK(r.verdict == Verdict::Check); // 0.186 % is above the 0.10 % Good limit and under 0.50 %
}

TEST_CASE("verdict limits", "[pdfcheck][req394][issue732]") {
  const CheckLimits lim;
  CHECK(VerdictFor(0.0, lim) == Verdict::Good);
  CHECK(VerdictFor(0.10, lim) == Verdict::Good);       // on the limit: the better verdict
  CHECK(VerdictFor(0.1001, lim) == Verdict::Check);
  CHECK(VerdictFor(0.50, lim) == Verdict::Check);
  CHECK(VerdictFor(0.5001, lim) == Verdict::Blunder);
  CHECK(VerdictFor(12.0, lim) == Verdict::Blunder);
  CheckLimits tight;
  tight.goodPct = 0.01;
  tight.checkPct = 0.05;
  CHECK(VerdictFor(0.02, tight) == Verdict::Check);
  CHECK(VerdictFor(0.06, tight) == Verdict::Blunder);

  // Through EvaluateCheck: stated 100 ft read as 100.1 / 100.1001 / 100.5 / 100.5001.
  const double reads[] = {100.1, 100.1001, 100.5, 100.5001};
  const Verdict want[] = {Verdict::Good, Verdict::Check, Verdict::Check, Verdict::Blunder};
  for (int i = 0; i < 4; ++i) {
    const PageScale s = ScaleFromCalibration(100.0, reads[i], Unit::Foot); // 100 pt reads reads[i]
    INFO("reads " << reads[i]);
    CHECK(EvaluateCheck(Check(100.0, 100.0), s, lim).verdict == want[i]);
  }
  // A check in another unit than the page scale.
  const ScaleCheck metric = Check(100.0, 15.24, Unit::Metre); // 50 ft = 15.24 m
  CHECK(EvaluateCheck(metric, HalfFoot(), lim).verdict == Verdict::Good);
}

TEST_CASE("best fit weights by length and marks the one outlier", "[pdfcheck][req394][issue732]") {
  const double base = 4.0 * kFootM / 72.0; // metres per point at 4 ft per inch
  const auto obs = [&](std::initializer_list<std::pair<double, double>> list) { // {pts, implied multiple of base}
    std::vector<FitObs> v;
    for (const auto& [pts, mult] : list)
      v.push_back({pts, pts * base * mult});
    return v;
  };

  // Equal lengths implying 4.000, 4.000, 4.004: s = (sum 1/k) / (sum 1/k^2) by the weighting rule.
  const BestFit a = BestFitScale(obs({{100, 1.0}, {100, 1.0}, {100, 1.001}}));
  REQUIRE(a.valid);
  const double k0 = base, k2 = base * 1.001;
  CHECK(Close(a.metresPerPt, (2.0 / k0 + 1.0 / k2) / (2.0 / (k0 * k0) + 1.0 / (k2 * k2)), 1e-12));
  CHECK_FALSE(a.anyOutlier);

  // A long span outweighs a short one: 1000 pt at 1.000 and 100 pt at 1.010.
  const BestFit w = BestFitScale(obs({{1000, 1.0}, {100, 1.01}}));
  REQUIRE(w.valid);
  CHECK(std::fabs(w.metresPerPt - base) < std::fabs(w.metresPerPt - base * 1.01));
  CHECK(std::fabs(w.metresPerPt / base - 1.0) < 0.003); // nearer the long one than the 0.5 % midpoint
  CHECK_FALSE(w.anyOutlier);                            // two observations cannot say which one is wrong

  // 3.980 among three 4.000: that one is the only outlier, and the fit without it is 4.000.
  const BestFit o = BestFitScale(obs({{100, 1.0}, {100, 1.0}, {100, 1.0}, {100, 3.98 / 4.0}}));
  REQUIRE(o.valid);
  CHECK(o.anyOutlier);
  CHECK(o.outlier == std::vector<bool>({false, false, false, true}));
  REQUIRE(o.withoutValid);
  CHECK(Close(o.withoutMetresPerPt, base, 1e-9));

  // One check alone: no best fit and no outlier.
  const BestFit one = BestFitScale(obs({{100, 1.0}}));
  CHECK_FALSE(one.valid);
  CHECK_FALSE(one.anyOutlier);
  CHECK_FALSE(BestFitScale({}).valid);
}

TEST_CASE("a correction can match one check, use a percentage, or be left", "[pdfcheck][req394][issue732]") {
  const CheckLimits lim;
  const PageScale scale = HalfFoot();
  std::vector<ScaleCheck> checks;
  ScaleCheck cal = Check(100.0, 50.0);
  cal.calibration = true;
  checks.push_back(cal);
  checks.push_back(Check(85.96, 43.06)); // reads 42.98, the drawing says 43.06

  // Match the second check.
  const double f = FactorMatching(checks[1], scale);
  CHECK(Close(f, 43.06 / 42.98, 1e-6));
  const std::vector<CheckResult> after = PreviewCorrection(checks, scale, f, lim);
  REQUIRE(after.size() == 2);
  CHECK(Close(after[1].measured, 43.06, 1e-9)); // that check now reads what the drawing says
  CHECK(std::fabs(after[1].diff) < 1e-9);
  CHECK(Close(after[0].measured, 50.0 * f, 1e-9)); // the calibration check moves by the same fraction
  CHECK(Close(after[0].pct, (f - 1.0) * 100.0, 1e-9));
  CHECK(after[0].verdict == Verdict::Check); // +0.186 %

  const PageScale fixed = ApplyFactor(scale, f);
  CHECK(Close(fixed.RealPerPoint(), scale.RealPerPoint() * f, 1e-12));
  CHECK(fixed.note == "adjusted +0.19 %");
  CHECK(fixed.label.empty());
  CHECK(Close(fixed.originalRealValue, scale.realValue));
  // The preview is exactly what applying produces.
  CHECK(Close(EvaluateCheck(checks[1], fixed, lim).measured, after[1].measured, 1e-12));

  // A second correction keeps the original and reports the total.
  const PageScale twice = ApplyFactor(fixed, 1.001);
  CHECK(Close(twice.originalRealValue, scale.realValue));
  CHECK(twice.note == "adjusted +0.29 %");

  // A typed +0.20 % multiplies every reading by 1.0020; a negative one divides.
  CHECK(Close(FactorFromPercent(0.20), 1.002));
  const auto typed = PreviewCorrection(checks, scale, FactorFromPercent(0.20), lim);
  CHECK(Close(typed[0].measured, 50.0 * 1.002, 1e-12));
  CHECK(Close(typed[1].measured, 42.98 * 1.002, 1e-6));
  CHECK(Close(FactorFromPercent(-0.20), 0.998));

  // Leave it: nothing changes.
  const PageScale same = ApplyFactor(scale, 1.0);
  CHECK(same == scale);
  CHECK(same.note.empty());
  const auto none = PreviewCorrection(checks, scale, 1.0, lim);
  CHECK(Close(none[1].measured, 42.98, 1e-6));

  // A named preset loses its label once it is corrected: it is no longer exactly that scale.
  PageScale preset = PresetScales()[8];
  CHECK_FALSE(preset.label.empty());
  CHECK(ApplyFactor(preset, 1.002).label.empty());
}

TEST_CASE("a calibration counts as a check, and each correction is one undo step", "[pdfcheck][req394][issue732]") {
  AnnotSession s;
  CHECK_FALSE(s.Dirty());
  ScaleCheck cal = Check(100.0, 50.0);
  cal.calibration = true;
  std::map<int, PageScale> set;
  set[0] = HalfFoot();
  REQUIRE(s.SetScales(set, &cal)); // the scale and the calibration check in one step
  REQUIRE(s.Checks().size() == 1);
  CHECK(s.Checks()[0].calibration);
  CHECK(s.Dirty());

  // Applying a correction is one step, and undo restores the scale and its text exactly.
  const PageScale before = s.Scales().at(0);
  std::map<int, PageScale> fix;
  fix[0] = ApplyFactor(before, 1.0019);
  REQUIRE(s.SetScales(fix));
  CHECK(s.Scales().at(0).note == "adjusted +0.19 %");
  REQUIRE(s.Undo());
  CHECK(s.Scales().at(0) == before);
  CHECK(s.Scales().at(0).note.empty());
  REQUIRE(s.Undo()); // the calibration step: scale and check both go
  CHECK(s.Scales().empty());
  CHECK(s.Checks().empty());
  CHECK_FALSE(s.Dirty());
  REQUIRE(s.Redo());
  CHECK(s.Checks().size() == 1);

  // Checks alone never make the session unsaved, and can be removed and undone.
  AnnotSession t;
  t.AddCheck(Check(50.0, 25.0));
  t.AddCheck(Check(60.0, 30.0));
  CHECK_FALSE(t.Dirty());
  REQUIRE(t.RemoveCheck(0));
  CHECK(t.Checks().size() == 1);
  CHECK_FALSE(t.RemoveCheck(5));
  REQUIRE(t.Undo());
  CHECK(t.Checks().size() == 2);
}

TEST_CASE("robust calibration weights follow length", "[pdfcheck][req394][issue732]") {
  const RobustParams p; // 0.25 pt pick error, 0.05 % of length
  CHECK(Close(RobustWeight(100.0, p), 1.0 / (0.25 * 0.25 + 0.05 * 0.05), 1e-12));
  CHECK(Close(RobustWeight(0.0, p), 1.0 / (0.25 * 0.25), 1e-12));
  // A short span, where one pick is a large share of the length, weighs for less once it is compared per length
  // squared: the information it carries about the scale is w * m^2.
  CHECK(RobustWeight(1000.0, p) * 1000.0 * 1000.0 > 50.0 * RobustWeight(50.0, p) * 50.0 * 50.0);

  // A long span pulls the answer more than a short one.
  const double s0 = 4.0 * kFootM / 72.0;
  std::vector<RobustObs> obs = {{1000.0, 1000.0 * s0 * 1.000, true}, {50.0, 50.0 * s0 * 1.020, true}, {400.0, 400.0 * s0 * 1.000, true}};
  const RobustResult r = SolveRobust(obs, p);
  REQUIRE(r.ok);
  CHECK(std::fabs(r.metresPerPt / s0 - 1.0) < 0.001); // the two long spans agree; the short one barely moves it
}

TEST_CASE("robust calibration recovers the scale better than one short span, and says how sure it is",
          "[pdfcheck][req394][issue732]") {
  const double s0 = 4.0 * kFootM / 72.0;
  const double feet[] = {10.0, 10.0, 25.0, 43.0, 60.0};
  const double noise[] = {+0.30, -0.20, +0.25, -0.30, +0.10}; // picks off by a fraction of a point
  std::vector<RobustObs> obs;
  for (int i = 0; i < 5; ++i) {
    const double pts = feet[i] * 18.0 + noise[i]; // 18 pt per foot at 1/4" = 1'
    obs.push_back({pts, feet[i] * kFootM, true});
  }
  const RobustResult all = SolveRobust(obs, RobustParams{});
  REQUIRE(all.ok);
  const double singleShort = obs[0].metres / obs[0].pts; // calibrate on the first 10' span alone
  const double errShort = std::fabs(singleShort / s0 - 1.0), errLs = std::fabs(all.metresPerPt / s0 - 1.0);
  CHECK(errLs < errShort);
  CHECK(errLs < 0.001);
  CHECK(all.relSigma > 0.0);

  // More dimensions, more certainty.
  const RobustResult three = SolveRobust({obs[0], obs[1], obs[2]}, RobustParams{});
  REQUIRE(three.ok);
  CHECK(three.relSigma > all.relSigma);

  // Each residual is "adjusted reading minus stated", and they report percentages.
  REQUIRE(all.residualMetres.size() == 5);
  for (int i = 0; i < 5; ++i)
    CHECK(Close(all.residualMetres[static_cast<size_t>(i)], all.metresPerPt * obs[static_cast<size_t>(i)].pts - obs[static_cast<size_t>(i)].metres, 1e-12));
  CHECK(Close(all.residualPct[2], all.residualMetres[2] / obs[2].metres * 100.0, 1e-12));
  for (bool s : all.suspect)
    CHECK_FALSE(s); // honest noise is not suspect

  // The scale it describes, in feet per inch.
  const PageScale made = ScaleFromRobust(all, Unit::Foot, nullptr);
  CHECK(made.realUnit == Unit::Foot);
  CHECK(Close(made.realValue, all.metresPerPt * 72.0 / kFootM, 1e-12));
  CHECK(made.note.rfind("robust, 5 dimensions, +/- ", 0) == 0);
}

TEST_CASE("robust calibration flags a wrong value but never removes it itself", "[pdfcheck][req394][issue732]") {
  const double feet[] = {10.0, 10.0, 25.0, 43.0, 60.0};
  std::vector<RobustObs> obs;
  for (double f : feet)
    obs.push_back({f * 18.0, f * kFootM, true});
  obs[3].metres = 4.3 * kFootM; // the 43 ft dimension typed as 4.3 ft
  const RobustResult r = SolveRobust(obs, RobustParams{});
  REQUIRE(r.ok);
  REQUIRE(r.suspect.size() == 5);
  CHECK(r.suspect[3]);
  CHECK_FALSE(r.suspect[0]);
  CHECK_FALSE(r.suspect[1]);
  CHECK_FALSE(r.suspect[2]);
  CHECK_FALSE(r.suspect[4]);
  CHECK(r.used == 5); // it is still in the fit until the user removes it

  // The user removes it: the fit is re-solved without it and is clean.
  obs[3].use = false;
  const RobustResult clean = SolveRobust(obs, RobustParams{});
  REQUIRE(clean.ok);
  CHECK(clean.used == 4);
  for (bool s : clean.suspect)
    CHECK_FALSE(s);
  CHECK(Close(clean.metresPerPt, 4.0 * kFootM / 72.0, 1e-9));
  CHECK(clean.z[3] == 0.0); // an unused one has no residual of its own
}

TEST_CASE("robust calibration needs the minimum number of dimensions", "[pdfcheck][req394][issue732]") {
  RobustParams p;
  std::vector<RobustObs> obs = {{180.0, 10.0 * kFootM, true}, {450.0, 25.0 * kFootM, true}};
  const RobustResult two = SolveRobust(obs, p);
  CHECK_FALSE(two.ok);
  CHECK(two.why.find("add 1 more dimension") != std::string::npos);
  const RobustResult none = SolveRobust({}, p);
  CHECK_FALSE(none.ok);
  CHECK(none.why.find("add 3 more dimensions") != std::string::npos);
  obs.push_back({774.0, 43.0 * kFootM, true});
  CHECK(SolveRobust(obs, p).ok);
  p.minDimensions = 4; // the minimum is a setting
  CHECK_FALSE(SolveRobust(obs, p).ok);
  // A removed dimension does not count.
  obs[0].use = false;
  p.minDimensions = 3;
  CHECK_FALSE(SolveRobust(obs, p).ok);

  // Applying is one undo step; cancelling changes nothing (nothing is passed to the session).
  AnnotSession s;
  obs[0].use = true;
  const RobustResult ok = SolveRobust(obs, p);
  REQUIRE(ok.ok);
  std::map<int, PageScale> change;
  change[0] = ScaleFromRobust(ok, Unit::Foot, nullptr);
  REQUIRE(s.SetScales(change));
  CHECK(s.Scales().at(0).note.rfind("robust, 3 dimensions", 0) == 0);
  REQUIRE(s.Undo());
  CHECK(s.Scales().empty());
  CHECK_FALSE(s.Dirty());
}

TEST_CASE("the scale report lists each check, the best fit and the original scale", "[pdfcheck][req394][issue732]") {
  PageScale scale = HalfFoot();
  std::vector<ScaleCheck> checks = {Check(100.0, 50.0), Check(85.96, 43.06), Check(100.0, 50.0), Check(100.0, 50.5)};
  checks[0].calibration = true;
  const std::string rep = ScaleReport("sheet.pdf", 15, scale, checks, CheckLimits{});
  CHECK(rep.find("sheet.pdf, page 16") != std::string::npos);
  CHECK(rep.find("calibration") != std::string::npos);
  CHECK(rep.find("Check") != std::string::npos);
  CHECK(rep.find("Best fit of 4 checks") != std::string::npos);
  CHECK(rep.find("Limits: Good up to 0.10 %") != std::string::npos);

  const PageScale fixed = ApplyFactor(scale, 1.002);
  const std::string rep2 = ScaleReport("sheet.pdf", 0, fixed, checks, CheckLimits{});
  CHECK(rep2.find("adjusted +0.20 %") != std::string::npos);
  CHECK(rep2.find("Original calibrated scale: 1 in = 36 ft") != std::string::npos);
}
