#include "ProjectFiles.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace projfiles {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

bool Fail(std::string* err, const std::string& msg) {
  if (err)
    *err = msg;
  return false;
}

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string FolderOf(const gsproj::Project& p, Role r) {
  const auto it = p.layout.find(LayoutRole(r));
  if (it != p.layout.end())
    return it->second;
  const auto std_ = gsproj::StandardLayout();
  const auto d = std_.find(LayoutRole(r));
  return d != std_.end() ? d->second : std::string();
}

/// The part after the last '/' or '\' — a stored path may come from another machine.
std::string FileNameOf(const std::string& path) {
  const size_t cut = path.find_last_of("/\\");
  return cut == std::string::npos ? path : path.substr(cut + 1);
}

fs::path Canon(const fs::path& p) {
  std::error_code ec;
  const fs::path c = fs::weakly_canonical(p, ec);
  return ec ? p.lexically_normal() : c;
}

gsproj::TrackedItem* FindByPath(gsproj::Project* p, const std::string& path) {
  for (gsproj::TrackedItem& it : p->items)
    if (it.path == path)
      return &it;
  return nullptr;
}

bool HasAssociation(const gsproj::TrackedItem& it, const std::string& drawingRel) {
  return std::find(it.associations.begin(), it.associations.end(), drawingRel) != it.associations.end();
}

/// The item a file maps to: in-project path when inside the folder; else the in-project item of the
/// same name already tied to this drawing (a link that was copied in); else a "local-link". Adds the
/// item when new. Never returns null.
gsproj::TrackedItem* EnsureItem(gsproj::Project* p, const std::string& drawingRel, const fs::path& file) {
  if (IsInsideProject(*p, file)) {
    const std::string rel = Canon(file).lexically_relative(Canon(p->Folder())).generic_u8string();
    if (gsproj::TrackedItem* t = FindByPath(p, rel))
      return t;
    gsproj::TrackedItem t;
    t.path = rel;
    t.kind = gsproj::kKindInProject;
    p->items.push_back(std::move(t));
    return &p->items.back();
  }
  const std::string name = Lower(file.filename().u8string());
  for (gsproj::TrackedItem& it : p->items)
    if (it.kind == gsproj::kKindInProject && HasAssociation(it, drawingRel) &&
        Lower(FileNameOf(it.path)) == name) {
      // Same name is not enough: a different file that happens to share it stays its own link.
      std::error_code ec;
      const auto inSize = fs::file_size(fs::u8path(ResolveItem(*p, it)), ec);
      const auto outSize = fs::file_size(file, ec);
      if (!ec && inSize == outSize)
        return &it;
    }
  const std::string abs = Canon(file).u8string();
  if (gsproj::TrackedItem* t = FindByPath(p, abs))
    return t;
  gsproj::TrackedItem t;
  t.path = abs;
  t.kind = gsproj::kKindLocalLink;
  p->items.push_back(std::move(t));
  return &p->items.back();
}

json ParsePlacements(const gsproj::TrackedItem& it) {
  json j = json::parse(it.placementsJson, nullptr, false);
  return j.is_array() ? j : json::array();
}

void DropPlacementsFor(gsproj::TrackedItem* it, const std::string& drawingRel) {
  json now = json::array();
  for (const json& e : ParsePlacements(*it))
    if (!(e.is_object() && e.value("drawing", std::string()) == drawingRel))
      now.push_back(e);
  it->placementsJson = now.dump();
}

}  // namespace

const char* LayoutRole(Role r) {
  switch (r) {
    case Role::PointCloud: return "pointClouds";
    case Role::Pdf:        return "pdfs";
    case Role::PointFile:  return "points";
  }
  return "points";
}

Role RoleForFile(const fs::path& file) {
  const std::string e = Lower(file.extension().string());
  if (e == ".e57" || e == ".gscloud")
    return Role::PointCloud;
  if (e == ".pdf")
    return Role::Pdf;
  return Role::PointFile;
}

bool IsInsideProject(const gsproj::Project& p, const fs::path& file) {
  const fs::path root = Canon(p.Folder());
  const fs::path f = Canon(file);
  const fs::path rel = f.lexically_relative(root);
  return !rel.empty() && !rel.is_absolute() && *rel.begin() != fs::path("..");
}

