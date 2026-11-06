#include "LandXmlIo.hpp"

#include "CadCommands.hpp"
#include "Version.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>

namespace {

constexpr const char* kLandXmlNamespace = "http://www.landxml.org/schema/LandXML-1.2";

/// The enumeration values LandXML 1.2 accepts for one drawing unit. `elevation` is null where the
/// schema's `elevationUnit` enumeration (meter/kilometer/feet/miles) has no matching value.
struct LandXmlUnits {
  bool metric;
  const char* linear;
  const char* area;
  const char* volume;
  const char* elevation;
};

/// Null for a unitless drawing: there is no unit to preserve, and writing one would be a guess.
const LandXmlUnits* UnitsFor(const AppCommandState& st) {
  static const LandXmlUnits kInch{false, "inch", "squareInch", "cubicInch", nullptr};
  static const LandXmlUnits kUsFoot{false, "USSurveyFoot", "squareFoot", "cubicFeet", "feet"};
  static const LandXmlUnits kFoot{false, "foot", "squareFoot", "cubicFeet", "feet"};
  static const LandXmlUnits kMillimeter{true, "millimeter", "squareMillimeter", "cubicMeter", nullptr};
  static const LandXmlUnits kMeter{true, "meter", "squareMeter", "cubicMeter", "meter"};
  switch (st.drawingInsUnits) {
    case 1: return &kInch;
    case 2:
      return st.drawingSettings.footDefinition == DrawingSettings::FootDefinition::UsSurvey ? &kUsFoot : &kFoot;
    case 4: return &kMillimeter;
    case 6: return &kMeter;
    default: return nullptr;
  }
}

/// Shortest decimal text that reads back to exactly \p value, never in exponent form and never
/// locale-dependent (REQ-101: the world coordinate must survive the round trip). Finite values only.
void AppendNumber(std::string& out, double value) {
  char buffer[512];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed);
  out.append(buffer, result.ptr);
}

