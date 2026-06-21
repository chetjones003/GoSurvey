#pragma once

#include <cctype>
#include <string>
#include <vector>

// Parser for AutoCAD .pat hatch-pattern definition files (REQ-043 follow-up). The grammar is:
//
//   ; comment lines start with a semicolon
//   *NAME, optional description text
//   angle, x-origin, y-origin, delta-x, delta-y [, dash-1, dash-2, …]
//   …one line per family member…
//
// Each family line is an infinite family of parallel lines at `angle`; delta-y is the perpendicular
// spacing between members, delta-x staggers successive members along the line direction, and the dash
// list (positive = pen-down, negative = pen-up, 0 = dot) defines the dash pattern along each line.
// Pure text → structs, dependency-free, so it is unit-tested without any file IO or the GUI.

namespace hatchpat {

struct Line {
  double angleDeg = 0.0;
  double x0 = 0.0, y0 = 0.0;        ///< pattern origin (pattern units)
  double dx = 0.0, dy = 0.0;        ///< delta along the line / perpendicular spacing
  std::vector<double> dashes;       ///< empty → a solid (continuous) line
};

struct Def {
  std::string name;                 ///< upper-case pattern name (e.g. "ANSI31")
  std::string description;
  std::vector<Line> lines;
};

namespace detail {

inline std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

inline std::string UpperCopy(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

// Split on commas into trimmed fields.
inline std::vector<std::string> SplitCsv(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == ',') {
      out.push_back(Trim(s.substr(start, i - start)));
      start = i + 1;
    }
  }
  return out;
}

inline bool ParseDouble(const std::string& s, double* out) {
  if (s.empty()) return false;
  try {
    size_t idx = 0;
    const double v = std::stod(s, &idx);
    *out = v;
    return idx > 0;
  } catch (...) {
    return false;
  }
}

}  // namespace detail

/// Parse \p text into \p out (appends). Returns the number of pattern definitions parsed.
inline int Parse(const std::string& text, std::vector<Def>* out) {
  if (!out) return 0;
  const int before = static_cast<int>(out->size());
  std::string line;
  auto flushLine = [&](const std::string& raw) {
    const std::string t = detail::Trim(raw);
    if (t.empty() || t[0] == ';')
      return;  // blank or comment
    if (t[0] == '*') {
      // *NAME, description…  (name = up to the first comma)
      Def d;
      const size_t comma = t.find(',');
      const std::string namePart = (comma == std::string::npos) ? t.substr(1) : t.substr(1, comma - 1);
      d.name = detail::UpperCopy(detail::Trim(namePart));
      if (comma != std::string::npos)
        d.description = detail::Trim(t.substr(comma + 1));
      out->push_back(std::move(d));
      return;
    }
    if (out->size() <= static_cast<size_t>(before))
      return;  // a data line before any *NAME — ignore
    const std::vector<std::string> f = detail::SplitCsv(t);
    if (f.size() < 5)
      return;  // need at least angle,x,y,dx,dy
    Line ln;
    double v = 0.0;
    if (!detail::ParseDouble(f[0], &ln.angleDeg)) return;
    if (!detail::ParseDouble(f[1], &ln.x0)) return;
    if (!detail::ParseDouble(f[2], &ln.y0)) return;
    if (!detail::ParseDouble(f[3], &ln.dx)) return;
    if (!detail::ParseDouble(f[4], &ln.dy)) return;
    for (size_t i = 5; i < f.size(); ++i)
      if (!f[i].empty() && detail::ParseDouble(f[i], &v))
        ln.dashes.push_back(v);
    out->back().lines.push_back(std::move(ln));
  };
  for (char c : text) {
    if (c == '\n') { flushLine(line); line.clear(); }
    else if (c != '\r') line += c;
  }
  flushLine(line);
  return static_cast<int>(out->size()) - before;
}

/// Case-insensitive lookup by name; nullptr if absent.
inline const Def* Find(const std::vector<Def>& defs, const std::string& name) {
  const std::string up = detail::UpperCopy(detail::Trim(name));
  for (const Def& d : defs)
    if (d.name == up)
      return &d;
  return nullptr;
}

}  // namespace hatchpat
