#pragma once

// REQ-394 / ADR-067 addendum 3 — testing a page's scale against dimensions the drawing already states.
// Pure maths and parsing: no PDFium, no window.
//
//   * ParseLength      the value a drawing prints ("43'-0 3/4\"", "10'6\"", "12.5 m", "850 mm", "43.0625")
//   * EvaluateCheck    measured vs stated, the difference and a Good / Check / Blunder verdict
//   * BestFitScale     a length-weighted best fit of two or more checks, with outliers
//   * Factor / Apply   the correction a user may choose after a check (clause 4a)
//   * SolveRobust      the opt-in robust calibration: weighted least squares, uncertainty, suspects (clause 4b)

#include "PdfMeasure.hpp"

#include <string>
#include <vector>

namespace pdfview {

// ---------------------------------------------------------------------------------------------------
// The value a drawing states
// ---------------------------------------------------------------------------------------------------

struct ParsedLength {
  double value = 0.0;
  Unit unit = Unit::Foot;
  bool hasUnit = false; ///< false for a bare number: the caller supplies the page scale's unit
};

/// Reads a length in feet-and-inches (43'-0 3/4", 43' 0 3/4", 10'6", 6 1/2"), with a unit (43.0625 ft, 12.5m,
/// 850 mm) or bare (43.0625). False with a message naming the problem (empty, unreadable, not above zero, a zero
/// denominator, a dangling mark such as 10'-, trailing text).
bool ParseLength(const std::string& text, ParsedLength& out, std::string& why);

// ---------------------------------------------------------------------------------------------------
// One check
// ---------------------------------------------------------------------------------------------------

/// A distance the user picked on the sheet and the value the drawing states for it.
struct ScaleCheck {
  int page = 0;
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f; ///< the two picked points, page points
  double stated = 0.0;                           ///< the drawing's value, in \p unit
  Unit unit = Unit::Foot;
  bool calibration = false;                      ///< the calibration itself counts as the first check

  double MeasuredPt() const;
  double StatedMetres() const { return stated * UnitInMetres(unit); }
  bool operator==(const ScaleCheck& o) const;
};

struct CheckLimits {
  double goodPct = 0.10;  ///< up to this: Good
  double checkPct = 0.50; ///< up to this: Check; above: Blunder
};

enum class Verdict { Good, Check, Blunder };
const char* VerdictName(Verdict v);
Verdict VerdictFor(double absPercent, const CheckLimits& lim);

struct CheckResult {
  double measured = 0.0; ///< what the page scale reads for the picked points, in the check's unit
  double stated = 0.0;   ///< what the drawing states, in the check's unit
  double diff = 0.0;     ///< measured - stated
  double pct = 0.0;      ///< diff as a percentage of stated
  Verdict verdict = Verdict::Good;
};
CheckResult EvaluateCheck(const ScaleCheck& c, const PageScale& scale, const CheckLimits& lim);

// ---------------------------------------------------------------------------------------------------
// Best fit and outliers (clause 4)
// ---------------------------------------------------------------------------------------------------

struct FitObs {
  double pts = 0.0;    ///< the picked distance, page points
  double metres = 0.0; ///< the stated value, metres
};

struct BestFit {
  bool valid = false;           ///< two or more observations
  double metresPerPt = 0.0;     ///< the fitted scale
  std::vector<double> deviation; ///< each observation's implied scale against the median implied scale, as a fraction
  std::vector<bool> outlier;    ///< marked only with three or more observations (two cannot say which one is wrong)
  bool anyOutlier = false;
  bool withoutValid = false;    ///< a fit without the outliers exists (two or more left)
  double withoutMetresPerPt = 0.0;
};

/// The scale minimising the squared relative error, each observation weighted by its length. An observation is an
/// outlier when its implied scale differs from the median implied scale by more than 0.25 % or three times the median
/// deviation, whichever is larger.
BestFit BestFitScale(const std::vector<FitObs>& obs);

// ---------------------------------------------------------------------------------------------------
// Corrections (clause 4a)
// ---------------------------------------------------------------------------------------------------

/// The factor that makes \p c read exactly its stated value under \p scale (stated / measured).
double FactorMatching(const ScaleCheck& c, const PageScale& scale);
/// A typed percentage: +0.20 % makes every reading 0.20 % larger.
double FactorFromPercent(double percent);
/// The scale with every reading multiplied by \p factor, marked adjusted (a named preset loses its label).
PageScale ApplyFactor(const PageScale& scale, double factor);
/// How every check would read after \p factor, so the user can see the trade-off before applying.
std::vector<CheckResult> PreviewCorrection(const std::vector<ScaleCheck>& checks, const PageScale& scale, double factor,
                                           const CheckLimits& lim);

// ---------------------------------------------------------------------------------------------------
// Robust calibration (clause 4b)
// ---------------------------------------------------------------------------------------------------

struct RobustParams {
  double pickPt = 0.25;          ///< uncertainty of one picked distance, whatever its length (points)
  double drawingFraction = 0.0005; ///< drawing error that grows with length (0.05 %)
  int minDimensions = 3;
  double suspectZ = 3.0;         ///< standardised residual above which a dimension is Suspect
};

struct RobustObs {
  double pts = 0.0;
  double metres = 0.0;
  bool use = true; ///< false: the user removed it; it is shown but not part of the fit
};

struct RobustResult {
  bool ok = false;
  std::string why;                 ///< when !ok: how many more dimensions are needed
  int used = 0;
  double metresPerPt = 0.0;        ///< the adjusted scale
  double relSigma = 0.0;           ///< its uncertainty as a fraction of itself
  double rmsMetres = 0.0;          ///< RMS of the residuals, metres
  std::vector<double> residualMetres; ///< adjusted reading minus stated, per observation (0 for unused)
  std::vector<double> residualPct;
  std::vector<double> z;           ///< standardised residuals (0 for unused)
  std::vector<bool> suspect;
};

/// 1 / (pickPt^2 + (drawingFraction * lengthPt)^2): a short span, where a pick is a large share of the length, weighs less.
double RobustWeight(double lengthPt, const RobustParams& p);
RobustResult SolveRobust(const std::vector<RobustObs>& obs, const RobustParams& p);

/// The scale a robust result describes, in \p unit per page inch, noted "robust, n dimensions, +/- x %"; the
/// previous scale (if any) is remembered as the original.
PageScale ScaleFromRobust(const RobustResult& r, Unit unit, const PageScale* previous);

// ---------------------------------------------------------------------------------------------------
// The report (clause 3)
// ---------------------------------------------------------------------------------------------------

/// Text: each check (stated, measured, difference, verdict), the best fit and any outlier, and the original
/// calibrated scale when the scale has been adjusted.
std::string ScaleReport(const std::string& pdfName, int page, const PageScale& scale, const std::vector<ScaleCheck>& checks,
                        const CheckLimits& lim);

} // namespace pdfview
