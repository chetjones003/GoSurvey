#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "PdfAnnotate.hpp"

#include "PdfDocument.hpp"

#include <fpdf_annot.h>
#include <fpdf_edit.h>
#include <fpdf_save.h>
#include <fpdfview.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <system_error>

namespace pdfview {

// ---------------------------------------------------------------------------------------------------
// Annot / AnnotSession
// ---------------------------------------------------------------------------------------------------

bool Annot::operator==(const Annot& o) const {
  return kind == o.kind && page == o.page && x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1 &&
         color == o.color && thickness == o.thickness && fill == o.fill && text == o.text && font == o.font &&
         bold == o.bold && italic == o.italic && fontFile == o.fontFile && fontSize == o.fontSize && pts == o.pts &&
         decimals == o.decimals && offset == o.offset;
}

// ---------------------------------------------------------------------------------------------------
// Dimension values (REQ-391)
// ---------------------------------------------------------------------------------------------------

double PathLengthPt(const std::vector<std::pair<float, float>>& pts, bool closed) {
  double sum = 0.0;
  for (size_t i = 1; i < pts.size(); ++i)
    sum += std::hypot(static_cast<double>(pts[i].first - pts[i - 1].first), static_cast<double>(pts[i].second - pts[i - 1].second));
  if (closed && pts.size() > 2)
    sum += std::hypot(static_cast<double>(pts.back().first - pts.front().first),
                      static_cast<double>(pts.back().second - pts.front().second));
  return sum;
}

double PolygonAreaSqPt(const std::vector<std::pair<float, float>>& pts) {
  double twice = 0.0;
  for (size_t i = 0; i < pts.size(); ++i) {
    const auto& a = pts[i];
    const auto& b = pts[(i + 1) % pts.size()];
    twice += static_cast<double>(a.first) * b.second - static_cast<double>(b.first) * a.second;
  }
  return std::fabs(twice) * 0.5;
}

double AngleDegrees(const std::vector<std::pair<float, float>>& pts) {
  if (pts.size() < 3)
    return 0.0;
  const double ax = static_cast<double>(pts[0].first) - pts[1].first, ay = static_cast<double>(pts[0].second) - pts[1].second;
  const double bx = static_cast<double>(pts[2].first) - pts[1].first, by = static_cast<double>(pts[2].second) - pts[1].second;
  const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
  if (la <= 0.0 || lb <= 0.0)
    return 0.0;
  const double c = std::clamp((ax * bx + ay * by) / (la * lb), -1.0, 1.0);
  return std::acos(c) * 180.0 / 3.14159265358979323846;
}

bool DimensionComplete(const Annot& a) {
  switch (a.kind) {
  case Annot::Kind::Length: return a.pts.size() == 2;
  case Annot::Kind::PolyLength: return a.pts.size() >= 2;
  case Annot::Kind::Area: return a.pts.size() >= 3;
  case Annot::Kind::Angle: return a.pts.size() == 3;
  default: return false;
  }
}

DimLine LengthDimLine(const Annot& a) {
  DimLine d;
  if (a.pts.size() < 2)
    return d;
  const float dx = a.pts[1].first - a.pts[0].first, dy = a.pts[1].second - a.pts[0].second;
  const float len = std::hypot(dx, dy);
  if (len > 1e-6f) {
    d.nx = -dy / len;
    d.ny = dx / len;
  }
  d.x0 = a.pts[0].first + d.nx * a.offset;
  d.y0 = a.pts[0].second + d.ny * a.offset;
  d.x1 = a.pts[1].first + d.nx * a.offset;
  d.y1 = a.pts[1].second + d.ny * a.offset;
  return d;
}

float DimensionLabelAngleDeg(const Annot& a) {
  if (a.kind != Annot::Kind::Length || a.pts.size() < 2)
    return 0.f;
  float deg = std::atan2(a.pts[1].second - a.pts[0].second, a.pts[1].first - a.pts[0].first) * 180.f / 3.14159265f;
  if (deg > 90.f)
    deg -= 180.f;
  else if (deg <= -90.f)
    deg += 180.f;
  return deg;
}

std::pair<float, float> DimensionLabelAnchor(const Annot& a) {
  const auto& pts = a.pts;
  if (pts.empty())
    return {0.f, 0.f};
  const float fs = std::max(4.f, a.fontSize);
  float ax = 0.f, ay = 0.f;
  if (a.kind == Annot::Kind::Length && pts.size() >= 2) {
    const DimLine d = LengthDimLine(a);
    const float mx = (d.x0 + d.x1) * 0.5f, my = (d.y0 + d.y1) * 0.5f;
    // The label sits just above its line: along the "up" of the text, which is the line's left normal turned
    // with the label so it stays on the readable side.
    const float rad = DimensionLabelAngleDeg(a) * 3.14159265f / 180.f;
    return {mx - std::sin(rad) * fs * 0.45f, my + std::cos(rad) * fs * 0.45f};
  }
  if (a.kind == Annot::Kind::Area) {
    for (const auto& p : pts) {
      ax += p.first;
      ay += p.second;
    }
    ax /= static_cast<float>(pts.size());
    ay /= static_cast<float>(pts.size());
  } else if (a.kind == Annot::Kind::Angle && pts.size() >= 3) {
    const float d1x = pts[0].first - pts[1].first, d1y = pts[0].second - pts[1].second;
    const float d2x = pts[2].first - pts[1].first, d2y = pts[2].second - pts[1].second;
    const float l1 = std::max(1e-3f, std::hypot(d1x, d1y)), l2 = std::max(1e-3f, std::hypot(d2x, d2y));
    float bx = d1x / l1 + d2x / l2, by = d1y / l1 + d2y / l2;
    const float bl = std::hypot(bx, by);
    if (bl < 1e-3f) { // a straight angle: the bisector is perpendicular to the line
      bx = -d1y / l1;
      by = d1x / l1;
    } else {
      bx /= bl;
      by /= bl;
    }
    ax = pts[1].first + bx * fs * 2.5f;
    ay = pts[1].second + by * fs * 2.5f;
  } else {
    float best = -1.f;
    for (size_t i = 1; i < pts.size(); ++i) {
      const float len = std::hypot(pts[i].first - pts[i - 1].first, pts[i].second - pts[i - 1].second);
      if (len > best) {
        best = len;
        ax = (pts[i].first + pts[i - 1].first) * 0.5f;
        ay = (pts[i].second + pts[i - 1].second) * 0.5f;
      }
    }
  }
  return {ax, ay};
}

std::string DimensionLabel(const Annot& a, const PageScale& s) {
  const std::string u = UnitLabel(s.realUnit);
  switch (a.kind) {
  case Annot::Kind::Length:
  case Annot::Kind::PolyLength:
    return FormatValue(s.PointsToReal(PathLengthPt(a.pts, false)), a.decimals) + " " + u;
  case Annot::Kind::Area:
    return "A = " + FormatValue(s.SqPointsToReal(PolygonAreaSqPt(a.pts)), a.decimals) + " sq " + u + "\nP = " +
           FormatValue(s.PointsToReal(PathLengthPt(a.pts, true)), a.decimals) + " " + u;
  case Annot::Kind::Angle:
    return FormatValue(AngleDegrees(a.pts), a.decimals) + "\xC2\xB0";
  default:
    return {};
  }
}

void AnnotSession::Push() {
  undo_.push_back(state_);
  redo_.clear();
}

int AnnotSession::Add(const Annot& a) {
  Push();
  state_.items.push_back(a);
  return static_cast<int>(state_.items.size()) - 1;
}

bool AnnotSession::Remove(int index) {
  if (index < 0 || index >= static_cast<int>(state_.items.size()))
    return false;
  Push();
  state_.items.erase(state_.items.begin() + index);
  return true;
}

bool AnnotSession::Replace(int index, const Annot& a) {
  if (index < 0 || index >= static_cast<int>(state_.items.size()) || state_.items[static_cast<size_t>(index)] == a)
    return false;
  Push();
  state_.items[static_cast<size_t>(index)] = a;
  return true;
}

bool AnnotSession::SetScales(const std::map<int, PageScale>& changes, const ScaleCheck* alsoAdd) {
  State next = state_;
  for (const auto& [page, scale] : changes)
    next.scales[page] = scale;
  if (alsoAdd != nullptr)
    next.checks.push_back(*alsoAdd);
  if (next == state_ && alsoAdd == nullptr)
    return false;
  Push();
  state_ = std::move(next);
  return true;
}

int AnnotSession::AddCheck(const ScaleCheck& c) {
  Push();
  state_.checks.push_back(c);
  return static_cast<int>(state_.checks.size()) - 1;
}

bool AnnotSession::RemoveCheck(int index) {
  if (index < 0 || index >= static_cast<int>(state_.checks.size()))
    return false;
  Push();
  state_.checks.erase(state_.checks.begin() + index);
  return true;
}

bool AnnotSession::Undo() {
  if (undo_.empty())
    return false;
  redo_.push_back(state_);
  state_ = std::move(undo_.back());
  undo_.pop_back();
  return true;
}

bool AnnotSession::Redo() {
  if (redo_.empty())
    return false;
  undo_.push_back(state_);
  state_ = std::move(redo_.back());
  redo_.pop_back();
  return true;
}

void EstimateTextBox(const std::string& utf8, float fontSize, float& wPt, float& hPt) {
  size_t lines = 1, widest = 0, cur = 0;
  for (unsigned char c : utf8) {
    if (c == '\n') {
      ++lines;
      cur = 0;
    } else if ((c & 0xC0) != 0x80) { // count code points, not bytes
      widest = std::max(widest, ++cur);
    }
  }
  wPt = std::max(1.f, static_cast<float>(widest)) * fontSize * 0.55f;
  hPt = static_cast<float>(lines) * fontSize * 1.2f;
}

LeaderGeom LeaderLine(const Annot& a) {
  LeaderGeom g;
  const float tx = a.pts.empty() ? a.x0 : a.pts[0].first, ty = a.pts.empty() ? a.y0 : a.pts[0].second;
  const float l = std::min(a.x0, a.x1), r = std::max(a.x0, a.x1), b = std::min(a.y0, a.y1), t = std::max(a.y0, a.y1);
  const float cx = (l + r) * 0.5f, cy = (b + t) * 0.5f;
  const float mids[4][2] = {{l, cy}, {r, cy}, {cx, t}, {cx, b}};
  int best = 0;
  float bestD = 1e30f;
  for (int i = 0; i < 4; ++i) {
    const float d = std::hypot(mids[i][0] - tx, mids[i][1] - ty);
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  g.sx = mids[best][0];
  g.sy = mids[best][1];
  g.tx = tx;
  g.ty = ty;
  const float len = std::hypot(g.sx - tx, g.sy - ty);
  if (len > 1e-3f) {
    const float al = std::min(std::max(6.f, a.thickness * 3.f + 4.f), len * 0.8f), aw = al * 0.35f;
    const float ux = (g.sx - tx) / len, uy = (g.sy - ty) / len; // from the tip back along the line
    g.w1x = tx + ux * al - uy * aw;
    g.w1y = ty + uy * al + ux * aw;
    g.w2x = tx + ux * al + uy * aw;
    g.w2y = ty + uy * al - ux * aw;
  } else {
    g.w1x = g.w2x = tx;
    g.w1y = g.w2y = ty;
  }
  return g;
}

// ---------------------------------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------------------------------

namespace {

std::vector<unsigned short> Utf16(const std::string& s) {
  std::vector<unsigned short> out;
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    unsigned cp = 0xFFFD;
    size_t len = 1;
    if (c < 0x80) {
      cp = c;
    } else if ((c >> 5) == 0x6 && i + 1 < s.size()) {
      cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
      len = 2;
    } else if ((c >> 4) == 0xE && i + 2 < s.size()) {
      cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
           (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
      len = 3;
    } else if ((c >> 3) == 0x1E && i + 3 < s.size()) {
      cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
           ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
      len = 4;
    }
    i += len;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<unsigned short>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<unsigned short>(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back(static_cast<unsigned short>(cp));
    }
  }
  out.push_back(0);
  return out;
}

std::string Utf8(const std::vector<unsigned short>& u16, size_t n) {
  std::string out;
  for (size_t i = 0; i < n; ++i) {
    unsigned cp = u16[i];
    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < n) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (u16[i + 1] - 0xDC00u);
      ++i;
    }
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  return out;
}

bool SetString(FPDF_ANNOTATION a, const char* key, const std::string& utf8) {
  const std::vector<unsigned short> w = Utf16(utf8);
  return FPDFAnnot_SetStringValue(a, key, reinterpret_cast<FPDF_WIDESTRING>(w.data())) != 0;
}

std::string GetString(FPDF_ANNOTATION a, const char* key) {
  const unsigned long bytes = FPDFAnnot_GetStringValue(a, key, nullptr, 0);
  if (bytes < 2)
    return {};
  std::vector<unsigned short> buf(bytes / 2 + 1, 0);
  FPDFAnnot_GetStringValue(a, key, reinterpret_cast<FPDF_WCHAR*>(buf.data()), bytes);
  return Utf8(buf, bytes / 2 - 1);
}

struct Rgb {
  unsigned r, g, b;
};
Rgb Split(unsigned c) { return {(c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF}; }

std::string Num(float v) {
  char b[32];
  std::snprintf(b, sizeof(b), "%.4f", static_cast<double>(v));
  return b;
}

// A colour component 0..255 as the PDF 0..1 number: enough digits that PDFium's truncating read gives the byte back.
std::string Comp(unsigned v) {
  char b[32];
  std::snprintf(b, sizeof(b), "%.6f", (static_cast<double>(v) + 0.001) / 255.0);
  return b;
}

// The PDF font behind a text annotation: a standard font by name, or an embedded TrueType file.
std::string StandardFontName(const Annot& a) {
  std::string f = a.font;
  std::string fam = "Helvetica";
  if (f.rfind("Times", 0) == 0)
    fam = "Times";
  else if (f.rfind("Courier", 0) == 0)
    fam = "Courier";
  if (fam == "Times")
    return a.bold ? (a.italic ? "Times-BoldItalic" : "Times-Bold") : (a.italic ? "Times-Italic" : "Times-Roman");
  return fam + (a.bold ? (a.italic ? "-BoldOblique" : "-Bold") : (a.italic ? "-Oblique" : ""));
}

constexpr size_t kPadChars = 200; // room, inside the placeholder annotation, for the Line / FreeText byte patch

// One annotation that PDFium cannot write as-is: it is created as a Stamp (which keeps the drawn objects as its
// appearance) and rewritten to its true type in the saved bytes (D-2026-10-06-e).
struct Patch {
  std::string marker;     ///< unique /NM value that finds the annotation in the saved file
  std::string subtype;    ///< "Line" or "FreeText"
  std::string extra;      ///< for a Line: "/L[x1 y1 x2 y2]/C[r g b]/Border[0 0 w]"
};

bool AddRect(FPDF_PAGE page, const Annot& a) {
  const float t = std::max(0.f, a.thickness);
  const float l = std::min(a.x0, a.x1), r = std::max(a.x0, a.x1);
  const float b = std::min(a.y0, a.y1), tp = std::max(a.y0, a.y1);
  const bool ellipse = a.kind == Annot::Kind::Ellipse;
  FPDF_ANNOTATION an = FPDFPage_CreateAnnot(page, ellipse ? FPDF_ANNOT_CIRCLE : FPDF_ANNOT_SQUARE);
  if (an == nullptr)
    return false;
  const Rgb c = Split(a.color);
  FS_RECTF rc{l - t / 2, tp + t / 2, r + t / 2, b - t / 2}; // left, top, right, bottom
  bool ok = FPDFAnnot_SetRect(an, &rc) != 0;
  ok = ok && FPDFAnnot_SetColor(an, FPDFANNOT_COLORTYPE_Color, c.r, c.g, c.b, 255) != 0;
  if (a.fill)
    ok = ok && FPDFAnnot_SetColor(an, FPDFANNOT_COLORTYPE_InteriorColor, c.r, c.g, c.b, 255) != 0;
  ok = ok && FPDFAnnot_SetBorder(an, 0.f, 0.f, t) != 0;
  // The appearance PDFium does not generate for us: the shape, in page space, inside the annotation rect.
  std::ostringstream ap;
  ap << "q " << Comp(c.r) << " " << Comp(c.g) << " " << Comp(c.b) << " RG ";
  if (a.fill)
    ap << Comp(c.r) << " " << Comp(c.g) << " " << Comp(c.b) << " rg ";
  ap << Num(t) << " w ";
  if (!ellipse) {
    ap << Num(l) << " " << Num(b) << " " << Num(r - l) << " " << Num(tp - b) << " re ";
  } else {
    const float cx = (l + r) / 2, cy = (b + tp) / 2, rx = (r - l) / 2, ry = (tp - b) / 2, k = 0.5522847f;
    ap << Num(cx + rx) << " " << Num(cy) << " m ";
    ap << Num(cx + rx) << " " << Num(cy + k * ry) << " " << Num(cx + k * rx) << " " << Num(cy + ry) << " " << Num(cx)
       << " " << Num(cy + ry) << " c ";
    ap << Num(cx - k * rx) << " " << Num(cy + ry) << " " << Num(cx - rx) << " " << Num(cy + k * ry) << " " << Num(cx - rx)
       << " " << Num(cy) << " c ";
    ap << Num(cx - rx) << " " << Num(cy - k * ry) << " " << Num(cx - k * rx) << " " << Num(cy - ry) << " " << Num(cx)
       << " " << Num(cy - ry) << " c ";
    ap << Num(cx + k * rx) << " " << Num(cy - ry) << " " << Num(cx + rx) << " " << Num(cy - k * ry) << " " << Num(cx + rx)
       << " " << Num(cy) << " c h ";
  }
  ap << (a.fill ? "B" : "S") << " Q";
  const std::vector<unsigned short> w = Utf16(ap.str());
  ok = ok && FPDFAnnot_SetAP(an, FPDF_ANNOT_APPEARANCEMODE_NORMAL, reinterpret_cast<FPDF_WIDESTRING>(w.data())) != 0;
  FPDFPage_CloseAnnot(an);
  return ok;
}

// A Stamp annotation holding the given page objects; its rectangle is exactly their bounds.
FPDF_ANNOTATION NewObjectAnnot(FPDF_PAGE page, const std::vector<FPDF_PAGEOBJECT>& objs) {
  FPDF_ANNOTATION an = FPDFPage_CreateAnnot(page, FPDF_ANNOT_STAMP);
  if (an == nullptr)
    return nullptr;
  float l = 1e30f, b = 1e30f, r = -1e30f, t = -1e30f;
  for (FPDF_PAGEOBJECT o : objs) {
    float ol, ob, orr, ot;
    if (FPDFPageObj_GetBounds(o, &ol, &ob, &orr, &ot)) {
      l = std::min(l, ol);
      b = std::min(b, ob);
      r = std::max(r, orr);
      t = std::max(t, ot);
    }
  }
  // The rectangle goes in first: PDFium takes the appearance's BBox from it when the objects are appended.
  FS_RECTF rc{l, t, r, b};
  if (!FPDFAnnot_SetRect(an, &rc)) {
    FPDFPage_CloseAnnot(an);
    return nullptr;
  }
  for (FPDF_PAGEOBJECT o : objs)
    if (!FPDFAnnot_AppendObject(an, o)) {
      FPDFPage_CloseAnnot(an);
      return nullptr;
    }
  return an;
}

bool AddLine(FPDF_PAGE page, const Annot& a, int serial, std::vector<Patch>& patches) {
  const Rgb c = Split(a.color);
  FPDF_PAGEOBJECT path = FPDFPageObj_CreateNewPath(a.x0, a.y0);
  if (path == nullptr)
    return false;
  FPDFPath_LineTo(path, a.x1, a.y1);
  FPDFPageObj_SetStrokeColor(path, c.r, c.g, c.b, 255);
  FPDFPageObj_SetStrokeWidth(path, std::max(0.1f, a.thickness));
  FPDFPath_SetDrawMode(path, 0, 1);
  FPDFPageObj_Transform(path, 1, 0, 0, 1, 0, 0); // makes PDFium work out the object's bounds, stroke width included
  FPDF_ANNOTATION an = NewObjectAnnot(page, {path});
  if (an == nullptr) {
    FPDFPageObj_Destroy(path);
    return false;
  }
  Patch p;
  char id[16];
  std::snprintf(id, sizeof(id), "gsa%05d", serial);
  p.marker = id;
  p.subtype = "Line";
  // PDFium refuses colour and border on a Stamp, so they ride in the same patch as the end points.
  p.extra = "/L[" + Num(a.x0) + " " + Num(a.y0) + " " + Num(a.x1) + " " + Num(a.y1) + "]/C[" + Comp(c.r) + " " +
            Comp(c.g) + " " + Comp(c.b) + "]/Border[0 0 " + Num(std::max(0.1f, a.thickness)) + "]";
  bool ok = SetString(an, "NM", p.marker);
  ok = ok && SetString(an, "GSPAD", std::string(kPadChars, 'x'));
  FPDFPage_CloseAnnot(an);
  if (ok)
    patches.push_back(p);
  return ok;
}

FPDF_PAGEOBJECT StrokePath(const std::vector<std::pair<float, float>>& pts, const Rgb& c, float width, bool fillToo) {
  FPDF_PAGEOBJECT p = FPDFPageObj_CreateNewPath(pts[0].first, pts[0].second);
  if (p == nullptr)
    return nullptr;
  for (size_t i = 1; i < pts.size(); ++i)
    FPDFPath_LineTo(p, pts[i].first, pts[i].second);
  if (fillToo)
    FPDFPath_Close(p);
  FPDFPageObj_SetStrokeColor(p, c.r, c.g, c.b, 255);
  FPDFPageObj_SetStrokeWidth(p, width);
  if (fillToo)
    FPDFPageObj_SetFillColor(p, c.r, c.g, c.b, 255);
  FPDFPath_SetDrawMode(p, fillToo ? 2 : 0, 1);
  FPDFPageObj_Transform(p, 1, 0, 0, 1, 0, 0);
  return p;
}

// A Length with its offset (REQ-391): extension lines from the two measured points to the dimension line, the
// dimension line with an arrow at each end, and the label turned along the line. Same placeholder-then-patch as
// the other dimensions; /L is the dimension line and /LL the (negated) offset back to the measured points.
bool AddLengthDimension(FPDF_DOCUMENT doc, FPDF_PAGE page, const Annot& a, const PageScale& scale, int serial,
                        std::vector<Patch>& patches, std::string& error) {
  const Rgb c = Split(a.color);
  const float w = std::max(0.1f, a.thickness);
  const float fs = std::max(4.f, a.fontSize);
  const DimLine d = LengthDimLine(a);
  const float len = std::hypot(d.x1 - d.x0, d.y1 - d.y0);
  std::vector<FPDF_PAGEOBJECT> objs;
  const auto add = [&](FPDF_PAGEOBJECT o) {
    if (o != nullptr)
      objs.push_back(o);
  };
  add(StrokePath({{d.x0, d.y0}, {d.x1, d.y1}}, c, w, false));
  if (std::fabs(a.offset) > 0.5f) { // extension lines, with a small gap at the measured point and an overshoot
    const float s = a.offset > 0.f ? 1.f : -1.f, gap = std::min(2.f, std::fabs(a.offset) * 0.3f), over = 2.f;
    for (int i = 0; i < 2; ++i) {
      const auto& p = a.pts[static_cast<size_t>(i)];
      add(StrokePath({{p.first + d.nx * s * gap, p.second + d.ny * s * gap},
                      {(i == 0 ? d.x0 : d.x1) + d.nx * s * over, (i == 0 ? d.y0 : d.y1) + d.ny * s * over}},
                     c, w, false));
    }
  }
  if (len > 1e-3f) { // arrows
    const float ux = (d.x1 - d.x0) / len, uy = (d.y1 - d.y0) / len;
    const float al = std::clamp(fs * 0.6f, 2.f, len / 3.f), aw = al * 0.3f;
    add(StrokePath({{d.x0, d.y0}, {d.x0 + ux * al - uy * aw, d.y0 + uy * al + ux * aw}, {d.x0 + ux * al + uy * aw, d.y0 + uy * al - ux * aw}},
                   c, 0.1f, true));
    add(StrokePath({{d.x1, d.y1}, {d.x1 - ux * al - uy * aw, d.y1 - uy * al + ux * aw}, {d.x1 - ux * al + uy * aw, d.y1 - uy * al - ux * aw}},
                   c, 0.1f, true));
  }
  const std::string label = DimensionLabel(a, scale);
  FPDF_FONT font = FPDFText_LoadStandardFont(doc, "Helvetica");
  if (font == nullptr) {
    for (FPDF_PAGEOBJECT o : objs)
      FPDFPageObj_Destroy(o);
    error = "the font for the dimension label could not be loaded";
    return false;
  }
  {
    float tw = 0.f, th = 0.f;
    EstimateTextBox(label, fs, tw, th);
    const float rad = DimensionLabelAngleDeg(a) * 3.14159265f / 180.f, cs = std::cos(rad), sn = std::sin(rad);
    const auto [ax, ay] = DimensionLabelAnchor(a); // the middle of the label
    FPDF_PAGEOBJECT t = FPDFPageObj_CreateTextObj(doc, font, fs);
    if (t != nullptr) {
      const std::vector<unsigned short> u16 = Utf16(label);
      FPDFText_SetText(t, reinterpret_cast<FPDF_WIDESTRING>(u16.data()));
      FPDFPageObj_SetFillColor(t, c.r, c.g, c.b, 255);
      // Baseline start: back half the text width along the line, down 0.3 of the size so the text is centred on the anchor.
      const float px = ax - cs * tw * 0.5f + sn * fs * 0.3f, py = ay - sn * tw * 0.5f - cs * fs * 0.3f;
      FPDFPageObj_Transform(t, cs, sn, -sn, cs, px, py);
      objs.push_back(t);
    }
  }
  FPDF_ANNOTATION an = NewObjectAnnot(page, objs);
  if (an == nullptr) {
    for (FPDF_PAGEOBJECT o : objs)
      FPDFPageObj_Destroy(o);
    error = "PDFium could not create a dimension annotation";
    return false;
  }
  Patch p;
  char id[16];
  std::snprintf(id, sizeof(id), "gsa%05d", serial);
  p.marker = id;
  p.subtype = "Line";
  p.extra = "/L[" + Num(d.x0) + " " + Num(d.y0) + " " + Num(d.x1) + " " + Num(d.y1) + "]/LL " + Num(-a.offset) +
            "/LLE 0/IT/LineDimension/C[" + Comp(c.r) + " " + Comp(c.g) + " " + Comp(c.b) + "]/Border[0 0 " + Num(w) +
            "]/Measure" + BuildMeasureDict(scale);
  bool ok = SetString(an, "NM", p.marker);
  ok = ok && SetString(an, "GSPAD", std::string(p.extra.size() + 48, 'x'));
  ok = ok && SetString(an, "Contents", label);
  FPDFPage_CloseAnnot(an);
  if (ok)
    patches.push_back(p);
  else
    error = "PDFium could not fill in a dimension annotation";
  return ok;
}

// A scaled dimension (REQ-391): a Stamp placeholder drawn as the path plus its label, then rewritten to a true
// Line / PolyLine / Polygon carrying the page's /Measure (D-2026-10-06-e).
bool AddDimension(FPDF_DOCUMENT doc, FPDF_PAGE page, const Annot& a, const PageScale& scale, int serial,
                  std::vector<Patch>& patches, std::string& error) {
  if (!DimensionComplete(a)) {
    error = "a dimension on page " + std::to_string(a.page + 1) + " has too few points";
    return false;
  }
  if (a.kind == Annot::Kind::Length)
    return AddLengthDimension(doc, page, a, scale, serial, patches, error);
  const Rgb c = Split(a.color);
  const auto& pts = a.pts;
  const bool closed = a.kind == Annot::Kind::Area;
  FPDF_PAGEOBJECT path = FPDFPageObj_CreateNewPath(pts[0].first, pts[0].second);
  if (path == nullptr)
    return false;
  for (size_t i = 1; i < pts.size(); ++i)
    FPDFPath_LineTo(path, pts[i].first, pts[i].second);
  if (closed)
    FPDFPath_Close(path);
  FPDFPageObj_SetStrokeColor(path, c.r, c.g, c.b, 255);
  FPDFPageObj_SetStrokeWidth(path, std::max(0.1f, a.thickness));
  FPDFPath_SetDrawMode(path, 0, 1);
  FPDFPageObj_Transform(path, 1, 0, 0, 1, 0, 0);
  std::vector<FPDF_PAGEOBJECT> objs = {path};

  const float fs = std::max(4.f, a.fontSize);
  const auto [ax, ay] = DimensionLabelAnchor(a);
  const std::string label = DimensionLabel(a, scale);
  FPDF_FONT font = FPDFText_LoadStandardFont(doc, "Helvetica");
  if (font == nullptr) {
    FPDFPageObj_Destroy(path);
    error = "the font for the dimension label could not be loaded";
    return false;
  }
  float tw = 0.f, th = 0.f;
  EstimateTextBox(label, fs, tw, th);
  float baseline = ay + fs * 0.4f + (th - fs * 1.2f); // the first line sits above the anchor, later ones below it
  std::istringstream lines(label);
  std::string line;
  while (std::getline(lines, line)) {
    FPDF_PAGEOBJECT t = FPDFPageObj_CreateTextObj(doc, font, fs);
    if (t == nullptr)
      break;
    float w1 = 0.f, h1 = 0.f;
    EstimateTextBox(line, fs, w1, h1);
    const std::vector<unsigned short> w = Utf16(line);
    FPDFText_SetText(t, reinterpret_cast<FPDF_WIDESTRING>(w.data()));
    FPDFPageObj_SetFillColor(t, c.r, c.g, c.b, 255);
    FPDFPageObj_Transform(t, 1, 0, 0, 1, ax - w1 * 0.5f, baseline);
    objs.push_back(t);
    baseline -= fs * 1.2f;
  }
  FPDF_ANNOTATION an = NewObjectAnnot(page, objs);
  if (an == nullptr) {
    for (FPDF_PAGEOBJECT o : objs)
      FPDFPageObj_Destroy(o);
    error = "PDFium could not create a dimension annotation";
    return false;
  }
  Patch p;
  char id[16];
  std::snprintf(id, sizeof(id), "gsa%05d", serial);
  p.marker = id;
  std::string vertices;
  for (const auto& pt : pts)
    vertices += (vertices.empty() ? "" : " ") + Num(pt.first) + " " + Num(pt.second);
  const std::string style = "/C[" + Comp(c.r) + " " + Comp(c.g) + " " + Comp(c.b) + "]/Border[0 0 " +
                            Num(std::max(0.1f, a.thickness)) + "]";
  switch (a.kind) {
  case Annot::Kind::Length:
    p.subtype = "Line";
    p.extra = "/L[" + vertices + "]/IT/LineDimension" + style + "/Measure" + BuildMeasureDict(scale);
    break;
  case Annot::Kind::PolyLength:
    p.subtype = "PolyLine";
    p.extra = "/Vertices[" + vertices + "]/IT/PolyLineDimension" + style + "/Measure" + BuildMeasureDict(scale);
    break;
  case Annot::Kind::Area:
    p.subtype = "Polygon";
    p.extra = "/Vertices[" + vertices + "]/IT/PolygonDimension" + style + "/Measure" + BuildMeasureDict(scale);
    break;
  default: // Angle: the standard has no angle dimension, so a labelled PolyLine with a GoSurvey mark
    p.subtype = "PolyLine";
    p.extra = "/Vertices[" + vertices + "]/GSKind(angle)" + style;
    break;
  }
  bool ok = SetString(an, "NM", p.marker);
  ok = ok && SetString(an, "GSPAD", std::string(p.extra.size() + 48, 'x'));
  ok = ok && SetString(an, "Contents", label);
  FPDFPage_CloseAnnot(an);
  if (ok)
    patches.push_back(p);
  else
    error = "PDFium could not fill in a dimension annotation";
  return ok;
}

// The font a note (or a Leader's text) is written in: the embedded TrueType file when it has one, else the
// standard PDF font. Null, with `error` set, when neither loads.
FPDF_FONT LoadNoteFont(FPDF_DOCUMENT doc, const Annot& a, std::string& error) {
  FPDF_FONT font = nullptr;
  std::string fontBytes;
  if (!a.fontFile.empty()) {
    std::ifstream in(std::filesystem::u8path(a.fontFile), std::ios::binary);
    fontBytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (!fontBytes.empty())
      font = FPDFText_LoadFont(doc, reinterpret_cast<const uint8_t*>(fontBytes.data()),
                               static_cast<uint32_t>(fontBytes.size()), FPDF_FONT_TRUETYPE, 0);
  }
  if (font == nullptr)
    font = FPDFText_LoadStandardFont(doc, StandardFontName(a).c_str());
  if (font == nullptr)
    error = "the font \"" + a.font + "\" could not be loaded";
  return font;
}

// One text object per line of the note, the first line's top at `top` and its left at `left`.
void NoteTextObjects(FPDF_DOCUMENT doc, FPDF_FONT font, const Annot& a, float left, float top,
                     std::vector<FPDF_PAGEOBJECT>& objs) {
  const Rgb c = Split(a.color);
  std::istringstream lines(a.text);
  std::string line;
  float baseline = top - a.fontSize * 0.9f;
  while (std::getline(lines, line)) {
    if (!line.empty()) {
      FPDF_PAGEOBJECT t = FPDFPageObj_CreateTextObj(doc, font, a.fontSize);
      if (t == nullptr)
        break;
      const std::vector<unsigned short> w = Utf16(line);
      FPDFText_SetText(t, reinterpret_cast<FPDF_WIDESTRING>(w.data()));
      FPDFPageObj_SetFillColor(t, c.r, c.g, c.b, 255);
      FPDFPageObj_Transform(t, 1, 0, 0, 1, left, baseline);
      objs.push_back(t);
    }
    baseline -= a.fontSize * 1.2f;
  }
}

// The entries both note kinds carry: /DA, the text, and the font label our own reader gets the font back from.
bool SetNoteEntries(FPDF_ANNOTATION an, const Annot& a, const std::string& marker, size_t padChars) {
  const Rgb c = Split(a.color);
  const std::string da = "/Helv " + Num(a.fontSize) + " Tf " + Comp(c.r) + " " + Comp(c.g) + " " + Comp(c.b) + " rg";
  bool ok = SetString(an, "NM", marker);
  ok = ok && SetString(an, "GSPAD", std::string(padChars, 'x'));
  ok = ok && SetString(an, "DA", da);
  ok = ok && SetString(an, "Contents", a.text);
  ok = ok && SetString(an, "GSFont", a.font + "|" + (a.bold ? "b" : "-") + "|" + (a.italic ? "i" : "-"));
  return ok;
}

bool AddText(FPDF_DOCUMENT doc, FPDF_PAGE page, const Annot& a, int serial, std::vector<Patch>& patches,
             std::string& error) {
  FPDF_FONT font = LoadNoteFont(doc, a, error);
  if (font == nullptr)
    return false;
  std::vector<FPDF_PAGEOBJECT> objs;
  NoteTextObjects(doc, font, a, std::min(a.x0, a.x1), std::max(a.y0, a.y1), objs);
  if (objs.empty()) {
    error = "a text note has no text";
    return false;
  }
  FPDF_ANNOTATION an = NewObjectAnnot(page, objs);
  if (an == nullptr) {
    for (FPDF_PAGEOBJECT o : objs)
      FPDFPageObj_Destroy(o);
    error = "PDFium could not create the text annotation";
    return false;
  }
  Patch p;
  char id[16];
  std::snprintf(id, sizeof(id), "gsa%05d", serial);
  p.marker = id;
  p.subtype = "FreeText";
  const bool ok = SetNoteEntries(an, a, p.marker, kPadChars);
  FPDFPage_CloseAnnot(an);
  if (ok)
    patches.push_back(p);
  else
    error = "PDFium could not fill in the text annotation";
  return ok;
}

// A Leader (REQ-396): a FreeText callout. The placeholder draws the box, its text and the arrow; the patch makes it
// a FreeText of intent FreeTextCallout whose /CL starts at the arrow tip. GSLeader keeps the tip and box for our
// own reader (the annotation's /Rect also covers the arrow).
bool AddLeader(FPDF_DOCUMENT doc, FPDF_PAGE page, const Annot& a, int serial, std::vector<Patch>& patches,
               std::string& error) {
  if (a.pts.empty() || a.text.empty()) {
    error = "a leader has no tip or no text";
    return false;
  }
  FPDF_FONT font = LoadNoteFont(doc, a, error);
  if (font == nullptr)
    return false;
  const Rgb c = Split(a.color);
  const float w = std::max(0.1f, a.thickness);
  const float l = std::min(a.x0, a.x1), r = std::max(a.x0, a.x1), b = std::min(a.y0, a.y1), t = std::max(a.y0, a.y1);
  const LeaderGeom g = LeaderLine(a);
  std::vector<FPDF_PAGEOBJECT> objs;
  const auto add = [&](FPDF_PAGEOBJECT o) {
    if (o != nullptr)
      objs.push_back(o);
  };
  add(StrokePath({{l, t}, {r, t}, {r, b}, {l, b}, {l, t}}, c, w, false));
  add(StrokePath({{g.sx, g.sy}, {g.tx, g.ty}}, c, w, false));
  add(StrokePath({{g.tx, g.ty}, {g.w1x, g.w1y}, {g.w2x, g.w2y}}, c, 0.1f, true));
  NoteTextObjects(doc, font, a, l + kLeaderPad, t - kLeaderPad, objs);
  FPDF_ANNOTATION an = NewObjectAnnot(page, objs);
  if (an == nullptr) {
    for (FPDF_PAGEOBJECT o : objs)
      FPDFPageObj_Destroy(o);
    error = "PDFium could not create the leader annotation";
    return false;
  }
  Patch p;
  char id[16];
  std::snprintf(id, sizeof(id), "gsa%05d", serial);
  p.marker = id;
  p.subtype = "FreeText";
  p.extra = "/IT/FreeTextCallout/CL[" + Num(g.tx) + " " + Num(g.ty) + " " + Num(g.sx) + " " + Num(g.sy) +
            "]/LE/OpenArrow";
  bool ok = SetNoteEntries(an, a, p.marker, p.extra.size() + 48);
  ok = ok && SetString(an, "GSLeader",
                       Num(g.tx) + " " + Num(g.ty) + " " + Num(l) + " " + Num(b) + " " + Num(r) + " " + Num(t) + " " + Num(w));
  FPDFPage_CloseAnnot(an);
  if (ok)
    patches.push_back(p);
  else
    error = "PDFium could not fill in the leader annotation";
  return ok;
}

// Viewer page coordinates (points from the displayed page's bottom-left) -> page user space, found by asking
// PDFium where three viewer points land. Identity for an ordinary unrotated page that starts at zero.
struct ViewerToUser {
  bool identity = true;
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; ///< ux = a x + c y + e,  uy = b x + d y + f
  void Pt(float& x, float& y) const {
    const double ux = a * x + c * y + e, uy = b * x + d * y + f;
    x = static_cast<float>(ux);
    y = static_cast<float>(uy);
  }
  void Apply(Annot& m) const {
    Pt(m.x0, m.y0);
    Pt(m.x1, m.y1);
    for (auto& p : m.pts)
      Pt(p.first, p.second);
  }
};

ViewerToUser MakeViewerToUser(FPDF_PAGE page) {
  ViewerToUser m;
  const double W = FPDF_GetPageWidthF(page), H = FPDF_GetPageHeightF(page);
  constexpr int kScale = 8;
  const int dw = std::max(1, static_cast<int>(std::lround(W * kScale))), dh = std::max(1, static_cast<int>(std::lround(H * kScale)));
  const double sx = dw / std::max(1e-3, W), sy = dh / std::max(1e-3, H);
  const auto toUser = [&](double vx, double vy, double& ux, double& uy) {
    FPDF_DeviceToPage(page, 0, 0, dw, dh, 0, static_cast<int>(std::lround(vx * sx)), static_cast<int>(std::lround((H - vy) * sy)), &ux, &uy);
  };
  double ox = 0, oy = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  toUser(0, 0, ox, oy);
  toUser(100, 0, x1, y1);
  toUser(0, 100, x2, y2);
  m.a = (x1 - ox) / 100.0;
  m.b = (y1 - oy) / 100.0;
  m.c = (x2 - ox) / 100.0;
  m.d = (y2 - oy) / 100.0;
  m.e = ox;
  m.f = oy;
  m.identity = std::fabs(m.a - 1) < 1e-3 && std::fabs(m.b) < 1e-3 && std::fabs(m.c) < 1e-3 && std::fabs(m.d - 1) < 1e-3 &&
               std::fabs(m.e) < 0.2 && std::fabs(m.f) < 0.2;
  return m;
}

struct MemWriter : FPDF_FILEWRITE {
  std::string bytes;
};
int WriteMem(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
  static_cast<MemWriter*>(self)->bytes.append(static_cast<const char*>(data), size);
  return 1;
}

// Rewrites each placeholder annotation to its true type, in place and at the same total length, so no
// cross-reference offset moves. Returns "" or the reason it could not.
std::string ApplyPatches(std::string& bytes, const std::vector<Patch>& patches) {
  for (const Patch& p : patches) {
    const std::string nm = "/NM(" + p.marker + ")";
    const size_t at = bytes.find(nm);
    if (at == std::string::npos)
      return "the saved file did not contain an expected annotation entry";
    const size_t objStart = bytes.rfind(" obj", at);
    const size_t objEnd = bytes.find("endobj", at);
    if (objStart == std::string::npos || objEnd == std::string::npos)
      return "the saved file's annotation object could not be located";
    std::string obj = bytes.substr(objStart, objEnd - objStart);

    const std::string oldType = "/Subtype/Stamp";
    const size_t st = obj.find(oldType);
    const size_t padAt = obj.find("/GSPAD(");
    if (st == std::string::npos || padAt == std::string::npos)
      return "the saved file's annotation entries were not in the expected form";
    const size_t padEnd = obj.find(')', padAt);
    if (padEnd == std::string::npos)
      return "the saved file's annotation padding was not in the expected form";
    const std::string newType = "/Subtype/" + p.subtype;
    const long delta = static_cast<long>(newType.size()) - static_cast<long>(oldType.size());
    const size_t padLen = padEnd + 1 - padAt;
    const long room = static_cast<long>(padLen) - delta;
    if (room < static_cast<long>(p.extra.size()))
      return "the saved file's annotation had no room for the line end points";
    std::string repl = p.extra + std::string(static_cast<size_t>(room) - p.extra.size(), ' ');
    // Edit the later position first so the earlier one's index stays valid.
    if (st < padAt) {
      obj.replace(padAt, padLen, repl);
      obj.replace(st, oldType.size(), newType);
    } else {
      obj.replace(st, oldType.size(), newType);
      obj.replace(padAt, padLen, repl);
    }
    if (obj.size() != objEnd - objStart)
      return "internal error: the annotation edit changed the file length";
    bytes.replace(objStart, obj.size(), obj);
  }
  return {};
}

} // namespace

std::string SaveAnnotated(const std::filesystem::path& source, const std::vector<Annot>& items,
                          const std::filesystem::path& dest, const std::map<int, PageScale>& scales,
                          const std::map<int, PageScale>& pageScales) {
  std::error_code ec;
  for (const Annot& a : items) // a length or area with no scale would be a guess: refuse before anything is written
    if (a.IsDimension() && a.kind != Annot::Kind::Angle && pageScales.count(a.page) == 0)
      return "page " + std::to_string(a.page + 1) + " has a dimension but no scale; set the page's scale first";
  if (source.lexically_normal() == dest.lexically_normal() ||
      (std::filesystem::exists(dest, ec) && std::filesystem::equivalent(source, dest, ec)))
    return "Save As must use a new file name; the original is never overwritten";

  std::string bytes;
  {
    std::ifstream in(source, std::ios::binary);
    if (!in)
      return "cannot read the original file";
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  if (bytes.empty())
    return "the original file is empty";

  std::string error;
  MemWriter w;
  {
    std::lock_guard<std::recursive_mutex> lock(PdfiumMutex());
    FPDF_InitLibrary();
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument(bytes.data(), static_cast<int>(bytes.size()), nullptr);
    if (doc == nullptr)
      return "the original file could not be read as a PDF";
    const int pageCount = FPDF_GetPageCount(doc);
    std::map<int, std::vector<const Annot*>> byPage;
    for (const Annot& a : items) {
      if (a.page < 0 || a.page >= pageCount) {
        FPDF_CloseDocument(doc);
        return "an annotation is on page " + std::to_string(a.page + 1) + ", which is not in the file";
      }
      byPage[a.page].push_back(&a);
    }
    std::vector<Patch> patches;
    int serial = 0;
    for (const auto& [pageIdx, list] : byPage) {
      FPDF_PAGE page = FPDF_LoadPage(doc, pageIdx);
      if (page == nullptr) {
        error = "page " + std::to_string(pageIdx + 1) + " could not be loaded";
        break;
      }
      // The viewer measures from the page as displayed; the file wants page user space. They differ on a rotated page
      // or one whose page box does not start at zero, so the marks are carried over before they are written.
      const ViewerToUser vu = MakeViewerToUser(page);
      for (const Annot* orig : list) {
        Annot mapped = *orig;
        if (!vu.identity)
          vu.Apply(mapped);
        const Annot* a = &mapped;
        bool ok = false;
        switch (a->kind) {
        case Annot::Kind::Rect:
        case Annot::Kind::Ellipse:
          ok = AddRect(page, *a);
          break;
        case Annot::Kind::Line:
          ok = AddLine(page, *a, ++serial, patches);
          break;
        case Annot::Kind::Text:
          ok = AddText(doc, page, *a, ++serial, patches, error);
          break;
        case Annot::Kind::Leader:
          ok = AddLeader(doc, page, *a, ++serial, patches, error);
          break;
        case Annot::Kind::Length:
        case Annot::Kind::PolyLength:
        case Annot::Kind::Area:
        case Annot::Kind::Angle: {
          const auto sc = pageScales.find(a->page);
          ok = AddDimension(doc, page, *a, sc != pageScales.end() ? sc->second : PageScale{}, ++serial, patches, error);
          break;
        }
        }
        if (!ok) {
          if (error.empty())
            error = "PDFium could not create an annotation on page " + std::to_string(pageIdx + 1);
          break;
        }
      }
      FPDF_ClosePage(page);
      if (!error.empty())
        break;
    }
    if (error.empty()) {
      w.version = 1;
      w.WriteBlock = &WriteMem;
      if (FPDF_SaveAsCopy(doc, &w, 0) == 0)
        error = "PDFium could not write the new PDF";
      else
        error = ApplyPatches(w.bytes, patches);
      if (error.empty() && !scales.empty()) { // REQ-390: each changed page's /VP, on the bytes PDFium just wrote
        std::vector<PageSize> sizes(static_cast<size_t>(pageCount));
        for (int i = 0; i < pageCount; ++i) {
          FS_SIZEF sz{};
          if (FPDF_GetPageSizeByIndexF(doc, i, &sz))
            sizes[static_cast<size_t>(i)] = {sz.width, sz.height};
        }
        error = ApplyPageScales(w.bytes, scales, sizes);
      }
    }
    FPDF_CloseDocument(doc);
  }
  if (!error.empty())
    return error;

  const std::filesystem::path tmp = dest.parent_path() / (dest.filename().string() + ".gsannot.tmp");
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(w.bytes.data(), static_cast<std::streamsize>(w.bytes.size()));
    out.close();
    if (!out) {
      std::filesystem::remove(tmp, ec);
      return "writing the new PDF failed (disk full or no permission?)";
    }
  }
  std::filesystem::rename(tmp, dest, ec);
  if (ec) {
    std::error_code ec2;
    std::filesystem::remove(tmp, ec2);
    return "could not put the new file in place: " + ec.message();
  }
  return {};
}

// ---------------------------------------------------------------------------------------------------
// Reading back
// ---------------------------------------------------------------------------------------------------

namespace {

// Pulls "<size> Tf" and "<r> <g> <b> rg" out of a /DA string.
void ParseDa(const std::string& da, float& size, unsigned& color) {
  std::istringstream in(da);
  std::vector<std::string> tok;
  std::string t;
  while (in >> t)
    tok.push_back(t);
  for (size_t i = 0; i < tok.size(); ++i) {
    if (tok[i] == "Tf" && i >= 1)
      size = static_cast<float>(std::atof(tok[i - 1].c_str()));
    if (tok[i] == "rg" && i >= 3) {
      const auto to8 = [](const std::string& s) {
        return static_cast<unsigned>(std::lround(std::clamp(std::atof(s.c_str()), 0.0, 1.0) * 255.0));
      };
      color = (to8(tok[i - 3]) << 16) | (to8(tok[i - 2]) << 8) | to8(tok[i - 1]);
    }
  }
}

} // namespace

std::vector<Annot> ReadAnnotations(const std::filesystem::path& file) {
  std::vector<Annot> out;
  std::string bytes;
  {
    std::ifstream in(file, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  if (bytes.empty())
    return out;
  std::lock_guard<std::recursive_mutex> lock(PdfiumMutex());
  FPDF_InitLibrary();
  FPDF_DOCUMENT doc = FPDF_LoadMemDocument(bytes.data(), static_cast<int>(bytes.size()), nullptr);
  if (doc == nullptr)
    return out;
  const int pages = FPDF_GetPageCount(doc);
  for (int pi = 0; pi < pages; ++pi) {
    FPDF_PAGE page = FPDF_LoadPage(doc, pi);
    if (page == nullptr)
      continue;
    const int n = FPDFPage_GetAnnotCount(page);
    for (int i = 0; i < n; ++i) {
      FPDF_ANNOTATION an = FPDFPage_GetAnnot(page, i);
      if (an == nullptr)
        continue;
      const FPDF_ANNOTATION_SUBTYPE st = FPDFAnnot_GetSubtype(an);
      Annot a;
      a.page = pi;
      FS_RECTF rc{};
      FPDFAnnot_GetRect(an, &rc);
      float h = 0.f, v = 0.f, bw = 1.f;
      FPDFAnnot_GetBorder(an, &h, &v, &bw);
      // GetColor refuses an annotation that has an appearance stream; this document is a private in-memory
      // copy that is never saved, so the appearance is dropped to read the colour entries.
      FPDFAnnot_SetAP(an, FPDF_ANNOT_APPEARANCEMODE_NORMAL, nullptr);
      unsigned r = 0, g = 0, b = 0, alpha = 0;
      const bool hasColor = FPDFAnnot_GetColor(an, FPDFANNOT_COLORTYPE_Color, &r, &g, &b, &alpha) != 0;
      a.color = (r << 16) | (g << 8) | b;
      a.thickness = bw;
      bool known = true;
      if (st == FPDF_ANNOT_LINE) {
        a.kind = FPDFAnnot_HasKey(an, "Measure") ? Annot::Kind::Length : Annot::Kind::Line;
        FS_POINTF s{}, e{};
        FPDFAnnot_GetLine(an, &s, &e);
        a.x0 = s.x;
        a.y0 = s.y;
        a.x1 = e.x;
        a.y1 = e.y;
        if (a.kind == Annot::Kind::Length) {
          // /L is the dimension line and /LL the leader length back to the measured points (to the left when
          // positive), so the measured points are the line's ends moved by LL along its left normal.
          float ll = 0.f;
          FPDFAnnot_GetNumberValue(an, "LL", &ll);
          const float dx = e.x - s.x, dy = e.y - s.y, len = std::max(1e-6f, std::hypot(dx, dy));
          const float nx = -dy / len, ny = dx / len;
          a.pts = {{s.x + nx * ll, s.y + ny * ll}, {e.x + nx * ll, e.y + ny * ll}};
          a.offset = -ll;
          a.text = GetString(an, "Contents");
        }
      } else if (st == FPDF_ANNOT_POLYLINE || st == FPDF_ANNOT_POLYGON) {
        a.kind = st == FPDF_ANNOT_POLYGON ? Annot::Kind::Area
                 : FPDFAnnot_HasKey(an, "GSKind") ? Annot::Kind::Angle
                                                  : Annot::Kind::PolyLength;
        const unsigned long count = FPDFAnnot_GetVertices(an, nullptr, 0);
        std::vector<FS_POINTF> v2(count);
        if (count > 0)
          FPDFAnnot_GetVertices(an, v2.data(), count);
        for (const FS_POINTF& p : v2)
          a.pts.push_back({p.x, p.y});
        a.text = GetString(an, "Contents");
      } else if (st == FPDF_ANNOT_SQUARE || st == FPDF_ANNOT_CIRCLE) {
        a.kind = st == FPDF_ANNOT_SQUARE ? Annot::Kind::Rect : Annot::Kind::Ellipse;
        a.x0 = rc.left + bw / 2;
        a.x1 = rc.right - bw / 2;
        a.y0 = rc.bottom + bw / 2;
        a.y1 = rc.top - bw / 2;
        a.fill = FPDFAnnot_HasKey(an, "IC") != 0;
      } else if (st == FPDF_ANNOT_FREETEXT) {
        a.kind = Annot::Kind::Text;
        a.x0 = rc.left;
        a.x1 = rc.right;
        a.y0 = rc.bottom;
        a.y1 = rc.top;
        float lead[7] = {};
        if (std::sscanf(GetString(an, "GSLeader").c_str(), "%f %f %f %f %f %f %f", &lead[0], &lead[1], &lead[2], &lead[3],
                        &lead[4], &lead[5], &lead[6]) == 7) { // a Leader: its tip and box are kept beside the rect
          a.kind = Annot::Kind::Leader;
          a.pts = {{lead[0], lead[1]}};
          a.x0 = lead[2];
          a.y0 = lead[3];
          a.x1 = lead[4];
          a.y1 = lead[5];
        }
        a.text = GetString(an, "Contents");
        float size = 12.f;
        unsigned col = 0;
        ParseDa(GetString(an, "DA"), size, col);
        a.fontSize = size;
        a.color = col;
        a.thickness = a.kind == Annot::Kind::Leader ? lead[6] : 1.f;
        const std::string f = GetString(an, "GSFont"); // family|b|i
        const size_t p1 = f.find('|');
        if (p1 != std::string::npos && f.size() >= p1 + 4) {
          a.font = f.substr(0, p1);
          a.bold = f[p1 + 1] == 'b';
          a.italic = f[p1 + 3] == 'i';
        }
      } else {
        known = false;
      }
      (void)hasColor;
      FPDFPage_CloseAnnot(an);
      if (known)
        out.push_back(a);
    }
    FPDF_ClosePage(page);
  }
  FPDF_CloseDocument(doc);
  return out;
}

} // namespace pdfview
