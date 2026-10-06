// REQ-380 (issue #696 P7) — Pack Project / Open Packed Project: the pure pack and unpack. Each case
// works in its own temp folders, no window. Hand-made hostile packs are built with miniz directly.

#include "ProjectFiles.hpp"
#include "ProjectPack.hpp"

#include <catch2/catch_test_macros.hpp>

#include <miniz.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

struct TempDir {
  fs::path path;
  explicit TempDir(const char* stem) {
    path = fs::temp_directory_path() / (std::string("gosurvey-pack-test-") + stem);
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

/// A project "Job" with a drawing, a PDF, a point cloud + its cache, plus a lock file and a temp file.
gsproj::Project MakeProject(const fs::path& parent) {
  gsproj::Project p;
  std::string err;
  REQUIRE(gsproj::Create(parent, "Job", &p, &err));
  const fs::path root = p.Folder();
  WriteText(root / "Drawings" / "EG.dwg", "drawing bytes EG");
  WriteText(root / "PDFs" / "plan.pdf", "%PDF fake");
  WriteText(root / "PointClouds" / "site.e57", std::string(4096, 'e'));
  WriteText(root / "PointClouds" / "site.e57.gscloud", std::string(2048, 'c'));
  WriteText(root / "Points" / "survey-points.gspdb", R"({"points":[]})");
  WriteText(root / "Job.gsproj.lock", "someone");
  WriteText(root / "Drawings" / "half-written.tmp", "temp");
  // A cloud's cache is matched to it by exact modified time; give both an odd time ZIP cannot hold.
  const auto t = fs::file_time_type::clock::now() - std::chrono::seconds(12345) + fs::file_time_type::duration(1234567);
  fs::last_write_time(root / "PointClouds" / "site.e57", t);
  fs::last_write_time(root / "PointClouds" / "site.e57.gscloud", t);

  gsproj::TrackedItem dwg;
  dwg.path = "Drawings/EG.dwg";
  gsproj::TrackedItem cloud;
  cloud.path = "PointClouds/site.e57";
  cloud.associations = {"Drawings/EG.dwg"};
  gsproj::TrackedItem pdf;
  pdf.path = "PDFs/plan.pdf";
  pdf.associations = {"Drawings/EG.dwg"};
  pdf.placementsJson = R"([{"drawing":"Drawings/EG.dwg","page":0,"x":1.5,"y":2.5}])";
  p.items = {dwg, cloud, pdf};
  REQUIRE(gsproj::Save(p, &err));
  return p;
}

/// Builds a zip with exactly these entries (name, content), stored uncompressed so a test can corrupt
/// a byte of the content and be sure of what it hit.
void MakeZip(const fs::path& out, const std::vector<std::pair<std::string, std::string>>& entries) {
  fs::create_directories(out.parent_path());
  mz_zip_archive zip{};
  REQUIRE(mz_zip_writer_init_file(&zip, out.string().c_str(), 0));
  for (const auto& e : entries)
    REQUIRE(mz_zip_writer_add_mem(&zip, e.first.c_str(), e.second.data(), e.second.size(), MZ_NO_COMPRESSION));
  REQUIRE(mz_zip_writer_finalize_archive(&zip));
  REQUIRE(mz_zip_writer_end(&zip));
}

std::string ManifestFor(const std::string& id) {
  json j;
  j["formatVersion"] = 1;
  j["projectId"] = id;
  j["projectName"] = "Hostile";
  j["packedUnix"] = 1;
  j["excluded"] = json::array();
  j["files"] = json::array();
  return j.dump();
}

std::string MarkerFor(const std::string& id) {
  json j;
  j["formatVersion"] = 1;
  j["id"] = id;
  j["name"] = "Hostile";
  j["layout"] = {{"drawings", "Drawings"}};
  j["items"] = json::array();
  return j.dump();
}

bool PackTo(const gsproj::Project& p, const fs::path& out, bool excludeClouds, std::string* err) {
  gspack::PackPlan plan;
  if (!gspack::PlanPack(p, out, &plan, err))
    return false;
  gspack::PackOptions opt;
  opt.excludePointClouds = excludeClouds;
  opt.nowUnix = 1700000000;
  return gspack::WritePack(p, plan, opt, out, err);
}

bool DirIsEmptyOrMissing(const fs::path& d) {
  std::error_code ec;
  return !fs::exists(d, ec) || fs::directory_iterator(d, ec) == fs::directory_iterator();
}

}  // namespace

TEST_CASE("req380 pack then open round-trips a project: same bytes, same project id, exact times",
          "[req380]") {
  TempDir tmp("roundtrip");
  const gsproj::Project p = MakeProject(tmp.path / "src");
  const fs::path pack = tmp.path / "out" / "Job.gspack";
  std::string err;
  fs::create_directories(pack.parent_path());
  REQUIRE(PackTo(p, pack, false, &err));
  REQUIRE(fs::is_regular_file(pack));
  REQUIRE_FALSE(fs::exists(pack.string() + ".tmp"));

  const fs::path dest = tmp.path / "opened";
  fs::path marker;
  gspack::Manifest m;
  REQUIRE(gspack::ExtractPack(pack, dest, &marker, &m, &err));
  CHECK(m.projectId == p.id);
  CHECK(m.projectName == "Job");
  CHECK(m.excluded.empty());
  CHECK(marker == dest / "Job.gsproj");

  for (const char* rel : {"Drawings/EG.dwg", "PDFs/plan.pdf", "PointClouds/site.e57",
                          "PointClouds/site.e57.gscloud", "Points/survey-points.gspdb"})
    CHECK(ReadText(dest / rel) == ReadText(p.Folder() / rel));

  // A cache is matched to its cloud by exact time (ADR-060): the pack must restore it to the tick.
  CHECK(fs::last_write_time(dest / "PointClouds" / "site.e57") ==
        fs::last_write_time(p.Folder() / "PointClouds" / "site.e57"));
  CHECK(fs::last_write_time(dest / "PointClouds" / "site.e57.gscloud") ==
        fs::last_write_time(p.Folder() / "PointClouds" / "site.e57.gscloud"));

  gsproj::Project opened;
  REQUIRE(gsproj::Load(marker, &opened, &err));
  CHECK(opened.id == p.id);  // the project ID survives
  REQUIRE(opened.items.size() == 3);
  CHECK(opened.items[2].placementsJson.find("1.5") != std::string::npos);  // PDF placements travel
  const projfiles::Health h = projfiles::CheckHealth(opened, {});
  CHECK(h.missing.empty());  // no missing-file errors
  CHECK(h.omitted.empty());
  CHECK(h.Clean());
}

TEST_CASE("req380 a pack never holds the lock file or temporary files, nor itself", "[req380]") {
  TempDir tmp("neverpacked");
  const gsproj::Project p = MakeProject(tmp.path / "src");
  // Save the pack INSIDE the project folder: it must not swallow itself.
  const fs::path pack = p.Folder() / "Job.gspack";
  std::string err;
  REQUIRE(PackTo(p, pack, false, &err));

  gspack::PackPlan plan;
  REQUIRE(gspack::PlanPack(p, pack, &plan, &err));
  for (const auto& f : plan.files) {
    CHECK(f.rel != "Job.gsproj.lock");
    CHECK(f.rel != "Drawings/half-written.tmp");
    CHECK(f.rel != "Job.gspack");
  }

  const fs::path dest = tmp.path / "opened";
  REQUIRE(gspack::ExtractPack(pack, dest, nullptr, nullptr, &err));
  CHECK_FALSE(fs::exists(dest / "Job.gsproj.lock"));
  CHECK_FALSE(fs::exists(dest / "Drawings" / "half-written.tmp"));
  CHECK_FALSE(fs::exists(dest / "Job.gspack"));
  CHECK(fs::exists(dest / "Job.gsproj"));
}

TEST_CASE("req380 size plan separates the point clouds' share", "[req380]") {
  TempDir tmp("plan");
  const gsproj::Project p = MakeProject(tmp.path / "src");
  gspack::PackPlan plan;
  std::string err;
  REQUIRE(gspack::PlanPack(p, fs::path(), &plan, &err));
  CHECK(plan.pointCloudBytes == 4096 + 2048);
  CHECK(plan.totalBytes > plan.pointCloudBytes);
  CHECK_FALSE(plan.Large());
}

TEST_CASE("req380 excluding point clouds leaves them out and marks them unavailable, not missing",
          "[req380]") {
  TempDir tmp("exclude");
  const gsproj::Project p = MakeProject(tmp.path / "src");
  const fs::path pack = tmp.path / "Job.gspack";
  std::string err;
  REQUIRE(PackTo(p, pack, true, &err));

  const fs::path dest = tmp.path / "opened";
  fs::path marker;
  gspack::Manifest m;
  REQUIRE(gspack::ExtractPack(pack, dest, &marker, &m, &err));
  CHECK_FALSE(fs::exists(dest / "PointClouds" / "site.e57"));
  CHECK_FALSE(fs::exists(dest / "PointClouds" / "site.e57.gscloud"));
  CHECK(fs::exists(dest / "Drawings" / "EG.dwg"));
  CHECK(m.excluded.size() == 2);

  gsproj::Project opened;
  REQUIRE(gsproj::Load(marker, &opened, &err));
  CHECK(projfiles::PackOmitted(opened) == std::vector<std::string>{"PointClouds/site.e57"});
  const projfiles::Health h = projfiles::CheckHealth(opened, {});
  CHECK(h.missing.empty());
  CHECK(h.omitted == std::vector<std::string>{"PointClouds/site.e57"});
  CHECK(h.Clean());  // information, not a problem
  CHECK(projfiles::IsPackOmittedName(opened, "C:/other machine/scans/site.e57"));
  CHECK_FALSE(projfiles::IsPackOmittedName(opened, "other.e57"));

  // The cloud turns up later: it is no longer "unavailable".
  WriteText(dest / "PointClouds" / "site.e57", "back");
  CHECK_FALSE(projfiles::IsPackOmittedName(opened, "site.e57"));
}

TEST_CASE("req380 a pack with an unsafe entry is rejected and extracts nothing", "[req380]") {
  const char* bad[] = {"../evil.txt",       "Drawings/../../evil.txt", "/abs/evil.txt",
                       "C:/evil.txt",       "Drawings\\evil.txt",      "Drawings//evil.txt",
                       "Drawings/evil.",    "Drawings/./evil.txt"};
  for (const char* name : bad) {
    TempDir tmp("unsafe");
    const fs::path pack = tmp.path / "evil.gspack";
    // miniz's writer refuses these names itself, so write a same-length placeholder and patch the
    // name into the finished file (it appears in the local header and the central directory).
    const std::string evil = name;
    const std::string placeholder(evil.size(), 'Q');
    MakeZip(pack, {{"gspack.json", ManifestFor("id-1")},
                   {"Hostile.gsproj", MarkerFor("id-1")},
                   {placeholder, "payload"}});
    std::string raw = ReadText(pack);
    for (size_t at = raw.find(placeholder); at != std::string::npos; at = raw.find(placeholder, at + 1))
      raw.replace(at, evil.size(), evil);
    {
      std::ofstream f(pack, std::ios::binary | std::ios::trunc);
      f << raw;
    }
    const fs::path dest = tmp.path / "opened";
    std::string err;
    INFO("entry name: " << name);
    CHECK_FALSE(gspack::ExtractPack(pack, dest, nullptr, nullptr, &err));
    CHECK(err.find("unsafe") != std::string::npos);
    CHECK(DirIsEmptyOrMissing(dest));
    CHECK_FALSE(fs::exists(tmp.path / "evil.txt"));
    CHECK_FALSE(fs::exists(tmp.path.parent_path() / "evil.txt"));
  }
}

TEST_CASE("req380 a pack that is not a valid GoSurvey pack is rejected", "[req380]") {
  std::string err;
  {  // not a zip at all
    TempDir tmp("notzip");
    WriteText(tmp.path / "a.gspack", "this is not a zip");
    CHECK_FALSE(gspack::ExtractPack(tmp.path / "a.gspack", tmp.path / "o", nullptr, nullptr, &err));
    CHECK(DirIsEmptyOrMissing(tmp.path / "o"));
  }
  {  // no manifest
    TempDir tmp("nomanifest");
    MakeZip(tmp.path / "a.gspack", {{"Hostile.gsproj", MarkerFor("id-1")}});
    CHECK_FALSE(gspack::ExtractPack(tmp.path / "a.gspack", tmp.path / "o", nullptr, nullptr, &err));
    CHECK(DirIsEmptyOrMissing(tmp.path / "o"));
  }
  {  // no project file
    TempDir tmp("nomarker");
    MakeZip(tmp.path / "a.gspack", {{"gspack.json", ManifestFor("id-1")}, {"Drawings/a.dwg", "x"}});
    CHECK_FALSE(gspack::ExtractPack(tmp.path / "a.gspack", tmp.path / "o", nullptr, nullptr, &err));
    CHECK(DirIsEmptyOrMissing(tmp.path / "o"));
  }
  {  // marker id differs from the manifest's
    TempDir tmp("idmismatch");
    MakeZip(tmp.path / "a.gspack", {{"gspack.json", ManifestFor("id-1")}, {"Hostile.gsproj", MarkerFor("id-2")}});
    CHECK_FALSE(gspack::ExtractPack(tmp.path / "a.gspack", tmp.path / "o", nullptr, nullptr, &err));
    CHECK(err.find("does not match") != std::string::npos);
    CHECK(DirIsEmptyOrMissing(tmp.path / "o"));
  }
  {  // the same name twice (differing only in case)
    TempDir tmp("dup");
    MakeZip(tmp.path / "a.gspack", {{"gspack.json", ManifestFor("id-1")},
                                    {"Hostile.gsproj", MarkerFor("id-1")},
                                    {"Drawings/a.dwg", "1"},
                                    {"drawings/A.dwg", "2"}});
    CHECK_FALSE(gspack::ExtractPack(tmp.path / "a.gspack", tmp.path / "o", nullptr, nullptr, &err));
    CHECK(err.find("twice") != std::string::npos);
    CHECK(DirIsEmptyOrMissing(tmp.path / "o"));
  }
  {  // a pack from a newer GoSurvey
    TempDir tmp("newer");
    json mj = json::parse(ManifestFor("id-1"));
    mj["formatVersion"] = 99;
    MakeZip(tmp.path / "a.gspack", {{"gspack.json", mj.dump()}, {"Hostile.gsproj", MarkerFor("id-1")}});
    CHECK_FALSE(gspack::ExtractPack(tmp.path / "a.gspack", tmp.path / "o", nullptr, nullptr, &err));
    CHECK(err.find("newer") != std::string::npos);
  }
}

TEST_CASE("req380 a non-empty destination is refused and left untouched", "[req380]") {
  TempDir tmp("nonempty");
  const gsproj::Project p = MakeProject(tmp.path / "src");
  const fs::path pack = tmp.path / "Job.gspack";
  std::string err;
  REQUIRE(PackTo(p, pack, false, &err));

  const fs::path dest = tmp.path / "occupied";
  WriteText(dest / "mine.txt", "keep me");
  CHECK_FALSE(gspack::ExtractPack(pack, dest, nullptr, nullptr, &err));
  CHECK(err.find("already holds files") != std::string::npos);
  CHECK(ReadText(dest / "mine.txt") == "keep me");
  CHECK_FALSE(fs::exists(dest / "Job.gsproj"));

  // An existing EMPTY folder is fine.
  const fs::path empty = tmp.path / "empty";
  fs::create_directories(empty);
  CHECK(gspack::ExtractPack(pack, empty, nullptr, nullptr, &err));
  CHECK(fs::exists(empty / "Job.gsproj"));
}

TEST_CASE("req380 a damaged pack (failed checksum) leaves nothing behind", "[req380]") {
  TempDir tmp("damaged");
  const std::string payload(500, 'Z');
  const fs::path pack = tmp.path / "a.gspack";
  MakeZip(pack, {{"gspack.json", ManifestFor("id-1")},
                 {"Hostile.gsproj", MarkerFor("id-1")},
                 {"Drawings/a.dwg", "first file is fine"},
                 {"Drawings/b.dwg", payload}});
  // The entries are stored uncompressed: flip one byte inside the last file's content.
  std::string raw = ReadText(pack);
  const size_t at = raw.find(payload);
  REQUIRE(at != std::string::npos);
  raw[at + 100] = 'Y';
  {
    std::ofstream f(pack, std::ios::binary | std::ios::trunc);
    f << raw;
  }
  const fs::path dest = tmp.path / "opened";
  std::string err;
  CHECK_FALSE(gspack::ExtractPack(pack, dest, nullptr, nullptr, &err));
  CHECK(err.find("damaged") != std::string::npos);
  CHECK_FALSE(fs::exists(dest));  // the folder we made is removed again, with the files already written

  // Into a folder that already existed (empty): it is emptied again, not deleted.
  fs::create_directories(dest);
  CHECK_FALSE(gspack::ExtractPack(pack, dest, nullptr, nullptr, &err));
  CHECK(fs::is_directory(dest));
  CHECK(DirIsEmptyOrMissing(dest));
}

TEST_CASE("req380 review fixes: reserved names, earlier packs, two project files, non-cloud files kept",
          "[req380]") {
  std::string err;
  {  // Windows device names would open a device, not create a file
    TempDir tmp("reserved");
    MakeZip(tmp.path / "a.gspack", {{"gspack.json", ManifestFor("id-1")},
                                    {"Hostile.gsproj", MarkerFor("id-1")},
                                    {"Drawings/NUL.dwg", "x"}});
    CHECK_FALSE(gspack::ExtractPack(tmp.path / "a.gspack", tmp.path / "o", nullptr, nullptr, &err));
    CHECK(err.find("unsafe") != std::string::npos);
    CHECK(DirIsEmptyOrMissing(tmp.path / "o"));
  }
  {  // wrong JSON types in a hostile manifest are a clean rejection, not a throw
    TempDir tmp("types");
    MakeZip(tmp.path / "a.gspack", {{"gspack.json", R"({"formatVersion":1,"projectId":5,"projectName":[]})"},
                                    {"Hostile.gsproj", MarkerFor("id-1")}});
    CHECK_FALSE(gspack::ExtractPack(tmp.path / "a.gspack", tmp.path / "o", nullptr, nullptr, &err));
    CHECK(DirIsEmptyOrMissing(tmp.path / "o"));
  }
  {  // an earlier pack in the folder is not packed again; a second project file blocks packing
    TempDir tmp("plan2");
    gsproj::Project p = MakeProject(tmp.path / "src");
    WriteText(p.Folder() / "old.gspack", "earlier pack");
    WriteText(p.Folder() / "PointClouds" / "README.txt", "keep me");
    gspack::PackPlan plan;
    REQUIRE(gspack::PlanPack(p, fs::path(), &plan, &err));
    for (const auto& f : plan.files) {
      CHECK(f.rel != "old.gspack");
      if (f.rel == "PointClouds/README.txt")
        CHECK_FALSE(f.pointCloud);  // not a cloud just because of the folder it is in
    }
    WriteText(p.Folder() / "Copy.gsproj", "{}");
    CHECK_FALSE(gspack::PlanPack(p, fs::path(), &plan, &err));
    CHECK(err.find("exactly one project file") != std::string::npos);
  }
  {  // re-packing an opened pack: the left-out list follows the new pack, and clears when nothing is left out
    TempDir tmp("reomit");
    const gsproj::Project p = MakeProject(tmp.path / "src");
    REQUIRE(PackTo(p, tmp.path / "a.gspack", true, &err));
    fs::path marker;
    REQUIRE(gspack::ExtractPack(tmp.path / "a.gspack", tmp.path / "o1", &marker, nullptr, &err));
    gsproj::Project opened;
    REQUIRE(gsproj::Load(marker, &opened, &err));
    CHECK_FALSE(projfiles::PackOmitted(opened).empty());
    // The cloud comes back, and the project is packed in full.
    WriteText(opened.Folder() / "PointClouds" / "site.e57", "back");
    REQUIRE(PackTo(opened, tmp.path / "b.gspack", false, &err));
    REQUIRE(gspack::ExtractPack(tmp.path / "b.gspack", tmp.path / "o2", &marker, nullptr, &err));
    gsproj::Project again;
    REQUIRE(gsproj::Load(marker, &again, &err));
    CHECK(projfiles::PackOmitted(again).empty());
  }
}

TEST_CASE("req380 a pack that cannot be written leaves no half file", "[req380]") {
  TempDir tmp("nowrite");
  const gsproj::Project p = MakeProject(tmp.path / "src");
  std::string err;
  // The target's folder does not exist: nothing to create the file in.
  CHECK_FALSE(PackTo(p, tmp.path / "no-such-folder" / "Job.gspack", false, &err));
  CHECK_FALSE(err.empty());
  CHECK_FALSE(fs::exists(tmp.path / "no-such-folder"));
}
