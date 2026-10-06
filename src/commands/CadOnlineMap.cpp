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

void OnlineMapController::ReleaseTexturesExcept(
    const std::map<MapTileKey, std::shared_ptr<const std::string>>& keep) {
  for (auto it = tiles_.begin(); it != tiles_.end();) {
    if (keep.count(it->first)) {
      ++it;
      continue;
    }
    if (it->second.texture && hooks_.release)
      hooks_.release(it->second.texture);
    it = tiles_.erase(it);
  }
  pendingUpload_.erase(std::remove_if(pendingUpload_.begin(), pendingUpload_.end(),
                                      [&](const MapTileResult& r) { return keep.count(r.key) == 0; }),
                       pendingUpload_.end());
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

bool OnlineMapController::LocalRectToMercator(double minX, double minY, double maxX, double maxY,
                                              geo::MercatorBox* box) const {
  *box = {1e300, 1e300, -1e300, -1e300};
  bool haveCentre = false;
  for (int j = 0; j <= 2; ++j) {
    for (int i = 0; i <= 2; ++i) {
      const geo::GeoResult ll = geo_.Wgs84FromLocal(minX + (maxX - minX) * i / 2.0, minY + (maxY - minY) * j / 2.0);
      if (!ll.ok)
        continue;
      const double mx = geo::MercatorXFromLongitude(ll.x);
      const double my = geo::MercatorYFromLatitude(ll.y);
      box->minX = std::min(box->minX, mx);
      box->maxX = std::max(box->maxX, mx);
      box->minY = std::min(box->minY, my);
      box->maxY = std::max(box->maxY, my);
      if (i == 1 && j == 1)
        haveCentre = true;
    }
  }
  return haveCentre;
}

void OnlineMapController::Update(AppCommandState& st, bool modelView, const Camera& cam, int fbWidth,
                                 int fbHeight, std::vector<std::string>& log) {
  using GP = AppCommandState::GeoCmdPhase;
  ++frame_;
  draws_.clear();
  const DrawingSettings& s = st.drawingSettings;
  const OnlineMapInfo& info = OnlineMapInfoOf(s.onlineMap);
  bool capturing = st.active == AppCommandState::Kind::GeoCaptureArea && st.geoCmdPhase == GP::Capturing;
  if (!capturing)
    job_ = {};  // ended or cancelled (Esc): nothing is kept
  const auto failCapture = [&](const std::string& why) {
    FailMapCapture(st, why, log);
    job_ = {};
    capturing = false;
  };

  // The drawing's captured tiles (REQ-364): drawn with the map on or off, never fetched.
  std::map<MapTileKey, std::shared_ptr<const std::string>> captured;
  for (const DrawingSettings::CapturedArea& a : s.capturedAreas)
    for (const DrawingSettings::CapturedTile& t : a.tiles)
      if (t.image)
        captured[{static_cast<int>(a.map), a.level, t.x, t.y}] = t.image;

  const bool mapOn = info.service != nullptr && s.Geolocated();
  if (!mapOn) {
    // Map Off releases the live map's textures (ADR-064 (d)); the disk cache brings them back
    // quickly. Paper space does not: switching to a layout and back should not reload the view.
    ReleaseTexturesExcept(captured);
    if (capturing)
      failCapture("the map was turned off.");
  }
  if (!s.Geolocated() || !modelView || fbWidth <= 0 || fbHeight <= 0) {
    // Not geolocated: nothing can be placed. Paper space / the Start tab: nothing is drawn
    // (item 4); a capture in progress waits for model space. Tiles in flight still land.
    if (!sent_.empty()) {
      service_->SetWanted({});
      sent_.clear();
    }
    level_ = -1;
    return;
  }

  // ---- The drawing's location: any change re-places every tile (the textures stay) -----------------
  const int map = static_cast<int>(s.onlineMap);
  const bool locationChanged = !haveLocation_ || s.zoneCode != locZone_ || s.transform != locTransform_ ||
                               s.footDefinition != locFoot_ || st.drawingInsUnits != locInsUnits_ ||
                               st.worldDocumentOriginX != locOriginX_ || st.worldDocumentOriginY != locOriginY_;
  if (locationChanged || map != map_) {
    latch_.OnLocationChanged();
    havePlan_ = false;
  }
  map_ = map;
  if (locationChanged) {
    haveLocation_ = true;
    locZone_ = s.zoneCode;
    locTransform_ = s.transform;
    locFoot_ = s.footDefinition;
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
    if (capturing)
      failCapture("the map cannot be placed in this drawing.");
    if (!sent_.empty()) {
      service_->SetWanted({});
      sent_.clear();
    }
    level_ = -1;
    return;
  }

  // ---- Plan the view's tiles (only when the view changed) -----------------------------------------
  const double halfH = static_cast<double>(cam.orthoHalfH);
  const double halfW = halfH * fbWidth / fbHeight;
  if (!mapOn) {
    wanted_.clear();
    level_ = -1;
    havePlan_ = false;
  } else if (!havePlan_ || cam.targetX != viewX_ || cam.targetY != viewY_ || halfH != viewHalfH_ ||
             fbWidth != viewW_ || fbHeight != viewH_) {
    havePlan_ = true;
    viewX_ = cam.targetX;
    viewY_ = cam.targetY;
    viewHalfH_ = halfH;
    viewW_ = fbWidth;
    viewH_ = fbHeight;
    wanted_.clear();
    level_ = -1;
    // The plan window a plan view at this centre and zoom shows (item 4).
    geo::MercatorBox box;
    const geo::GeoResult c = geo_.Wgs84FromLocal(viewX_, viewY_);
    const geo::GeoResult r = geo_.Wgs84FromLocal(viewX_ + halfW, viewY_);
    if (LocalRectToMercator(viewX_ - halfW, viewY_ - halfH, viewX_ + halfW, viewY_ + halfH, &box) && c.ok && r.ok) {
      // Screen pixel size in mercator meters, from the centre to the right edge's midpoint.
      const double metersPerPixel =
          std::hypot(geo::MercatorXFromLongitude(r.x) - geo::MercatorXFromLongitude(c.x),
                     geo::MercatorYFromLatitude(r.y) - geo::MercatorYFromLatitude(c.y)) /
          (fbWidth * 0.5);
      const geo::TileRange tr =
          geo::TilesCoveringAtMost(box, geo::ChooseTileLevel(metersPerPixel, kMaxLevel), kMaxTiles);
      level_ = tr.z;
      const double midX = (tr.minX + tr.maxX) * 0.5, midY = (tr.minY + tr.maxY) * 0.5;
      for (int ty = tr.minY; ty <= tr.maxY; ++ty)
        for (int tx = tr.minX; tx <= tr.maxX; ++tx)
          wanted_.push_back({map, tr.z, tx, ty});
      // Centre first: the tiles the user is looking at arrive first.
      std::stable_sort(wanted_.begin(), wanted_.end(), [&](const MapTileKey& a, const MapTileKey& b) {
        return std::hypot(a.x - midX, a.y - midY) < std::hypot(b.x - midX, b.y - midY);
      });
    } else if (std::string m = latch_.OnPlacementFailed("the view is outside the zone's area."); !m.empty()) {
      log.push_back(std::move(m));
    }
  }

  // ---- Plan a Capture Area (REQ-364 item 2): the displayed level's tiles over the area ----------------
  if (capturing && !job_.active) {
    const AppCommandState::MapCaptureState& c = st.mapCapture;
    geo::MercatorBox box;
    const bool ok = c.visibleArea
                        ? LocalRectToMercator(viewX_ - halfW, viewY_ - halfH, viewX_ + halfW, viewY_ + halfH, &box)
                        : LocalRectToMercator(c.minX, c.minY, c.maxX, c.maxY, &box);
    if (level_ < 0 || !ok) {
      failCapture("the area is outside the map's reach from this drawing.");
    } else {
      const geo::TileRange tr = geo::TilesCovering(box, level_);
      if (tr.Count() > kMaxCaptureTiles) {
        failCapture("the area needs " + std::to_string(tr.Count()) + " map tiles at this zoom (the most is " +
                    std::to_string(kMaxCaptureTiles) + "). Zoom in or pick a smaller area.");
      } else {
        job_.active = true;
        job_.map = map;
        job_.level = level_;
        for (int ty = tr.minY; ty <= tr.maxY; ++ty)
          for (int tx = tr.minX; tx <= tr.maxX; ++tx)
            job_.keys.push_back({map, level_, tx, ty});
        st.mapCapture.total = static_cast<int>(job_.keys.size());
      }
    }
  }
  const auto inJob = [&](const MapTileKey& k) {
    return job_.active && std::find(job_.keys.begin(), job_.keys.end(), k) != job_.keys.end();
  };

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
        if (inJob(r.key))
          job_.images[r.key] = r.bytes;
        pendingUpload_.push_back(std::move(r));
        break;
      case MapTileStatus::NotFound:
        notFound_.insert(r.key);
        if (inJob(r.key))
          job_.images[r.key] = nullptr;  // no tile there: nothing to keep
        if (r.key.map == map_ && mapOn)
          if (std::string m = latch_.OnNotFound(); !m.empty())
            log.push_back(std::move(m));
        break;
      case MapTileStatus::Failed:
        retryAt_[r.key] = now + retryAfterFailure_;
        if (inJob(r.key))
          failCapture("a map tile could not be downloaded \xE2\x80\x94 " + r.error + ".");
        else if (std::string m = latch_.OnFailed(r.error); !m.empty())
          log.push_back(std::move(m));
        break;
    }
  }

  // ---- A capture gathers what is already here, and ends when it has everything ------------------------
  if (job_.active) {
    for (const MapTileKey& k : job_.keys) {
      if (job_.images.count(k))
        continue;
      if (auto it = tiles_.find(k); it != tiles_.end() && it->second.image)
        job_.images[k] = it->second.image;
      else if (auto ci = captured.find(k); ci != captured.end())
        job_.images[k] = ci->second;
      else if (notFound_.count(k))
        job_.images[k] = nullptr;
    }
    st.mapCapture.gathered = static_cast<int>(job_.images.size());
    st.mapCapture.prompt = "Capturing map\xE2\x80\xA6 " + std::to_string(st.mapCapture.gathered) + " of " +
                           std::to_string(st.mapCapture.total) + " tiles";
    if (job_.images.size() == job_.keys.size()) {
      DrawingSettings::CapturedArea area;
      area.map = static_cast<DrawingSettings::OnlineMap>(job_.map);
      area.level = job_.level;
      for (const MapTileKey& k : job_.keys)
        if (const std::shared_ptr<const std::string>& img = job_.images[k])
          area.tiles.push_back({k.x, k.y, img});
      CommitMapCapture(st, std::move(area), log);
      job_ = {};
      capturing = false;
    }
  }

  // ---- Upload a few ------------------------------------------------------------------------------------
  int uploads = kWorkPerFrame;
  while (uploads > 0 && !pendingUpload_.empty()) {
    MapTileResult r = std::move(pendingUpload_.front());
    pendingUpload_.erase(pendingUpload_.begin());
    Tile& t = tiles_[r.key];
    if (t.texture)
      continue;
    --uploads;
    t.texture = hooks_.upload ? hooks_.upload(r.rgba, r.width, r.height) : 0;
    t.image = r.bytes;
    t.lastUsedFrame = frame_;
    if (!t.texture)
      tiles_.erase(r.key);
  }

  // ---- Ask for what is missing: the capture first, then the view, then the captured areas -------------
  auto pendingHas = [&](const MapTileKey& k) {
    return std::any_of(pendingUpload_.begin(), pendingUpload_.end(),
                       [&](const MapTileResult& r) { return r.key == k; });
  };
  auto loaded = [&](const MapTileKey& k) {
    auto it = tiles_.find(k);
    return it != tiles_.end() && it->second.texture;
  };
  std::vector<MapTileKey> ask;
  const auto add = [&](const MapTileKey& k) {
    if (std::find(ask.begin(), ask.end(), k) == ask.end())
      ask.push_back(k);
  };
  if (job_.active)
    for (const MapTileKey& k : job_.keys)
      if (!job_.images.count(k) && !pendingHas(k))
        add(k);  // a capture does not wait out a retry delay: it asks, and a failure ends it
  for (const MapTileKey& k : wanted_) {
    if (loaded(k) || notFound_.count(k) || pendingHas(k))
      continue;
    if (auto it = retryAt_.find(k); it != retryAt_.end() && now < it->second && !captured.count(k))
      continue;
    add(k);
  }
  for (const auto& [k, img] : captured)
    if (!loaded(k) && !pendingHas(k))
      add(k);
  if (ask != sent_) {
    std::vector<MapTileRequest> requests;
    requests.reserve(ask.size());
    for (const MapTileKey& k : ask) {
      MapTileRequest req;
      req.key = k;
      if (auto ci = captured.find(k); ci != captured.end()) {
        req.image = ci->second;  // in the drawing already: decode only, never the network
      } else {
        const char* service = OnlineMapInfoOf(static_cast<DrawingSettings::OnlineMap>(k.map)).service;
        if (!service)
          continue;
        char path[96];
        std::snprintf(path, sizeof(path), "%s/%d/%d/%d", service, k.z, k.x, k.y);
        req.url = UsgsTileUrl(service, k.z, k.x, k.y);
        req.cachePath = path;
      }
      requests.push_back(std::move(req));
    }
    service_->SetWanted(std::move(requests));
    sent_ = std::move(ask);
  }

  // ---- Draw list: live map (a loaded ancestor stands in for a tile on its way), then captured areas ---
  int placeBudget = kWorkPerFrame;
  std::set<MapTileKey> standIns;
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
        standIns.insert(a);
        break;
      }
    }
  }
  std::vector<MapTileKey> order(standIns.begin(), standIns.end());  // std::set: coarsest level first
  order.insert(order.end(), exact.begin(), exact.end());
  for (const auto& [k, img] : captured) {  // REQ-364 item 4: above the live map
    auto it = tiles_.find(k);
    if (it != tiles_.end() && it->second.texture && EnsurePlaced(k, it->second, placeBudget, log))
      order.push_back(k);
  }
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