bool PlanAttach(const gsproj::Project& p, const fs::path& source, Role role, AttachPlan* out, std::string* err) {
  std::error_code ec;
  if (!fs::is_regular_file(source, ec))
    return Fail(err, "The file '" + source.u8string() + "' could not be found.");
  AttachPlan plan;
  plan.source = source;
  plan.sizeBytes = fs::file_size(source, ec);
  if (ec)
    return Fail(err, "The size of '" + source.u8string() + "' could not be read: " + ec.message());
  if (IsInsideProject(p, source)) {
    plan.alreadyInProject = true;
    plan.dest = source;
    plan.destRel = Canon(source).lexically_relative(Canon(p.Folder())).generic_u8string();
    *out = std::move(plan);
    return true;
  }
  const fs::path folder = p.Folder() / fs::u8path(FolderOf(p, role));
  const std::string stem = source.stem().u8string();
  const std::string ext = source.extension().u8string();
  const auto srcTime = fs::last_write_time(source, ec);
  fs::path dest = folder / source.filename();
  for (int n = 2; fs::exists(dest, ec) && n < 10000; ++n) {
    // The same file copied before: reuse it rather than pile up duplicates.
    if (fs::file_size(dest, ec) == plan.sizeBytes && fs::last_write_time(dest, ec) == srcTime) {
      plan.reuseExisting = true;
      break;
    }
    dest = folder / fs::u8path(stem + " (" + std::to_string(n) + ")" + ext);
  }
  if (!plan.reuseExisting && fs::exists(dest, ec))
    return Fail(err, "No free file name for the copy in " + folder.u8string() + ".");
  plan.dest = dest;
  plan.destRel = dest.lexically_relative(p.Folder()).generic_u8string();
  *out = std::move(plan);
  return true;
}

bool CopyIn(const AttachPlan& plan, Role role, std::string* err) {
  if (plan.alreadyInProject || plan.reuseExisting)
    return true;
  std::error_code ec;
  fs::create_directories(plan.dest.parent_path(), ec);
  auto copyOne = [&](const fs::path& from, const fs::path& to) {
    std::error_code e;
    fs::copy_file(from, to, fs::copy_options::none, e);
    if (e) {
      std::error_code rm;
      fs::remove(to, rm);
      return Fail(err, "Could not copy '" + from.u8string() + "' into the project: " + e.message());
    }
    const auto t = fs::last_write_time(from, e);
    if (!e)
      fs::last_write_time(to, t, e);  // the cache is matched to its source by size and time
    return true;
  };
  if (!copyOne(plan.source, plan.dest))
    return false;
  if (role == Role::PointCloud) {
    const fs::path cache = fs::u8path(plan.source.u8string() + ".gscloud");
    if (fs::is_regular_file(cache, ec))
      copyOne(cache, fs::u8path(plan.dest.u8string() + ".gscloud"));  // a missing cache is rebuilt, not an error
  }
  return true;
}

bool SyncDrawing(gsproj::Project* p, const std::string& drawingRel, const std::vector<Attached>& clouds,
                 const std::vector<Attached>& pdfs) {
  const std::string before = [&] {
    json j = json::array();
    for (const gsproj::TrackedItem& t : p->items)
      j.push_back({{"p", t.path}, {"k", t.kind}, {"a", t.associations}, {"l", t.placementsJson}});
    return j.dump();
  }();

  // The drawing itself.
  if (!FindByPath(p, drawingRel)) {
    gsproj::TrackedItem t;
    t.path = drawingRel;
    p->items.push_back(std::move(t));
  }

  std::vector<std::string> held;  // item paths the drawing holds now
  auto hold = [&](gsproj::TrackedItem* it) {
    held.push_back(it->path);
    if (!HasAssociation(*it, drawingRel))
      it->associations.push_back(drawingRel);
    return it;
  };
  for (const Attached& a : clouds)
    hold(EnsureItem(p, drawingRel, a.file));
  // Placements are rebuilt for the PDFs the drawing holds: drop this drawing's old ones first.
  for (const Attached& a : pdfs) {
    gsproj::TrackedItem* it = EnsureItem(p, drawingRel, a.file);
    if (std::find(held.begin(), held.end(), it->path) == held.end())
      DropPlacementsFor(it, drawingRel);
    hold(it);
  }
  for (const Attached& a : pdfs) {
    gsproj::TrackedItem* it = EnsureItem(p, drawingRel, a.file);
    json placement = json::parse(a.placementJson, nullptr, false);
    if (!placement.is_object())
      continue;
    placement["drawing"] = drawingRel;
    json arr = ParsePlacements(*it);
    arr.push_back(std::move(placement));
    it->placementsJson = arr.dump();
  }

  // A file the drawing no longer holds loses its tie to it (the file stays tracked).
  for (gsproj::TrackedItem& it : p->items) {
    if (it.path == drawingRel || std::find(held.begin(), held.end(), it.path) != held.end())
      continue;
    const auto a = std::find(it.associations.begin(), it.associations.end(), drawingRel);
    if (a != it.associations.end())
      it.associations.erase(a);
    DropPlacementsFor(&it, drawingRel);
  }

  json after = json::array();
  for (const gsproj::TrackedItem& t : p->items)
    after.push_back({{"p", t.path}, {"k", t.kind}, {"a", t.associations}, {"l", t.placementsJson}});
  return after.dump() != before;
}

