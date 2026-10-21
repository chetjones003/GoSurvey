#include "LandXmlIo.hpp"

#include "CadCommands.hpp"
#include "Version.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

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
