#pragma once

// Online map tile fetch + disk cache (REQ-363 items 5-7, ADR-064 (c)).
//
// Owns the network and the disk for the online map so the UI thread never waits on either. Each tile
// is one §8 one-shot worker (spec/architecture.md §8): the request is copied in, the worker looks the
// tile up in the disk cache, otherwise fetches it, decodes it to RGBA, and release-stores `done`; the
// UI thread polls, joins and moves the result out. No mutex, no queue shared with a thread, no pool —
// at most \c maxInFlight workers exist at once, and the rest of the wanted tiles wait on the UI side.
// No GL here — the caller uploads textures.
//
// The fetch function is injected: the application passes a WinHTTP one (`HttpFetch`), tests pass a
// fake, so the failure handling is unit-tested with no network (ADR-064 (c)).

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

/// One tile of one map. \c map is the caller's map id (DrawingSettings::OnlineMap as int).
struct MapTileKey {
  int map = 0;
  int z = 0;
  int x = 0;
  int y = 0;
  bool operator==(const MapTileKey& o) const { return map == o.map && z == o.z && x == o.x && y == o.y; }
  bool operator<(const MapTileKey& o) const {
    if (map != o.map) return map < o.map;
    if (z != o.z) return z < o.z;
    if (x != o.x) return x < o.x;
    return y < o.y;
  }
};

enum class MapTileStatus {
  Ok,        ///< The tile's image.
  NotFound,  ///< The server has no tile there (HTTP 404: outside the map's coverage or levels).
  Failed,    ///< No answer: offline, timeout, TLS, another HTTP status, or not an image.
};

/// GETs \p url into \p body. On Failed, \p error says why. Called on a worker thread, so it must not
/// touch UI-thread state.
using MapTileFetch = std::function<MapTileStatus(const std::string& url, std::string& body, std::string& error)>;

struct MapTileRequest {
  MapTileKey  key;
  std::string url;
  /// Relative path of the tile in the disk cache, e.g. "USGSImageryOnly/16/15000/26000".
  std::string cachePath;
  /// When set, the image is already in hand (a tile captured into the drawing, REQ-364): the worker
  /// only decodes it — no disk cache, no network.
  std::shared_ptr<const std::string> image;
};

struct MapTileResult {
  MapTileKey    key;
  MapTileStatus status = MapTileStatus::Failed;
  std::string   error;               ///< Why, when Failed.
  bool          fromNetwork = false;  ///< Fetched now, rather than read from the disk cache.
  /// The image file exactly as served (JPEG / PNG), when Ok — what Capture Area keeps (REQ-364).
  /// Immutable, so sharing it across threads and undo snapshots is allowed (§8, ADR-064 (e)).
  std::shared_ptr<const std::string> bytes;
  std::vector<unsigned char> rgba;  ///< Decoded, 4 bytes per pixel, top (north) row first.
  int width = 0;
  int height = 0;
};

class MapTileService {
 public:
  /// \p cacheDir empty = no disk cache. The cache is pruned to \p cacheMaxBytes (least recently used
  /// first) by a one-shot worker when the service starts (REQ-363 item 6).
  MapTileService(std::filesystem::path cacheDir, MapTileFetch fetch, int maxInFlight = 2,
                 std::uint64_t cacheMaxBytes = 500ull * 1024 * 1024);
  /// Joins every worker (a fetch in flight finishes first: its own timeout bounds the wait).
  ~MapTileService();
  MapTileService(const MapTileService&) = delete;
  MapTileService& operator=(const MapTileService&) = delete;

  /// Replaces every request not yet started with \p wanted, in order (the current view wins over a
  /// view already panned away from), and starts workers for them. A tile already being fetched is
  /// not requested twice.
  void SetWanted(std::vector<MapTileRequest> wanted);
  /// Up to \p max finished results, oldest first; starts the next waiting requests.
  [[nodiscard]] std::vector<MapTileResult> TakeResults(size_t max);
  /// Nothing waiting, nothing in flight, no result uncollected.
  [[nodiscard]] bool Idle() const;
  /// How many tiles have come back from the fetch function (tests: "Map Off makes no request").
  [[nodiscard]] std::uint64_t FetchCount() const { return fetchCount_; }

  /// Deletes the least recently used files under \p dir until it holds at most 90% of \p maxBytes,
  /// when it holds more than \p maxBytes. Errors are ignored: a cache is allowed to be imperfect.
  static void PruneDiskCache(const std::filesystem::path& dir, std::uint64_t maxBytes);

  /// Decodes a JPEG / PNG into top-row-first RGBA. False when it is not an image.
  static bool DecodeImage(const std::string& bytes, std::vector<unsigned char>& rgba, int& width, int& height);

 private:
  /// One §8 worker: the copied request, the result, and the done flag the worker release-stores last.
  struct Task {
    MapTileRequest    request;
    MapTileResult     result;
    std::atomic<bool> done{false};
    std::thread       thread;
  };

  void Pump();  ///< Collects finished workers into results_, then starts waiting requests.

  std::filesystem::path              cacheDir_;
  MapTileFetch                       fetch_;
  int                                maxInFlight_;
  std::vector<MapTileRequest>        waiting_;
  std::vector<std::unique_ptr<Task>> inFlight_;
  std::vector<MapTileResult>         results_;
  std::uint64_t                      fetchCount_ = 0;
  std::unique_ptr<Task>              prune_;  ///< The start-up cache prune (no request, no result).
};
