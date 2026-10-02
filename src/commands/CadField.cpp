#include "CadField.hpp"

#include "CadCommands.hpp"
#include "geom2d.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>

namespace {

bool SvStartsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool ContainsFieldMarker(std::string_view s) { return s.find("%<") != std::string_view::npos; }

std::string ApplyPrintfFormat(std::string_view fmt, double value) {
  assert(!fmt.empty());
  if (fmt.empty())
    return "----";
  char buf[128];
  std::string f = "%";
  f.append(fmt);
  const int n = std::snprintf(buf, sizeof(buf), f.c_str(), value);
  if (n <= 0 || n >= static_cast<int>(sizeof(buf)))
    return "----";
  return std::string(buf, static_cast<size_t>(n));
}

std::string ApplyPrintfFormatInt(std::string_view fmt, long value) {
  assert(!fmt.empty());
  char buf[128];
  std::string f = "%";
  f.append(fmt);
  const int n = std::snprintf(buf, sizeof(buf), f.c_str(), value);
  if (n <= 0 || n >= static_cast<int>(sizeof(buf)))
    return "----";
  return std::string(buf, static_cast<size_t>(n));
}

const SurveyPoint* FindSurveyPointById(const AppCommandState& st, int pointId) {
  assert(pointId > 0);
  for (const SurveyPoint& p : st.surveyPoints) {
    if (p.id == pointId)
      return &p;
  }
  return nullptr;
}

double PolylineSignedAreaPlan(const AppCommandState& st, int pi) {
  assert(pi >= 0);
  if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
    return 0.0;
  const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
  const bool closed = static_cast<size_t>(pi) < st.userPolylineClosed.size() &&
                      st.userPolylineClosed[static_cast<size_t>(pi)] != 0;
  if (!closed || (v1 - v0) < 3)
    return 0.0;
  double area = 0.0;
  const int nvert = v1 - v0;
  for (int s = 0; s < nvert; ++s) {
    const int va = v0 + s;
    const int vb = (s == nvert - 1) ? v0 : v0 + s + 1;
    const size_t A = static_cast<size_t>(va) * 3;
    const size_t B = static_cast<size_t>(vb) * 3;
    if (B + 1 >= st.userPolylineVerts.size())
      break;
    const double x0 = st.userPolylineVerts[A];
    const double y0 = st.userPolylineVerts[A + 1];
    const double x1 = st.userPolylineVerts[B];
    const double y1 = st.userPolylineVerts[B + 1];
    const float bulge = static_cast<size_t>(va) < st.userPolylineVertsBulge.size()
                            ? st.userPolylineVertsBulge[static_cast<size_t>(va)]
                            : 0.f;
    if (std::fabs(static_cast<double>(bulge)) < 1e-12) {
      area += 0.5 * (x0 * y1 - x1 * y0);
      continue;
    }
    const BulgeArcSpan arc = BulgeArc(x0, y0, x1, y1, static_cast<double>(bulge));
    if (!arc.valid) {
      area += 0.5 * (x0 * y1 - x1 * y0);
      continue;
    }
    area += 0.5 * arc.radius * arc.radius * arc.sweep;
    area += 0.5 * (arc.cx * (y0 - y1) + x0 * (y1 - arc.cy) + x1 * (arc.cy - y0));
  }
  return area;
}

double PolylinePathLength(const AppCommandState& st, int pi) {
  if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
    return 0.0;
  const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
  const bool closed = static_cast<size_t>(pi) < st.userPolylineClosed.size() &&
                      st.userPolylineClosed[static_cast<size_t>(pi)] != 0;
  const int nseg = (v1 - v0) - 1 + (closed && (v1 - v0) >= 2 ? 1 : 0);
  double total = 0.0;
  for (int s = 0; s < nseg; ++s) {
    const int va = v0 + s;
    const int vb = (s == (v1 - v0) - 1) ? v0 : v0 + s + 1;
    const size_t A = static_cast<size_t>(va) * 3;
    const size_t B = static_cast<size_t>(vb) * 3;
    if (B + 1 >= st.userPolylineVerts.size())
      break;
    const float bulge = static_cast<size_t>(va) < st.userPolylineVertsBulge.size()
                            ? st.userPolylineVertsBulge[static_cast<size_t>(va)]
                            : 0.f;
    total += BulgeSegmentLength(st.userPolylineVerts[A], st.userPolylineVerts[A + 1],
                                st.userPolylineVerts[B], st.userPolylineVerts[B + 1],
                                static_cast<double>(bulge));
  }
  return total;
}

double CircleAreaPlan(const AppCommandState& st, int ci) {
  if (ci < 0 || static_cast<size_t>(ci) >= st.userCirclesCxCyZR.size() / 3)
    return 0.0;
  const size_t k = static_cast<size_t>(ci) * 3;
  const double r = st.userCirclesCxCyZR[k + 2];
  if (!(r > 0.0))
    return 0.0;
  constexpr double kPi = 3.14159265358979323846;
  return kPi * r * r;
}

double CircleCircumferencePlan(const AppCommandState& st, int ci) {
  if (ci < 0 || static_cast<size_t>(ci) >= st.userCirclesCxCyZR.size() / 3)
    return 0.0;
  const size_t k = static_cast<size_t>(ci) * 3;
  const double r = st.userCirclesCxCyZR[k + 2];
  if (!(r > 0.0))
    return 0.0;
  constexpr double kPi = 3.14159265358979323846;
  return 2.0 * kPi * r;
}

bool ParseEntityIdFromHandleToken(std::string_view token, std::uint64_t* outId) {
  assert(outId != nullptr);
  const size_t pos = token.find("EntHandle");
  std::string_view digits = token;
  if (pos != std::string_view::npos) {
    digits = token.substr(pos + 9);
    while (!digits.empty() && digits.front() == ' ')
      digits.remove_prefix(1);
  }
  if (digits.empty())
    return false;
  std::uint64_t v = 0;
  const int base = (digits.size() >= 2 && digits[0] == '0' &&
                    (digits[1] == 'x' || digits[1] == 'X'))
                       ? 16
                       : 10;
  size_t i = (base == 16) ? 2u : 0u;
  for (; i < digits.size(); ++i) {
    const char c = digits[i];
    if (c == '>' || c == '%' || c == ' ')
      break;
    int d = -1;
    if (c >= '0' && c <= '9')
      d = c - '0';
    else if (base == 16 && c >= 'a' && c <= 'f')
      d = 10 + (c - 'a');
    else if (base == 16 && c >= 'A' && c <= 'F')
      d = 10 + (c - 'A');
    else
      return false;
    v = v * static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(d);
  }
  *outId = v;
  return true;
}

std::string EvalEntityProperty(const AppCommandState& st, EntityKind kind, int index,
                               std::string_view propUpper, std::string_view format) {
  if (format.empty())
    format = ".2f";
  if (kind == EntityKind::Polyline) {
    if (propUpper == "AREA")
      return ApplyPrintfFormat(format, std::fabs(PolylineSignedAreaPlan(st, index)));
    if (propUpper == "LENGTH" || propUpper == "PERIMETER")
      return ApplyPrintfFormat(format, PolylinePathLength(st, index));
  }
  if (kind == EntityKind::Circle) {
    if (propUpper == "AREA")
      return ApplyPrintfFormat(format, CircleAreaPlan(st, index));
    if (propUpper == "CIRCUMFERENCE" || propUpper == "LENGTH" || propUpper == "PERIMETER")
      return ApplyPrintfFormat(format, CircleCircumferencePlan(st, index));
  }
  return "----";
}

std::string EvalSurveyPointProperty(const AppCommandState& st, int pointId, std::string_view propUpper,
                                    std::string_view format) {
  if (format.empty())
    format = ".2f";
  const SurveyPoint* p = FindSurveyPointById(st, pointId);
  if (p == nullptr)
    return "----";
  if (propUpper == "NUMBER" || propUpper == "POINTNUMBER")
    return ApplyPrintfFormatInt(format, static_cast<long>(p->id));
  if (propUpper == "ELEVATION" || propUpper == "Z")
    return ApplyPrintfFormat(format, p->elevation);
  if (propUpper == "NORTHING" || propUpper == "Y")
    return ApplyPrintfFormat(format, p->northing);
  if (propUpper == "EASTING" || propUpper == "X")
    return ApplyPrintfFormat(format, p->easting);
  return "----";
}

std::string EvalDocumentVar(std::string_view varUpper, const CadFieldContext& ctx) {
  if (varUpper == "FILENAME" || varUpper == "DWGNAME") {
    std::string_view path = ctx.drawingPath;
    if (path.empty())
      return "Drawing";
    const size_t slash = path.find_last_of("/\\");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
  }
  if (varUpper == "DATE" || varUpper == "SAVEDATE") {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%m/%d/%Y", &local) == 0)
      return "----";
    return buf;
  }
  if (varUpper == "LAYOUTNAME" || varUpper == "CTAB")
    return ctx.activeLayoutTabName.empty() ? std::string("Model") : std::string(ctx.activeLayoutTabName);
  return "----";
}

std::string EvalOneFieldExpression(std::string_view expr, const AppCommandState& st,
                                   const CadFieldContext& ctx) {
  assert(!expr.empty());
  std::string_view e = expr;
  if (e.size() > 1 && e[0] == '\\')
    e.remove_prefix(1);
  if (SvStartsWith(e, "AcVar ")) {
    e.remove_prefix(6);
    std::string var;
    while (!e.empty() && e.front() != ' ')
      var.push_back(static_cast<char>(e.front())), e.remove_prefix(1);
    for (char& c : var)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return EvalDocumentVar(var, ctx);
  }
  if (SvStartsWith(e, "GoSurvey ")) {
    e.remove_prefix(9);
    if (SvStartsWith(e, "Point ")) {
      e.remove_prefix(6);
      int pointId = 0;
      while (!e.empty() && e.front() >= '0' && e.front() <= '9') {
        pointId = pointId * 10 + (e.front() - '0');
        e.remove_prefix(1);
      }
      while (!e.empty() && e.front() == ' ')
        e.remove_prefix(1);
      std::string prop;
      if (SvStartsWith(e, "Prop ")) {
        e.remove_prefix(5);
        while (!e.empty() && e.front() != ' ')
          prop.push_back(static_cast<char>(e.front())), e.remove_prefix(1);
      }
      std::string_view format = ".2f";
      const size_t fpos = e.find("\\f \"");
      if (fpos != std::string_view::npos) {
        const size_t q1 = fpos + 4;
        const size_t q2 = e.find('"', q1);
        if (q2 != std::string_view::npos)
          format = e.substr(q1, q2 - q1);
      }
      for (char& c : prop)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      return EvalSurveyPointProperty(st, pointId, prop, format);
    }
    std::uint64_t entId = 0;
    if (SvStartsWith(e, "Ent ")) {
      e.remove_prefix(4);
      while (!e.empty() && e.front() >= '0' && e.front() <= '9') {
        entId = entId * 10 + static_cast<std::uint64_t>(e.front() - '0');
        e.remove_prefix(1);
      }
    }
    while (!e.empty() && e.front() == ' ')
      e.remove_prefix(1);
    std::string prop;
    if (SvStartsWith(e, "Prop ")) {
      e.remove_prefix(5);
      while (!e.empty() && e.front() != ' ')
        prop.push_back(static_cast<char>(e.front())), e.remove_prefix(1);
    }
    std::string_view format = ".2f";
    const size_t fpos = e.find("\\f \"");
    if (fpos != std::string_view::npos) {
      const size_t q1 = fpos + 4;
      const size_t q2 = e.find('"', q1);
      if (q2 != std::string_view::npos)
        format = e.substr(q1, q2 - q1);
    }
    for (char& c : prop)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const EntityRef ref = FindEntityById(st, entId);
    if (!ref.valid())
      return "----";
    return EvalEntityProperty(st, ref.kind, ref.index, prop, format);
  }
  if (SvStartsWith(e, "AcObjProp ")) {
    std::uint64_t entId = 0;
    const size_t handlePos = e.find("%<EntHandle");
    if (handlePos != std::string_view::npos) {
      const size_t end = e.find(">%", handlePos);
      if (end != std::string_view::npos) {
        const std::string_view handleTok = e.substr(handlePos, end - handlePos + 2);
        ParseEntityIdFromHandleToken(handleTok, &entId);
      }
    }
    std::string prop = "AREA";
    if (e.find(" Area") != std::string_view::npos || e.find(" area") != std::string_view::npos)
      prop = "AREA";
    else if (e.find(" Length") != std::string_view::npos || e.find(" Perimeter") != std::string_view::npos)
      prop = "LENGTH";
    std::string_view format = ".2f";
    const size_t fpos = e.find("\\f \"");
    if (fpos != std::string_view::npos) {
      const size_t q1 = fpos + 4;
      const size_t q2 = e.find('"', q1);
      if (q2 != std::string_view::npos)
        format = e.substr(q1, q2 - q1);
    }
    const EntityRef ref = FindEntityById(st, entId);
    if (!ref.valid())
      return "----";
    return EvalEntityProperty(st, ref.kind, ref.index, prop, format);
  }
  return "----";
}

}  // namespace

