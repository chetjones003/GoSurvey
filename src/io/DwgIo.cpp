#include "DwgIo.hpp"

#include "DxfIo.hpp"
#include "ProcessRun.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <process.h> // _getpid
#else
#include <unistd.h> // getpid
#endif

namespace {

/// Conversions are out-of-process and can be slow on large drawings; cap the wait so a wedged
/// converter cannot hang the app. Measured: a 1.3 MB R2018 drawing converts in ~2 s.
constexpr int kConvertTimeoutMs = 10 * 60 * 1000;

/// DWG version written when GoSurvey saves. AC1032 matches what DxfIo already emits, so the
/// converter never has to down-convert. UI-03 (a user-facing version selector) is a follow-up.
constexpr const char* kOdaOutputVersion = "ACAD2018";
constexpr const char* kAcadSaveAsVersion = "2018";

std::string TrimTrailingSlash(std::string s) {
  while (!s.empty() && (s.back() == '\\' || s.back() == '/'))
    s.pop_back();
  return s;
}

/// AutoCAD script files treat a space as Enter, so every path a script names must be quoted.
/// Forward slashes are accepted at file prompts and avoid any backslash-escaping question.
std::string ScriptPath(const std::filesystem::path& p) {
  std::string s = p.generic_string();
  return "\"" + s + "\"";
}

bool WriteTextFile(const std::filesystem::path& p, const std::string& body) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f)
    return false;
  f.write(body.data(), static_cast<std::streamsize>(body.size()));
  return f.good();
}

/// Temporary working directory, removed when the scope exits so failed conversions leave nothing.
struct TempWorkDir {
  std::filesystem::path path;
  bool ok = false;

  explicit TempWorkDir(const char* tag) {
    std::error_code ec;
    const auto base = std::filesystem::temp_directory_path(ec);
    if (ec)
      return;
    static unsigned counter = 0;
    char name[128];
    std::snprintf(name, sizeof(name), "GoSurvey-%s-%u-%u", tag,
                  static_cast<unsigned>(
#ifdef _WIN32
                      _getpid()
#else
                      getpid()
#endif
                          ),
                  ++counter);
    path = base / name;
    std::filesystem::create_directories(path, ec);
    ok = !ec;
  }

  ~TempWorkDir() {
    if (!path.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(path, ec);
    }
  }

  TempWorkDir(const TempWorkDir&) = delete;
  TempWorkDir& operator=(const TempWorkDir&) = delete;
};

/// Runs one conversion. \p srcDir must contain exactly the one input file; the produced file is
/// expected at \p expectedOut. Returns false and logs a specific reason on any failure.
bool RunConversion(const DwgConverter& conv,
                   const std::filesystem::path& srcFile,
                   const std::filesystem::path& outDir,
                   const std::filesystem::path& expectedOut,
                   bool toDxf,
                   std::vector<std::string>& log) {
  int exitCode = -1;

  if (conv.kind == DwgConverterKind::OdaFileConverter) {
    // ODAFileConverter <inFolder> <outFolder> <outVer> <outType> <recurse> <audit> [filter]
    const std::vector<std::string> args = {
        TrimTrailingSlash(srcFile.parent_path().string()),
        TrimTrailingSlash(outDir.string()),
        kOdaOutputVersion,
        toDxf ? "DXF" : "DWG",
        "0",
        "0",
        toDxf ? "*.dwg" : "*.dxf",
    };
    if (!RunProcessAndWait(conv.exePath, args, outDir.string(), kConvertTimeoutMs, &exitCode)) {
      log.push_back("DWG conversion failed: ODA File Converter did not finish (timed out or could not start).");
      return false;
    }
  } else if (conv.kind == DwgConverterKind::AutoCadCore) {
    // accoreconsole /i <input> /s <script>. A blank line in a script REPEATS the previous
    // command, so the script must not contain one — that hangs the console at a prompt.
    const std::filesystem::path scr = outDir / "gosurvey-convert.scr";
    std::string body;
    if (toDxf) {
      body = "FILEDIA\n0\n_DXFOUT\n" + ScriptPath(expectedOut) + "\n16\n_QUIT\n_N\n";
    } else {
      body = "FILEDIA\n0\n_SAVEAS\n" + std::string(kAcadSaveAsVersion) + "\n" +
             ScriptPath(expectedOut) + "\n_QUIT\n_N\n";
    }
    if (!WriteTextFile(scr, body)) {
      log.push_back("DWG conversion failed: could not write the converter script to " + scr.string() + ".");
      return false;
    }
    const std::vector<std::string> args = {"/i", srcFile.string(), "/s", scr.string()};
    if (!RunProcessAndWait(conv.exePath, args, outDir.string(), kConvertTimeoutMs, &exitCode)) {
      log.push_back("DWG conversion failed: accoreconsole did not finish (timed out or could not start).");
      return false;
    }
  } else {
    log.push_back("DWG conversion failed: no converter selected.");
    return false;
  }

  std::error_code ec;
  if (!std::filesystem::exists(expectedOut, ec)) {
    log.push_back("DWG conversion failed: " + conv.displayName + " exited with code " +
                  std::to_string(exitCode) + " and produced no output file.");
    return false;
  }
  if (std::filesystem::file_size(expectedOut, ec) == 0) {
    log.push_back("DWG conversion failed: " + conv.displayName + " produced an empty file.");
    return false;
  }
  return true;
}