/// XML 1.0 text must be valid UTF-8 made of permitted characters; pugixml escapes markup but writes
/// everything else byte for byte, so a stray control character or a bad byte in a point description
/// would make the whole file unreadable. Such sequences become U+FFFD and are counted.
std::string XmlText(const std::string& in, int& replaced) {
  std::string out;
  out.reserve(in.size());
  const auto at = [&](size_t i) { return static_cast<unsigned char>(in[i]); };
  for (size_t i = 0; i < in.size();) {
    const unsigned char lead = at(i);
    size_t length = 1;
    std::uint32_t cp = lead;
    bool wellFormed = true;
    if (lead >= 0x80) {
      length = (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 0;
      wellFormed = length != 0 && i + length <= in.size();
      if (wellFormed) {
        cp = lead & (0xFFu >> (length + 1));
        for (size_t k = 1; k < length; ++k) {
          wellFormed = wellFormed && (at(i + k) & 0xC0) == 0x80;
          cp = (cp << 6) | (at(i + k) & 0x3Fu);
        }
        constexpr std::uint32_t kShortest[] = {0, 0, 0x80, 0x800, 0x10000};
        wellFormed = wellFormed && cp >= kShortest[length];
      }
    }
    const bool allowed = (cp == 0x9 || cp == 0xA || cp == 0xD || (cp >= 0x20 && cp <= 0xD7FF) ||
                          (cp >= 0xE000 && cp <= 0xFFFD) || (cp >= 0x10000 && cp <= 0x10FFFF));
    if (wellFormed && allowed) {
      out.append(in, i, length);
    } else {
      out += "\xEF\xBF\xBD";
      ++replaced;
    }
    i += wellFormed ? length : 1;
  }
  return out;
}

bool Finite(double a, double b, double c) { return std::isfinite(a) && std::isfinite(b) && std::isfinite(c); }

/// Document-local (x, y) plus absolute z as LandXML's "northing easting elevation" text, with the
/// world origin added back in double (geometry is stored LOCAL, ADR-025 / REQ-101).
std::string NorthEastElevation(const AppCommandState& st, double x, double y, double z) {
  std::string text;
  AppendNumber(text, y + st.worldDocumentOriginY);
  text += ' ';
  AppendNumber(text, x + st.worldDocumentOriginX);
  text += ' ';
  AppendNumber(text, z);
  return text;
}

struct ExportCounts {
  int points = 0, surfaces = 0, featureLines = 0;
  int sanitized = 0;
};

void WriteUnits(const AppCommandState& st, pugi::xml_node root, std::vector<std::string>& log) {
  const LandXmlUnits* units = UnitsFor(st);
  if (!units) {
    log.push_back("EXPORTLANDXML — the drawing is unitless, so the file declares no units.");
    return;
  }
  auto node = root.append_child("Units").append_child(units->metric ? "Metric" : "Imperial");
  node.append_attribute("areaUnit") = units->area;
  node.append_attribute("linearUnit") = units->linear;
  node.append_attribute("volumeUnit") = units->volume;
  node.append_attribute("temperatureUnit") = units->metric ? "celsius" : "fahrenheit";
  node.append_attribute("pressureUnit") = units->metric ? "milliBars" : "inchHG";
  if (units->elevation) node.append_attribute("elevationUnit") = units->elevation;
}

void WritePoints(const AppCommandState& st, pugi::xml_node root, ExportCounts& counts,
                 std::vector<std::string>& log) {
  pugi::xml_node group;
  std::set<int> seen;
  for (const SurveyPoint& point : st.surveyPoints) {
    if (!Finite(point.easting, point.northing, point.elevation)) {
      log.push_back("EXPORTLANDXML — skipped point " + std::to_string(point.id) + ": non-finite coordinate.");
      continue;
    }
    if (!seen.insert(point.id).second)
      log.push_back("EXPORTLANDXML — point number " + std::to_string(point.id) +
                    " is used more than once; LandXML expects unique CgPoint names.");
    if (!group) group = root.append_child("CgPoints");
    auto node = group.append_child("CgPoint");
    node.append_attribute("name") = std::to_string(point.id).c_str();
    if (!point.description.empty())
      node.append_attribute("desc") = XmlText(point.description, counts.sanitized).c_str();
    if (!point.rawDescription.empty())
      node.append_attribute("code") = XmlText(point.rawDescription, counts.sanitized).c_str();
    node.text().set(NorthEastElevation(st, point.easting, point.northing, point.elevation).c_str());
    ++counts.points;
  }
}

/// A TIN is exportable when it has a built triangulation whose faces all name real vertices.
/// Returns the reason it is not, or an empty string.
std::string TinProblem(const CadSurface& surface) {
  if (surface.kind == SurfaceKind::Grid || surface.kind == SurfaceKind::GridVolume)
    return "it is a grid surface, which this export does not write";
  const CadTin* tin = surface.tin.get();
  if (!tin || tin->vertexCount() < 3 || tin->triangleCount() < 1) return "it has no built triangulation";
  for (int v = 0; v < tin->vertexCount(); ++v) {
    const size_t base = static_cast<size_t>(v) * 3;
    if (!Finite(tin->vertsXyz[base], tin->vertsXyz[base + 1], tin->vertsXyz[base + 2]))
      return "it has a non-finite vertex";
  }
  for (std::uint32_t index : tin->indices)
    if (index >= static_cast<std::uint32_t>(tin->vertexCount())) return "a face refers to a missing vertex";
  return {};
}

void WriteSurfaces(const AppCommandState& st, pugi::xml_node root, ExportCounts& counts,
                   std::vector<std::string>& log) {
  pugi::xml_node group;
  for (const CadSurface& surface : st.cadSurfaces) {
    const std::string problem = TinProblem(surface);
    if (!problem.empty()) {
      log.push_back("EXPORTLANDXML — skipped surface \"" + surface.name + "\": " + problem + ".");
      continue;
    }
    if (surface.lastBuildIncomplete)
      log.push_back("EXPORTLANDXML — surface \"" + surface.name +
                    "\" shows an older triangulation than its definition describes (a source could not be read).");
    const CadTin& tin = *surface.tin;
    if (!group) group = root.append_child("Surfaces");
    auto node = group.append_child("Surface");
    node.append_attribute("name") = XmlText(surface.name, counts.sanitized).c_str();
    if (!surface.description.empty())
      node.append_attribute("desc") = XmlText(surface.description, counts.sanitized).c_str();
    auto definition = node.append_child("Definition");
    definition.append_attribute("surfType") = "TIN";
    double lowest = tin.vertsXyz[2], highest = tin.vertsXyz[2];
    auto points = definition.append_child("Pnts");
    for (int v = 0; v < tin.vertexCount(); ++v) {
      const size_t base = static_cast<size_t>(v) * 3;
      lowest = std::min(lowest, tin.vertsXyz[base + 2]);
      highest = std::max(highest, tin.vertsXyz[base + 2]);
      auto p = points.append_child("P");
      p.append_attribute("id") = v + 1;  // LandXML ids are positive; the store's indices start at 0
      p.text().set(NorthEastElevation(st, tin.vertsXyz[base], tin.vertsXyz[base + 1], tin.vertsXyz[base + 2]).c_str());
    }
    std::string text;
    AppendNumber(text, lowest);
    definition.append_attribute("elevMin") = text.c_str();
    text.clear();
    AppendNumber(text, highest);
    definition.append_attribute("elevMax") = text.c_str();
    auto faces = definition.append_child("Faces");
    for (int t = 0; t < tin.triangleCount(); ++t) {
      text = std::to_string(tin.indices[static_cast<size_t>(t) * 3] + 1) + ' ' +
             std::to_string(tin.indices[static_cast<size_t>(t) * 3 + 1] + 1) + ' ' +
             std::to_string(tin.indices[static_cast<size_t>(t) * 3 + 2] + 1);
      faces.append_child("F").text().set(text.c_str());
    }
    ++counts.surfaces;
  }
}

/// One `Line` per segment, in order, so a reader rebuilds the chain from consecutive Start/End
/// points; a closed feature line gets its closing segment. LandXML has no elevation-point flag
/// (ADR-035 (a)), so flagged vertices are written as ordinary vertices of the same line.
void WriteFeatureLines(const AppCommandState& st, pugi::xml_node root, ExportCounts& counts,
                       std::vector<std::string>& log) {
  if (st.featureLineOffsets.size() < 2) return;
  pugi::xml_node group;
  const size_t lineCount = st.featureLineOffsets.size() - 1;
  for (size_t i = 0; i < lineCount; ++i) {
    const std::string name = i < st.featureLineInfo.size() ? st.featureLineInfo[i].name : std::string();
    const std::string label = "feature line " + (name.empty() ? std::to_string(i + 1) : "\"" + name + "\"");
    const int first = st.featureLineOffsets[i], last = st.featureLineOffsets[i + 1];
    if (first < 0 || last - first < 2 || static_cast<size_t>(last) * 3 > st.featureLineVerts.size()) {
      log.push_back("EXPORTLANDXML — skipped " + label + ": it has fewer than two usable vertices.");
      continue;
    }
    std::vector<std::string> vertices;
    bool finite = true;
    for (int v = first; v < last; ++v) {
      const double* xyz = &st.featureLineVerts[static_cast<size_t>(v) * 3];
      finite = finite && Finite(xyz[0], xyz[1], xyz[2]);
      if (finite) vertices.push_back(NorthEastElevation(st, xyz[0], xyz[1], xyz[2]));
    }
    if (!finite) {
      log.push_back("EXPORTLANDXML — skipped " + label + ": non-finite coordinate.");
      continue;
    }
    if (i < st.featureLineClosed.size() && st.featureLineClosed[i] != 0) vertices.push_back(vertices.front());
    if (!group) group = root.append_child("PlanFeatures");
    auto feature = group.append_child("PlanFeature");
    if (!name.empty()) feature.append_attribute("name") = XmlText(name, counts.sanitized).c_str();
    if (i < st.featureLineInfo.size() && !st.featureLineInfo[i].description.empty())
      feature.append_attribute("desc") = XmlText(st.featureLineInfo[i].description, counts.sanitized).c_str();
    auto geometry = feature.append_child("CoordGeom");
    for (size_t v = 0; v + 1 < vertices.size(); ++v) {
      auto line = geometry.append_child("Line");
      line.append_child("Start").text().set(vertices[v].c_str());
      line.append_child("End").text().set(vertices[v + 1].c_str());
    }
    ++counts.featureLines;
  }
}

/// Written beside the target and renamed over it, so a failed or interrupted write never leaves a
/// half-written file where the user's previous export was.
bool WriteFileAtomically(const std::filesystem::path& target, const std::string& bytes, std::string& error) {
  std::filesystem::path temporary = target;
  temporary += ".tmp";
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
      error = "could not write " + temporary.u8string();
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      return false;
    }
  }
  std::error_code code;
  std::filesystem::rename(temporary, target, code);
  if (code) {
    error = "could not replace " + target.u8string() + ": " + code.message();
    std::filesystem::remove(temporary, code);
    return false;
  }
  return true;
}

}  // namespace