bool CadTextContainsFieldCodes(std::string_view text) { return ContainsFieldMarker(text); }

std::string CadFieldEvaluateWire(const AppCommandState& st, std::string_view wire,
                                 const CadFieldContext& ctx) {
  if (!ContainsFieldMarker(wire))
    return std::string(wire);
  std::string out;
  out.reserve(wire.size());
  size_t i = 0;
  while (i < wire.size()) {
    const size_t start = wire.find("%<", i);
    if (start == std::string_view::npos) {
      out.append(wire.substr(i));
      break;
    }
    out.append(wire.substr(i, start - i));
    const size_t end = wire.find(">%", start);
    if (end == std::string_view::npos) {
      out.append(wire.substr(start));
      break;
    }
    const std::string_view expr = wire.substr(start + 2, end - (start + 2));
    out.append(EvalOneFieldExpression(expr, st, ctx));
    i = end + 2;
  }
  return out;
}

std::string CadFieldMakeGoSurveyWire(std::uint64_t entityId, std::string_view propName,
                                     std::string_view format) {
  assert(entityId != 0);
  assert(!propName.empty());
  if (format.empty())
    format = ".2f";
  std::ostringstream os;
  os << "%<\\GoSurvey Ent " << entityId << " Prop " << propName << " \\f \"" << format << "\">%";
  return os.str();
}

