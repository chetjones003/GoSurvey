// Fixture-authoring helper for issue #264 (retire standalone .gs): the GoSurvey JSON document
// format (the same shape a .gst template holds, REQ-175) lives on ONLY as the trailer embedded
// inside a .dwg. samples/*.dwg fixtures and their tools/Make-*.ps1 generators are JSON-authored,
// so this converts between raw JSON and a .dwg carrying it as a trailer:
//
//   GsJsonDwgFixture to-dwg   <in.json> <out.dwg>   JSON -> .dwg (ExportDwgFile)
//   GsJsonDwgFixture from-dwg <in.dwg>  <out.json>  .dwg -> the embedded JSON trailer
//
// Not shipped: GoSurvey.exe neither links nor installs this (see CMakeLists.txt).
#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "QtText.hpp"
#include "DwgIo.hpp"


#include <QCoreApplication>
#include <QGuiApplication>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int Usage() {
  std::fprintf(stderr,
               "usage: GsJsonDwgFixture to-dwg   <in.json> <out.dwg>\n"
               "       GsJsonDwgFixture from-dwg <in.dwg>  <out.json>\n");
  return 1;
}

static int ToDwg(const char* inJson, const char* outDwg) {
  AppCommandState st;
  st.measureAnnotationText = MeasureQtText;
  std::vector<std::string> log;
  if (!LoadGoSurveyTemplateFile(st, inJson, log)) {
    for (auto& s : log) std::fprintf(stderr, "%s\n", s.c_str());
    return 2;
  }
  if (!ExportDwgFile(st, outDwg, log)) {
    for (auto& s : log) std::fprintf(stderr, "%s\n", s.c_str());
    return 3;
  }
  std::printf("wrote %s\n", outDwg);
  return 0;
}

static int FromDwg(const char* inDwg, const char* outJson) {
  std::ifstream in(inDwg, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "could not open %s\n", inDwg);
    return 2;
  }
  const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::string json;
  if (!TryGoSurveyDwgPayloadFromBytes(bytes, json)) {
    std::fprintf(stderr, "%s has no embedded GoSurvey trailer\n", inDwg);
    return 3;
  }
  std::ofstream out(outJson, std::ios::binary | std::ios::trunc);
  out << json;
  std::printf("wrote %s\n", outJson);
  return 0;
}

int main(int argc, char** argv) {
  try {
    if (argc != 4)
      return Usage();
    const std::string mode = argv[1];
    if (mode == "to-dwg") {
      qputenv("QT_QPA_PLATFORM", "offscreen");
      qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QCoreApplication::applicationDirPath().toLocal8Bit() + "/qt-plugins/platforms");
      QGuiApplication qtApp(argc, argv);
      return ToDwg(argv[2], argv[3]);
    }
    if (mode == "from-dwg")
      return FromDwg(argv[2], argv[3]);
    return Usage();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "unhandled exception: %s\n", e.what());
    return 1;
  }
}