bool ExportLandXmlFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  try {
    if (!pathUtf8 || !*pathUtf8) {
      log.push_back("EXPORTLANDXML — no destination file was given.");
      return false;
    }
    pugi::xml_document document;
    auto root = document.append_child("LandXML");
    root.append_attribute("xmlns") = kLandXmlNamespace;
    root.append_attribute("xmlns:xsi") = "http://www.w3.org/2001/XMLSchema-instance";
    root.append_attribute("xsi:schemaLocation") =
        "http://www.landxml.org/schema/LandXML-1.2 http://www.landxml.org/schema/LandXML-1.2/LandXML-1.2.xsd";
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_s(&utc, &now);
    char date[16]{}, time[16]{};
    std::strftime(date, sizeof(date), "%Y-%m-%d", &utc);
    std::strftime(time, sizeof(time), "%H:%M:%S", &utc);
    root.append_attribute("date") = date;
    root.append_attribute("time") = time;
    root.append_attribute("version") = "1.2";
    root.append_attribute("language") = "English";
    root.append_attribute("readOnly") = "false";
    WriteUnits(st, root, log);
    auto application = root.append_child("Application");
    application.append_attribute("name") = "GoSurvey";
    application.append_attribute("manufacturer") = "GoSurvey";
    application.append_attribute("version") = GOSURVEY_VERSION_FULL;

    ExportCounts counts;
    WritePoints(st, root, counts, log);
    WriteSurfaces(st, root, counts, log);
    WriteFeatureLines(st, root, counts, log);
    if (counts.points + counts.surfaces + counts.featureLines == 0) {
      log.push_back("EXPORTLANDXML — nothing to export: the drawing has no survey points, built TIN surfaces "
                    "or feature lines. No file was written.");
      return false;
    }
    if (counts.sanitized > 0)
      log.push_back("EXPORTLANDXML — replaced " + std::to_string(counts.sanitized) +
                    " character(s) that XML cannot hold in names or descriptions.");

    std::ostringstream xml;
    document.save(xml, "  ", pugi::format_default, pugi::encoding_utf8);
    std::string error;
    if (!WriteFileAtomically(std::filesystem::u8path(pathUtf8), xml.str(), error)) {
      log.push_back("EXPORTLANDXML — " + error + ".");
      return false;
    }
    log.push_back("EXPORTLANDXML — wrote " + std::to_string(counts.points) + " point(s), " +
                  std::to_string(counts.surfaces) + " surface(s) and " + std::to_string(counts.featureLines) +
                  " feature line(s) to " + pathUtf8 + ".");
    return true;
  } catch (const std::exception& error) {
    log.push_back(std::string("EXPORTLANDXML — failed: ") + error.what() + ".");
    return false;
  }
}