std::string CadFieldMakeGoSurveyPointWire(int pointId, std::string_view propName, std::string_view format) {
  assert(pointId > 0);
  assert(!propName.empty());
  if (format.empty())
    format = ".2f";
  std::ostringstream os;
  os << "%<\\GoSurvey Point " << pointId << " Prop " << propName << " \\f \"" << format << "\">%";
  return os.str();
}

std::string CadFieldMakeAcVarWire(std::string_view varName, std::string_view format) {
  assert(!varName.empty());
  if (format.empty())
    format = "tc1";
  std::ostringstream os;
  os << "%<\\AcVar " << varName << " \\f \"" << format << "\">%";
  return os.str();
}

std::string CadFieldTextForDwgExport(const AppCommandState& st, std::string_view wire,
                                     const CadFieldContext& ctx, bool r2004OrNewer) {
  if (r2004OrNewer || !ContainsFieldMarker(wire))
    return std::string(wire);
  return CadFieldEvaluateWire(st, wire, ctx);
}

CadFieldContext CadFieldContextFromState(const AppCommandState& st) {
  CadFieldContext ctx;
  ctx.drawingPath = st.activeDocFilePath;
  if (st.activeSpaceIndex >= 0 &&
      static_cast<size_t>(st.activeSpaceIndex) < st.paperLayouts.size())
    ctx.activeLayoutTabName = st.paperLayouts[static_cast<size_t>(st.activeSpaceIndex)].name;
  else
    ctx.activeLayoutTabName = "Model";
  return ctx;
}

std::string CadAnnotationResolvedText(const AppCommandState& st, const CadAnnotation& ann) {
  return CadFieldEvaluateWire(st, ann.text, CadFieldContextFromState(st));
}
