// The online map controller (REQ-363, ADR-064). See CadOnlineMap.hpp.

#include "CadOnlineMap.hpp"

#include "geo/WebMercator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

std::string UsgsTileUrl(const char* service, int z, int x, int y) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "https://basemap.nationalmap.gov/arcgis/rest/services/%s/MapServer/tile/%d/%d/%d",
                service, z, y, x);  // ArcGIS order: level / row / column
  return buf;
}

bool PlaceMapTile(const DrawingWgs84Frame& frame, int z, int x, int y, int cells, std::vector<double>& xyuv,
                  std::string* error) {
  xyuv.clear();
  const int n = std::max(1, cells);
  const geo::MercatorBox box = geo::TileMercatorBox(z, x, y);
  // (n + 1)² vertices, row j = 0 on the north edge (v = 0, the image's first row).
  std::vector<double> local(static_cast<size_t>((n + 1) * (n + 1)) * 2u);
  for (int j = 0; j <= n; ++j) {
    for (int i = 0; i <= n; ++i) {
      const double mx = box.minX + (box.maxX - box.minX) * i / n;
      const double my = box.maxY - (box.maxY - box.minY) * j / n;
      const geo::GeoResult p = frame.LocalFromWgs84(geo::LongitudeFromMercatorX(mx), geo::LatitudeFromMercatorY(my));
      if (!p.ok) {
        if (error)
          *error = p.error;
        return false;
      }
      const size_t k = static_cast<size_t>(j * (n + 1) + i) * 2u;
      local[k] = p.x;
      local[k + 1] = p.y;
    }
  }
  xyuv.reserve(static_cast<size_t>(n * n) * 6u * 4u);
  auto vert = [&](int i, int j) {
    const size_t k = static_cast<size_t>(j * (n + 1) + i) * 2u;
    xyuv.push_back(local[k]);
    xyuv.push_back(local[k + 1]);
    xyuv.push_back(static_cast<double>(i) / n);
    xyuv.push_back(static_cast<double>(j) / n);
  };
  for (int j = 0; j < n; ++j) {
    for (int i = 0; i < n; ++i) {
      vert(i, j), vert(i + 1, j), vert(i + 1, j + 1);
      vert(i, j), vert(i + 1, j + 1), vert(i, j + 1);
    }
  }
  return true;
}

std::string OnlineMapMessageLatch::OnFailed(const std::string& reason) {
  if (failureSaid_)
    return {};
  failureSaid_ = true;
  return "Online map: USGS could not be reached \xE2\x80\x94 " + reason + "; showing cached tiles only.";
}

std::string OnlineMapMessageLatch::OnNotFound() {
  if (notFoundSaid_)
    return {};
  notFoundSaid_ = true;
  return "Online map: USGS has no map at this location.";
}

std::string OnlineMapMessageLatch::OnPlacementFailed(const std::string& reason) {
  if (placementSaid_)
    return {};
  placementSaid_ = true;
  return "Online map: the map cannot be placed in this drawing \xE2\x80\x94 " + reason;
}

OnlineMapController::OnlineMapController(std::unique_ptr<MapTileService> service, OnlineMapTextureHooks hooks,
                                         std::chrono::milliseconds retryAfterFailure)
    : service_(std::move(service)), hooks_(std::move(hooks)), retryAfterFailure_(retryAfterFailure) {}

OnlineMapController::~OnlineMapController() { Clear(true); }

void OnlineMapController::Clear(bool releaseTextures) {
  if (releaseTextures) {
    for (auto& [key, t] : tiles_)
      if (t.texture && hooks_.release)
        hooks_.release(t.texture);
    tiles_.clear();
  }
  draws_.clear();
}

bool OnlineMapController::EnsurePlaced(const MapTileKey& key, Tile& t, int& placeBudget,
                                       std::vector<std::string>& log) {
  if (!t.xyuv.empty())
    return true;
  if (t.placeFailed || placeBudget <= 0)
    return false;
  --placeBudget;
  std::string why;
  if (!PlaceMapTile(geo_, key.z, key.x, key.y, kCellsPerSide, t.xyuv, &why)) {
    t.placeFailed = true;
    if (std::string m = latch_.OnPlacementFailed(why); !m.empty())
      log.push_back(std::move(m));
    return false;
  }
  return true;
}

