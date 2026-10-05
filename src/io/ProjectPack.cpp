#include "ProjectPack.hpp"

#include "ProjectFiles.hpp"

#include <miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace gspack {

namespace {

bool Fail(std::string* err, const std::string& msg) {
  if (err)
    *err = msg;
  return false;
}

FILE* OpenFile(const fs::path& p, const wchar_t* mode) {
#ifdef _WIN32
  return _wfopen(p.c_str(), mode);
#else
  std::string m;
  for (const wchar_t* c = mode; *c; ++c)
    m += static_cast<char>(*c);
  return std::fopen(p.c_str(), m.c_str());
#endif
}

struct FileCloser {
  FILE* f = nullptr;
  ~FileCloser() {
    if (f)
      std::fclose(f);
  }
  FileCloser() = default;
  explicit FileCloser(FILE* file) : f(file) {}
  FileCloser(const FileCloser&) = delete;
  FileCloser& operator=(const FileCloser&) = delete;
  int Close() {
    const int r = f ? std::fclose(f) : 0;
    f = nullptr;
    return r;
  }
};

std::string Lower(std::string s) {
  for (char& c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool EndsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && Lower(s.substr(s.size() - suffix.size())) == suffix;
}

/// REQ-380 clause 1: never packed — the lock file (REQ-382) and temporary files.
bool NeverPacked(const std::string& name) {
  return EndsWith(name, ".gsproj.lock") || EndsWith(name, ".tmp");
}

bool IsPointCloudFile(const gsproj::Project& p, const std::string& rel) {
  if (projfiles::RoleForFile(fs::u8path(rel)) == projfiles::Role::PointCloud)
    return true;
  const auto it = p.layout.find(projfiles::LayoutRole(projfiles::Role::PointCloud));
  if (it == p.layout.end() || it->second.empty())
    return false;
  const std::string dir = Lower(it->second) + "/";
  return Lower(rel).compare(0, dir.size(), dir) == 0;
}

/// A name that may be extracted: relative, inside the folder, forward slashes, no empty / "." / ".."
/// segment and no segment Windows would quietly rewrite (trailing dot or space).
bool SafeEntryName(const std::string& name) {
  if (!gsproj::IsSafeRelativePath(name) || name.find('\\') != std::string::npos)
    return false;
  std::string seg;
  for (size_t i = 0; i <= name.size(); ++i) {
    if (i == name.size() || name[i] == '/') {
      if (seg.empty() || seg == "." || seg == ".." || seg.back() == '.' || seg.back() == ' ')
        return false;
      seg.clear();
    } else {
      seg += name[i];
    }
  }
  return true;
}

std::int64_t TicksOf(const fs::path& p) {
  std::error_code ec;
  const auto t = fs::last_write_time(p, ec);
  return ec ? 0 : static_cast<std::int64_t>(t.time_since_epoch().count());
}

}  // namespace

bool PlanPack(const gsproj::Project& p, const fs::path& skip, PackPlan* out, std::string* err) {
  PackPlan plan;
  std::error_code ec;
  const fs::path root = p.Folder();
  if (!fs::is_regular_file(p.file, ec))
    return Fail(err, "The project file is missing, so there is nothing to pack. Save the project first.");
  const fs::path skipAbs = skip.empty() ? fs::path() : fs::weakly_canonical(skip, ec);
  for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    std::error_code fe;
    if (!it->is_regular_file(fe))
      continue;
    const fs::path file = it->path();
    if (NeverPacked(file.filename().u8string()))
      continue;
    if (!skipAbs.empty() && fs::weakly_canonical(file, fe) == skipAbs)
      continue;
    PackFile f;
    f.abs = file;
    f.rel = file.lexically_relative(root).generic_u8string();
    f.sizeBytes = it->file_size(fe);
    if (fe)
      return Fail(err, "A project file could not be read: " + f.rel);
    f.mtimeTicks = TicksOf(file);
    f.pointCloud = IsPointCloudFile(p, f.rel);
    plan.totalBytes += f.sizeBytes;
    if (f.pointCloud)
      plan.pointCloudBytes += f.sizeBytes;
    plan.files.push_back(std::move(f));
  }
  if (ec)
    return Fail(err, "The project folder could not be listed: " + ec.message());
  std::sort(plan.files.begin(), plan.files.end(), [](const PackFile& a, const PackFile& b) { return a.rel < b.rel; });
  if (out)
    *out = std::move(plan);
  return true;
}

