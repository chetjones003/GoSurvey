#include "PdfScaleCheck.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace pdfview {

// ---------------------------------------------------------------------------------------------------
// ParseLength
// ---------------------------------------------------------------------------------------------------

namespace {

std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
    ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    --b;
  return s.substr(a, b - a);
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
  for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
    s.replace(at, from.size(), to);
}

// Typographic prime and quote marks, and long dashes, as the plain marks a keyboard types.
std::string Normalise(std::string s) {
  ReplaceAll(s, "\xE2\x80\x99", "'");
  ReplaceAll(s, "\xE2\x80\xB2", "'");
  ReplaceAll(s, "\xC2\xB4", "'");
  ReplaceAll(s, "\xE2\x80\x9D", "\"");
  ReplaceAll(s, "\xE2\x80\xB3", "\"");
  ReplaceAll(s, "\xE2\x80\x93", "-");
  ReplaceAll(s, "\xE2\x80\x94", "-");
  return s;
}

bool IsDigitStr(const std::string& s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// digits with at most one point, at least one digit, nothing else
bool ParseDecimal(const std::string& s, double& v) {
  if (s.empty())
    return false;
  bool point = false, digit = false;
  for (char c : s) {
    if (c == '.' && !point)
      point = true;
    else if (c >= '0' && c <= '9')
      digit = true;
    else
      return false;
  }
  if (!digit)
    return false;
  v = std::atof(s.c_str());
  return true;
}

bool ParseFraction(const std::string& s, double& v, std::string& why) {
  const size_t slash = s.find('/');
  const std::string a = Trim(s.substr(0, slash)), b = Trim(s.substr(slash + 1));
  if (!IsDigitStr(a) || !IsDigitStr(b)) {
    why = "\"" + s + "\" is not a fraction such as 3/4";
    return false;
  }
  const double den = std::atof(b.c_str());
  if (den == 0.0) {
    why = "a fraction cannot have a zero denominator";
    return false;
  }
  v = std::atof(a.c_str()) / den;
  return true;
}

// "6", "6.5", "3/4" or "6 3/4": a count of inches
bool ParseMixed(const std::string& text, double& v, std::string& why) {
  const std::string s = Trim(text);
  std::vector<std::string> tok;
  std::istringstream in(s);
  std::string t;
  while (in >> t)
    tok.push_back(t);
  if (tok.empty()) {
    why = "the inches are missing";
    return false;
  }
  if (tok.size() == 2) {
    double whole = 0.0, frac = 0.0;
    if (!IsDigitStr(tok[0]) || tok[1].find('/') == std::string::npos) {
      why = "cannot read the inches \"" + s + "\"";
      return false;
    }
    whole = std::atof(tok[0].c_str());
    if (!ParseFraction(tok[1], frac, why))
      return false;
    v = whole + frac;
    return true;
  }
  if (tok.size() == 1) {
    if (tok[0].find('/') != std::string::npos)
      return ParseFraction(tok[0], v, why);
    if (ParseDecimal(tok[0], v))
      return true;
  }
  why = "cannot read the inches \"" + s + "\"";
  return false;
}

} // namespace

