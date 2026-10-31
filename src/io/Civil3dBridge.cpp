#include "Civil3dBridge.hpp"

#include "CadCommands.hpp"
#include "ProcessRun.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

using Json = nlohmann::json;

std::string Environment(const char* name) {
  const char* value = std::getenv(name);
  return value ? value : "";
}

std::filesystem::path CurrentExecutableDirectory() {
#ifdef _WIN32
  std::wstring path(32768, L'\0');
  const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (size == 0 || size >= path.size()) return {};
  path.resize(size);
  return std::filesystem::path(path).parent_path();
#else
  return {};
#endif
}

std::filesystem::path FindCivilRoot() {
  std::error_code ec;
  const std::string overrideRoot = Environment("GOSURVEY_CIVIL3D_ROOT");
  if (!overrideRoot.empty()) {
    const std::filesystem::path configured(overrideRoot);
    if (std::filesystem::exists(configured / "AeccDbMgd.dll", ec) &&
        std::filesystem::exists(configured.parent_path() / "accoreconsole.exe", ec))
      return configured;
    if (std::filesystem::exists(configured / "C3D" / "AeccDbMgd.dll", ec) &&
        std::filesystem::exists(configured / "accoreconsole.exe", ec))
      return configured / "C3D";
  }

  std::filesystem::path best;
  const std::filesystem::path parent("C:/Program Files/Autodesk");
  if (!std::filesystem::is_directory(parent, ec)) return best;
  for (const auto& entry : std::filesystem::directory_iterator(parent, ec)) {
    if (ec || !entry.is_directory(ec)) continue;
    const auto console = entry.path() / "accoreconsole.exe";
    const auto civil = entry.path() / "C3D";
    if (std::filesystem::exists(console, ec) &&
        std::filesystem::exists(civil / "AeccDbMgd.dll", ec) &&
        (best.empty() || entry.path().string() > best.parent_path().string()))
      best = civil;
  }
  return best;
}

std::filesystem::path FindBridgeAssembly(const std::filesystem::path& appDirectory, bool forImport) {
  std::error_code ec;
  const std::string explicitAssembly = Environment(forImport ? "GOSURVEY_CIVIL3D_IMPORT_DLL" : "GOSURVEY_CIVIL3D_EXPORT_DLL");
  if (!explicitAssembly.empty() && std::filesystem::is_regular_file(explicitAssembly, ec))
    return std::filesystem::path(explicitAssembly);
  const std::string legacyAssembly = Environment("GOSURVEY_CIVIL3D_BRIDGE_DLL");
  if (!legacyAssembly.empty() && std::filesystem::is_regular_file(legacyAssembly, ec))
    return std::filesystem::path(legacyAssembly);
  const auto directionDll = appDirectory / (forImport ? "GoSurveyCivilImport.dll" : "GoSurveyCivilExport.dll");
  for (const auto& candidate : {directionDll,
                                appDirectory / "GoSurveyCivilBridge.dll",
                                appDirectory / "artifacts" / (forImport ? "GoSurveyCivilImport.dll" : "GoSurveyCivilExport.dll"),
                                appDirectory / "artifacts" / "GoSurveyCivilBridge.dll"})
    if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
  return {};
}

std::string ScriptString(const std::filesystem::path& path) {
  std::string value = path.u8string();
  std::string result = "\"";
  for (char ch : value) {
    if (ch == '\\' || ch == '"') result.push_back('\\');
    result.push_back(ch);
  }
  result.push_back('"');
  return result;
}