void OnlineMapController::Update(const AppCommandState& st, bool modelView, const Camera& cam, int fbWidth,
                                 int fbHeight, std::vector<std::string>& log) {
  ++frame_;
  draws_.clear();
  const DrawingSettings& s = st.drawingSettings;
  const OnlineMapInfo& info = OnlineMapInfoOf(s.onlineMap);
  const bool mapOff = info.service == nullptr || !s.Geolocated();
  if (mapOff) {
    // Map Off releases the map's textures (ADR-064 (d)); the disk cache brings them back quickly.
    // Paper space does not: switching to a layout and back should not reload the view.
    Clear(true);
    pendingUpload_.clear();
  }
  if (!modelView || mapOff || fbWidth <= 0 || fbHeight <= 0) {
    // Map Off draws nothing and requests nothing (item 8). Tiles already in flight still land.
    if (!sent_.empty()) {
      service_->SetWanted({});
      sent_.clear();
    }
    level_ = -1;
    return;
  }

  // ---- The drawing's location: any change re-places every tile (the textures stay) -----------------
  const int map = static_cast<int>(s.onlineMap);
  const bool locationChanged = !haveLocation_ || s.zoneCode != locSettings_.zoneCode ||
                               s.transform != locSettings_.transform ||
                               s.footDefinition != locSettings_.footDefinition ||
                               st.drawingInsUnits != locInsUnits_ || st.worldDocumentOriginX != locOriginX_ ||
                               st.worldDocumentOriginY != locOriginY_;
  if (locationChanged || map != map_) {
    latch_.OnLocationChanged();
    havePlan_ = false;
  }
  map_ = map;
  if (locationChanged) {
    haveLocation_ = true;
    locSettings_ = s;
    locInsUnits_ = st.drawingInsUnits;
    locOriginX_ = st.worldDocumentOriginX;
    locOriginY_ = st.worldDocumentOriginY;
    for (auto& [key, t] : tiles_) {
      t.xyuv.clear();
      t.placeFailed = false;
    }
    std::string why;
    geoOk_ = geo_.Open(st, &why);
    if (!geoOk_)
      if (std::string m = latch_.OnPlacementFailed(why); !m.empty())
        log.push_back(std::move(m));
  }
  if (!geoOk_) {
    if (!sent_.empty()) {
      service_->SetWanted({});
      sent_.clear();
    }
    level_ = -1;
    return;
  }

  // ---- Plan the view's tiles (only when the view changed) -----------------------------------------
  const double halfH = static_cast<double>(cam.orthoHalfH);
  if (!havePlan_ || cam.targetX != viewX_ || cam.targetY != viewY_ || halfH != viewHalfH_ ||
      fbWidth != viewW_ || fbHeight != viewH_) {
    havePlan_ = true;
    viewX_ = cam.targetX;
    viewY_ = cam.targetY;
    viewHalfH_ = halfH;
    viewW_ = fbWidth;
    viewH_ = fbHeight;
    wanted_.clear();
    level_ = -1;
    // The plan window a plan view at this centre and zoom shows (item 4), sampled at its corners,
    // edge midpoints and centre so a rotated drawing still gets a box that covers it.
    const double halfW = halfH * fbWidth / fbHeight;
    geo::MercatorBox box{1e300, 1e300, -1e300, -1e300};
    double cx = 0.0, cy = 0.0, rx = 0.0, ry = 0.0;
    bool haveCentre = false, haveRight = false;
    for (int j = -1; j <= 1; ++j) {
      for (int i = -1; i <= 1; ++i) {
        const geo::GeoResult ll = geo_.Wgs84FromLocal(viewX_ + i * halfW, viewY_ + j * halfH);
        if (!ll.ok)
          continue;
        const double mx = geo::MercatorXFromLongitude(ll.x);
        const double my = geo::MercatorYFromLatitude(ll.y);
        box.minX = std::min(box.minX, mx);
        box.maxX = std::max(box.maxX, mx);
        box.minY = std::min(box.minY, my);
        box.maxY = std::max(box.maxY, my);
        if (i == 0 && j == 0)
          cx = mx, cy = my, haveCentre = true;
        if (i == 1 && j == 0)
          rx = mx, ry = my, haveRight = true;
      }
    }
    if (haveCentre && haveRight) {
      const double metersPerPixel = std::hypot(rx - cx, ry - cy) / (fbWidth * 0.5);
      const geo::TileRange r =
          geo::TilesCoveringAtMost(box, geo::ChooseTileLevel(metersPerPixel, kMaxLevel), kMaxTiles);
      level_ = r.z;
      const double midX = (r.minX + r.maxX) * 0.5, midY = (r.minY + r.maxY) * 0.5;
      for (int ty = r.minY; ty <= r.maxY; ++ty)
        for (int tx = r.minX; tx <= r.maxX; ++tx)
          wanted_.push_back({map, r.z, tx, ty});
      // Centre first: the tiles the user is looking at arrive first.
      std::stable_sort(wanted_.begin(), wanted_.end(), [&](const MapTileKey& a, const MapTileKey& b) {
        return std::hypot(a.x - midX, a.y - midY) < std::hypot(b.x - midX, b.y - midY);
      });
    } else if (std::string m = latch_.OnPlacementFailed("the view is outside the zone's area."); !m.empty()) {
      log.push_back(std::move(m));
    }
  }

  // ---- Collect finished tiles -------------------------------------------------------------------------
  const Clock::time_point now = Clock::now();
  for (MapTileResult& r : service_->TakeResults(64)) {
    // Answered, so no longer outstanding: a failed tile due for a retry must read as a new request.
    sent_.erase(std::remove(sent_.begin(), sent_.end(), r.key), sent_.end());
    switch (r.status) {
      case MapTileStatus::Ok:
        if (r.fromNetwork)
          latch_.OnFetched();
        retryAt_.erase(r.key);
        pendingUpload_.push_back(std::move(r));
        break;
      case MapTileStatus::NotFound:
        notFound_.insert(r.key);
        if (r.key.map == map_)
          if (std::string m = latch_.OnNotFound(); !m.empty())
            log.push_back(std::move(m));
        break;
      case MapTileStatus::Failed:
        retryAt_[r.key] = now + retryAfterFailure_;
        if (std::string m = latch_.OnFailed(r.error); !m.empty())
          log.push_back(std::move(m));
        break;
    }
  }
  int uploads = kWorkPerFrame;
  while (uploads > 0 && !pendingUpload_.empty()) {
    MapTileResult r = std::move(pendingUpload_.front());
    pendingUpload_.erase(pendingUpload_.begin());
    Tile& t = tiles_[r.key];
    if (t.texture)
      continue;
    --uploads;
    t.texture = hooks_.upload ? hooks_.upload(r.rgba, r.width, r.height) : 0;
    t.lastUsedFrame = frame_;
    if (!t.texture)
      tiles_.erase(r.key);
  }

  // ---- Ask for what is missing ------------------------------------------------------------------------
  auto pendingHas = [&](const MapTileKey& k) {
    return std::any_of(pendingUpload_.begin(), pendingUpload_.end(),
                       [&](const MapTileResult& r) { return r.key == k; });
  };
  std::vector<MapTileKey> ask;
  for (const MapTileKey& k : wanted_) {
    if (auto it = tiles_.find(k); it != tiles_.end() && it->second.texture)
      continue;
    if (notFound_.count(k) || pendingHas(k))
      continue;
    if (auto it = retryAt_.find(k); it != retryAt_.end() && now < it->second)
      continue;
    ask.push_back(k);
  }
  if (ask != sent_) {
    std::vector<MapTileRequest> requests;
    requests.reserve(ask.size());
    for (const MapTileKey& k : ask) {
      char path[96];
      std::snprintf(path, sizeof(path), "%s/%d/%d/%d", info.service, k.z, k.x, k.y);
      requests.push_back({k, UsgsTileUrl(info.service, k.z, k.x, k.y), path});
    }
    service_->SetWanted(std::move(requests));
    sent_ = std::move(ask);
  }

  // ---- Draw list: a loaded ancestor stands in for a tile still on its way -----------------------------
  int placeBudget = kWorkPerFrame;
  std::set<MapTileKey> stand_ins;
  std::vector<MapTileKey> exact;
  for (const MapTileKey& k : wanted_) {
    auto it = tiles_.find(k);
    if (it != tiles_.end() && it->second.texture && EnsurePlaced(k, it->second, placeBudget, log)) {
      exact.push_back(k);
      continue;
    }
    for (int up = 1; up <= 6 && k.z - up >= 0; ++up) {
      const MapTileKey a{k.map, k.z - up, k.x >> up, k.y >> up};
      auto ai = tiles_.find(a);
      if (ai != tiles_.end() && ai->second.texture && EnsurePlaced(a, ai->second, placeBudget, log)) {
        stand_ins.insert(a);
        break;
      }
    }
  }
  std::vector<MapTileKey> order(stand_ins.begin(), stand_ins.end());  // std::set: coarsest level first
  order.insert(order.end(), exact.begin(), exact.end());
  for (const MapTileKey& k : order) {
    Tile& t = tiles_[k];
    t.lastUsedFrame = frame_;
    draws_.push_back({t.texture, &t.xyuv});
  }

  // ---- Texture budget: drop the least recently used, never one drawn this frame -----------------------
  if (tiles_.size() > kMaxTextures) {
    std::vector<std::pair<std::uint64_t, MapTileKey>> age;
    for (const auto& [key, t] : tiles_)
      if (t.lastUsedFrame != frame_)
        age.push_back({t.lastUsedFrame, key});
    std::sort(age.begin(), age.end());
    for (size_t i = 0; i < age.size() && tiles_.size() > kMaxTextures; ++i) {
      auto it = tiles_.find(age[i].second);
      if (it->second.texture && hooks_.release)
        hooks_.release(it->second.texture);
      tiles_.erase(it);
    }
  }
}
