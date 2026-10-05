// REQ-379 (issue #696 P6) — tracked files: attach (copy / link), what a saved drawing records, finding a
// file again, and Project Health. Pure: each case works in its own temp folders, no window.

#include "ProjectFiles.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

struct TempDir {
  fs::path path;
  explicit TempDir(const char* stem) {
    path = fs::temp_directory_path() / (std::string("gosurvey-projfiles-test-") + stem);
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

std::string ReadText(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

gsproj::Project MakeProject(const fs::path& parent) {
  gsproj::Project p;
  std::string err;
  REQUIRE(gsproj::Create(parent, "Job", &p, &err));
  return p;
}

const gsproj::TrackedItem* Find(const gsproj::Project& p, const std::string& path) {
  for (const auto& it : p.items)
    if (it.path == path)
      return &it;
  return nullptr;
}

}  // namespace

TEST_CASE("req379 copy is the default: file lands in the role folder, project-relative, source untouched",
          "[req379]") {
  TempDir root("copy");
  gsproj::Project p = MakeProject(root.path / "proj");
  const fs::path src = root.path / "outside" / "site.e57";
  WriteText(src, "scan-bytes");
  WriteText(root.path / "outside" / "site.e57.gscloud", "cache-bytes");

  projfiles::AttachPlan plan;
  std::string err;
  REQUIRE(projfiles::PlanAttach(p, src, projfiles::Role::PointCloud, &plan, &err));
  CHECK_FALSE(plan.alreadyInProject);
  CHECK(plan.destRel == "PointClouds/site.e57");
  CHECK_FALSE(fs::exists(plan.dest));  // planning changes nothing

  REQUIRE(projfiles::CopyIn(plan, projfiles::Role::PointCloud, &err));
  CHECK(ReadText(p.Folder() / "PointClouds" / "site.e57") == "scan-bytes");
  CHECK(ReadText(p.Folder() / "PointClouds" / "site.e57.gscloud") == "cache-bytes");  // the cache travels too
  CHECK(fs::last_write_time(plan.dest) == fs::last_write_time(src));  // cache is matched by size + time
  CHECK(ReadText(src) == "scan-bytes");  // the original is left alone
}

TEST_CASE("req379 a file already inside the project is not copied; same name gets a free name; same file is reused",
          "[req379]") {
  TempDir root("names");
  gsproj::Project p = MakeProject(root.path / "proj");

  const fs::path inside = p.Folder() / "PDFs" / "plan.pdf";
  WriteText(inside, "pdf");
  projfiles::AttachPlan plan;
  std::string err;
  REQUIRE(projfiles::PlanAttach(p, inside, projfiles::Role::Pdf, &plan, &err));
  CHECK(plan.alreadyInProject);
  CHECK(plan.destRel == "PDFs/plan.pdf");

  // A different file with the same name must not overwrite the one already there.
  const fs::path other = root.path / "outside" / "plan.pdf";
  WriteText(other, "a different, longer pdf");
  REQUIRE(projfiles::PlanAttach(p, other, projfiles::Role::Pdf, &plan, &err));
  CHECK(plan.destRel == "PDFs/plan (2).pdf");
  REQUIRE(projfiles::CopyIn(plan, projfiles::Role::Pdf, &err));
  CHECK(ReadText(inside) == "pdf");

  // Attaching the same outside file again reuses its earlier copy instead of piling up duplicates.
  REQUIRE(projfiles::PlanAttach(p, other, projfiles::Role::Pdf, &plan, &err));
  CHECK(plan.reuseExisting);
  CHECK(plan.destRel == "PDFs/plan (2).pdf");

  CHECK_FALSE(projfiles::PlanAttach(p, root.path / "nope.pdf", projfiles::Role::Pdf, &plan, &err));
  CHECK_FALSE(err.empty());
}

TEST_CASE("req379 size prompt threshold", "[req379]") {
  projfiles::AttachPlan plan;
  plan.sizeBytes = projfiles::kLargeFileBytes - 1;
  CHECK_FALSE(plan.Large());
  plan.sizeBytes = projfiles::kLargeFileBytes;
  CHECK(plan.Large());
}

TEST_CASE("req379 saving a drawing records its files; a link is flagged; relative path is recorded", "[req379]") {
  TempDir root("sync");
  gsproj::Project p = MakeProject(root.path / "proj");
  const fs::path cloud = p.Folder() / "PointClouds" / "site.e57";
  const fs::path linked = root.path / "elsewhere" / "ortho.pdf";
  WriteText(cloud, "c");
  WriteText(linked, "p");

  const json place = {{"page", 0}, {"x", 100.0}, {"y", 200.0}, {"scale", 2.0}};
  REQUIRE(projfiles::SyncDrawing(&p, "Drawings/EG.dwg", {{cloud, {}}}, {{linked, place.dump()}}));

  const auto* d = Find(p, "Drawings/EG.dwg");
  REQUIRE(d);
  CHECK(d->kind == gsproj::kKindInProject);
  const auto* c = Find(p, "PointClouds/site.e57");  // relative, never absolute
  REQUIRE(c);
  CHECK(c->kind == gsproj::kKindInProject);
  CHECK(c->associations == std::vector<std::string>{"Drawings/EG.dwg"});
  const auto* l = Find(p, fs::weakly_canonical(linked).u8string());
  REQUIRE(l);
  CHECK(l->kind == gsproj::kKindLocalLink);
  const json pl = json::parse(l->placementsJson);
  REQUIRE(pl.size() == 1);
  CHECK(pl[0]["drawing"] == "Drawings/EG.dwg");
  CHECK(pl[0]["scale"] == 2.0);

  // Nothing changed -> nothing to rewrite.
  CHECK_FALSE(projfiles::SyncDrawing(&p, "Drawings/EG.dwg", {{cloud, {}}}, {{linked, place.dump()}}));

  // The drawing drops the PDF: the file stays tracked but is no longer tied to the drawing.
  REQUIRE(projfiles::SyncDrawing(&p, "Drawings/EG.dwg", {{cloud, {}}}, {}));
  l = Find(p, fs::weakly_canonical(linked).u8string());
  REQUIRE(l);
  CHECK(l->associations.empty());
  CHECK(json::parse(l->placementsJson).empty());
}

TEST_CASE("req379 placements survive the .gsproj round trip and only list their own drawing", "[req379]") {
  TempDir root("roundtrip");
  gsproj::Project p = MakeProject(root.path / "proj");
  const fs::path pdf = p.Folder() / "PDFs" / "plan.pdf";
  WriteText(pdf, "p");
  const json a = {{"page", 0}, {"x", 1.0}, {"y", 2.0}};
  const json b = {{"page", 1}, {"x", 3.0}, {"y", 4.0}};
  REQUIRE(projfiles::SyncDrawing(&p, "Drawings/EG.dwg", {}, {{pdf, a.dump()}}));
  REQUIRE(projfiles::SyncDrawing(&p, "Drawings/FG.dwg", {}, {{pdf, b.dump()}}));

  std::string err;
  REQUIRE(gsproj::Save(p, &err));
  gsproj::Project back;
  REQUIRE(gsproj::Load(p.file, &back, &err));

  const auto eg = projfiles::PlacementsFor(back, "Drawings/EG.dwg");
  REQUIRE(eg.size() == 1);
  CHECK(json::parse(eg[0].second)["page"] == 0);
  CHECK(fs::u8path(eg[0].first) == pdf);
  const auto fg = projfiles::PlacementsFor(back, "Drawings/FG.dwg");
  REQUIRE(fg.size() == 1);
  CHECK(json::parse(fg[0].second)["page"] == 1);
  CHECK(projfiles::PlacementsFor(back, "Drawings/none.dwg").empty());
}

TEST_CASE("req379 a project moved to another machine still finds its point cloud", "[req379]") {
  TempDir root("find");
  gsproj::Project p = MakeProject(root.path / "proj");
  const fs::path cloud = p.Folder() / "PointClouds" / "site.e57";
  WriteText(cloud, "c");
  REQUIRE(projfiles::SyncDrawing(&p, "Drawings/EG.dwg", {{cloud, {}}}, {}));

  // The drawing remembers the sender's path; the file of that name is tracked in this project.
  const std::string senders = "D:\\Jobs\\Smith\\PointClouds\\site.e57";
  CHECK(fs::u8path(projfiles::FindAttachedFile(p, "Drawings/EG.dwg", senders)) == cloud);
  // Not tied to another drawing, and not present anywhere -> not found.
  CHECK(projfiles::FindAttachedFile(p, "Drawings/FG.dwg", senders).empty());
  // The tracked file wins over a stored path that still exists elsewhere.
  const fs::path stale = root.path / "old" / "site.e57";
  WriteText(stale, "c");
  CHECK(fs::u8path(projfiles::FindAttachedFile(p, "Drawings/EG.dwg", stale.u8string())) == cloud);
  CHECK(fs::u8path(projfiles::FindAttachedFile(p, "Drawings/FG.dwg", stale.u8string())) == stale);
}

TEST_CASE("req379 Project Health lists each problem class and Copy Links In fixes links", "[req379]") {
  TempDir root("health");
  gsproj::Project p = MakeProject(root.path / "proj");
  const fs::path linkedPdf = root.path / "outside" / "ortho.pdf";
  const fs::path goneLink = root.path / "outside" / "gone.e57";
  WriteText(linkedPdf, "p");
  WriteText(p.Folder() / "Drawings" / "EG.dwg", "d");  // the saved drawing itself exists
  const fs::path missingIn = p.Folder() / "PointClouds" / "lost.e57";  // tracked, never written
  REQUIRE(projfiles::SyncDrawing(&p, "Drawings/EG.dwg", {{missingIn, {}}, {goneLink, {}}}, {{linkedPdf, "{}"}}));
  gsproj::TrackedItem remote;
  remote.path = "s3://bucket/big.e57";
  remote.kind = "remote";
  p.items.push_back(remote);

  const projfiles::Health h = projfiles::CheckHealth(p, {"EG (unsaved)"});
  CHECK(h.linked.size() == 2);       // the PDF and the cloud that is gone
  CHECK(h.missing.size() == 2);      // lost.e57 and gone.e57
  CHECK(h.unavailable == std::vector<std::string>{"s3://bucket/big.e57"});
  CHECK(h.unsaved == std::vector<std::string>{"EG (unsaved)"});
  CHECK_FALSE(h.Clean());

  const projfiles::CopyLinksResult r = projfiles::CopyLinksIn(&p);
  CHECK(r.converted == 1);  // only the present link can be copied
  CHECK(r.failed.size() == 1);
  CHECK(fs::exists(p.Folder() / "PDFs" / "ortho.pdf"));
  const auto* moved = Find(p, "PDFs/ortho.pdf");
  REQUIRE(moved);
  CHECK(moved->kind == gsproj::kKindInProject);
  CHECK(moved->associations == std::vector<std::string>{"Drawings/EG.dwg"});  // still attached

  // A drawing still holding the old link path does not bring the link back.
  projfiles::SyncDrawing(&p, "Drawings/EG.dwg", {{missingIn, {}}, {goneLink, {}}}, {{linkedPdf, "{}"}});
  CHECK(Find(p, fs::weakly_canonical(linkedPdf).u8string()) == nullptr);

  gsproj::Project clean = MakeProject(root.path / "clean");
  CHECK(projfiles::CheckHealth(clean, {}).Clean());
}

TEST_CASE("req379 a different outside file sharing a copied file's name stays its own link; no duplicate paths",
          "[req379]") {
  TempDir root("samename");
  gsproj::Project p = MakeProject(root.path / "proj");
  const fs::path copied = p.Folder() / "PDFs" / "site.pdf";
  WriteText(copied, "short");
  REQUIRE(projfiles::SyncDrawing(&p, "Drawings/EG.dwg", {}, {{copied, "{}"}}));

  const fs::path other = root.path / "elsewhere" / "site.pdf";
  WriteText(other, "a completely different and longer file");
  REQUIRE(projfiles::SyncDrawing(&p, "Drawings/EG.dwg", {}, {{copied, "{}"}, {other, "{}"}}));
  const auto* link = Find(p, fs::weakly_canonical(other).u8string());
  REQUIRE(link);
  CHECK(link->kind == gsproj::kKindLocalLink);

  // Copying that link in lands on a name already taken by a different file: its own item, no duplicates.
  const projfiles::CopyLinksResult r = projfiles::CopyLinksIn(&p);
  CHECK(r.converted == 1);
  size_t clashes = 0;
  for (const auto& a : p.items)
    for (const auto& b : p.items)
      if (&a != &b && a.path == b.path)
        ++clashes;
  CHECK(clashes == 0);
  CHECK(Find(p, "PDFs/site (2).pdf") != nullptr);
}