bool WriteExportScript(const std::filesystem::path& script,
                       const std::filesystem::path& assembly,
                       const std::filesystem::path& jsonPath) {
  std::ofstream out(script, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << "_.NETLOAD\n" << ScriptString(assembly) << "\n"
      << "GOSURVEY_CIVIL_EXPORT\n" << ScriptString(jsonPath) << "\n"
      << "_.QUIT\n_N\n";
  return out.good();
}

bool WriteImportScript(const std::filesystem::path& script,
                       const std::filesystem::path& assembly,
                       const std::filesystem::path& jsonPath) {
  std::ofstream out(script, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << "_.NETLOAD\n" << ScriptString(assembly) << "\n"
      << "GOSURVEY_CIVIL_IMPORT\n" << ScriptString(jsonPath) << "\n"
      << "_.QSAVE\n_.QUIT\n_N\n";
  return out.good();
}

bool RunCivilExport(const char* dwgPath, const std::filesystem::path& root,
                    const std::filesystem::path& appDir, Json* output,
                    std::vector<std::string>& log) {
#ifndef _WIN32
  (void)dwgPath; (void)root; (void)appDir; (void)output;
  log.push_back("Civil 3D object import is available only on Windows.");
  return false;
#else
  const std::filesystem::path console = root.parent_path() / "accoreconsole.exe";
  const std::filesystem::path assembly = FindBridgeAssembly(appDir, false);
  if (assembly.empty()) {
    log.push_back("Civil 3D object import skipped: GoSurveyCivilBridge.dll is not installed beside GoSurvey.exe.");
    return false;
  }

  std::error_code ec;
  const auto temp = std::filesystem::temp_directory_path(ec) /
                    ("gosurvey-civil-" + std::to_string(GetCurrentProcessId()));
  if (ec || !std::filesystem::create_directories(temp, ec) && ec) {
    log.push_back("Civil 3D object import skipped: cannot create temporary bridge directory.");
    return false;
  }
  const auto jsonPath = temp / "objects.json";
  const auto scriptPath = temp / "export.scr";
  if (!WriteExportScript(scriptPath, assembly, jsonPath)) {
    std::filesystem::remove_all(temp, ec);
    log.push_back("Civil 3D object import failed: could not write Core Console script.");
    return false;
  }

  const auto aecBase = root.parent_path() / "ACA" / "AecBase.dbx";
  const std::string consoleOverride = Environment("GOSURVEY_CIVIL3D_CORECONSOLE");
  const std::string consolePath = consoleOverride.empty() ? console.u8string() : consoleOverride;
  std::vector<std::string> args = {"/i", std::string(dwgPath), "/s", scriptPath.u8string(),
                                  "/product", "C3D", "/language", "en-US"};
  if (std::filesystem::is_regular_file(aecBase, ec)) {
    args.push_back("/ld");
    args.push_back(aecBase.u8string());
  }
  int exitCode = -1;
  const bool completed = RunProcessAndWait(consolePath, args, root.parent_path().u8string(), 180000, &exitCode);
  if (!completed || exitCode != 0 || !std::filesystem::is_regular_file(jsonPath, ec)) {
    std::filesystem::remove_all(temp, ec);
    log.push_back("Civil 3D object import failed in accoreconsole (exit " + std::to_string(exitCode) + ").");
    return false;
  }
  try {
    std::ifstream in(jsonPath, std::ios::binary);
    *output = Json::parse(in);
  } catch (const std::exception& e) {
    std::filesystem::remove_all(temp, ec);
    log.push_back(std::string("Civil 3D object import returned invalid data: ") + e.what());
    return false;
  }
  std::filesystem::remove_all(temp, ec);
  return true;
#endif
}

bool RunCivilImport(const char* dwgPath, const std::filesystem::path& root,
                    const std::filesystem::path& appDir, const Json& input,
                    std::vector<std::string>& log) {
#ifndef _WIN32
  (void)dwgPath; (void)root; (void)appDir; (void)input;
  log.push_back("Civil 3D object export is available only on Windows.");
  return false;
#else
  const std::filesystem::path console = root.parent_path() / "accoreconsole.exe";
  const std::filesystem::path assembly = FindBridgeAssembly(appDir, true);
  if (assembly.empty()) {
    log.push_back("Civil 3D native objects were not written: GoSurveyCivilBridge.dll is missing.");
    return false;
  }
  std::error_code ec;
  const auto temp = std::filesystem::temp_directory_path(ec) /
                    ("gosurvey-civil-save-" + std::to_string(GetCurrentProcessId()));
  if (ec || (!std::filesystem::create_directories(temp, ec) && ec)) {
    log.push_back("Civil 3D native objects were not written: cannot create bridge working directory.");
    return false;
  }
  const auto jsonPath = temp / "model.json";
  const auto scriptPath = temp / "import.scr";
  {
    std::ofstream out(jsonPath, std::ios::binary | std::ios::trunc);
    if (!out) {
      std::filesystem::remove_all(temp, ec);
      log.push_back("Civil 3D native objects were not written: cannot create bridge data file.");
      return false;
    }
    out << input.dump();
  }
  if (!WriteImportScript(scriptPath, assembly, jsonPath)) {
    std::filesystem::remove_all(temp, ec);
    log.push_back("Civil 3D native objects were not written: cannot create Core Console script.");
    return false;
  }
  const auto aecBase = root.parent_path() / "ACA" / "AecBase.dbx";
  const std::string consoleOverride = Environment("GOSURVEY_CIVIL3D_CORECONSOLE");
  const std::string consolePath = consoleOverride.empty() ? console.u8string() : consoleOverride;
  std::vector<std::string> args = {"/i", std::string(dwgPath), "/s", scriptPath.u8string(),
                                  "/product", "C3D", "/language", "en-US"};
  if (std::filesystem::is_regular_file(aecBase, ec)) {
    args.push_back("/ld");
    args.push_back(aecBase.u8string());
  }
  int exitCode = -1;
  const bool completed = RunProcessAndWait(consolePath, args, root.parent_path().u8string(), 240000, &exitCode);
  const auto resultPath = std::filesystem::path(jsonPath.u8string() + ".result");
  bool resultOk = false;
  try {
    std::ifstream resultFile(resultPath, std::ios::binary);
    if (resultFile) {
      const Json result = Json::parse(resultFile);
      resultOk = result.value("ok", false);
      if (!resultOk && result.contains("error"))
        log.push_back("Civil 3D native object export error: " + result.value("error", std::string()));
    }
  } catch (const std::exception& e) {
    log.push_back(std::string("Civil 3D native object export returned invalid status: ") + e.what());
  }
  std::filesystem::remove_all(temp, ec);
  if (!completed || exitCode != 0 || !resultOk) {
    log.push_back("Civil 3D native object export failed in accoreconsole (exit " + std::to_string(exitCode) + ").");
    return false;
  }
  return true;
#endif
}

std::string StringField(const Json& value, const char* key) {
  const auto it = value.find(key);
  return it != value.end() && it->is_string() ? it->get<std::string>() : std::string();
}

double NumberField(const Json& value, const char* key) {
  const auto it = value.find(key);
  if (it == value.end() || !it->is_number()) return 0.0;
  return it->get<double>();
}

bool HasName(const std::vector<SurveyPointStyleDefinition>& styles, const std::string& name) {
  return std::any_of(styles.begin(), styles.end(), [&](const auto& item) { return item.name == name; });
}

bool HasName(const std::vector<SurveyPointLabelStyleDefinition>& styles, const std::string& name) {
  return std::any_of(styles.begin(), styles.end(), [&](const auto& item) { return item.name == name; });
}

template <typename Styles>
void ImportNames(const Json& root, const char* key, Styles& styles) {
  const auto found = root.find(key);
  if (found == root.end() || !found->is_array()) return;
  for (const Json& record : *found) {
    const std::string name = StringField(record, "name");
    if (name.empty() || HasName(styles, name)) continue;
    using Style = typename std::decay_t<decltype(styles)>::value_type;
    Style style;
    style.name = name;
    styles.push_back(std::move(style));
  }
}

void ImportCivilPoints(AppCommandState& state, const Json& root) {
  const auto found = root.find("points");
  if (found == root.end() || !found->is_array()) return;
  for (const Json& record : *found) {
    if (!record.is_object()) continue;
    SurveyPoint point;
    point.id = static_cast<int>(NumberField(record, "number"));
    if (point.id <= 0) continue;
    if (std::any_of(state.surveyPoints.begin(), state.surveyPoints.end(), [&](const auto& p) { return p.id == point.id; }))
      continue;
    point.easting = NumberField(record, "easting") - state.worldDocumentOriginX;
    point.northing = NumberField(record, "northing") - state.worldDocumentOriginY;
    point.elevation = NumberField(record, "elevation");
    point.description = StringField(record, "rawDescription");
    point.rawDescription = point.description;
    point.styleName = StringField(record, "pointStyle");
    point.labelStyleName = StringField(record, "labelStyle");
    if (point.styleName.empty()) point.styleName = "Standard";
    if (point.labelStyleName.empty()) point.labelStyleName = "Standard";
    state.surveyPoints.push_back(std::move(point));
  }
}

void ImportCivilSurfaces(AppCommandState& state, const Json& root) {
  const auto found = root.find("surfaces");
  if (found == root.end() || !found->is_array()) return;
  for (const Json& record : *found) {
    if (!record.is_object()) continue;
    const auto triangles = record.find("triangles");
    if (triangles == record.end() || !triangles->is_array() || triangles->empty()) continue;
    auto tin = std::make_shared<CadTin>();
    for (const Json& triangle : *triangles) {
      if (!triangle.is_array() || triangle.size() != 3) continue;
      const std::uint32_t first = static_cast<std::uint32_t>(tin->vertsXyz.size() / 3);
      bool valid = true;
      for (const Json& vertex : triangle) {
        if (!vertex.is_array() || vertex.size() != 3 || !vertex[0].is_number() ||
            !vertex[1].is_number() || !vertex[2].is_number()) { valid = false; break; }
      }
      if (!valid) continue;
      for (const Json& vertex : triangle) {
        tin->vertsXyz.push_back(vertex[0].get<double>() - state.worldDocumentOriginX);
        tin->vertsXyz.push_back(vertex[1].get<double>() - state.worldDocumentOriginY);
        tin->vertsXyz.push_back(vertex[2].get<double>());
      }
      tin->indices.insert(tin->indices.end(), {first, first + 1, first + 2});
    }
    if (tin->triangleCount() == 0) continue;
    CadSurface surface;
    surface.name = StringField(record, "name");
    if (surface.name.empty()) surface.name = "Civil 3D Surface";
    const std::string baseName = surface.name;
    for (int suffix = 2; std::any_of(state.cadSurfaces.begin(), state.cadSurfaces.end(),
         [&](const CadSurface& item) { return item.name == surface.name; }); ++suffix)
      surface.name = baseName + " (" + std::to_string(suffix) + ")";
    surface.kind = SurfaceKind::Tin;
    surface.description = "Imported from Civil 3D";
    surface.styleName = StringField(record, "style");
    surface.tin = std::move(tin);
    state.cadSurfaces.push_back(std::move(surface));
  }
}

void ImportCivilAlignments(AppCommandState& state, const Json& root) {
  const auto found = root.find("alignments");
  if (found == root.end() || !found->is_array()) return;
  // Alignment centerlines use GoSurvey's ordinary editable polyline store. The layer marker keeps
  // their Civil 3D identity/name through GoSurvey's existing drawing trailer, and makes SaveAs
  // recreate them as native Civil 3D alignments rather than as anonymous linework.
  if (state.userPolylineOffsets.empty()) state.userPolylineOffsets.push_back(0);
  for (const Json& record : *found) {
    if (!record.is_object()) continue;
    const std::string name = StringField(record, "name");
    const auto vertices = record.find("vertices");
    if (name.empty() || vertices == record.end() || !vertices->is_array() || vertices->size() < 2)
      continue;
    const std::string marker = "GS_C3D_ALIGN:" + name;
    if (std::any_of(state.userPolylineAttrs.begin(), state.userPolylineAttrs.end(),
                    [&](const EntityAttributes& attr) { return attr.layer == marker; }))
      continue;
    std::vector<double> importedVertices;
    std::vector<float> importedBulges;
    for (const Json& vertex : *vertices) {
      if (!vertex.is_array() || vertex.size() < 2 || !vertex[0].is_number() || !vertex[1].is_number())
        continue;
      importedVertices.push_back(vertex[0].get<double>() - state.worldDocumentOriginX);
      importedVertices.push_back(vertex[1].get<double>() - state.worldDocumentOriginY);
      importedVertices.push_back(vertex.size() > 2 && vertex[2].is_number()
                                   ? vertex[2].get<double>() : 0.0);
      importedBulges.push_back(vertex.size() > 3 && vertex[3].is_number()
                                   ? static_cast<float>(vertex[3].get<double>()) : 0.0f);
    }
    if (importedVertices.size() < 6) continue;
    SyncPolylineBulge(state.userPolylineVertsBulge, state.userPolylineVerts.size());
    state.userPolylineVerts.insert(state.userPolylineVerts.end(), importedVertices.begin(), importedVertices.end());
    state.userPolylineVertsBulge.insert(state.userPolylineVertsBulge.end(), importedBulges.begin(), importedBulges.end());
    const int end = static_cast<int>(state.userPolylineVerts.size() / 3);
    state.userPolylineOffsets.push_back(end);
    state.userPolylineClosed.push_back(0);
    EntityAttributes attr;
    attr.layer = marker;
    state.userPolylineAttrs.push_back(std::move(attr));
  }
  SyncPolylineBulge(state.userPolylineVertsBulge, state.userPolylineVerts.size());
}

} // namespace

bool ImportCivil3dModelObjects(AppCommandState& state, const char* dwgPathUtf8,
                               std::vector<std::string>& log) {
  if (!dwgPathUtf8 || !*dwgPathUtf8) return false;
#ifndef _WIN32
  log.push_back("Civil 3D object bridge is not available on this platform.");
  return false;
#else
  const auto root = FindCivilRoot();
  if (root.empty()) return false;
  Json data;
  if (!RunCivilExport(dwgPathUtf8, root, CurrentExecutableDirectory(), &data, log)) return false;
  if (!data.is_object() || NumberField(data, "schema") != 1.0) {
    log.push_back("Civil 3D object import skipped: unsupported bridge schema.");
    return false;
  }
  ImportNames(data, "pointStyles", state.surveyPointStyles);
  ImportNames(data, "pointLabelStyles", state.surveyPointLabelStyles);
  const size_t pointsBefore = state.surveyPoints.size();
  const size_t surfacesBefore = state.cadSurfaces.size();
  const size_t alignmentsBefore = static_cast<size_t>(std::count_if(state.userPolylineAttrs.begin(), state.userPolylineAttrs.end(),
      [](const EntityAttributes& attr) { return attr.layer.rfind("GS_C3D_ALIGN:", 0) == 0; }));
  ImportCivilPoints(state, data);
  ImportCivilSurfaces(state, data);
  ImportCivilAlignments(state, data);
  for (size_t i = pointsBefore; i < state.surveyPoints.size(); ++i)
    EnsureSurveyPointLabelMtext(state, i, &log);
  for (size_t i = surfacesBefore; i < state.cadSurfaces.size(); ++i)
    state.cadSurfaceAttrs.push_back(EntityAttributes{});
  EnsureEntityIds(state);
  const size_t pointCount = state.surveyPoints.size() - pointsBefore;
  const size_t surfaceCount = state.cadSurfaces.size() - surfacesBefore;
  const size_t alignmentCount = static_cast<size_t>(std::count_if(state.userPolylineAttrs.begin(), state.userPolylineAttrs.end(),
      [](const EntityAttributes& attr) { return attr.layer.rfind("GS_C3D_ALIGN:", 0) == 0; })) - alignmentsBefore;
  log.push_back("Civil 3D native import: " + std::to_string(pointCount) + " COGO point(s), " +
                std::to_string(surfaceCount) + " TIN surface(s), " + std::to_string(alignmentCount) +
                " alignment centerline(s).");
  return true;
#endif
}

bool ExportCivil3dModelObjects(const AppCommandState& state, const char* dwgPathUtf8,
                               std::vector<std::string>& log) {
  const bool hasAlignment = std::any_of(state.userPolylineAttrs.begin(), state.userPolylineAttrs.end(),
      [](const EntityAttributes& attr) { return attr.layer.rfind("GS_C3D_ALIGN:", 0) == 0; });
  if (!dwgPathUtf8 || !*dwgPathUtf8 || (state.surveyPoints.empty() && state.cadSurfaces.empty() && !hasAlignment))
    return false;
#ifndef _WIN32
  log.push_back("Civil 3D native object export is not available on this platform.");
  return false;
#else
  const auto root = FindCivilRoot();
  if (root.empty()) {
    log.push_back("Civil 3D native objects were not written: Civil 3D is not installed.");
    return false;
  }
  Json data;
  data["schema"] = 1;
  // During Save As, activeDocFilePath still points at the source drawing. Civil 3D uses that
  // source database to clone native style records before rebuilding AECC objects in the target.
  data["sourceDwg"] = state.activeDocFilePath;
  data["points"] = Json::array();
  for (const SurveyPoint& point : state.surveyPoints) {
    data["points"].push_back({
      {"number", point.id},
      {"easting", point.easting + state.worldDocumentOriginX},
      {"northing", point.northing + state.worldDocumentOriginY},
      {"elevation", point.elevation},
      {"rawDescription", point.rawDescription.empty() ? point.description : point.rawDescription},
      {"pointStyle", point.styleName},
      {"labelStyle", point.labelStyleName}
    });
  }
  data["surfaces"] = Json::array();
  for (const CadSurface& surface : state.cadSurfaces) {
    if (!surface.tin || surface.tin->indices.size() < 3) continue;
    Json output = {{"name", surface.name}, {"style", surface.styleName}, {"triangles", Json::array()}};
    for (size_t i = 0; i + 2 < surface.tin->indices.size(); i += 3) {
      Json triangle = Json::array();
      for (size_t j = 0; j < 3; ++j) {
        const std::uint32_t ix = surface.tin->indices[i + j];
        const size_t offset = static_cast<size_t>(ix) * 3;
        if (offset + 2 >= surface.tin->vertsXyz.size()) continue;
        triangle.push_back({
          surface.tin->vertsXyz[offset] + state.worldDocumentOriginX,
          surface.tin->vertsXyz[offset + 1] + state.worldDocumentOriginY,
          surface.tin->vertsXyz[offset + 2]
        });
      }
      if (triangle.size() == 3) output["triangles"].push_back(std::move(triangle));
    }
    data["surfaces"].push_back(std::move(output));
  }
  data["alignments"] = Json::array();
  for (size_t index = 0; index < state.userPolylineAttrs.size() && index + 1 < state.userPolylineOffsets.size(); ++index) {
    const std::string& marker = state.userPolylineAttrs[index].layer;
    constexpr char prefix[] = "GS_C3D_ALIGN:";
    if (marker.rfind(prefix, 0) != 0) continue;
    const int first = state.userPolylineOffsets[index];
    const int last = state.userPolylineOffsets[index + 1];
    if (first < 0 || last < first || (static_cast<size_t>(last) * 3) > state.userPolylineVerts.size() || last - first < 2)
      continue;
    Json output = {{"name", marker.substr(sizeof(prefix) - 1)}, {"vertices", Json::array()}};
    for (int vertex = first; vertex < last; ++vertex) {
      const size_t offset = static_cast<size_t>(vertex) * 3;
      const double bulge = static_cast<size_t>(vertex) < state.userPolylineVertsBulge.size()
                               ? state.userPolylineVertsBulge[static_cast<size_t>(vertex)] : 0.0;
      output["vertices"].push_back({state.userPolylineVerts[offset] + state.worldDocumentOriginX,
                                    state.userPolylineVerts[offset + 1] + state.worldDocumentOriginY,
                                    state.userPolylineVerts[offset + 2], bulge});
    }
    data["alignments"].push_back(std::move(output));
  }
  return RunCivilImport(dwgPathUtf8, root, CurrentExecutableDirectory(), data, log);
#endif
}
