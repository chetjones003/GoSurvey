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
#include "DwgIo.hpp"

#include <imgui.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Mirrors HeadlessDriver.cpp's InitHeadlessImGui (ADR-031 (c')) — sets up a headless ImGui font
// atlas so text/surface-label measurement (ImGui::GetFont()) does not crash outside the GUI.
// Duplicated rather than linked: HeadlessDriver.cpp owns its own main().
static void InitHeadlessImGuiLocal() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1920.f, 1080.f);
  io.DeltaTime = 1.0f / 60.0f;
  io.IniFilename = nullptr;
  io.Fonts->AddFontDefault();
  io.FontGlobalScale = 1.35f;
  unsigned char* pixels = nullptr;
  int w = 0;
  int h = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
  ImGui::NewFrame();
}

static int Usage() {
  std::fprintf(stderr,
               "usage: GsJsonDwgFixture to-dwg   <in.json> <out.dwg>\n"
               "       GsJsonDwgFixture from-dwg <in.dwg>  <out.json>\n");
  return 1;
}

static int ToDwg(const char* inJson, const char* outDwg) {
  InitHeadlessImGuiLocal();
  AppCommandState st;
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
  if (argc != 4)
    return Usage();
  const std::string mode = argv[1];
  if (mode == "to-dwg")
    return ToDwg(argv[2], argv[3]);
  if (mode == "from-dwg")
    return FromDwg(argv[2], argv[3]);
  return Usage();
}