bool WritePack(const gsproj::Project& p, const PackPlan& plan, const PackOptions& opt, const fs::path& out,
               std::string* err) {
  Manifest m;
  m.projectId = p.id;
  m.projectName = p.name;
  m.packedUnix = opt.nowUnix;
  std::vector<const PackFile*> keep;
  for (const PackFile& f : plan.files) {
    if (opt.excludePointClouds && f.pointCloud)
      m.excluded.push_back(f.rel);
    else
      keep.push_back(&f);
  }

  json mj;
  mj["formatVersion"] = m.formatVersion;
  mj["projectId"] = m.projectId;
  mj["projectName"] = m.projectName;
  mj["packedUnix"] = m.packedUnix;
  mj["excluded"] = m.excluded;
  json files = json::array();
  for (const PackFile* f : keep)
    files.push_back({{"path", f->rel}, {"size", f->sizeBytes}, {"mtimeTicks", f->mtimeTicks}});
  mj["files"] = std::move(files);
  const std::string manifestText = mj.dump(2);

  const fs::path tmp = out.parent_path() / fs::u8path(out.filename().u8string() + ".tmp");
  std::error_code ec;
  auto fail = [&](const std::string& msg) {
    std::error_code e2;
    fs::remove(tmp, e2);
    return Fail(err, msg);
  };

  FileCloser fc(OpenFile(tmp, L"wb"));
  if (!fc.f)
    return Fail(err, "The pack file could not be created: " + out.u8string());
  mz_zip_archive zip{};
  if (!mz_zip_writer_init_cfile(&zip, fc.f, 0))
    return fail("The pack file could not be started.");
  bool ok = mz_zip_writer_add_mem(&zip, kManifestName, manifestText.data(), manifestText.size(), MZ_DEFAULT_LEVEL) != 0;
  std::string failedName = kManifestName;
  for (size_t i = 0; ok && i < keep.size(); ++i) {
    const PackFile& f = *keep[i];
    FileCloser src(OpenFile(f.abs, L"rb"));
    if (!src.f) {
      ok = false;
      failedName = f.rel + " (cannot be read)";
      break;
    }
    // A point cloud and a PDF are already dense; the fast level keeps packing a big scan quick.
    const mz_uint level = (f.pointCloud || EndsWith(f.rel, ".pdf")) ? MZ_BEST_SPEED : MZ_DEFAULT_LEVEL;
    ok = mz_zip_writer_add_cfile(&zip, f.rel.c_str(), src.f, f.sizeBytes, nullptr, nullptr, 0, level, nullptr, 0,
                                 nullptr, 0) != 0;
    failedName = f.rel;
  }
  if (ok)
    ok = mz_zip_writer_finalize_archive(&zip) != 0;
  mz_zip_writer_end(&zip);
  if (!ok)
    return fail("The pack could not be written (" + failedName + "). Is the disk full?");
  if (fc.Close() != 0)
    return fail("The pack could not be written. Is the disk full?");
  fs::rename(tmp, out, ec);
  if (ec)
    return fail("The pack file could not be moved into place: " + ec.message());
  return true;
}

namespace {

struct ZipReader {
  mz_zip_archive zip{};
  FileCloser     file;
  bool           open = false;
  ~ZipReader() {
    if (open)
      mz_zip_reader_end(&zip);
  }
  bool Open(const fs::path& path) {
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec)
      return false;
    file.f = OpenFile(path, L"rb");
    if (!file.f)
      return false;
    open = mz_zip_reader_init_cfile(&zip, file.f, size, 0) != 0;
    return open;
  }
};

struct EntryInfo {
  mz_uint       index = 0;
  std::string   name;
  bool          isDir = false;
  std::uint64_t size = 0;
};

bool ReadText(ZipReader& z, mz_uint index, std::string* text) {
  size_t n = 0;
  void* p = mz_zip_reader_extract_to_heap(&z.zip, index, &n, 0);
  if (!p)
    return false;
  text->assign(static_cast<const char*>(p), n);
  mz_free(p);
  return true;
}