// --- Import (issue #1003, REQ-506) ---------------------------------------------------------------

namespace {

/// Meters per LandXML `linearUnit` value, or 0 for a value this reader does not know.
double MetersPerLandXmlUnit(const std::string& unit) {
  if (unit == "meter") return 1.0;
  if (unit == "millimeter") return 0.001;
  if (unit == "inch") return 0.0254;
  if (unit == "foot") return 0.3048;
  if (unit == "USSurveyFoot") return 1200.0 / 3937.0;
  return 0.0;
}

/// Meters per drawing unit (INSUNITS, as UnitsFor reads it), or 0 for a unitless drawing.
double MetersPerDrawingUnit(const AppCommandState& st) {
  switch (st.drawingInsUnits) {
    case 1: return 0.0254;
    case 2:
      return st.drawingSettings.footDefinition == DrawingSettings::FootDefinition::UsSurvey ? 1200.0 / 3937.0 : 0.3048;
    case 4: return 0.001;
    case 6: return 1.0;
    default: return 0.0;
  }
}

/// Exactly three finite decimal numbers separated by white space; anything else is rejected.
bool ParseTriple(const char* text, double out[3]) {
  if (!text) return false;
  const std::string_view view(text);
  const char* p = view.data();
  const char* end = p + view.size();
  auto skipSpace = [&] {
    while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
  };
  for (int i = 0; i < 3; ++i) {
    const char* before = p;
    skipSpace();
    if (i > 0 && p == before) return false;  // "1-2-3" and "1,2,3" are not three numbers
    const auto [next, ec] = std::from_chars(p, end, out[i]);
    if (ec != std::errc() || !std::isfinite(out[i])) return false;
    p = next;
  }
  skipSpace();
  return p == end;
}

/// True when two WORLD points coincide to within the chord tolerance used for chaining segments.
bool SamePoint(const double* a, const double* b) {
  constexpr double kChainTolerance = 1e-6;
  return std::fabs(a[0] - b[0]) <= kChainTolerance && std::fabs(a[1] - b[1]) <= kChainTolerance &&
         std::fabs(a[2] - b[2]) <= kChainTolerance;
}

/// One feature line ready to append: WORLD x,y,z triples, already unit-converted.
struct PendingFeatureLine {
  std::string name;
  std::string description;
  std::vector<double> worldXyz;
  bool closed = false;
};

}  // namespace

