#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "PdfMeasure.hpp"

#include "PdfDocument.hpp"
#include "PdfRaw.hpp"

#include <fpdf_save.h>
#include <fpdfview.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

namespace pdfview {

// ---------------------------------------------------------------------------------------------------
// Units and scale maths
// ---------------------------------------------------------------------------------------------------

const std::vector<Unit>& AllUnits() {
  static const std::vector<Unit> all = {Unit::Inch, Unit::Foot,       Unit::Yard,  Unit::Mile,
                                        Unit::Millimetre, Unit::Centimetre, Unit::Metre, Unit::Kilometre};
  return all;
}

const char* UnitLabel(Unit u) {
  switch (u) {
  case Unit::Inch: return "in";
  case Unit::Foot: return "ft";
  case Unit::Yard: return "yd";
  case Unit::Mile: return "mi";
  case Unit::Millimetre: return "mm";
  case Unit::Centimetre: return "cm";
  case Unit::Metre: return "m";
  case Unit::Kilometre: return "km";
  }
  return "";
}

const char* UnitName(Unit u) {
  switch (u) {
  case Unit::Inch: return "inch";
  case Unit::Foot: return "foot";
  case Unit::Yard: return "yard";
  case Unit::Mile: return "mile";
  case Unit::Millimetre: return "millimetre";
  case Unit::Centimetre: return "centimetre";
  case Unit::Metre: return "metre";
  case Unit::Kilometre: return "kilometre";
  }
  return "";
}

double UnitInMetres(Unit u) {
  switch (u) {
  case Unit::Inch: return 0.0254;
  case Unit::Foot: return 0.3048;
  case Unit::Yard: return 0.9144;
  case Unit::Mile: return 1609.344;
  case Unit::Millimetre: return 0.001;
  case Unit::Centimetre: return 0.01;
  case Unit::Metre: return 1.0;
  case Unit::Kilometre: return 1000.0;
  }
  return 1.0;
}

bool ParseUnit(const std::string& text, Unit& out) {
  std::string t;
  for (char c : text)
    if (!std::isspace(static_cast<unsigned char>(c)))
      t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  struct Alias {
    const char* name;
    Unit unit;
  };
  static const Alias aliases[] = {
      {"in", Unit::Inch},       {"inch", Unit::Inch},         {"inches", Unit::Inch},       {"\"", Unit::Inch},
      {"ft", Unit::Foot},       {"foot", Unit::Foot},         {"feet", Unit::Foot},         {"'", Unit::Foot},
      {"yd", Unit::Yard},       {"yard", Unit::Yard},         {"yards", Unit::Yard},        {"mi", Unit::Mile},
      {"mile", Unit::Mile},     {"miles", Unit::Mile},        {"mm", Unit::Millimetre},     {"millimetre", Unit::Millimetre},
      {"millimeter", Unit::Millimetre}, {"millimetres", Unit::Millimetre}, {"millimeters", Unit::Millimetre},
      {"cm", Unit::Centimetre}, {"centimetre", Unit::Centimetre}, {"centimeter", Unit::Centimetre},
      {"centimetres", Unit::Centimetre}, {"centimeters", Unit::Centimetre}, {"m", Unit::Metre},
      {"metre", Unit::Metre},   {"meter", Unit::Metre},       {"metres", Unit::Metre},      {"meters", Unit::Metre},
      {"km", Unit::Kilometre},  {"kilometre", Unit::Kilometre}, {"kilometer", Unit::Kilometre},
      {"kilometres", Unit::Kilometre}, {"kilometers", Unit::Kilometre},
  };
  for (const Alias& a : aliases)
    if (t == a.name) {
      out = a.unit;
      return true;
    }
  return false;
}

std::string FormatValue(double v, int decimals) {
  char b[64];
  std::snprintf(b, sizeof(b), "%.*f", std::clamp(decimals, 0, 8), v);
  return b;
}

namespace {

std::string Trimmed(double v) { // 0.0625 -> "0.0625", 20 -> "20"
  char b[64];
  std::snprintf(b, sizeof(b), "%.6g", v);
  return b;
}

} // namespace

double PageScale::RealPerPoint() const {
  if (!Valid())
    return 0.0;
  const double pagePoints = pageValue * UnitInMetres(pageUnit) / 0.0254 * 72.0;
  return realValue / pagePoints;
}

std::string PageScale::RatioText() const {
  if (!label.empty())
    return label;
  return Trimmed(pageValue) + " " + UnitLabel(pageUnit) + " = " + Trimmed(realValue) + " " + UnitLabel(realUnit);
}

bool PageScale::operator==(const PageScale& o) const {
  return pageValue == o.pageValue && pageUnit == o.pageUnit && realValue == o.realValue && realUnit == o.realUnit &&
         label == o.label;
}

PageScale ScaleFromCalibration(double pagePoints, double realValue, Unit realUnit) {
  PageScale s;
  if (pagePoints <= 0.0 || realValue <= 0.0) {
    s.pageValue = 0.0; // invalid
    return s;
  }
  s.pageUnit = Unit::Inch;
  s.pageValue = 1.0;
  s.realUnit = realUnit;
  s.realValue = realValue * 72.0 / pagePoints; // the real length of one page inch
  return s;
}

const std::vector<PageScale>& PresetScales() {
  static const std::vector<PageScale> list = [] {
    std::vector<PageScale> out;
    const auto add = [&](double pv, Unit pu, double rv, Unit ru, const char* label) {
      PageScale s;
      s.pageValue = pv;
      s.pageUnit = pu;
      s.realValue = rv;
      s.realUnit = ru;
      s.label = label;
      out.push_back(s);
    };
    // Architectural: a fraction of an inch on the sheet is one foot.
    add(1.0 / 16, Unit::Inch, 1, Unit::Foot, "1/16\" = 1'-0\"");
    add(3.0 / 32, Unit::Inch, 1, Unit::Foot, "3/32\" = 1'-0\"");
    add(1.0 / 8, Unit::Inch, 1, Unit::Foot, "1/8\" = 1'-0\"");
    add(3.0 / 16, Unit::Inch, 1, Unit::Foot, "3/16\" = 1'-0\"");
    add(1.0 / 4, Unit::Inch, 1, Unit::Foot, "1/4\" = 1'-0\"");
    add(3.0 / 8, Unit::Inch, 1, Unit::Foot, "3/8\" = 1'-0\"");
    add(1.0 / 2, Unit::Inch, 1, Unit::Foot, "1/2\" = 1'-0\"");
    add(3.0 / 4, Unit::Inch, 1, Unit::Foot, "3/4\" = 1'-0\"");
    add(1.0, Unit::Inch, 1, Unit::Foot, "1\" = 1'-0\"");
    add(1.5, Unit::Inch, 1, Unit::Foot, "1 1/2\" = 1'-0\"");
    add(3.0, Unit::Inch, 1, Unit::Foot, "3\" = 1'-0\"");
    // Engineering: one inch on the sheet is so many feet.
    for (int ft : {10, 20, 30, 40, 50, 60, 100, 200}) {
      const std::string l = "1\" = " + std::to_string(ft) + "'";
      PageScale s;
      s.pageValue = 1.0;
      s.pageUnit = Unit::Inch;
      s.realValue = ft;
      s.realUnit = Unit::Foot;
      s.label = l;
      out.push_back(s);
    }
    // Metric: 1 mm on the sheet is N mm in reality, shown in metres.
    for (int n : {1, 2, 5, 10, 20, 25, 50, 75, 100, 200, 250, 500, 1000, 2000}) {
      PageScale s;
      s.pageValue = 1.0;
      s.pageUnit = Unit::Millimetre;
      s.realValue = n / 1000.0;
      s.realUnit = Unit::Metre;
      s.label = "1:" + std::to_string(n);
      out.push_back(s);
    }
    return out;
  }();
  return list;
}

// ---------------------------------------------------------------------------------------------------
// The /VP entry
// ---------------------------------------------------------------------------------------------------

namespace {

std::string PdfString(const std::string& s) {
  std::string out = "(";
  for (char c : s) {
    if (c == '(' || c == ')' || c == 92)
      out += static_cast<char>(92);
    out += c;
  }
  return out + ")";
}

std::string Num(double v) {
  char b[48];
  std::snprintf(b, sizeof(b), "%.10g", v);
  return b;
}

// The unescaped text of the literal string starting at s[pos] == '('.
std::string ReadString(const std::string& s, size_t pos) {
  std::string out;
  int depth = 0;
  for (size_t i = pos; i < s.size(); ++i) {
    const char c = s[i];
    if (c == 92 && i + 1 < s.size()) {
      out += s[++i];
    } else if (c == '(') {
      if (depth++ > 0)
        out += c;
    } else if (c == ')') {
      if (--depth == 0)
        return out;
      out += c;
    } else if (depth > 0) {
      out += c;
    }
  }
  return out;
}

} // namespace

std::string BuildViewport(const PageScale& s, double x0, double y0, double x1, double y1) {
  const std::string unit = UnitLabel(s.realUnit);
  const double perPt = s.RealPerPoint();
  std::string fmt = "/Type/NumberFormat/F/D/D 100";
  std::ostringstream o;
  o << "/VP[<</Type/Viewport/BBox[" << Num(x0) << " " << Num(y0) << " " << Num(x1) << " " << Num(y1)
    << "]/Name(GoSurvey)/Measure<</Type/Measure/Subtype/RL/R" << PdfString(s.RatioText()) << "/X[<<" << fmt << "/U"
    << PdfString(unit) << "/C " << Num(perPt) << ">>]/D[<<" << fmt << "/U" << PdfString(unit) << "/C 1>>]/A[<<" << fmt << "/U"
    << PdfString("sq " + unit) << "/C 1>>]/O[0 0]>>>>]";
  return o.str();
}

bool ParseViewport(const std::string& text, PageScale& out, std::string& why) {
  why.clear();
  const size_t vp = raw::FindKey(text, "/VP");
  if (vp == std::string::npos)
    return false;
  const std::string rest = text.substr(vp);
  const size_t m = rest.find("/Measure");
  if (m == std::string::npos)
    return false;
  const std::string sub = rest.substr(m);
  if (sub.find("/Subtype/RL") == std::string::npos && sub.find("/Subtype /RL") == std::string::npos) {
    why = "the page's measurement is not a plain scale (it is geospatial or another kind)";
    return false;
  }
  for (const char* key : {"/X", "/D"}) {
    const size_t k = raw::FindKey(sub, key);
    if (k == std::string::npos || k >= sub.size() || sub[k] != '[')
      continue;
    const size_t e = raw::SkipValue(sub, k);
    if (e == std::string::npos)
      continue;
    const std::string arr = sub.substr(k, e - k);
    const size_t c = raw::FindKey(arr, "/C");
    const size_t u = raw::FindKey(arr, "/U");
    if (c == std::string::npos || u == std::string::npos || u >= arr.size() || arr[u] != '(') {
      why = "the page's scale entry is incomplete";
      return false;
    }
    const double perPt = std::atof(arr.c_str() + c);
    Unit unit;
    if (!ParseUnit(ReadString(arr, u), unit)) {
      why = "the page's scale uses the unit \"" + ReadString(arr, u) + "\", which GoSurvey does not know";
      return false;
    }
    if (!(perPt > 0.0) || !std::isfinite(perPt)) {
      why = "the page's scale is not a positive number";
      return false;
    }
    out = PageScale{};
    out.pageUnit = Unit::Inch;
    out.pageValue = 1.0;
    out.realUnit = unit;
    out.realValue = perPt * 72.0;
    const size_t r = raw::FindKey(sub, "/R");
    if (r != std::string::npos && r < sub.size() && sub[r] == '(')
      out.label = ReadString(sub, r);
    return true;
  }
  why = "the page's measurement has no scale entry";
  return false;
}

// ---------------------------------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------------------------------

namespace {

struct MemWriter : FPDF_FILEWRITE {
  std::string bytes;
};
int WriteMem(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
  static_cast<MemWriter*>(self)->bytes.append(static_cast<const char*>(data), size);
  return 1;
}

} // namespace

ScaleRead ReadPageScales(const std::filesystem::path& file) {
  ScaleRead r;
  std::string bytes;
  {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
      r.error = "cannot read the file";
      return r;
    }
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  MemWriter w;
  int pageCount = 0;
  {
    std::lock_guard<std::recursive_mutex> lock(PdfiumMutex());
    FPDF_InitLibrary();
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument(bytes.data(), static_cast<int>(bytes.size()), nullptr);
    if (doc == nullptr) {
      r.error = "the file could not be read as a PDF";
      return r;
    }
    pageCount = FPDF_GetPageCount(doc);
    w.version = 1;
    w.WriteBlock = &WriteMem;
    if (FPDF_SaveAsCopy(doc, &w, FPDF_NO_INCREMENTAL) == 0)
      r.error = "the file could not be rewritten to read its page scales";
    FPDF_CloseDocument(doc);
  }
  if (!r.error.empty())
    return r;
  const std::map<int, raw::Span> objs = raw::ScanObjects(w.bytes);
  std::vector<int> pages;
  std::string why;
  if (!raw::PageObjects(w.bytes, objs, pages, why) || static_cast<int>(pages.size()) != pageCount) {
    r.error = why.empty() ? "the page tree does not match the page count" : why;
    return r;
  }
  for (int i = 0; i < pageCount; ++i) {
    const std::string body = raw::Body(w.bytes, objs, pages[static_cast<size_t>(i)]);
    const size_t vp = raw::FindKey(body, "/VP");
    if (vp == std::string::npos)
      continue;
    // Only the /VP value is expanded: the page's /Parent would otherwise pull in every other page's entry.
    size_t vpEnd = std::string::npos;
    if (vp < body.size() && body[vp] == '[') {
      vpEnd = raw::SkipValue(body, vp);
    } else {
      int ref = 0;
      raw::ParseRef(body, vp, ref, vpEnd);
      if (vpEnd == 0)
        vpEnd = std::string::npos;
    }
    if (vpEnd == std::string::npos) {
      r.unusable[i] = "the page's scale entry could not be read";
      continue;
    }
    PageScale s;
    std::string reason;
    if (ParseViewport("/VP " + raw::Expand(w.bytes, objs, body.substr(vp, vpEnd - vp)), s, reason))
      r.scales[i] = s;
    else if (!reason.empty())
      r.unusable[i] = reason;
  }
  return r;
}

std::string ApplyPageScales(std::string& pdf, const std::map<int, PageScale>& changes, const std::vector<PageSize>& sizes) {
  if (changes.empty())
    return {};
  const std::map<int, raw::Span> objs = raw::ScanObjects(pdf);
  std::vector<int> pages;
  std::string why;
  if (!raw::PageObjects(pdf, objs, pages, why))
    return "cannot set the scale: " + why;
  struct Edit {
    size_t body, end;
    std::string text;
  };
  std::vector<Edit> edits;
  for (const auto& [page, scale] : changes) {
    if (page < 0 || page >= static_cast<int>(pages.size()) || page >= static_cast<int>(sizes.size()))
      return "cannot set the scale: page " + std::to_string(page + 1) + " is not in the file";
    const raw::Span& sp = objs.at(pages[static_cast<size_t>(page)]);
    std::string body = pdf.substr(sp.body, sp.end - sp.body);
    // Drop any /VP the page already has (an array, or a reference to one).
    const size_t at = raw::FindKey(body, "/VP");
    if (at != std::string::npos) {
      size_t valueEnd = std::string::npos;
      if (at < body.size() && body[at] == '[') {
        valueEnd = raw::SkipValue(body, at);
      } else {
        int num = 0;
        raw::ParseRef(body, at, num, valueEnd);
        if (valueEnd == 0)
          valueEnd = std::string::npos;
      }
      if (valueEnd == std::string::npos)
        return "cannot set the scale: the existing scale entry on page " + std::to_string(page + 1) + " is not in a form GoSurvey can replace";
      const size_t keyAt = body.rfind("/VP", at);
      body.erase(keyAt, valueEnd - keyAt);
    }
    if (scale.Valid()) {
      const size_t close = body.rfind(">>");
      if (close == std::string::npos)
        return "cannot set the scale: page " + std::to_string(page + 1) + " has no dictionary to put it in";
      double x0 = 0, y0 = 0, x1 = sizes[static_cast<size_t>(page)].wPt, y1 = sizes[static_cast<size_t>(page)].hPt;
      const size_t mb = raw::FindKey(body, "/MediaBox");
      if (mb != std::string::npos && mb < body.size() && body[mb] == '[') {
        double v[4] = {0, 0, 0, 0};
        const char* p = body.c_str() + mb + 1;
        char* end = nullptr;
        int n = 0;
        for (; n < 4; ++n) {
          v[n] = std::strtod(p, &end);
          if (end == p)
            break;
          p = end;
        }
        if (n == 4 && v[2] > v[0] && v[3] > v[1]) {
          x0 = v[0];
          y0 = v[1];
          x1 = v[2];
          y1 = v[3];
        }
      }
      body.insert(close, BuildViewport(scale, x0, y0, x1, y1));
    }
    edits.push_back({sp.body, sp.end, body});
  }
  std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.body > b.body; });
  std::string out = pdf;
  for (const Edit& e : edits)
    out.replace(e.body, e.end - e.body, e.text);
  std::string withXref = raw::AppendXref(out);
  if (withXref.empty())
    return "cannot set the scale: the saved file's structure is not supported";
  pdf = std::move(withXref);
  return {};
}

} // namespace pdfview
