// Online map tile fetch + disk cache (REQ-363, ADR-064 (c)). §8 one-shot workers; see the header.

#include "MapTileService.hpp"

#include <stb_image.h>

#include <algorithm>
#include <climits>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace {

bool ReadFile(const fs::path& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f)
    return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return !f.bad();
}

/// Written beside the target and renamed over it, so a crash never leaves half a tile to be read
/// back as a whole one. A failed write only means the tile is fetched again next time.
void WriteFileAtomically(const fs::path& p, const std::string& bytes) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  fs::path tmp = p;
  tmp += ".part";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f)
      return;
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!f)
      return;
  }
  fs::rename(tmp, p, ec);
  if (ec)
    fs::remove(tmp, ec);
}

/// The worker's whole job, on copies only: disk cache, else fetch; decode; cache what was fetched.
MapTileResult FetchTile(const MapTileRequest& req, const fs::path& cacheDir, const MapTileFetch& fetch) {
  MapTileResult r;
  r.key = req.key;
  const fs::path cached = cacheDir.empty() ? fs::path() : cacheDir / fs::u8path(req.cachePath);
  std::string bytes;
  if (!cached.empty() && ReadFile(cached, bytes) && MapTileService::DecodeImage(bytes, r.rgba, r.width, r.height)) {
    std::error_code ec;
    fs::last_write_time(cached, fs::file_time_type::clock::now(), ec);  // least-recently-used order
    r.status = MapTileStatus::Ok;
    r.bytes = std::make_shared<const std::string>(std::move(bytes));
    return r;
  }
  bytes.clear();
  std::string error;
  r.fromNetwork = true;
  r.status = fetch ? fetch(req.url, bytes, error) : MapTileStatus::Failed;
  if (r.status == MapTileStatus::Failed) {
    r.error = error.empty() ? "no response" : error;
    return r;
  }
  if (r.status == MapTileStatus::NotFound)
    return r;
  if (!MapTileService::DecodeImage(bytes, r.rgba, r.width, r.height)) {
    r.status = MapTileStatus::Failed;
    r.error = "the server's answer is not an image";
    return r;
  }
  if (!cached.empty())
    WriteFileAtomically(cached, bytes);
  r.bytes = std::make_shared<const std::string>(std::move(bytes));
  return r;
}

}  // namespace

bool MapTileService::DecodeImage(const std::string& bytes, std::vector<unsigned char>& rgba, int& width,
                                 int& height) {
  if (bytes.empty() || bytes.size() > static_cast<size_t>(INT_MAX))
    return false;
  int w = 0, h = 0, channels = 0;
  unsigned char* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                            static_cast<int>(bytes.size()), &w, &h, &channels, 4);
  if (!px)
    return false;
  rgba.assign(px, px + static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
  stbi_image_free(px);
  width = w;
  height = h;
  return w > 0 && h > 0;
}

MapTileService::MapTileService(fs::path cacheDir, MapTileFetch fetch, int maxInFlight, std::uint64_t cacheMaxBytes)
    : cacheDir_(std::move(cacheDir)), fetch_(std::move(fetch)), maxInFlight_(std::max(1, maxInFlight)) {
  if (cacheDir_.empty())
    return;
  // A prune racing a worker's read can only turn that read into a fetch.
  prune_ = std::make_unique<Task>();
  Task* t = prune_.get();
  t->thread = std::thread([t, dir = cacheDir_, cacheMaxBytes] {
    PruneDiskCache(dir, cacheMaxBytes);
    t->done.store(true, std::memory_order_release);
  });
}

MapTileService::~MapTileService() {
  for (std::unique_ptr<Task>& t : inFlight_)
    if (t->thread.joinable())
      t->thread.join();
  if (prune_ && prune_->thread.joinable())
    prune_->thread.join();
}

void MapTileService::Pump() {
  for (auto it = inFlight_.begin(); it != inFlight_.end();) {
    Task& t = **it;
    if (!t.done.load(std::memory_order_acquire)) {
      ++it;
      continue;
    }
    t.thread.join();
    if (t.result.fromNetwork)
      ++fetchCount_;
    results_.push_back(std::move(t.result));
    it = inFlight_.erase(it);
  }
  if (prune_ && prune_->done.load(std::memory_order_acquire)) {
    prune_->thread.join();
    prune_.reset();
  }
  size_t next = 0;
  while (static_cast<int>(inFlight_.size()) < maxInFlight_ && next < waiting_.size()) {
    auto task = std::make_unique<Task>();
    task->request = waiting_[next];
    Task* t = task.get();
    // Inputs copied into the worker (§8 rule 1); the Task outlives it (joined before it is freed).
    try {
      t->thread = std::thread([t, dir = cacheDir_, fetch = fetch_] {
        t->result = FetchTile(t->request, dir, fetch);
        t->done.store(true, std::memory_order_release);
      });
    } catch (const std::system_error&) {
      break;  // no thread to be had now (REQ-201: not a crash); the request waits for the next Pump
    }
    ++next;
    inFlight_.push_back(std::move(task));
  }
  waiting_.erase(waiting_.begin(), waiting_.begin() + static_cast<std::ptrdiff_t>(next));
}

void MapTileService::SetWanted(std::vector<MapTileRequest> wanted) {
  waiting_.clear();
  for (MapTileRequest& r : wanted) {
    const bool running = std::any_of(inFlight_.begin(), inFlight_.end(),
                                     [&](const std::unique_ptr<Task>& t) { return t->request.key == r.key; });
    if (!running)
      waiting_.push_back(std::move(r));
  }
  Pump();
}

std::vector<MapTileResult> MapTileService::TakeResults(size_t max) {
  Pump();
  std::vector<MapTileResult> out;
  const size_t n = std::min(max, results_.size());
  out.reserve(n);
  for (size_t i = 0; i < n; ++i)
    out.push_back(std::move(results_[i]));
  results_.erase(results_.begin(), results_.begin() + static_cast<std::ptrdiff_t>(n));
  return out;
}

bool MapTileService::Idle() const { return waiting_.empty() && inFlight_.empty() && results_.empty(); }

void MapTileService::PruneDiskCache(const fs::path& dir, std::uint64_t maxBytes) {
  struct Entry {
    fs::file_time_type time;
    std::uint64_t      size;
    fs::path           path;
  };
  std::vector<Entry> files;
  std::uint64_t total = 0;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (!it->is_regular_file(e2))
      continue;
    const std::uint64_t size = it->file_size(e2);
    const fs::file_time_type time = it->last_write_time(e2);
    if (e2)
      continue;
    files.push_back({time, size, it->path()});
    total += size;
  }
  if (total <= maxBytes)
    return;
  std::sort(files.begin(), files.end(), [](const Entry& a, const Entry& b) { return a.time < b.time; });
  const std::uint64_t target = maxBytes / 10 * 9;
  for (const Entry& f : files) {
    if (total <= target)
      break;
    std::error_code e3;
    if (fs::remove(f.path, e3))
      total -= f.size;
  }
}
