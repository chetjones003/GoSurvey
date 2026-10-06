#include "PdfDimAudit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace pdfview {

namespace {

// A coarse grid over axis-aligned strokes: each stroke is listed in every cell its box touches.
class SegGrid {
public:
  static constexpr float kCell = 32.f;

  void Add(uint32_t id, float x0, float y0, float x1, float y1) {
    const int cx0 = Cell(std::min(x0, x1)), cx1 = Cell(std::max(x0, x1));
    const int cy0 = Cell(std::min(y0, y1)), cy1 = Cell(std::max(y0, y1));
    for (int cy = cy0; cy <= cy1; ++cy)
      for (int cx = cx0; cx <= cx1; ++cx)
        cells_[Key(cx, cy)].push_back(id);
  }

  /// Calls f(id) for every stroke listed in a cell the box touches (a stroke can be reported more than once).
  template <class F> void Query(float x0, float y0, float x1, float y1, F&& f) const {
    for (int cy = Cell(y0); cy <= Cell(y1); ++cy)
      for (int cx = Cell(x0); cx <= Cell(x1); ++cx) {
        const auto it = cells_.find(Key(cx, cy));
        if (it == cells_.end())
          continue;
        for (uint32_t id : it->second)
          f(id);
      }
  }

private:
  static int Cell(float v) { return static_cast<int>(std::floor(v / kCell)); }
  static int64_t Key(int cx, int cy) { return (static_cast<int64_t>(cx) << 32) ^ static_cast<uint32_t>(cy); }
  std::unordered_map<int64_t, std::vector<uint32_t>> cells_;
};

// A horizontal stroke is (a..b at y = c); a vertical one is (a..b at x = c).
struct Axis {
  float a = 0.f, b = 0.f, c = 0.f;
};

struct Line {
  Axis ax;
  bool horizontal = true;
  bool checked = false;
  bool ok = false;      ///< an extension line at each end
  float ea = 0.f, eb = 0.f; ///< where the two extension lines sit along the line
};

float Dist(float a, float b) { return std::fabs(a - b); }

bool Polled(const std::function<bool()>& cancel, size_t& n) { return (++n & 1023u) == 0 && cancel && cancel(); }

} // namespace

