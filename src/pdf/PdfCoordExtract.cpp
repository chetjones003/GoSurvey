#include "PdfCoordExtract.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <regex>

namespace pdfview {

namespace {

struct Run {
  std::string text;
  float x0, y0, x1, y1;
};

std::string Trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos)
    return {};
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::string Upper(const std::string& s) {
  std::string u = s;
  for (char& c : u)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return u;
}

/// Runs on one visual line, left to right.
using Line = std::vector<Run>;

/// Groups filtered runs into visual lines (top to bottom, each line left to right), the way the
/// three sample shapes read: a run's line is whichever existing line has a close-enough average
/// vertical centre, else it starts a new line.
std::vector<Line> GroupIntoLines(std::vector<Run> runs) {
  std::sort(runs.begin(), runs.end(), [](const Run& a, const Run& b) {
    const float ay = (a.y0 + a.y1) * 0.5f, by = (b.y0 + b.y1) * 0.5f;
    if (std::fabs(ay - by) > 1e-3f)
      return ay > by; // page points: larger y is higher on the page
    return a.x0 < b.x0;
  });
  std::vector<Line> lines;
  for (auto& r : runs) {
    const float cy = (r.y0 + r.y1) * 0.5f;
    const float h = std::max(1.f, r.y1 - r.y0);
    if (!lines.empty()) {
      float lastCy = 0.f;
      for (auto& x : lines.back())
        lastCy += (x.y0 + x.y1) * 0.5f;
      lastCy /= static_cast<float>(lines.back().size());
      if (std::fabs(cy - lastCy) <= h * 0.6f) {
        lines.back().push_back(r);
        continue;
      }
    }
    lines.push_back({r});
  }
  for (auto& l : lines)
    std::sort(l.begin(), l.end(), [](const Run& a, const Run& b) { return a.x0 < b.x0; });
  return lines;
}

std::string LineText(const Line& l) {
  std::string s;
  for (size_t i = 0; i < l.size(); ++i) {
    if (i)
      s += ' ';
    s += l[i].text;
  }
  return Trim(s);
}

bool TryParseDouble(const std::string& s, double& out) {
  try {
    size_t pos = 0;
    out = std::stod(s, &pos);
    return pos == s.size();
  } catch (...) {
    return false;
  }
}

bool TryParseInt(const std::string& s, int& out) {
  try {
    size_t pos = 0;
    double d = std::stod(s, &pos);
    if (pos != s.size())
      return false;
    out = static_cast<int>(d);
    return true;
  } catch (...) {
    return false;
  }
}

// Whole-TOKEN anchors (regex_match, not regex_search): a text run counts as a coordinate label only
// when its ENTIRE text is just that label, exactly how every sample sheet actually prints one -
// "N303385.313" or "N:303267.23" is its own PDF text run, glued together with no space, never split
// across runs and never sharing a run with anything else. Classifying RAW RUNS (not lines merged by
// proximity) is deliberate: merging runs into "lines" first, even with a tight horizontal gap, let nearby
// unrelated drawing text (a "(P2)" callout, a dimension) get glued onto a real label on a dense sheet and
// break the exact match, or - with a looser regex_search - get picked up as if it WERE a coordinate
// field. A loose regex_search was also what let a long run-on annotation ("BLDG COLUMN & ... EL.
// 101'-0"") get matched as a coordinate field in the first place.
const std::regex kReNorthingWhole(R"(^N\.?:?\s*(-?\d+(?:\.\d+)?)$)", std::regex::icase);
const std::regex kReEastingWhole(R"(^E\.?:?\s*(-?\d+(?:\.\d+)?)$)", std::regex::icase);
const std::regex kReElevationWhole(R"(^EL(?:EV)?\.?:?\s*(-?\d+(?:\.\d+)?)(?!['’])$)", std::regex::icase);

enum class LabelKind { Northing, Easting, Elevation, Other };

struct Label {
  LabelKind kind;
  std::string text; ///< the run's text (Other) or the matched number (Northing/Easting/Elevation)
  double value = 0.0;
  float x0, y0, y1;
};

std::vector<Label> ClassifyRuns(const std::vector<Run>& runs) {
  std::vector<Label> labels;
  labels.reserve(runs.size());
  for (const Run& run : runs) {
    const std::string text = Trim(run.text);
    if (text.empty())
      continue;
    std::smatch m;
    double v = 0.0;
    if (std::regex_match(text, m, kReNorthingWhole) && TryParseDouble(m[1].str(), v)) {
      labels.push_back({LabelKind::Northing, text, v, run.x0, run.y0, run.y1});
    } else if (std::regex_match(text, m, kReEastingWhole) && TryParseDouble(m[1].str(), v)) {
      labels.push_back({LabelKind::Easting, text, v, run.x0, run.y0, run.y1});
    } else if (std::regex_match(text, m, kReElevationWhole) && TryParseDouble(m[1].str(), v)) {
      labels.push_back({LabelKind::Elevation, text, v, run.x0, run.y0, run.y1});
    } else {
      labels.push_back({LabelKind::Other, text, 0.0, run.x0, run.y0, run.y1});
    }
  }
  return labels;
}

float LabelDist(const Label& a, const Label& b) {
  const float acx = a.x0, bcx = b.x0; // labels in one callout share a left margin; compare that, not centres
  const float acy = (a.y0 + a.y1) * 0.5f, bcy = (b.y0 + b.y1) * 0.5f;
  return std::hypot(acx - bcx, acy - bcy);
}

/// REQ-399 clause 4a/c: pairs each Northing label with its nearest still-free Easting label (within a
/// bounded distance, so a stray northing on one part of the sheet never pairs with an easting on
/// another), then attaches the nearest Elevation label and a directly-adjacent non-coordinate line as
/// the description, each independently bounded the same way. One candidate per pair; unmatched
/// northings/eastings (and "Other" text that never sits right next to a real pair) are simply not used.
std::vector<CoordCandidate> PairNorthingEasting(const std::vector<Label>& labels) {
  std::vector<CoordCandidate> out;
  float sumH = 0.f;
  size_t neCount = 0;
  for (const Label& l : labels)
    if (l.kind == LabelKind::Northing || l.kind == LabelKind::Easting) {
      sumH += std::max(1.f, l.y1 - l.y0);
      ++neCount;
    }
  if (neCount == 0)
    return out;
  const float meanH = sumH / static_cast<float>(neCount);
  const float maxPairDist = meanH * 8.f;   // N and E are normally one line apart, same left margin
  const float maxAttachDist = meanH * 3.f; // elevation/description must sit right against the pair

  std::vector<bool> used(labels.size(), false);
  for (size_t i = 0; i < labels.size(); ++i) {
    if (labels[i].kind != LabelKind::Northing || used[i])
      continue;
    int best = -1;
    float bestDist = 0.f;
    for (size_t j = 0; j < labels.size(); ++j) {
      if (labels[j].kind != LabelKind::Easting || used[j])
        continue;
      const float d = LabelDist(labels[i], labels[j]);
      if (d <= maxPairDist && (best < 0 || d < bestDist)) {
        best = static_cast<int>(j);
        bestDist = d;
      }
    }
    if (best < 0)
      continue;
    used[i] = true;
    used[static_cast<size_t>(best)] = true;

    CoordCandidate c;
    c.northing = labels[i].value;
    c.easting = labels[static_cast<size_t>(best)].value;

    int bestEl = -1;
    float bestElDist = 0.f;
    int bestDesc = -1;
    float bestDescDist = 0.f;
    for (size_t k = 0; k < labels.size(); ++k) {
      if (used[k])
        continue;
      const float d = std::min(LabelDist(labels[i], labels[k]), LabelDist(labels[static_cast<size_t>(best)], labels[k]));
      if (d > maxAttachDist)
        continue;
      if (labels[k].kind == LabelKind::Elevation && (bestEl < 0 || d < bestElDist)) {
        bestEl = static_cast<int>(k);
        bestElDist = d;
      } else if (labels[k].kind == LabelKind::Other && (bestDesc < 0 || d < bestDescDist)) {
        bestDesc = static_cast<int>(k);
        bestDescDist = d;
      }
    }
    std::string sourceText = labels[i].text + "\n" + labels[static_cast<size_t>(best)].text;
    if (bestEl >= 0) {
      c.elevation = labels[static_cast<size_t>(bestEl)].value;
      used[static_cast<size_t>(bestEl)] = true;
      sourceText += "\n" + labels[static_cast<size_t>(bestEl)].text;
    }
    if (bestDesc >= 0) {
      c.description = labels[static_cast<size_t>(bestDesc)].text;
      used[static_cast<size_t>(bestDesc)] = true;
      sourceText = labels[static_cast<size_t>(bestDesc)].text + "\n" + sourceText;
    }
    c.sourceText = sourceText;
    out.push_back(std::move(c));
  }
  return out;
}

enum class Col { None, Point, Description, Elevation, Northing, Easting };

Col ClassifyHeaderCell(const std::string& raw) {
  const std::string u = Upper(Trim(raw));
  if (u.find("NORTH") != std::string::npos)
    return Col::Northing;
  if (u.find("EAST") != std::string::npos)
    return Col::Easting;
  if (u.find("ELEV") != std::string::npos || u == "EL")
    return Col::Elevation;
  if (u.find("DESC") != std::string::npos)
    return Col::Description;
  if (u.find("POINT") != std::string::npos || u == "PT" || u == "PT#" || u == "NO" || u == "NO." || u == "NUM" ||
      u == "NUMBER")
    return Col::Point;
  return Col::None;
}

struct HeaderCol {
  Col kind;
  float xCenter;
};

/// A table (REQ-399 clause 4b): a header line naming Northing and Easting columns, each following
/// line being one row mapped to the header nearest (by x) to each of its cells.
bool TryParseTable(const std::vector<Line>& lines, std::vector<CoordCandidate>& out) {
  for (size_t hi = 0; hi < lines.size(); ++hi) {
    std::vector<HeaderCol> cols;
    bool haveN = false, haveE = false;
    for (const auto& run : lines[hi]) {
      const Col k = ClassifyHeaderCell(run.text);
      if (k == Col::None)
        continue;
      cols.push_back({k, (run.x0 + run.x1) * 0.5f});
      if (k == Col::Northing)
        haveN = true;
      if (k == Col::Easting)
        haveE = true;
    }
    if (!haveN || !haveE || cols.size() < 2)
      continue;

    std::vector<CoordCandidate> rows;
    for (size_t ri = hi + 1; ri < lines.size(); ++ri) {
      CoordCandidate c;
      bool rowHaveN = false, rowHaveE = false;
      std::string sourceText = LineText(lines[ri]);
      for (const auto& run : lines[ri]) {
        const float cx = (run.x0 + run.x1) * 0.5f;
        const HeaderCol* best = nullptr;
        float bestDist = 0.f;
        for (const auto& hc : cols) {
          const float d = std::fabs(hc.xCenter - cx);
          if (best == nullptr || d < bestDist) {
            best = &hc;
            bestDist = d;
          }
        }
        if (best == nullptr)
          continue;
        const std::string cell = Trim(run.text);
        switch (best->kind) {
        case Col::Point: {
          int v;
          if (TryParseInt(cell, v))
            c.pointNumber = v;
          break;
        }
        case Col::Description:
          c.description = cell;
          break;
        case Col::Elevation: {
          double v;
          if (TryParseDouble(cell, v))
            c.elevation = v;
          break;
        }
        case Col::Northing: {
          double v;
          if (TryParseDouble(cell, v)) {
            c.northing = v;
            rowHaveN = true;
          }
          break;
        }
        case Col::Easting: {
          double v;
          if (TryParseDouble(cell, v)) {
            c.easting = v;
            rowHaveE = true;
          }
          break;
        }
        case Col::None:
          break;
        }
      }
      if (rowHaveN && rowHaveE) {
        c.sourceText = sourceText;
        rows.push_back(std::move(c));
      }
      // a row with no northing/easting (a footer note, e.g. "POINT TABLE TO BE REVISED") is
      // simply skipped, not treated as ending the table.
    }
    if (!rows.empty()) {
      out = std::move(rows);
      return true;
    }
  }
  return false;
}

ExtractResult ExtractCoordinatesImpl(const std::vector<DimText>& texts, float rx0, float ry0, float rx1, float ry1) {
  const float x0 = std::min(rx0, rx1), x1 = std::max(rx0, rx1);
  const float y0 = std::min(ry0, ry1), y1 = std::max(ry0, ry1);

  std::vector<Run> runs;
  for (const auto& t : texts) {
    const float cx = (t.x0 + t.x1) * 0.5f;
    const float cy = (t.y0 + t.y1) * 0.5f;
    if (cx < x0 || cx > x1 || cy < y0 || cy > y1)
      continue;
    const std::string s = Trim(t.text);
    if (s.empty())
      continue;
    runs.push_back({s, t.x0, t.y0, t.x1, t.y1});
  }

  ExtractResult result;
  if (runs.empty()) {
    result.status = ExtractStatus::NoText;
    result.message = "This region has no selectable text. It is likely a scanned image, and "
                      "GoSurvey does not yet have OCR text recognition.";
    return result;
  }

  std::vector<CoordCandidate> tableRows;
  if (TryParseTable(GroupIntoLines(runs), tableRows)) {
    result.status = ExtractStatus::Ok;
    result.candidates = std::move(tableRows);
    return result;
  }

  // Not a table: the region may hold several independent callouts (REQ-399 clause 4a/c). Classify each
  // raw text run directly (no line-merging - see ClassifyRuns) and pair each northing with its nearest
  // free easting.
  result.candidates = PairNorthingEasting(ClassifyRuns(runs));
  if (!result.candidates.empty()) {
    result.status = ExtractStatus::Ok;
    return result;
  }

  result.status = ExtractStatus::Unparsed;
  result.message = "No recognizable northing/easting text was found in this region.";
  return result;
}

} // namespace

// REQ-201: a region whose text trips something unexpected (a std::regex_error from pathological or
// malformed text, an out-of-range number) is reported, never left to crash the app.
ExtractResult ExtractCoordinates(const std::vector<DimText>& texts, float rx0, float ry0, float rx1, float ry1) {
  try {
    return ExtractCoordinatesImpl(texts, rx0, ry0, rx1, ry1);
  } catch (const std::exception&) {
    ExtractResult result;
    result.status = ExtractStatus::Unparsed;
    result.message = "This region's text could not be read.";
    return result;
  }
}

} // namespace pdfview