bool ParseLength(const std::string& text, ParsedLength& out, std::string& why) {
  why.clear();
  const std::string t = Trim(Normalise(text));
  if (t.empty()) {
    why = "type the value the drawing states, for example 43'-0 3/4\" or 12.5 m";
    return false;
  }
  if (t[0] == '-' || t[0] == '+') {
    why = "the value must be above zero";
    return false;
  }
  ParsedLength r;
  const size_t ft = t.find('\'');
  if (ft != std::string::npos) { // feet and inches
    double feet = 0.0;
    if (!ParseDecimal(Trim(t.substr(0, ft)), feet)) {
      why = "cannot read the feet in \"" + t + "\"";
      return false;
    }
    std::string rest = Trim(t.substr(ft + 1));
    bool dash = false;
    if (!rest.empty() && rest[0] == '-') {
      dash = true;
      rest = Trim(rest.substr(1));
    }
    if (!rest.empty() && rest.back() == '"')
      rest = Trim(rest.substr(0, rest.size() - 1));
    double inches = 0.0;
    if (rest.empty()) {
      if (dash) {
        why = "the inches are missing after the dash";
        return false;
      }
    } else if (!ParseMixed(rest, inches, why)) {
      return false;
    }
    r.value = feet + inches / 12.0;
    r.unit = Unit::Foot;
    r.hasUnit = true;
  } else if (t.back() == '"') { // inches only
    double inches = 0.0;
    if (!ParseMixed(t.substr(0, t.size() - 1), inches, why))
      return false;
    r.value = inches;
    r.unit = Unit::Inch;
    r.hasUnit = true;
  } else { // a number, then maybe a unit
    size_t i = 0;
    while (i < t.size() && ((t[i] >= '0' && t[i] <= '9') || t[i] == '.'))
      ++i;
    if (!ParseDecimal(t.substr(0, i), r.value)) {
      why = "cannot read \"" + t + "\" as a length";
      return false;
    }
    const std::string unit = Trim(t.substr(i));
    if (!unit.empty()) {
      if (!ParseUnit(unit, r.unit)) {
        why = "unknown unit \"" + unit + "\"";
        return false;
      }
      r.hasUnit = true;
    }
  }
  if (!(r.value > 0.0) || !std::isfinite(r.value)) {
    why = "the value must be above zero";
    return false;
  }
  out = r;
  return true;
}

// ---------------------------------------------------------------------------------------------------
// One check
// ---------------------------------------------------------------------------------------------------

double ScaleCheck::MeasuredPt() const {
  return std::hypot(static_cast<double>(x1) - x0, static_cast<double>(y1) - y0);
}

bool ScaleCheck::operator==(const ScaleCheck& o) const {
  return page == o.page && x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1 && stated == o.stated && unit == o.unit &&
         calibration == o.calibration;
}

const char* VerdictName(Verdict v) {
  switch (v) {
  case Verdict::Good: return "Good";
  case Verdict::Check: return "Check";
  case Verdict::Blunder: return "Blunder";
  }
  return "";
}

Verdict VerdictFor(double absPercent, const CheckLimits& lim) {
  constexpr double kEps = 1e-9; // a value exactly on a limit belongs to the better verdict
  if (absPercent <= lim.goodPct + kEps)
    return Verdict::Good;
  if (absPercent <= lim.checkPct + kEps)
    return Verdict::Check;
  return Verdict::Blunder;
}

CheckResult EvaluateCheck(const ScaleCheck& c, const PageScale& scale, const CheckLimits& lim) {
  CheckResult r;
  const double metres = scale.PointsToReal(c.MeasuredPt()) * UnitInMetres(scale.realUnit);
  r.measured = metres / UnitInMetres(c.unit);
  r.stated = c.stated;
  r.diff = r.measured - r.stated;
  r.pct = r.stated != 0.0 ? r.diff / r.stated * 100.0 : 0.0;
  r.verdict = VerdictFor(std::fabs(r.pct), lim);
  return r;
}

// ---------------------------------------------------------------------------------------------------
// Best fit and outliers
// ---------------------------------------------------------------------------------------------------