bool MatchDimensions(const std::vector<DimText>& texts, const std::vector<DimSeg>& segs, const DimMatchParams& p,
                     const std::function<bool()>& cancel, DimMatchSet& out) {
  out = {};
  size_t tick = 0;

  // Horizontal and vertical strokes (a tiny error of drawing is tolerated; slanted strokes are not dimension lines).
  std::vector<Line> lines; // every axis-aligned stroke long enough to be a dimension line
  std::vector<Axis> hAll, vAll; // every axis-aligned stroke, as a possible extension line
  SegGrid hGrid, vGrid;         // hAll / vAll by position
  SegGrid lineGrid;             // lines by position
  for (const DimSeg& s : segs) {
    if (Polled(cancel, tick))
      return false;
    const float dx = std::fabs(s.x1 - s.x0), dy = std::fabs(s.y1 - s.y0);
    if (!std::isfinite(dx) || !std::isfinite(dy))
      continue;
    if (dy <= p.axisTolPt && dx > p.axisTolPt) { // horizontal
      Axis a{std::min(s.x0, s.x1), std::max(s.x0, s.x1), (s.y0 + s.y1) * 0.5f};
      hGrid.Add(static_cast<uint32_t>(hAll.size()), a.a, a.c, a.b, a.c);
      hAll.push_back(a);
      if (a.b - a.a >= p.minLinePt) {
        lineGrid.Add(static_cast<uint32_t>(lines.size()), a.a, a.c, a.b, a.c);
        lines.push_back({a, true, false, false, 0.f, 0.f});
      }
    } else if (dx <= p.axisTolPt && dy > p.axisTolPt) { // vertical
      Axis a{std::min(s.y0, s.y1), std::max(s.y0, s.y1), (s.x0 + s.x1) * 0.5f};
      vGrid.Add(static_cast<uint32_t>(vAll.size()), a.c, a.a, a.c, a.b);
      vAll.push_back(a);
      if (a.b - a.a >= p.minLinePt) {
        lineGrid.Add(static_cast<uint32_t>(lines.size()), a.c, a.a, a.c, a.b);
        lines.push_back({a, false, false, false, 0.f, 0.f});
      }
    }
  }

  // The extension line nearest the end \p end of \p ln (along the line): a stroke across it at that end that reaches the line.
  const auto extensionAt = [&](const Line& ln, float end, float& found) {
    const SegGrid& g = ln.horizontal ? vGrid : hGrid;
    const std::vector<Axis>& across = ln.horizontal ? vAll : hAll;
    float best = p.endTolPt + 1.f;
    // along the line the extension sits at `c` of the crossing stroke; its own span (a..b) must reach ln.ax.c
    const float x0 = ln.horizontal ? end - p.endTolPt : ln.ax.c - p.coverTolPt, x1 = ln.horizontal ? end + p.endTolPt : ln.ax.c + p.coverTolPt;
    const float y0 = ln.horizontal ? ln.ax.c - p.coverTolPt : end - p.endTolPt, y1 = ln.horizontal ? ln.ax.c + p.coverTolPt : end + p.endTolPt;
    g.Query(x0, y0, x1, y1, [&](uint32_t id) {
      const Axis& e = across[id];
      if (e.b - e.a < p.minExtensionPt)
        return;
      if (e.a > ln.ax.c + p.coverTolPt || e.b < ln.ax.c - p.coverTolPt)
        return;
      const float d = Dist(e.c, end);
      if (d <= p.endTolPt && d < best) {
        best = d;
        found = e.c;
      }
    });
    return best <= p.endTolPt;
  };
  const auto validate = [&](Line& ln) {
    if (ln.checked)
      return;
    ln.checked = true;
    float ea = 0.f, eb = 0.f;
    if (extensionAt(ln, ln.ax.a, ea) && extensionAt(ln, ln.ax.b, eb) && eb - ea >= p.minLinePt) {
      ln.ok = true;
      ln.ea = ea;
      ln.eb = eb;
    }
  };

  // Texts that read as a length with a unit (a bare number is not taken: room numbers and grid bubbles would match).
  struct Cand {
    size_t text;
    uint32_t line;
    float off;
  };
  std::vector<Cand> cands;
  std::vector<ParsedLength> parsed(texts.size());
  std::vector<bool> readable(texts.size(), false);
  for (size_t i = 0; i < texts.size(); ++i) {
    if (Polled(cancel, tick))
      return false;
    std::string why;
    if (!ParseLength(texts[i].text, parsed[i], why) || !parsed[i].hasUnit)
      continue;
    readable[i] = true;
    ++out.textsRead;
    const DimText& t = texts[i];
    const float w = t.x1 - t.x0, h = t.y1 - t.y0;
    if (!(w > 0.f) || !(h > 0.f))
      continue;
    const bool horizText = w >= h; // text runs along a horizontal line when it is wider than tall
    const float cx = (t.x0 + t.x1) * 0.5f, cy = (t.y0 + t.y1) * 0.5f;
    const float reach = std::max(p.maxOffsetMin, 3.f * std::min(w, h));
    const float qx0 = horizText ? cx : cx - reach, qx1 = horizText ? cx : cx + reach;
    const float qy0 = horizText ? cy - reach : cy, qy1 = horizText ? cy + reach : cy;
    lineGrid.Query(qx0, qy0, qx1, qy1, [&](uint32_t id) {
      Line& ln = lines[id];
      if (ln.horizontal != horizText)
        return;
      const float along = horizText ? cx : cy, across = horizText ? cy : cx;
      if (along < ln.ax.a || along > ln.ax.b)
        return;
      const float off = Dist(across, ln.ax.c);
      if (off > reach)
        return;
      validate(ln);
      if (ln.ok)
        cands.push_back({i, id, off});
    });
  }

  // Closest pairs first; each text and each line is used once.
  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
    if (a.off != b.off)
      return a.off < b.off;
    return a.text != b.text ? a.text < b.text : a.line < b.line;
  });
  std::vector<bool> textUsed(texts.size(), false), lineUsed(lines.size(), false);
  for (const Cand& c : cands) {
    if (textUsed[c.text] || lineUsed[c.line])
      continue;
    textUsed[c.text] = lineUsed[c.line] = true;
    const Line& ln = lines[c.line];
    const DimText& t = texts[c.text];
    DimMatch m;
    m.text = t.text;
    m.stated = parsed[c.text].value;
    m.unit = parsed[c.text].unit;
    m.lengthPt = ln.eb - ln.ea;
    if (ln.horizontal) {
      m.x0 = ln.ea, m.x1 = ln.eb, m.y0 = m.y1 = ln.ax.c;
    } else {
      m.y0 = ln.ea, m.y1 = ln.eb, m.x0 = m.x1 = ln.ax.c;
    }
    m.tx = (t.x0 + t.x1) * 0.5f;
    m.ty = (t.y0 + t.y1) * 0.5f;
    m.metresPerPt = m.stated * UnitInMetres(m.unit) / m.lengthPt;
    out.matches.push_back(std::move(m));
  }
  for (size_t i = 0; i < texts.size(); ++i)
    if (readable[i] && !textUsed[i])
      ++out.unmatchedText;
  // Lines with no text: every dimension line with an extension line at each end that no text took.
  for (size_t i = 0; i < lines.size(); ++i) {
    if (Polled(cancel, tick))
      return false;
    if (lineUsed[i])
      continue;
    validate(lines[i]);
    if (lines[i].ok)
      ++out.unmatchedLines;
  }
  if (cancel && cancel()) {
    out = {};
    return false;
  }
  return true;
}