bool ImportLandXmlFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  try {
    if (!pathUtf8 || !*pathUtf8) {
      log.push_back("IMPORTLANDXML — no file was given.");
      return false;
    }
    const std::string shownPath = pathUtf8;
    std::ifstream in(std::filesystem::u8path(pathUtf8), std::ios::binary);
    if (!in) {
      log.push_back("IMPORTLANDXML — could not open " + shownPath + ".");
      return false;
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    pugi::xml_document document;
    const pugi::xml_parse_result parsed = document.load_buffer(bytes.data(), bytes.size());
    if (!parsed) {
      log.push_back(std::string("IMPORTLANDXML — not a readable XML file: ") + parsed.description() + ".");
      return false;
    }
    const pugi::xml_node root = document.child("LandXML");
    if (!root) {
      log.push_back("IMPORTLANDXML — " + shownPath + " has no LandXML root element.");
      return false;
    }

    // Units: values are multiplied by file unit / drawing unit. A missing unit on either side keeps the
    // values as written and says so (REQ-506 item 4).
    const std::string fileUnit = root.child("Units").first_child().attribute("linearUnit").as_string();
    const double fileMeters = MetersPerLandXmlUnit(fileUnit);
    const double drawingMeters = MetersPerDrawingUnit(st);
    double factor = 1.0;
    if (fileMeters > 0.0 && drawingMeters > 0.0) {
      factor = fileMeters / drawingMeters;
    } else if (drawingMeters <= 0.0) {
      log.push_back("IMPORTLANDXML — the drawing is unitless, so values are taken as written.");
    } else {
      log.push_back("IMPORTLANDXML — the file declares no known linear unit, so values are taken as drawing units.");
    }
    const double originX = st.worldDocumentOriginX;
    const double originY = st.worldDocumentOriginY;

    // Points. LandXML stores northing first; the survey point model keeps easting first.
    std::set<int> existingIds;
    for (const SurveyPoint& point : st.surveyPoints) existingIds.insert(point.id);
    const std::string layer = CreatePointsLayer(st);
    std::set<int> seenIds;
    std::vector<SurveyPoint> newPoints, conflicts;
    for (pugi::xml_node group : root.children("CgPoints")) {
      for (pugi::xml_node node : group.children("CgPoint")) {
        const std::string name = node.attribute("name").as_string();
        int id = 0;
        const char* nameEnd = name.data() + name.size();
        const auto [idEnd, idEc] = std::from_chars(name.data(), nameEnd, id);
        if (idEc != std::errc() || idEnd != nameEnd || id <= 0) {
          log.push_back("IMPORTLANDXML — skipped point \"" + name + "\": its name is not a positive point number.");
          continue;
        }
        double v[3] = {0.0, 0.0, 0.0};
        if (!ParseTriple(node.text().as_string(), v)) {
          log.push_back("IMPORTLANDXML — skipped point " + name + ": its coordinates are not three numbers.");
          continue;
        }
        if (!seenIds.insert(id).second) {
          log.push_back("IMPORTLANDXML — skipped point " + name + ": repeated in the file; the first one is kept.");
          continue;
        }
        SurveyPoint point;
        point.id = id;
        point.northing = v[0] * factor;
        point.easting = v[1] * factor;
        point.elevation = v[2] * factor;
        if (!Finite(point.northing - originY, point.easting - originX, point.elevation)) {
          log.push_back("IMPORTLANDXML — skipped point " + name + ": its converted coordinates are not finite.");
          continue;
        }
        point.description = node.attribute("desc").as_string();
        point.rawDescription = node.attribute("code").as_string();
        point.layer = layer;
        (existingIds.count(id) ? conflicts : newPoints).push_back(std::move(point));
      }
    }

    // Surfaces: rebuilt from their points (REQ-144 added points), so the source triangle faces are not kept.
    std::set<std::string> surfaceNames;
    for (const CadSurface& surface : st.cadSurfaces) surfaceNames.insert(surface.name);
    std::vector<CadSurface> newSurfaces;
    for (pugi::xml_node group : root.children("Surfaces")) {
      for (pugi::xml_node node : group.children("Surface")) {
        const std::string name = node.attribute("name").as_string();
        if (name.empty()) {
          log.push_back("IMPORTLANDXML — skipped a surface with no name.");
          continue;
        }
        if (surfaceNames.count(name)) {
          log.push_back("IMPORTLANDXML — skipped surface \"" + name + "\": a surface with that name already exists.");
          continue;
        }
        const pugi::xml_node definition = node.child("Definition");
        const std::string type = definition.attribute("surfType").as_string();
        if (type != "TIN") {
          log.push_back("IMPORTLANDXML — skipped surface \"" + name + "\": only TIN surfaces are imported (surfType \"" +
                        type + "\").");
          continue;
        }
        CadSurface surface;
        surface.name = name;
        surface.description = node.attribute("desc").as_string();
        for (pugi::xml_node pnt : definition.child("Pnts").children("P")) {
          double v[3] = {0.0, 0.0, 0.0};
          if (!ParseTriple(pnt.text().as_string(), v)) {
            log.push_back("IMPORTLANDXML — skipped a point of surface \"" + name +
                          "\": its coordinates are not three numbers.");
            continue;
          }
          const double lx = v[1] * factor - originX;  // local = world - origin
          const double ly = v[0] * factor - originY;
          const double lz = v[2] * factor;
          if (!Finite(lx, ly, lz)) {
            log.push_back("IMPORTLANDXML — skipped a point of surface \"" + name +
                          "\": its converted coordinates are not finite.");
            continue;
          }
          surface.addedPointXyz.push_back(lx);
          surface.addedPointXyz.push_back(ly);
          surface.addedPointXyz.push_back(lz);
        }
        if (surface.addedPointXyz.size() < 9) {
          log.push_back("IMPORTLANDXML — skipped surface \"" + name + "\": it has fewer than three usable points.");
          continue;
        }
        if (definition.child("Faces"))
          log.push_back("IMPORTLANDXML — surface \"" + name +
                        "\": its triangle faces are not kept; GoSurvey triangulates the imported points.");
        surfaceNames.insert(name);
        newSurfaces.push_back(std::move(surface));
      }
    }

    // Feature lines: consecutive Line segments that share an end form one line; a gap starts a new one; a
    // closing segment back to the first point marks it closed. Curves and other geometry are skipped.
    std::vector<PendingFeatureLine> newLines;
    for (pugi::xml_node group : root.children("PlanFeatures")) {
      for (pugi::xml_node feature : group.children("PlanFeature")) {
        const std::string name = feature.attribute("name").as_string();
        const std::string label = name.empty() ? std::string("feature line") : "feature line \"" + name + "\"";
        std::vector<std::vector<double>> runs;
        bool skippedGeometry = false;
        for (pugi::xml_node geometry : feature.child("CoordGeom").children()) {
          if (std::string_view(geometry.name()) != "Line") {
            skippedGeometry = true;
            continue;
          }
          double a[3] = {0.0, 0.0, 0.0}, b[3] = {0.0, 0.0, 0.0};
          if (!ParseTriple(geometry.child("Start").text().as_string(), a) ||
              !ParseTriple(geometry.child("End").text().as_string(), b)) {
            log.push_back("IMPORTLANDXML — skipped a segment of " + label + ": its coordinates are not three numbers.");
            continue;
          }
          // World x = easting, y = northing; scaled to drawing units.
          const double start[3] = {a[1] * factor, a[0] * factor, a[2] * factor};
          const double end[3] = {b[1] * factor, b[0] * factor, b[2] * factor};
          if (!Finite(start[0] - originX, start[1] - originY, start[2]) ||
              !Finite(end[0] - originX, end[1] - originY, end[2])) {
            log.push_back("IMPORTLANDXML — skipped a segment of " + label + ": its converted coordinates are not finite.");
            continue;
          }
          if (runs.empty() || !SamePoint(&runs.back()[runs.back().size() - 3], start))
            runs.push_back({start[0], start[1], start[2]});
          runs.back().insert(runs.back().end(), end, end + 3);
        }
        if (skippedGeometry)
          log.push_back("IMPORTLANDXML — skipped curve or other non-line geometry in " + label + ".");
        if (runs.size() > 1)
          log.push_back("IMPORTLANDXML — " + label + " has gaps; it is imported as " + std::to_string(runs.size()) +
                        " feature lines.");
        for (std::vector<double>& run : runs) {
          const size_t count = run.size() / 3;
          if (count < 2) {
            log.push_back("IMPORTLANDXML — skipped part of " + label + ": it has fewer than two points.");
            continue;
          }
          PendingFeatureLine line;
          line.name = name;
          line.description = feature.attribute("desc").as_string();
          if (count >= 3 && SamePoint(&run[0], &run[run.size() - 3])) {
            line.closed = true;
            run.resize(run.size() - 3);  // the closing point repeats the first; the closed flag carries it
          }
          line.worldXyz = std::move(run);
          newLines.push_back(std::move(line));
        }
      }
    }

    // Apply. One undo step covers every point, surface and line from this file; a point-number answer is a
    // separate step (see the Qt editor).
    const bool changes = !newPoints.empty() || !newSurfaces.empty() || !newLines.empty();
    if (changes)
      PushUndoSnapshot(st, "Import LandXML");
    if (!newPoints.empty())
      ResolveConflictingWorldSurveyPoints(st, newPoints, false, 0, &log);
    for (CadSurface& surface : newSurfaces) {
      (void)BuildSurfaceFromSources(st, surface, log);
      st.cadSurfaces.push_back(std::move(surface));
      AppendNewObjectAttrs(st, st.cadSurfaceAttrs, st.cadSurfaces.size(), ObjectLayerKind::Surface,
                           st.cadSurfaces.back().name);  // REQ-361
    }
    for (const PendingFeatureLine& line : newLines) {
      if (st.featureLineOffsets.empty())
        st.featureLineOffsets.push_back(0);
      const int base = st.featureLineOffsets.back();
      const int count = static_cast<int>(line.worldXyz.size() / 3);
      for (int v = 0; v < count; ++v) {
        const size_t b = static_cast<size_t>(v) * 3;
        st.featureLineVerts.push_back(line.worldXyz[b] - originX);
        st.featureLineVerts.push_back(line.worldXyz[b + 1] - originY);
        st.featureLineVerts.push_back(line.worldXyz[b + 2]);
        st.featureLineElevPt.push_back(0);
      }
      st.featureLineOffsets.push_back(base + count);
      st.featureLineClosed.push_back(line.closed ? 1 : 0);
      CadFeatureLineInfo info;
      info.name = line.name;
      info.description = line.description;
      st.featureLineInfo.push_back(std::move(info));
      AppendNewObjectAttrs(st, st.featureLineAttrs, st.featureLineInfo.size(), ObjectLayerKind::FeatureLine,
                           st.featureLineInfo.back().name);  // REQ-361
    }
    if (!newSurfaces.empty() || !newLines.empty())
      BumpCadGpuCache(st);
    EnsureAttrCounts(st);

    st.pendingLandXmlConflictPoints = conflicts;
    log.push_back("IMPORTLANDXML — imported " + std::to_string(newPoints.size()) + " point(s), " +
                  std::to_string(newSurfaces.size()) + " surface(s) and " + std::to_string(newLines.size()) +
                  " feature line(s) from " + shownPath + ".");
    if (!conflicts.empty())
      log.push_back("IMPORTLANDXML — " + std::to_string(conflicts.size()) +
                    " point number(s) already exist in this drawing; choose Overwrite or Offset to add them.");
    return true;
  } catch (const std::exception& error) {
    log.push_back(std::string("IMPORTLANDXML — failed: ") + error.what() + ".");
    return false;
  }
}