void LogNoConverter(std::vector<std::string>& log) {
  log.push_back("DWG support needs a converter, and none was found on this machine.");
  log.push_back("  Install the free ODA File Converter (opendesign.com), or use a machine with AutoCAD installed,");
  log.push_back("  or set GOSURVEY_DWG_CONVERTER to the full path of ODAFileConverter.exe or accoreconsole.exe.");
  log.push_back("  Native DWG reading (no converter required) is planned — see docs/dwg-plan.txt.");
}

} // namespace

bool ImportDwgFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  if (!pathUtf8 || pathUtf8[0] == '\0') {
    log.push_back("DWG import — no path.");
    return false;
  }

  std::error_code ec;
  const std::filesystem::path src(pathUtf8);
  if (!std::filesystem::exists(src, ec)) {
    log.push_back("DWG import — file not found: " + src.string());
    return false;
  }

  const std::string version = DwgVersionName(pathUtf8);
  if (version.empty()) {
    log.push_back("DWG import — " + src.filename().string() + " is not a DWG file (no AC#### format tag).");
    return false;
  }
  log.push_back("DWG import — " + src.filename().string() + " (" + version + ").");

  const DwgConverter& conv = FindDwgConverter();
  if (!conv.available()) {
    LogNoConverter(log);
    return false;
  }
  log.push_back("Converting to DXF with " + conv.displayName + "…");

  TempWorkDir work("dwgin");
  if (!work.ok) {
    log.push_back("DWG import failed: could not create a temporary working directory.");
    return false;
  }

  // ODA File Converter works folder-to-folder, so the input is staged in its own directory.
  const std::filesystem::path inDir = work.path / "in";
  const std::filesystem::path outDir = work.path / "out";
  std::filesystem::create_directories(inDir, ec);
  std::filesystem::create_directories(outDir, ec);
  if (ec) {
    log.push_back("DWG import failed: could not create temporary directories.");
    return false;
  }

  const std::filesystem::path staged = inDir / src.filename();
  std::filesystem::copy_file(src, staged, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    log.push_back("DWG import failed: could not stage the drawing (" + ec.message() + ").");
    return false;
  }

  const std::filesystem::path dxf = outDir / (src.stem().string() + ".dxf");
  if (!RunConversion(conv, staged, outDir, dxf, /*toDxf=*/true, log))
    return false;

  const bool ok = ImportDxfFile(st, dxf.string().c_str(), log);
  if (!ok) {
    log.push_back("DWG import failed while reading the converted DXF.");
    return false;
  }

  log.push_back("DWG import complete. Phase 1 reads DWG through DXF, so it carries DXF's limits:");
  log.push_back("  block references are exploded, paper-space layouts beyond the first are dropped,");
  log.push_back("  and elevations, attributes, multileaders and tables are not modelled yet.");
  return true;
}

bool ExportDwgFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  if (!pathUtf8 || pathUtf8[0] == '\0') {
    log.push_back("DWG export — no path.");
    return false;
  }

  const DwgConverter& conv = FindDwgConverter();
  if (!conv.available()) {
    LogNoConverter(log);
    return false;
  }

  const std::filesystem::path dst(pathUtf8);
  TempWorkDir work("dwgout");
  if (!work.ok) {
    log.push_back("DWG export failed: could not create a temporary working directory.");
    return false;
  }

  std::error_code ec;
  const std::filesystem::path inDir = work.path / "in";
  const std::filesystem::path outDir = work.path / "out";
  std::filesystem::create_directories(inDir, ec);
  std::filesystem::create_directories(outDir, ec);
  if (ec) {
    log.push_back("DWG export failed: could not create temporary directories.");
    return false;
  }

  const std::filesystem::path dxf = inDir / (dst.stem().string() + ".dxf");
  if (!ExportDxfFile(st, dxf.string().c_str(), log)) {
    log.push_back("DWG export failed while writing the intermediate DXF.");
    return false;
  }

  log.push_back("Converting to DWG with " + conv.displayName + "…");
  const std::filesystem::path produced = outDir / (dst.stem().string() + ".dwg");
  if (!RunConversion(conv, dxf, outDir, produced, /*toDxf=*/false, log))
    return false;

  // Only touch the destination once a good file exists, so a failed export never destroys the
  // previous version of the drawing.
  std::filesystem::copy_file(produced, dst, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    log.push_back("DWG export failed: could not write " + dst.string() + " (" + ec.message() + ").");
    return false;
  }

  log.push_back("DWG export complete: " + dst.filename().string() + ".");
  return true;
}