DimAudit AuditDimensions(DimMatchSet set, const PageScale& scale, const CheckLimits& lim) {
  DimAudit a;
  a.set = std::move(set);
  const auto& ms = a.set.matches;
  if (a.set.textsRead == 0) {
    a.message = "No dimension text found on this page (a scanned page, or text drawn as outlines, has none to read).";
    return a;
  }
  std::vector<FitObs> fit;
  std::vector<std::pair<double, int>> worst; // |pct|, index
  for (size_t i = 0; i < ms.size(); ++i) {
    ScaleCheck c;
    c.x0 = ms[i].x0, c.y0 = ms[i].y0, c.x1 = ms[i].x1, c.y1 = ms[i].y1;
    c.stated = ms[i].stated;
    c.unit = ms[i].unit;
    const CheckResult r = scale.Valid() ? EvaluateCheck(c, scale, lim) : CheckResult{};
    a.results.push_back(r);
    if (scale.Valid()) {
      (r.verdict == Verdict::Good ? a.good : r.verdict == Verdict::Check ? a.check : a.blunder)++;
      if (r.verdict != Verdict::Good)
        worst.push_back({std::fabs(r.pct), static_cast<int>(i)});
    }
    fit.push_back({ms[i].lengthPt, ms[i].stated * UnitInMetres(ms[i].unit)});
  }
  std::sort(worst.begin(), worst.end(), [](const auto& x, const auto& y) { return x.first != y.first ? x.first > y.first : x.second < y.second; });
  for (const auto& w : worst)
    a.offenders.push_back(w.second);
  if (static_cast<int>(ms.size()) < kMinForConsensus) {
    a.message = ms.empty() ? "Dimension text was found, but none of it sits on a dimension line with extension lines."
                           : "Only " + std::to_string(ms.size()) + " dimension" + (ms.size() == 1 ? "" : "s") + " matched; " +
                                 std::to_string(kMinForConsensus) + " are needed for a consensus scale.";
    return a;
  }
  const BestFit bf = BestFitScale(fit);
  if (bf.valid) {
    a.consensusValid = true;
    a.consensusMetresPerPt = (bf.anyOutlier && bf.withoutValid) ? bf.withoutMetresPerPt : bf.metresPerPt;
  }
  return a;
}

PageScale ScaleFromConsensus(double metresPerPt, int n, const PageScale& previous) {
  PageScale s;
  s.pageUnit = Unit::Inch;
  s.pageValue = 1.0;
  s.realUnit = previous.Valid() ? previous.realUnit : Unit::Foot;
  s.realValue = metresPerPt * 72.0 / UnitInMetres(s.realUnit);
  s.note = "audit consensus, " + std::to_string(n) + " dimensions";
  if (previous.Valid())
    s.originalRealValue = previous.realValue * UnitInMetres(previous.realUnit) / UnitInMetres(s.realUnit);
  return s;
}

} // namespace pdfview