/// Removes what a failed extraction wrote. \p dest was empty (or new) when we started, so everything
/// in it is ours.
void CleanDest(const fs::path& dest, bool created) {
  std::error_code ec;
  if (created) {
    fs::remove_all(dest, ec);
    return;
  }
  for (fs::directory_iterator it(dest, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    fs::remove_all(it->path(), e2);
  }
}

}  // namespace

bool ExtractPack(const fs::path& pack, const fs::path& destDir, fs::path* gsprojOut, Manifest* manifestOut,
                 std::string* err) {
  const std::string packName = pack.filename().u8string();
  ZipReader z;
  if (!z.Open(pack))
    return Fail(err, "'" + packName + "' is not a readable GoSurvey pack.");

  // --- 1. Check every entry before anything is written (clause 4) ---------------------------------
  std::vector<EntryInfo> entries;
  std::set<std::string> seen;
  int manifestIdx = -1;
  std::vector<size_t> markers;  // indices into entries
  const mz_uint count = mz_zip_reader_get_num_files(&z.zip);
  for (mz_uint i = 0; i < count; ++i) {
    mz_zip_archive_file_stat st{};
    if (!mz_zip_reader_file_stat(&z.zip, i, &st))
      return Fail(err, "'" + packName + "' is damaged (an entry cannot be read).");
    EntryInfo e;
    e.index = i;
    e.name = st.m_filename;
    e.isDir = mz_zip_reader_is_file_a_directory(&z.zip, i) != 0;
    e.size = st.m_uncomp_size;
    std::string checkName = e.name;
    if (e.isDir && !checkName.empty() && checkName.back() == '/')
      checkName.pop_back();
    if (!SafeEntryName(checkName))
      return Fail(err, "'" + packName + "' was rejected: it holds an unsafe path ('" + e.name + "'). Nothing was extracted.");
    if (!seen.insert(Lower(checkName)).second)
      return Fail(err, "'" + packName + "' was rejected: '" + e.name + "' appears twice. Nothing was extracted.");
    if (!e.isDir && e.name == kManifestName)
      manifestIdx = static_cast<int>(entries.size());
    else if (!e.isDir && e.name.find('/') == std::string::npos && EndsWith(e.name, gsproj::kExtension))
      markers.push_back(entries.size());
    entries.push_back(std::move(e));
  }
  if (manifestIdx < 0)
    return Fail(err, "'" + packName + "' has no manifest, so it is not a GoSurvey pack. Nothing was extracted.");
  if (markers.size() != 1)
    return Fail(err, "'" + packName + "' must hold exactly one project file (.gsproj). Nothing was extracted.");

  std::string text;
  if (!ReadText(z, entries[static_cast<size_t>(manifestIdx)].index, &text))
    return Fail(err, "'" + packName + "' is damaged (the manifest failed its checksum). Nothing was extracted.");
  const json mj = json::parse(text, nullptr, false);
  if (!mj.is_object() || !mj.value("projectId", std::string()).size() || !mj.contains("formatVersion") ||
      !mj["formatVersion"].is_number_integer())
    return Fail(err, "'" + packName + "' has an unreadable manifest. Nothing was extracted.");
  Manifest m;
  m.formatVersion = mj["formatVersion"].get<int>();
  if (m.formatVersion > kFormatVersion)
    return Fail(err, "'" + packName + "' was made by a newer GoSurvey (pack format " + std::to_string(m.formatVersion) +
                         "). Update GoSurvey to open it.");
  m.projectId = mj.value("projectId", std::string());
  m.projectName = mj.value("projectName", std::string());
  m.packedUnix = mj.value("packedUnix", std::int64_t{0});
  if (mj.contains("excluded") && mj["excluded"].is_array())
    for (const auto& x : mj["excluded"])
      if (x.is_string() && SafeEntryName(x.get<std::string>()))
        m.excluded.push_back(x.get<std::string>());

  const EntryInfo& marker = entries[markers[0]];
  if (!ReadText(z, marker.index, &text))
    return Fail(err, "'" + packName + "' is damaged (the project file failed its checksum). Nothing was extracted.");
  const json pj = json::parse(text, nullptr, false);
  if (!pj.is_object() || pj.value("id", std::string()) != m.projectId)
    return Fail(err, "'" + packName + "' was rejected: its project file does not match its manifest. Nothing was extracted.");

  std::map<std::string, std::int64_t> mtimes;
  if (mj.contains("files") && mj["files"].is_array())
    for (const auto& f : mj["files"])
      if (f.is_object() && f.contains("path") && f["path"].is_string() && f.contains("mtimeTicks") &&
          f["mtimeTicks"].is_number_integer())
        mtimes[f["path"].get<std::string>()] = f["mtimeTicks"].get<std::int64_t>();

  // --- 2. The destination: empty or new, with room for everything -----------------------------------
  std::error_code ec;
  bool created = false;
  if (fs::exists(destDir, ec)) {
    if (!fs::is_directory(destDir, ec))
      return Fail(err, "'" + destDir.u8string() + "' is not a folder.");
    if (fs::directory_iterator(destDir, ec) != fs::directory_iterator())
      return Fail(err, "The folder '" + destDir.u8string() + "' already holds files. Pick an empty folder; nothing was extracted.");
  }
  std::uintmax_t need = 0;
  for (const EntryInfo& e : entries)
    need += e.size;
  fs::path probe = fs::absolute(destDir, ec);
  while (!probe.empty() && !fs::exists(probe, ec) && probe.has_parent_path() && probe.parent_path() != probe)
    probe = probe.parent_path();
  const fs::space_info sp = fs::space(probe, ec);
  if (!ec && sp.available < need)
    return Fail(err, "There is not enough free space to open '" + packName + "' (it needs " +
                         std::to_string(need / (1024 * 1024)) + " MB). Nothing was extracted.");
  if (!fs::exists(destDir, ec)) {
    fs::create_directories(destDir, ec);
    if (ec)
      return Fail(err, "The folder '" + destDir.u8string() + "' could not be created: " + ec.message());
    created = true;
  }

  // --- 3. Extract (checksums are verified as each file is read) -------------------------------------
  for (const EntryInfo& e : entries) {
    if (&e == &entries[static_cast<size_t>(manifestIdx)])
      continue;
    std::string rel = e.name;
    if (e.isDir && !rel.empty() && rel.back() == '/')
      rel.pop_back();
    const fs::path target = destDir / fs::u8path(rel);
    std::error_code te;
    if (e.isDir) {
      fs::create_directories(target, te);
      if (te) {
        CleanDest(destDir, created);
        return Fail(err, "A folder from the pack could not be created ('" + rel + "'). Nothing was kept.");
      }
      continue;
    }
    fs::create_directories(target.parent_path(), te);
    bool ok = !te;
    if (ok) {
      FileCloser out(OpenFile(target, L"wb"));
      ok = out.f != nullptr && mz_zip_reader_extract_to_cfile(&z.zip, e.index, out.f, 0) != 0;
      ok = ok && out.Close() == 0;
    }
    if (!ok) {
      CleanDest(destDir, created);
      return Fail(err, "'" + packName + "' is damaged or could not be written ('" + rel + "'). Nothing was kept.");
    }
    const auto mt = mtimes.find(rel);
    if (mt != mtimes.end())
      fs::last_write_time(target, fs::file_time_type(fs::file_time_type::duration(mt->second)), te);
  }

  // --- 4. Open as a project; record what was left out ----------------------------------------------
  gsproj::Project proj;
  std::string perr;
  const fs::path markerPath = destDir / fs::u8path(marker.name);
  if (!gsproj::Load(markerPath, &proj, &perr) || proj.id != m.projectId) {
    CleanDest(destDir, created);
    return Fail(err, "'" + packName + "' holds a project file GoSurvey cannot read" +
                         (perr.empty() ? std::string(".") : ": " + perr) + " Nothing was kept.");
  }
  std::vector<std::string> omitted;
  for (const std::string& x : m.excluded)
    if (!EndsWith(x, ".gscloud"))  // the cache is derived; the cloud itself is what the user sees
      omitted.push_back(x);
  if (!omitted.empty()) {
    projfiles::SetPackOmitted(&proj, omitted);
    if (!gsproj::Save(proj, &perr)) {
      CleanDest(destDir, created);
      return Fail(err, "The opened project could not be saved: " + perr + " Nothing was kept.");
    }
  }
  if (gsprojOut)
    *gsprojOut = markerPath;
  if (manifestOut)
    *manifestOut = std::move(m);
  return true;
}

}  // namespace gspack
