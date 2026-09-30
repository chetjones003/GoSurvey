#pragma once

// The online map controller (REQ-363, ADR-064): which tiles the view needs, asking the tile service
// for them, placing each arrived tile in the drawing, and the draw list the renderer draws under the
// geometry. GL-free — texture upload and release are injected — so it runs in tests with no window.
// UI thread only (CS-MAP, ADR-063 (c)).

#include "CadCommands.hpp"
#include "MapTileService.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

/// One tile to draw: a texture and its triangles, 4 doubles per vertex — x, y (LOCAL drawing
/// coordinates, on Z = 0) and u, v (v = 0 is the tile's north edge, its first image row).
struct MapTileDraw {
  unsigned int               texture = 0;
  const std::vector<double>* xyuv = nullptr;
};

/// The USGS tile URL of tile (z, x, y) of \p service (REQ-363 item 2).
[[nodiscard]] std::string UsgsTileUrl(const char* service, int z, int x, int y);

/// REQ-363 item 3: places tile (z, x, y) in the drawing — a grid of \p cells × \p cells cells whose
/// vertices go WGS 84 → \p frame → local — as triangles (see \ref MapTileDraw). False, with the
/// reason, when any vertex cannot be converted.
bool PlaceMapTile(const DrawingWgs84Frame& frame, int z, int x, int y, int cells, std::vector<double>& xyuv,
                  std::string* error);

/// REQ-363 item 7: each kind of problem is said once. Pure, so "exactly one message" is tested
/// directly. Every method returns the message to print, or "" when it was already said.
class OnlineMapMessageLatch {
 public:
  /// A tile arrived from the network: the next failure is news again.
  void OnFetched() { failureSaid_ = false; }
  [[nodiscard]] std::string OnFailed(const std::string& reason);
  [[nodiscard]] std::string OnNotFound();
  [[nodiscard]] std::string OnPlacementFailed(const std::string& reason);
  /// The map or the drawing's location changed: "no map here" and placement problems are news again.
  void OnLocationChanged() {
    notFoundSaid_ = false;
    placementSaid_ = false;
  }

 private:
  bool failureSaid_ = false;
  bool notFoundSaid_ = false;
  bool placementSaid_ = false;
};

/// Texture upload / release, supplied by the application (GL) or a test (fake ids).
struct OnlineMapTextureHooks {
  /// Uploads top-row-first RGBA; returns the texture, 0 on failure.
  std::function<unsigned int(const std::vector<unsigned char>& rgba, int width, int height)> upload;
  std::function<void(unsigned int texture)> release;
};

class OnlineMapController {
 public:
  static constexpr int    kMaxLevel = 16;       ///< USGS tiles stop at level 16 (REQ-363 item 2).
  static constexpr int    kMaxTiles = 64;       ///< Most tiles drawn for one view.
  static constexpr int    kCellsPerSide = 4;    ///< Placement grid per tile (ADR-064 (b)).
  static constexpr int    kWorkPerFrame = 4;    ///< Uploads, and separately placements, per frame.
  static constexpr size_t kMaxTextures = 256;   ///< Least-recently-used texture budget (~64 MB).

  OnlineMapController(std::unique_ptr<MapTileService> service, OnlineMapTextureHooks hooks,
                      std::chrono::milliseconds retryAfterFailure = std::chrono::seconds(30));
  ~OnlineMapController();
  OnlineMapController(const OnlineMapController&) = delete;
  OnlineMapController& operator=(const OnlineMapController&) = delete;

  /// Once per frame. \p modelView is false for paper space and the Start tab (item 4): nothing is
  /// drawn or requested then, as with Map Off or a drawing that is not geolocated (item 8).
  void Update(const AppCommandState& st, bool modelView, const Camera& cam, int fbWidth, int fbHeight,
              std::vector<std::string>& log);

  /// What to draw this frame, coarsest first. Pointers stay valid until the next Update.
  [[nodiscard]] const std::vector<MapTileDraw>& DrawList() const { return draws_; }
  /// Tiles are being drawn — the attribution is shown exactly then (item 9).
  [[nodiscard]] bool Drawing() const { return !draws_.empty(); }
  /// The level the current view uses (-1 when none).
  [[nodiscard]] int Level() const { return level_; }
  [[nodiscard]] const MapTileService& Service() const { return *service_; }

 private:
  struct Tile {
    unsigned int        texture = 0;
    std::vector<double> xyuv;  ///< Empty until placed for the current location.
    bool                placeFailed = false;
    std::uint64_t       lastUsedFrame = 0;
  };
  using Clock = std::chrono::steady_clock;

  void Clear(bool releaseTextures);
  bool EnsurePlaced(const MapTileKey& key, Tile& t, int& placeBudget, std::vector<std::string>& log);

  std::unique_ptr<MapTileService> service_;
  OnlineMapTextureHooks           hooks_;
  std::chrono::milliseconds       retryAfterFailure_;

  std::map<MapTileKey, Tile>             tiles_;
  std::set<MapTileKey>                   notFound_;
  std::map<MapTileKey, Clock::time_point> retryAt_;
  std::vector<MapTileResult>             pendingUpload_;
  std::vector<MapTileDraw>               draws_;
  std::vector<MapTileKey>                wanted_;
  std::vector<MapTileKey>                sent_;  ///< What the service was last asked for.
  OnlineMapMessageLatch                  latch_;
  std::uint64_t                          frame_ = 0;
  int                                    level_ = -1;

  // The drawing location the placements belong to; any change re-places every tile.
  DrawingWgs84Frame geo_;
  bool              geoOk_ = false;
  bool              haveLocation_ = false;
  DrawingSettings   locSettings_;
  int               locInsUnits_ = 0;
  double            locOriginX_ = 0.0, locOriginY_ = 0.0;
  int               map_ = 0;

  // The view the wanted tiles were planned for.
  double viewX_ = 0.0, viewY_ = 0.0, viewHalfH_ = 0.0;
  int    viewW_ = 0, viewH_ = 0;
  bool   havePlan_ = false;
};
