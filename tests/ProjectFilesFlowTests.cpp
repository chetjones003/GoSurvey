// REQ-379 (issue #696 P6) — the drawing-level half of file tracking: the copy / link question, the
// record written when a project drawing is saved, and what opening it does with that record. Domain
// only: AppCommandState and real files in a temp folder, no window. (Headless PdfAttach_Build always
// fails, so re-placing a real PDF is checked in the GUI, not here.)

#include "CadCommands.hpp"
#include "ProjectFiles.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;
using Kind = AppCommandState::ProjectAttachPrompt::Kind;

namespace {

struct TempDir {
  fs::path path;
  explicit TempDir(const char* stem) {
    path = fs::temp_directory_path() / (std::string("gosurvey-filesflow-test-") + stem);
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

void WriteText(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f << s;
}

/// A project in \p dir opened as session 7, with the ACTIVE tab (index 1) belonging to it.
AppCommandState::ProjectSession& OpenProjectTab(AppCommandState& st, const fs::path& dir, bool readOnly = false) {
  AppCommandState::ProjectSession s;
  s.uid = 7;
  std::string err;
  REQUIRE(gsproj::Create(dir, "Job", &s.project, &err));
  s.readOnly = readOnly;
  st.openProjects.push_back(std::move(s));
  st.drawingTabs.resize(2);
  st.drawingTabs[1].projectUid = 7;
  st.activeDrawingIdx = 1;
  return st.openProjects.back();
}

std::shared_ptr<const CadPointCloud> Cloud(const std::string& source) {
  auto pc = std::make_shared<CadPointCloud>();
  pc->sourcePath = source;
  pc->pointsXyz = {0, 0, 0, 1, 1, 1};
  pc->totalPointCount = 2;
  return pc;
}

}  // namespace

TEST_CASE("req379 attach question only appears for a writable project drawing and an outside file", "[req379]") {
  TempDir root("prompt");
  const fs::path outside = root.path / "outside" / "site.e57";
  WriteText(outside, "scan");
  std::vector<std::string> log;

  AppCommandState standalone;
  CHECK_FALSE(RequestProjectAttach(standalone, Kind::PointCloud, outside.u8string(), log));  // as before projects

  AppCommandState ro;
  OpenProjectTab(ro, root.path / "ro", /*readOnly=*/true);
  CHECK_FALSE(RequestProjectAttach(ro, Kind::PointCloud, outside.u8string(), log));

  AppCommandState st;
  auto& s = OpenProjectTab(st, root.path / "proj");
  const fs::path inside = s.project.Folder() / "PointClouds" / "in.e57";
  WriteText(inside, "scan");
  CHECK_FALSE(RequestProjectAttach(st, Kind::PointCloud, inside.u8string(), log));  // nothing to ask
  CHECK_FALSE(RequestProjectAttach(st, Kind::PointCloud, (root.path / "missing.e57").u8string(), log));

  REQUIRE(RequestProjectAttach(st, Kind::PointCloud, outside.u8string(), log));
  CHECK(st.projectAttachPrompt.kind == Kind::PointCloud);
  CHECK(st.projectAttachPrompt.destRel == "PointClouds/site.e57");
  CHECK(st.projectAttachPrompt.sizeBytes == 4);
}

TEST_CASE("req379 Copy puts the file in the project, Link leaves it and says so", "[req379]") {
  TempDir root("resolve");
  const fs::path outside = root.path / "outside" / "plan.pdf";
  WriteText(outside, "pdf-bytes");
  std::vector<std::string> log;
  AppCommandState st;
  auto& s = OpenProjectTab(st, root.path / "proj");

  REQUIRE(RequestProjectAttach(st, Kind::Pdf, outside.u8string(), log));
  std::string finalPath;
  REQUIRE(ResolveProjectAttach(st, /*copy=*/true, log, &finalPath));
  CHECK(fs::u8path(finalPath) == s.project.Folder() / "PDFs" / "plan.pdf");
  CHECK(fs::exists(finalPath));
  CHECK(st.projectAttachPrompt.kind == Kind::None);  // answered

  REQUIRE(RequestProjectAttach(st, Kind::Pdf, outside.u8string(), log));
  REQUIRE(ResolveProjectAttach(st, /*copy=*/false, log, &finalPath));
  CHECK(finalPath == outside.u8string());
  CHECK(log.back().find("will not travel") != std::string::npos);
  CHECK(fs::exists(outside));
}

TEST_CASE("req379 an attach whose project went read-only attaches nothing", "[req379]") {
  TempDir root("lost");
  const fs::path outside = root.path / "outside" / "plan.pdf";
  WriteText(outside, "pdf-bytes");
  std::vector<std::string> log;
  AppCommandState st;
  auto& s = OpenProjectTab(st, root.path / "proj");
  REQUIRE(RequestProjectAttach(st, Kind::Pdf, outside.u8string(), log));
  s.readOnly = true;
  std::string finalPath;
  CHECK_FALSE(ResolveProjectAttach(st, true, log, &finalPath));
  CHECK_FALSE(fs::exists(s.project.Folder() / "PDFs" / "plan.pdf"));
}

TEST_CASE("req379 saving records the drawing's clouds and PDFs; opening finds them under another path",
          "[req379]") {
  TempDir root("saveopen");
  std::vector<std::string> log;
  AppCommandState st;
  auto& s = OpenProjectTab(st, root.path / "proj");
  const fs::path cloud = s.project.Folder() / "PointClouds" / "site.e57";
  const fs::path pdf = s.project.Folder() / "PDFs" / "plan.pdf";
  const fs::path dwg = s.project.Folder() / "Drawings" / "EG.dwg";
  WriteText(cloud, "scan");
  WriteText(pdf, "pdf");
  WriteText(dwg, "dwg");

  st.worldDocumentOriginX = 1000.0;
  st.worldDocumentOriginY = 2000.0;
  st.cadPointClouds.push_back(Cloud(cloud.u8string()));
  PdfAttachment att;
  att.filePath = pdf.u8string();
  att.pageIndex = 2;
  att.insertX = 5.f;
  att.insertY = 6.f;
  att.scale = 0.5f;
  att.rotationDeg = 30.f;
  att.pageWidthPts = 72.f;
  att.texW = 150;
  st.pdfAttachments.push_back(att);

  SyncProjectFilesOnSave(st, 1, dwg.u8string(), log);
  gsproj::Project onDisk;
  std::string err;
  REQUIRE(gsproj::Load(s.project.file, &onDisk, &err));  // written to the .gsproj, not just memory
  const auto placements = projfiles::PlacementsFor(onDisk, "Drawings/EG.dwg");
  REQUIRE(placements.size() == 1);
  const json pl = json::parse(placements[0].second);
  CHECK(pl["page"] == 2);
  CHECK(pl["x"] == 1005.0);  // stored in world coordinates, so a different local origin still lines up
  CHECK(pl["y"] == 2006.0);
  CHECK(pl["rotationDeg"] == 30.0);
  CHECK(pl["dpi"] == 150.0);

  // "Another machine": the drawing remembers a path that does not exist here.
  AppCommandState other;
  other.openProjects = {s};
  other.drawingTabs.resize(2);
  other.drawingTabs[1].projectUid = 7;
  other.activeDrawingIdx = 1;
  other.worldDocumentOriginX = 1000.0;
  other.worldDocumentOriginY = 2000.0;
  other.cadPointClouds.push_back(Cloud("D:\\Jobs\\Smith\\PointClouds\\site.e57"));
  log.clear();
  ApplyProjectFilesOnOpen(other, 7, dwg.u8string(), log);
  CHECK(fs::u8path(other.cadPointClouds[0]->sourcePath) == cloud);
  CHECK(fs::u8path(other.cadPointClouds[0]->cloudCachePath) == fs::u8path(cloud.u8string() + ".gscloud"));
  CHECK(other.cadPointClouds[0]->pointsXyz.size() == 6);  // the preview sample survives the remap
  // Headless cannot rasterize, so the PDF is reported, not silently dropped; the drawing still opens.
  CHECK(other.pdfAttachments.empty());
  bool saidPdf = false;
  for (const std::string& l : log)
    saidPdf = saidPdf || l.find("plan.pdf") != std::string::npos;
  CHECK(saidPdf);
}

TEST_CASE("req379 a missing tracked PDF or cloud is reported and the drawing still opens", "[req379]") {
  TempDir root("missing");
  std::vector<std::string> log;
  AppCommandState st;
  auto& s = OpenProjectTab(st, root.path / "proj");
  const fs::path cloud = s.project.Folder() / "PointClouds" / "site.e57";
  const fs::path pdf = s.project.Folder() / "PDFs" / "plan.pdf";
  const fs::path dwg = s.project.Folder() / "Drawings" / "EG.dwg";
  WriteText(cloud, "scan");
  WriteText(pdf, "pdf");
  st.cadPointClouds.push_back(Cloud(cloud.u8string()));
  PdfAttachment att;
  att.filePath = pdf.u8string();
  st.pdfAttachments.push_back(att);
  SyncProjectFilesOnSave(st, 1, dwg.u8string(), log);

  fs::remove(cloud);
  fs::remove(pdf);
  AppCommandState reopened;
  reopened.openProjects = {s};
  reopened.drawingTabs.resize(2);
  reopened.drawingTabs[1].projectUid = 7;
  reopened.activeDrawingIdx = 1;
  reopened.cadPointClouds.push_back(Cloud(cloud.u8string()));
  log.clear();
  ApplyProjectFilesOnOpen(reopened, 7, dwg.u8string(), log);
  REQUIRE(reopened.cadPointClouds.size() == 1);  // still there: preview sample only
  CHECK(reopened.pdfAttachments.empty());
  CHECK(log.size() >= 2);
  const projfiles::Health h = ProjectHealthFor(reopened, 7);
  CHECK(h.missing.size() >= 2);
}

TEST_CASE("req380 a point cloud left out of a pack opens as unavailable, not as a missing-file error",
          "[req380]") {
  TempDir root("omitted");
  std::vector<std::string> log;
  AppCommandState st;
  auto& s = OpenProjectTab(st, root.path / "proj");
  const fs::path cloud = s.project.Folder() / "PointClouds" / "site.e57";
  const fs::path dwg = s.project.Folder() / "Drawings" / "EG.dwg";
  WriteText(cloud, "scan");
  WriteText(dwg, "drawing");
  st.cadPointClouds.push_back(Cloud(cloud.u8string()));
  SyncProjectFilesOnSave(st, 1, dwg.u8string(), log);

  // The pack was opened without the cloud: it is recorded as left out and is not on disk.
  fs::remove(cloud);
  projfiles::SetPackOmitted(&s.project, {"PointClouds/site.e57"});
  AppCommandState reopened;
  reopened.openProjects = {s};
  reopened.drawingTabs.resize(2);
  reopened.drawingTabs[1].projectUid = 7;
  reopened.activeDrawingIdx = 1;
  reopened.cadPointClouds.push_back(Cloud(cloud.u8string()));
  log.clear();
  ApplyProjectFilesOnOpen(reopened, 7, dwg.u8string(), log);
  REQUIRE(reopened.cadPointClouds.size() == 1);  // the drawing opens either way
  bool unavailable = false;
  for (const std::string& l : log) {
    CHECK(l.find("is missing") == std::string::npos);
    if (l.find("unavailable") != std::string::npos)
      unavailable = true;
  }
  CHECK(unavailable);
  const projfiles::Health h = ProjectHealthFor(reopened, 7);
  CHECK(h.missing.empty());
  CHECK(h.omitted.size() == 1);
}

TEST_CASE("req379 a standalone drawing, a read-only project and a drawing outside the folder record nothing",
          "[req379]") {
  TempDir root("noop");
  std::vector<std::string> log;
  const fs::path cloud = root.path / "site.e57";
  WriteText(cloud, "scan");

  AppCommandState standalone;
  standalone.drawingTabs.resize(2);
  standalone.activeDrawingIdx = 1;
  standalone.cadPointClouds.push_back(Cloud(cloud.u8string()));
  SyncProjectFilesOnSave(standalone, 1, (root.path / "a.dwg").u8string(), log);  // must not crash or write
  CHECK(standalone.openProjects.empty());

  AppCommandState ro;
  auto& rs = OpenProjectTab(ro, root.path / "ro", /*readOnly=*/true);
  ro.cadPointClouds.push_back(Cloud(cloud.u8string()));
  SyncProjectFilesOnSave(ro, 1, (rs.project.Folder() / "Drawings" / "a.dwg").u8string(), log);
  CHECK(rs.project.items.empty());

  AppCommandState st;
  auto& s = OpenProjectTab(st, root.path / "proj");
  st.cadPointClouds.push_back(Cloud(cloud.u8string()));
  SyncProjectFilesOnSave(st, 1, (root.path / "elsewhere.dwg").u8string(), log);  // not inside the project
  CHECK(s.project.items.empty());
}

TEST_CASE("req379 Project Health counts unsaved project drawings and Copy Links In saves the project", "[req379]") {
  TempDir root("healthflow");
  std::vector<std::string> log;
  AppCommandState st;
  auto& s = OpenProjectTab(st, root.path / "proj");
  st.drawingTabs[1].name = "EG";
  st.cadGpuRevision = 5;
  st.activeDocSavedRevision = 4;  // changed since the last save
  CHECK(ProjectHealthFor(st, 7).unsaved == std::vector<std::string>{"EG"});
  st.activeDocSavedRevision = 5;
  CHECK(ProjectHealthFor(st, 7).unsaved.empty());

  const fs::path outside = root.path / "outside" / "site.e57";
  WriteText(outside, "scan");
  gsproj::TrackedItem link;
  link.path = outside.u8string();
  link.kind = gsproj::kKindLocalLink;
  s.project.items.push_back(link);
  CHECK(ProjectHealthFor(st, 7).linked.size() == 1);

  size_t converted = 0;
  REQUIRE(CopyProjectLinksIn(st, 7, log, &converted));
  CHECK(converted == 1);
  gsproj::Project onDisk;
  std::string err;
  REQUIRE(gsproj::Load(s.project.file, &onDisk, &err));
  REQUIRE(onDisk.items.size() == 1);
  CHECK(onDisk.items[0].kind == gsproj::kKindInProject);
  CHECK(onDisk.items[0].path == "PointClouds/site.e57");
  CHECK(ProjectHealthFor(st, 7).linked.empty());

  s.readOnly = true;
  CHECK_FALSE(CopyProjectLinksIn(st, 7, log, &converted));  // a read-only project is never written
}

TEST_CASE("req379 Copy Links In repoints open drawings so the next save does not record the link again",
          "[req379][issue724]") {
  TempDir root("issue724");
  AppCommandState st;
  AppCommandState::ProjectSession& s = OpenProjectTab(st, root.path);
  st.drawingTabs.resize(3);  // tab 2 is a second drawing of the project, held in its document snapshot
  st.drawingTabs[2].projectUid = 7;
  st.documents.resize(3);
  std::vector<std::string> log;

  const fs::path outPdf = root.path / "outside" / "plan.pdf";
  const fs::path outCloud = root.path / "outside" / "site.e57";
  WriteText(outPdf, "%PDF-1.4 stand-in");
  WriteText(outCloud, "scan");
  PdfAttachment pdf;
  pdf.filePath = outPdf.u8string();
  st.pdfAttachments.push_back(pdf);  // active tab
  st.cadPointClouds.push_back(Cloud(outCloud.u8string()));
  st.documents[2].pdfAttachments.push_back(pdf);  // the other tab holds the same link
  st.documents[2].cadPointClouds.push_back(Cloud(outCloud.u8string()));

  const std::string egDwg = (s.project.Folder() / "Drawings" / "EG.dwg").u8string();
  SyncProjectFilesOnSave(st, 1, egDwg, log);
  REQUIRE(ProjectHealthFor(st, 7).linked.size() == 2);

  size_t converted = 0;
  REQUIRE(CopyProjectLinksIn(st, 7, log, &converted));
  CHECK(converted == 2);

  const std::string pdfCopy = (s.project.Folder() / "PDFs" / "plan.pdf").u8string();
  const std::string cloudCopy = (s.project.Folder() / "PointClouds" / "site.e57").u8string();
  REQUIRE(st.pdfAttachments.size() == 1);
  CHECK(st.pdfAttachments[0].filePath == pdfCopy);  // active tab
  REQUIRE(st.cadPointClouds.size() == 1);
  CHECK(st.cadPointClouds[0]->sourcePath == cloudCopy);
  REQUIRE(st.documents[2].pdfAttachments.size() == 1);
  CHECK(st.documents[2].pdfAttachments[0].filePath == pdfCopy);  // the tab that is not showing
  REQUIRE(st.documents[2].cadPointClouds.size() == 1);
  CHECK(st.documents[2].cadPointClouds[0]->sourcePath == cloudCopy);

  // The point of the fix: saving afterwards must not turn the copies back into links.
  SyncProjectFilesOnSave(st, 1, egDwg, log);
  CHECK(ProjectHealthFor(st, 7).linked.empty());
  gsproj::Project onDisk;
  std::string err;
  REQUIRE(gsproj::Load(s.project.file, &onDisk, &err));
  for (const gsproj::TrackedItem& it : onDisk.items)
    CHECK(it.kind != gsproj::kKindLocalLink);
}