namespace {

double Median(std::vector<double> v) {
  if (v.empty())
    return 0.0;
  std::sort(v.begin(), v.end());
  const size_t n = v.size();
  return n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// min sum m * ((s m - v) / v)^2  =>  s = sum(m^2 / v) / sum(m^3 / v^2)
double FitScale(const std::vector<FitObs>& obs, const std::vector<bool>* skip) {
  double num = 0.0, den = 0.0;
  for (size_t i = 0; i < obs.size(); ++i) {
    if (skip != nullptr && (*skip)[i])
      continue;
    const double m = obs[i].pts, v = obs[i].metres;
    if (m <= 0.0 || v <= 0.0)
      continue;
    num += m * m / v;
    den += m * m * m / (v * v);
  }
  return den > 0.0 ? num / den : 0.0;
}

} // namespace

BestFit BestFitScale(const std::vector<FitObs>& obs) {
  BestFit r;
  const size_t n = obs.size();
  r.deviation.assign(n, 0.0);
  r.outlier.assign(n, false);
  size_t good = 0;
  for (const FitObs& o : obs)
    good += (o.pts > 0.0 && o.metres > 0.0) ? 1 : 0;
  if (good < 2)
    return r;
  r.metresPerPt = FitScale(obs, nullptr);
  r.valid = r.metresPerPt > 0.0;
  if (!r.valid)
    return r;
  std::vector<double> implied;
  for (const FitObs& o : obs)
    implied.push_back(o.pts > 0.0 ? o.metres / o.pts : 0.0);
  const double centre = Median(implied);
  std::vector<double> devs;
  for (size_t i = 0; i < n; ++i) {
    r.deviation[i] = centre > 0.0 ? std::fabs(implied[i] / centre - 1.0) : 0.0;
    devs.push_back(r.deviation[i]);
  }
  if (n >= 3) { // two observations cannot say which one is wrong
    const double limit = std::max(0.0025, 3.0 * Median(devs));
    for (size_t i = 0; i < n; ++i)
      if (r.deviation[i] > limit) {
        r.outlier[i] = true;
        r.anyOutlier = true;
      }
  }
  if (r.anyOutlier) {
    size_t left = 0;
    for (size_t i = 0; i < n; ++i)
      left += r.outlier[i] ? 0 : 1;
    if (left >= 2) {
      r.withoutValid = true;
      r.withoutMetresPerPt = FitScale(obs, &r.outlier);
    }
  }
  return r;
}

// ---------------------------------------------------------------------------------------------------
// Corrections
// ---------------------------------------------------------------------------------------------------

double FactorMatching(const ScaleCheck& c, const PageScale& scale) {
  const CheckResult r = EvaluateCheck(c, scale, CheckLimits{});
  return r.measured > 0.0 && r.stated > 0.0 ? r.stated / r.measured : 1.0;
}

double FactorFromPercent(double percent) { return 1.0 + percent / 100.0; }

PageScale ApplyFactor(const PageScale& scale, double factor) {
  if (!scale.Valid() || !(factor > 0.0) || factor == 1.0)
    return scale;
  PageScale r = scale;
  if (r.originalRealValue == 0.0)
    r.originalRealValue = scale.realValue;
  r.realValue = scale.realValue * factor;
  r.label.clear(); // no longer exactly the named preset
  const double pct = (r.realValue / r.originalRealValue - 1.0) * 100.0;
  r.note = "adjusted " + std::string(pct >= 0.0 ? "+" : "-") + FormatValue(std::fabs(pct), 2) + " %";
  return r;
}

std::vector<CheckResult> PreviewCorrection(const std::vector<ScaleCheck>& checks, const PageScale& scale, double factor,
                                           const CheckLimits& lim) {
  const PageScale after = ApplyFactor(scale, factor);
  std::vector<CheckResult> out;
  for (const ScaleCheck& c : checks)
    out.push_back(EvaluateCheck(c, after, lim));
  return out;
}

// ---------------------------------------------------------------------------------------------------
// Robust calibration
// ---------------------------------------------------------------------------------------------------

double RobustWeight(double lengthPt, const RobustParams& p) {
  const double d = p.drawingFraction * lengthPt;
  return 1.0 / (p.pickPt * p.pickPt + d * d);
}

RobustResult SolveRobust(const std::vector<RobustObs>& obs, const RobustParams& p) {
  RobustResult r;
  const size_t n = obs.size();
  r.residualMetres.assign(n, 0.0);
  r.residualPct.assign(n, 0.0);
  r.z.assign(n, 0.0);
  r.suspect.assign(n, false);
  std::vector<size_t> use;
  for (size_t i = 0; i < n; ++i)
    if (obs[i].use && obs[i].pts > 0.0 && obs[i].metres > 0.0)
      use.push_back(i);
  r.used = static_cast<int>(use.size());
  if (r.used < std::max(2, p.minDimensions)) {
    const int more = std::max(2, p.minDimensions) - r.used;
    r.why = "add " + std::to_string(more) + " more dimension" + (more == 1 ? "" : "s") + " (at least " +
            std::to_string(std::max(2, p.minDimensions)) + " are needed)";
    return r;
  }
  // s = sum(w m v) / sum(w m^2), with w = 1 / sigma_pt^2 (the common factor s^2 of the stated-value noise cancels)
  double num = 0.0, den = 0.0;
  for (size_t i : use) {
    const double w = RobustWeight(obs[i].pts, p);
    num += w * obs[i].pts * obs[i].metres;
    den += w * obs[i].pts * obs[i].pts;
  }
  r.metresPerPt = num / den;
  double chi2 = 0.0, sumSq = 0.0;
  for (size_t i : use) {
    const double m = obs[i].pts, v = obs[i].metres;
    const double rpt = m - v / r.metresPerPt; // residual in page points
    const double sigma = std::sqrt(1.0 / RobustWeight(m, p));
    r.residualMetres[i] = r.metresPerPt * m - v;
    r.residualPct[i] = r.residualMetres[i] / v * 100.0;
    chi2 += (rpt / sigma) * (rpt / sigma);
    sumSq += r.residualMetres[i] * r.residualMetres[i];
  }

  // Which dimensions are Suspect. A long span decides a least-squares fit, so one wrong long dimension would drag
  // the fit and make the good ones look off. Each dimension is therefore tested against a fit made from the OTHER
  // dimensions (its leave-one-out residual against the picking error plus that fit's uncertainty), the worst
  // offender above the limit is flagged and set aside, and the rest are tested again, until none is left.
  const auto fitOf = [&](const std::vector<size_t>& idx, size_t skip, double& s, double& rel) {
    double n2 = 0.0, d2 = 0.0;
    for (size_t j : idx) {
      if (j == skip)
        continue;
      const double w = RobustWeight(obs[j].pts, p);
      n2 += w * obs[j].pts * obs[j].metres;
      d2 += w * obs[j].pts * obs[j].pts;
    }
    s = n2 / d2;
    rel = 1.0 / std::sqrt(d2);
  };
  const auto looZ = [&](const std::vector<size_t>& idx, size_t i) {
    double s = 0.0, rel = 0.0;
    fitOf(idx, i, s, rel);
    const double m = obs[i].pts, v = obs[i].metres;
    const double sigma2 = 1.0 / RobustWeight(m, p);
    return (m - v / s) / std::sqrt(sigma2 + (m * rel) * (m * rel));
  };
  std::vector<size_t> active = use;
  while (active.size() >= 3) {
    size_t worst = active.size();
    double worstZ = p.suspectZ;
    for (size_t k = 0; k < active.size(); ++k) {
      const double z = std::fabs(looZ(active, active[k]));
      if (z > worstZ) {
        worstZ = z;
        worst = k;
      }
    }
    if (worst == active.size())
      break;
    r.suspect[active[worst]] = true;
    active.erase(active.begin() + static_cast<long>(worst));
  }
  if (active.size() >= 2) // the reported standardised residual: against the fit of the others that are not suspect
    for (size_t i : use) {
      const bool isActive = std::find(active.begin(), active.end(), i) != active.end();
      if (isActive && active.size() < 3)
        continue;
      r.z[i] = looZ(active, i);
    }
  const double reduced = chi2 / static_cast<double>(r.used - 1);
  r.relSigma = 1.0 / std::sqrt(den) * std::max(1.0, std::sqrt(reduced));
  r.rmsMetres = std::sqrt(sumSq / static_cast<double>(r.used));
  r.ok = true;
  return r;
}

PageScale ScaleFromRobust(const RobustResult& r, Unit unit, const PageScale* previous) {
  PageScale s;
  s.pageUnit = Unit::Inch;
  s.pageValue = 1.0;
  s.realUnit = unit;
  s.realValue = r.metresPerPt * 72.0 / UnitInMetres(unit);
  s.note = "robust, " + std::to_string(r.used) + " dimensions, +/- " + FormatValue(r.relSigma * 100.0, 2) + " %";
  if (previous != nullptr && previous->Valid())
    s.originalRealValue = previous->realValue * UnitInMetres(previous->realUnit) / UnitInMetres(unit);
  return s;
}

// ---------------------------------------------------------------------------------------------------
// The report
// ---------------------------------------------------------------------------------------------------

namespace {

std::string Signed(double v, int decimals) { return (v >= 0.0 ? "+" : "-") + FormatValue(std::fabs(v), decimals); }

} // namespace

std::string ScaleReport(const std::string& pdfName, int page, const PageScale& scale, const std::vector<ScaleCheck>& checks,
                        const CheckLimits& lim) {
  std::ostringstream o;
  o << "Scale report - " << pdfName << ", page " << (page + 1) << "\n";
  o << "Scale: " << scale.RatioText();
  if (!scale.note.empty())
    o << " (" << scale.note << ")";
  o << "\n";
  if (scale.originalRealValue > 0.0 && scale.Valid()) {
    PageScale was = scale;
    was.realValue = scale.originalRealValue;
    was.label.clear();
    was.note.clear();
    o << "Original calibrated scale: " << was.RatioText() << "\n";
  }
  o << "Limits: Good up to " << FormatValue(lim.goodPct, 2) << " %, Check up to " << FormatValue(lim.checkPct, 2)
    << " %, Blunder above\n";
  std::vector<FitObs> fit;
  int index = 0;
  for (const ScaleCheck& c : checks) {
    const CheckResult r = EvaluateCheck(c, scale, lim);
    o << "  " << ++index << ". " << (c.calibration ? "calibration  " : "check        ") << "stated " << FormatValue(r.stated, 4) << " "
      << UnitLabel(c.unit) << ", reads " << FormatValue(r.measured, 4) << " " << UnitLabel(c.unit) << ", difference "
      << Signed(r.diff, 4) << " " << UnitLabel(c.unit) << " (" << Signed(r.pct, 2) << " %) - " << VerdictName(r.verdict) << "\n";
    fit.push_back({c.MeasuredPt(), c.StatedMetres()});
  }
  const BestFit bf = BestFitScale(fit);
  if (bf.valid) {
    PageScale fitted = scale;
    fitted.label.clear();
    fitted.note.clear();
    fitted.realValue = bf.metresPerPt * 72.0 / UnitInMetres(scale.realUnit);
    o << "Best fit of " << checks.size() << " checks: " << fitted.RatioText() << "\n";
    for (size_t i = 0; i < bf.outlier.size(); ++i)
      if (bf.outlier[i])
        o << "  Outlier: check " << (i + 1) << " (" << FormatValue(bf.deviation[i] * 100.0, 2) << " % from the median)\n";
    if (checks.size() == 2) {
      const double d = std::fabs(fit[0].metres / fit[0].pts / (fit[1].metres / fit[1].pts) - 1.0) * 100.0;
      if (d > 0.25)
        o << "  The two checks disagree by " << FormatValue(d, 2) << " %: add a third to see which one is off.\n";
    }
  }
  o << "A check proves the picked distances agree, not that the drawing is to scale.\n";
  return o.str();
}

} // namespace pdfview