std::string ResolveItem(const gsproj::Project& p, const gsproj::TrackedItem& item) {
  if (item.kind == gsproj::kKindInProject)
    return (p.Folder() / fs::u8path(item.path)).u8string();
  if (item.kind == gsproj::kKindLocalLink)
    return item.path;
  return {};
}

std::string FindAttachedFile(const gsproj::Project& p, const std::string& drawingRel, const std::string& stored) {
  std::error_code ec;
  const std::string name = Lower(FileNameOf(stored));
  for (const gsproj::TrackedItem& it : p.items) {
    if (it.path == drawingRel || !HasAssociation(it, drawingRel) || Lower(FileNameOf(it.path)) != name)
      continue;
    const std::string abs = ResolveItem(p, it);
    if (!abs.empty() && fs::exists(fs::u8path(abs), ec))
      return abs;
  }
  return !stored.empty() && fs::exists(fs::u8path(stored), ec) ? stored : std::string();
}

std::vector<std::pair<std::string, std::string>> PlacementsFor(const gsproj::Project& p,
                                                              const std::string& drawingRel) {
  std::vector<std::pair<std::string, std::string>> out;
  for (const gsproj::TrackedItem& it : p.items)
    for (const json& e : ParsePlacements(it))
      if (e.is_object() && e.value("drawing", std::string()) == drawingRel)
        out.emplace_back(ResolveItem(p, it), e.dump());
  return out;
}

Health CheckHealth(const gsproj::Project& p, const std::vector<std::string>& unsavedDrawings) {
  Health h;
  std::error_code ec;
  for (const gsproj::TrackedItem& it : p.items) {
    const std::string abs = ResolveItem(p, it);
    if (abs.empty()) {
      h.unavailable.push_back(it.path);
      continue;
    }
    if (it.kind == gsproj::kKindLocalLink)
      h.linked.push_back(it.path);
    if (!fs::exists(fs::u8path(abs), ec))
      h.missing.push_back(it.path);
  }
  h.unsaved = unsavedDrawings;
  return h;
}

CopyLinksResult CopyLinksIn(gsproj::Project* p) {
  CopyLinksResult r;
  for (gsproj::TrackedItem& it : p->items) {
    if (it.kind != gsproj::kKindLocalLink)
      continue;
    const fs::path src = fs::u8path(it.path);
    const Role role = RoleForFile(src);
    AttachPlan plan;
    std::string err;
    if (!PlanAttach(*p, src, role, &plan, &err) || !CopyIn(plan, role, &err)) {
      r.failed.push_back(it.path + ": " + err);
      continue;
    }
    // The copy may already be tracked (an identical earlier copy was reused): fold into that item.
    gsproj::TrackedItem* same = FindByPath(p, plan.destRel);
    if (same != nullptr && same != &it) {
      for (const std::string& a : it.associations)
        if (!HasAssociation(*same, a))
          same->associations.push_back(a);
      json merged = ParsePlacements(*same);
      for (const json& e : ParsePlacements(it))
        merged.push_back(e);
      same->placementsJson = merged.dump();
      it.kind = "";  // marked for removal below
    } else {
      it.path = plan.destRel;
      it.kind = gsproj::kKindInProject;
    }
    ++r.converted;
  }
  p->items.erase(std::remove_if(p->items.begin(), p->items.end(),
                                [](const gsproj::TrackedItem& t) { return t.kind.empty(); }),
                 p->items.end());
  return r;
}

}  // namespace projfiles
