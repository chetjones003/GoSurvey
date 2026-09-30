// REQ-363 (GitHub issue #583 increment 1, ADR-064): the online base map — Web Mercator tile maths,
// WGS 84 ↔ drawing placement lined up on NGS AG9976 (with and without a REQ-360 transformation), the
// tile service's disk cache and failure reporting with a fake network, the controller's "Map Off
// fetches nothing" and "one message per problem", and the map choice as a per-drawing, undoable,
// saved setting.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "CadCommands.hpp"
#include "CadOnlineMap.hpp"
#include "GsIo.hpp"
#include "MapTileService.hpp"
#include "geo/CoordinateSystems.hpp"
#include "geo/WebMercator.hpp"

namespace {

void LoadShippedDictionary() {
  if (!geo::DictionariesLoaded())
    REQUIRE(geo::LoadDictionaries(GOSURVEY_CSMAP_DICTIONARY_DIR));
}

double Dms(double d, double m, double s) { return d + m / 60.0 + s / 3600.0; }

// NGS datasheet, PID AG9976 (the REQ-358 / REQ-359 test point):
//   NAD 83(1993) POSITION- 30 17 10.51249(N) 097 44 21.71739(W)
//   SPC TX C -10,077,391.26  3,115,243.14   sFT
const double kLat = Dms(30, 17, 10.51249);
const double kLon = -Dms(97, 44, 21.71739);
constexpr double kNFt = 10077391.26;
constexpr double kEFt = 3115243.14;
constexpr double kOriginX = 3115000.0;
constexpr double kOriginY = 10077000.0;
constexpr const char* kZone = "HARN/TX.TX-CF";

AppCommandState TexasDrawing() {
  LoadShippedDictionary();
  AppCommandState st;
  st.drawingInsUnits = 2;  // feet (US survey foot by default)
  st.drawingSettings.zoneCode = kZone;
  st.worldDocumentOriginX = kOriginX;
  st.worldDocumentOriginY = kOriginY;
  return st;
}

/// A real (2 × 2) PNG, so the service's decoder runs on every fake fetch.
const std::string& TinyPng() {
  static const unsigned char kBytes[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00,
      0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xfd, 0xd4, 0x9a, 0x73, 0x00,
      0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x3c, 0x21, 0x27, 0xc7, 0xc0, 0xc0, 0xc0,
      0xc4, 0x00, 0x06, 0x00, 0x0d, 0x04, 0x01, 0x08, 0xa3, 0x13, 0x4e, 0x5a, 0x00, 0x00, 0x00, 0x00, 0x49,
      0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  static const std::string s(reinterpret_cast<const char*>(kBytes), sizeof(kBytes));
  return s;
}

/// A fake network whose answer the test switches.
struct FakeNetwork {
  std::atomic<int> mode{0};  // 0 = Ok, 1 = NotFound, 2 = Failed
  MapTileFetch Fetch() {
    return [this](const std::string&, std::string& body, std::string& error) {
      switch (mode.load()) {
        case 1: return MapTileStatus::NotFound;
        case 2: error = "no route to host"; return MapTileStatus::Failed;
        default: body = TinyPng(); return MapTileStatus::Ok;
      }
    };
  }
};

struct ScratchDir {
  std::filesystem::path path;
  explicit ScratchDir(const char* tag) {
    path = std::filesystem::temp_directory_path() / ("gosurvey-onlinemap-" + std::string(tag));
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

std::vector<MapTileResult> WaitForResults(MapTileService& s, size_t n) {
  std::vector<MapTileResult> out;
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (out.size() < n && std::chrono::steady_clock::now() < until) {
    for (MapTileResult& r : s.TakeResults(64))
      out.push_back(std::move(r));
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return out;
}

/// The local point that a tile mesh puts at (u, v): bilinear inside the cell holding it.
bool MeshPointAt(const std::vector<double>& xyuv, int cells, double u, double v, double* x, double* y) {
  const int i = std::min(cells - 1, static_cast<int>(u * cells));
  const int j = std::min(cells - 1, static_cast<int>(v * cells));
  // Cell (i, j) is triangles 2·(j·cells + i) and the next: vertices (i,j) (i+1,j) (i+1,j+1) | (i,j) (i+1,j+1) (i,j+1).
  const size_t base = static_cast<size_t>(j * cells + i) * 6u * 4u;
  if (base + 24 > xyuv.size())
    return false;
  const double* p00 = &xyuv[base + 0];
  const double* p10 = &xyuv[base + 4];
  const double* p11 = &xyuv[base + 8];
  const double* p01 = &xyuv[base + 20];
  const double fu = u * cells - i, fv = v * cells - j;
  *x = (1 - fu) * (1 - fv) * p00[0] + fu * (1 - fv) * p10[0] + fu * fv * p11[0] + (1 - fu) * fv * p01[0];
  *y = (1 - fu) * (1 - fv) * p00[1] + fu * (1 - fv) * p10[1] + fu * fv * p11[1] + (1 - fu) * fv * p01[1];
  return true;
}

/// REQ-363 acceptance 3: the level-16 tile pixel holding AG9976's WGS 84 position is placed within one
/// level-16 pixel of \p expectLocal. Returns the miss in feet.
double Level16MissFeet(const AppCommandState& st, double expectLocalX, double expectLocalY) {
  // AG9976 in WGS 84: the datasheet's NAD 83(1993) (HARN) position along CS-MAP's datum path.
  const geo::GeoResult w = geo::ConvertLatLong(kZone, "LL84", kLon, kLat);
  REQUIRE(w.ok);
  const double mx = geo::MercatorXFromLongitude(w.x), my = geo::MercatorYFromLatitude(w.y);
  const geo::TileRange r = geo::TilesCovering({mx, my, mx, my}, 16);
  REQUIRE(r.Count() == 1);
  const geo::MercatorBox box = geo::TileMercatorBox(16, r.minX, r.minY);
  const double u = (mx - box.minX) / (box.maxX - box.minX);
  const double v = (box.maxY - my) / (box.maxY - box.minY);

  DrawingWgs84Frame frame;
  std::string why;
  REQUIRE(frame.Open(st, &why));
  std::vector<double> xyuv;
  REQUIRE(PlaceMapTile(frame, 16, r.minX, r.minY, OnlineMapController::kCellsPerSide, xyuv, &why));
  double x = 0.0, y = 0.0;
  REQUIRE(MeshPointAt(xyuv, OnlineMapController::kCellsPerSide, u, v, &x, &y));
  return std::hypot(x - expectLocalX, y - expectLocalY);
}

/// One level-16 pixel on the ground at AG9976's latitude, in US survey feet (≈ 6.8 ft).
double Level16PixelFeet() {
  return geo::kWebMercatorLevel0PixelMeters / 65536.0 * std::cos(kLat * 3.14159265358979 / 180.0) * 39.37 / 12.0;
}

Camera ViewOn(double localX, double localY, float halfH) {
  Camera c;
  c.targetX = localX;
  c.targetY = localY;
  c.orthoHalfH = halfH;
  return c;
}

/// Frames the controller until the service is idle and a draw list exists (or a timeout).
void Settle(OnlineMapController& map, AppCommandState& st, const Camera& cam, std::vector<std::string>& log) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (int i = 0; i < 4 || std::chrono::steady_clock::now() < until; ++i) {
    map.Update(st, true, cam, 800, 600, log);
    if (i >= 4 && map.Service().Idle())
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  map.Update(st, true, cam, 800, 600, log);
}

OnlineMapTextureHooks FakeTextures(int* live) {
  auto next = std::make_shared<unsigned int>(0);
  return {[live, next](const std::vector<unsigned char>&, int, int) {
            ++*live;
            return ++*next;
          },
          [live](unsigned int) { --*live; }};
}

}  // namespace

TEST_CASE("Web Mercator tile maths (REQ-363)", "[req363]") {
  // The inverse is exact to rounding.
  CHECK(geo::LatitudeFromMercatorY(geo::MercatorYFromLatitude(kLat)) == Catch::Approx(kLat).margin(1e-12));
  CHECK(geo::LongitudeFromMercatorX(geo::MercatorXFromLongitude(kLon)) == Catch::Approx(kLon).margin(1e-12));
  // AG9976 at level 16 is in tile x 14977, y 26984 — the standard slippy-map formula, by hand.
  const double n = 65536.0;
  const int hx = static_cast<int>((kLon + 180.0) / 360.0 * n);
  const double latR = kLat * 3.14159265358979323846 / 180.0;
  const int hy = static_cast<int>((1.0 - std::asinh(std::tan(latR)) / 3.14159265358979323846) / 2.0 * n);
  const double mx = geo::MercatorXFromLongitude(kLon), my = geo::MercatorYFromLatitude(kLat);
  const geo::TileRange r = geo::TilesCovering({mx, my, mx, my}, 16);
  CHECK(r.minX == hx);
  CHECK(r.minY == hy);
  const geo::MercatorBox b = geo::TileMercatorBox(16, hx, hy);
  CHECK(b.minX <= mx);
  CHECK(mx < b.maxX);
  CHECK(b.minY < my);
  CHECK(my <= b.maxY);

  // Level: a screen pixel the size of a level-z tile pixel picks z; finer than level 16 stays 16.
  for (int z = 0; z <= 16; ++z)
    CHECK(geo::ChooseTileLevel(geo::kWebMercatorLevel0PixelMeters / std::ldexp(1.0, z), 16) == z);
  CHECK(geo::ChooseTileLevel(0.01, 16) == 16);
  CHECK(geo::ChooseTileLevel(0.0, 16) == 0);

  // At most 64 tiles: a wide box falls back to a coarser level.
  const geo::MercatorBox wide{mx - 50000.0, my - 50000.0, mx + 50000.0, my + 50000.0};
  const geo::TileRange capped = geo::TilesCoveringAtMost(wide, 16, 64);
  CHECK(capped.Count() <= 64);
  CHECK(capped.z < 16);
  CHECK(geo::TilesCovering(wide, capped.z + 1).Count() > 64);
}

TEST_CASE("The cached WGS 84 converter agrees with the one-shot conversions (REQ-363)", "[req363]") {
  LoadShippedDictionary();
  geo::Wgs84GridConverter c;
  std::string why;
  REQUIRE(c.Open(kZone, &why));
  const geo::GeoResult w = geo::ConvertLatLong(kZone, "LL84", kLon, kLat);
  REQUIRE(w.ok);
  const geo::GeoResult g = c.ToGrid(w.x, w.y);
  REQUIRE(g.ok);
  CHECK(g.x == Catch::Approx(kEFt).margin(0.01));  // the NGS datasheet's grid, within REQ-101
  CHECK(g.y == Catch::Approx(kNFt).margin(0.01));
  const geo::GeoResult back = c.ToWgs84(g.x, g.y);
  REQUIRE(back.ok);
  CHECK(back.x == Catch::Approx(w.x).margin(1e-9));
  CHECK(back.y == Catch::Approx(w.y).margin(1e-9));

  CHECK_FALSE(c.Open("NOT-A-ZONE", &why));
  CHECK_FALSE(c.IsOpen());
  CHECK_FALSE(why.empty());
  CHECK_FALSE(c.Open("LL84", &why));  // a lat/long system has no grid
}

TEST_CASE("A level-16 tile lands on AG9976 within one tile pixel (REQ-363)", "[req363]") {
  AppCommandState st = TexasDrawing();
  const double pixelFt = Level16PixelFeet();
  REQUIRE(pixelFt > 6.0);
  REQUIRE(pixelFt < 7.5);

  SECTION("transformation off: the point's grid coordinate less the origin") {
    const double miss = Level16MissFeet(st, kEFt - kOriginX, kNFt - kOriginY);
    INFO("miss " << miss << " ft, pixel " << pixelFt << " ft");
    CHECK(miss < pixelFt);
    CHECK(miss < 0.05);  // in fact the placement adds nothing measurable
  }
  SECTION("an applied REQ-360 transformation (k != 1, rotation != 0)") {
    DrawingSettings::Transform& t = st.drawingSettings.transform;
    t.apply = true;
    t.computation = DrawingSettings::Transform::Computation::UserDefined;
    t.userScaleFactor = 0.9999;
    t.rotation = DrawingSettings::Transform::Rotation::ToNorth;
    t.toNorthDeg = 1.0;
    t.refLocalX = 1000.0;  // WORLD, drawing units
    t.refLocalY = 2000.0;
    t.refGridE = kEFt - 300.0;
    t.refGridN = kNFt - 400.0;
    // The expected local point comes from the existing grid → drawing path, not the map's.
    const geo::GeoResult expect = GridToDrawingPoint(st, kEFt, kNFt);
    REQUIRE(expect.ok);
    const double miss = Level16MissFeet(st, expect.x, expect.y);
    INFO("miss " << miss << " ft");
    CHECK(miss < pixelFt);
    CHECK(miss < 0.05);
  }
}

TEST_CASE("The tile service caches on disk, reports 404 and failures, and prunes (REQ-363)", "[req363]") {
  ScratchDir dir("service");
  const MapTileRequest req{{1, 16, 14977, 26984}, "https://example.invalid/16/26984/14977", "S/16/14977/26984", nullptr};
  {
    FakeNetwork net;
    MapTileService s(dir.path, net.Fetch(), 1);
    s.SetWanted({req});
    const std::vector<MapTileResult> r = WaitForResults(s, 1);
    REQUIRE(r.size() == 1);
    CHECK(r[0].status == MapTileStatus::Ok);
    CHECK(r[0].fromNetwork);
    CHECK(r[0].width == 2);
    CHECK(r[0].rgba.size() == 2u * 2u * 4u);
    REQUIRE(r[0].bytes);
    CHECK(*r[0].bytes == TinyPng());  // the file as served, for Capture Area
    CHECK(std::filesystem::exists(dir.path / "S/16/14977/26984"));
  }
  {
    // Offline now: the cached tile still arrives, and the network is not touched.
    FakeNetwork net;
    net.mode = 2;
    MapTileService s(dir.path, net.Fetch(), 1);
    s.SetWanted({req});
    const std::vector<MapTileResult> r = WaitForResults(s, 1);
    REQUIRE(r.size() == 1);
    CHECK(r[0].status == MapTileStatus::Ok);
    CHECK_FALSE(r[0].fromNetwork);
    CHECK(s.FetchCount() == 0);

    MapTileRequest other = req;
    other.key.x += 1;
    other.cachePath = "S/16/14978/26984";
    s.SetWanted({other});
    const std::vector<MapTileResult> f = WaitForResults(s, 1);
    REQUIRE(f.size() == 1);
    CHECK(f[0].status == MapTileStatus::Failed);
    CHECK(f[0].error == "no route to host");
  }
  {
    FakeNetwork net;
    net.mode = 1;
    MapTileService s({}, net.Fetch(), 1);  // no disk cache
    s.SetWanted({req});
    const std::vector<MapTileResult> r = WaitForResults(s, 1);
    REQUIRE(r.size() == 1);
    CHECK(r[0].status == MapTileStatus::NotFound);
  }
  {
    // An answer that is not an image is a failure, and is not cached.
    MapTileService s({}, [](const std::string&, std::string& body, std::string&) {
      body = "<html>busy</html>";
      return MapTileStatus::Ok;
    }, 1);
    s.SetWanted({req});
    const std::vector<MapTileResult> r = WaitForResults(s, 1);
    REQUIRE(r.size() == 1);
    CHECK(r[0].status == MapTileStatus::Failed);
  }

  // Prune: least recently used first, down to 90% of the cap.
  ScratchDir pruneDir("prune");
  const auto now = std::filesystem::file_time_type::clock::now();
  for (int i = 0; i < 3; ++i) {
    const auto p = pruneDir.path / ("t" + std::to_string(i));
    std::ofstream(p, std::ios::binary) << std::string(400, 'x');
    std::filesystem::last_write_time(p, now - std::chrono::hours(3 - i));  // t0 oldest
  }
  MapTileService::PruneDiskCache(pruneDir.path, 1000);
  CHECK_FALSE(std::filesystem::exists(pruneDir.path / "t0"));
  CHECK(std::filesystem::exists(pruneDir.path / "t1"));
  CHECK(std::filesystem::exists(pruneDir.path / "t2"));
}

TEST_CASE("Each online-map problem is said once (REQ-363)", "[req363]") {
  OnlineMapMessageLatch latch;
  CHECK_FALSE(latch.OnFailed("timeout").empty());
  CHECK(latch.OnFailed("timeout").empty());
  CHECK(latch.OnFailed("dns").empty());
  latch.OnFetched();  // a success makes the next failure news
  CHECK(latch.OnFailed("timeout").find("timeout") != std::string::npos);
  CHECK(latch.OnNotFound() == "Online map: USGS has no map at this location.");
  CHECK(latch.OnNotFound().empty());
  CHECK_FALSE(latch.OnPlacementFailed("x").empty());
  CHECK(latch.OnPlacementFailed("x").empty());
  latch.OnLocationChanged();
  CHECK_FALSE(latch.OnNotFound().empty());
  CHECK_FALSE(latch.OnPlacementFailed("x").empty());
}

TEST_CASE("Map Off, or no location, fetches nothing and draws nothing (REQ-363)", "[req363]") {
  AppCommandState st = TexasDrawing();
  FakeNetwork net;
  int live = 0;
  OnlineMapController map(std::make_unique<MapTileService>(std::filesystem::path(), net.Fetch(), 1),
                          FakeTextures(&live));
  std::vector<std::string> log;
  const Camera cam = ViewOn(kEFt - kOriginX, kNFt - kOriginY, 500.f);
  for (int i = 0; i < 20; ++i)
    map.Update(st, true, cam, 800, 600, log);  // Map Off (the default)
  st.drawingSettings.onlineMap = DrawingSettings::OnlineMap::UsgsImagery;
  st.drawingSettings.zoneCode.clear();  // not geolocated
  for (int i = 0; i < 20; ++i)
    map.Update(st, true, cam, 800, 600, log);
  st.drawingSettings.zoneCode = kZone;
  for (int i = 0; i < 20; ++i)
    map.Update(st, false, cam, 800, 600, log);  // paper space / the Start tab
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(map.Service().FetchCount() == 0);
  CHECK_FALSE(map.Drawing());
  CHECK(log.empty());
}

TEST_CASE("The map fetches, places and draws the view's tiles (REQ-363)", "[req363]") {
  AppCommandState st = TexasDrawing();
  st.drawingSettings.onlineMap = DrawingSettings::OnlineMap::UsgsImagery;
  FakeNetwork net;
  int live = 0;
  std::vector<std::string> log;
  {
    OnlineMapController map(std::make_unique<MapTileService>(std::filesystem::path(), net.Fetch(), 2),
                            FakeTextures(&live));
    const Camera cam = ViewOn(kEFt - kOriginX, kNFt - kOriginY, 500.f);
    Settle(map, st, cam, log);
    CHECK(map.Level() == 16);  // 1.7 ft screen pixels are finer than level 16: capped
    REQUIRE(map.Drawing());
    CHECK(map.DrawList().size() >= 1);
    CHECK(map.DrawList().size() <= static_cast<size_t>(OnlineMapController::kMaxTiles));
    for (const MapTileDraw& d : map.DrawList()) {
      CHECK(d.texture != 0);
      REQUIRE(d.xyuv);
      CHECK(d.xyuv->size() == static_cast<size_t>(OnlineMapController::kCellsPerSide *
                                                   OnlineMapController::kCellsPerSide * 6 * 4));
    }
    CHECK(log.empty());

    // A wide view is held to 64 tiles by going coarser.
    Settle(map, st, ViewOn(kEFt - kOriginX, kNFt - kOriginY, 200000.f), log);
    CHECK(map.Level() < 16);
    // Each wanted tile draws itself or one coarser stand-in, so the cap holds while tiles stream in.
    CHECK(map.DrawList().size() <= static_cast<size_t>(OnlineMapController::kMaxTiles));

    // Paper space draws nothing but keeps the textures (a layout and back does not reload the view).
    REQUIRE(live > 0);
    const int held = live;
    map.Update(st, false, cam, 800, 600, log);
    CHECK_FALSE(map.Drawing());
    CHECK(live == held);

    // Turning the map off stops drawing at once and releases every texture (ADR-064 (d)).
    st.drawingSettings.onlineMap = DrawingSettings::OnlineMap::Off;
    map.Update(st, true, cam, 800, 600, log);
    CHECK_FALSE(map.Drawing());
    CHECK(live == 0);
  }
  CHECK(live == 0);  // every texture released with the controller
}

TEST_CASE("Offline: one message, no stall, and news again after a success (REQ-363)", "[req363]") {
  AppCommandState st = TexasDrawing();
  st.drawingSettings.onlineMap = DrawingSettings::OnlineMap::UsgsTopo;
  FakeNetwork net;
  net.mode = 2;
  int live = 0;
  std::vector<std::string> log;
  OnlineMapController map(std::make_unique<MapTileService>(std::filesystem::path(), net.Fetch(), 2),
                          FakeTextures(&live), std::chrono::milliseconds(0));  // retry at once
  const Camera cam = ViewOn(kEFt - kOriginX, kNFt - kOriginY, 500.f);
  for (int i = 0; i < 200; ++i) {
    const auto t0 = std::chrono::steady_clock::now();
    map.Update(st, true, cam, 800, 600, log);
    // "Never blocks a frame": Update never waits on the (failing) network.
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(250));
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(map.Service().FetchCount() > 1);  // it kept retrying…
  REQUIRE(log.size() == 1);               // …and said so once
  CHECK(log[0].find("USGS could not be reached") != std::string::npos);
  CHECK(log[0].find("no route to host") != std::string::npos);
  CHECK_FALSE(map.Drawing());

  net.mode = 0;  // back online
  Settle(map, st, cam, log);
  CHECK(map.Drawing());
  CHECK(log.size() == 1);
  net.mode = 2;  // and gone again: that is news
  Settle(map, st, ViewOn(kEFt - kOriginX + 20000.0, kNFt - kOriginY, 500.f), log);
  CHECK(log.size() == 2);
}

TEST_CASE("No tiles at this location is said once (REQ-363)", "[req363]") {
  AppCommandState st = TexasDrawing();
  st.drawingSettings.onlineMap = DrawingSettings::OnlineMap::UsgsImagery;
  FakeNetwork net;
  net.mode = 1;
  int live = 0;
  std::vector<std::string> log;
  OnlineMapController map(std::make_unique<MapTileService>(std::filesystem::path(), net.Fetch(), 2),
                          FakeTextures(&live));
  const Camera cam = ViewOn(kEFt - kOriginX, kNFt - kOriginY, 500.f);
  Settle(map, st, cam, log);
  const std::uint64_t fetched = map.Service().FetchCount();
  Settle(map, st, cam, log);
  CHECK(map.Service().FetchCount() == fetched);  // a 404 is remembered, not asked again
  REQUIRE(log.size() == 1);
  CHECK(log[0] == "Online map: USGS has no map at this location.");
}

TEST_CASE("A zone the map cannot use is said once and draws nothing (REQ-363)", "[req363]") {
  AppCommandState st = TexasDrawing();
  st.drawingSettings.zoneCode = "NOT-A-ZONE";  // REQ-358 item 5 keeps unknown codes
  st.drawingSettings.onlineMap = DrawingSettings::OnlineMap::UsgsImagery;
  FakeNetwork net;
  int live = 0;
  std::vector<std::string> log;
  OnlineMapController map(std::make_unique<MapTileService>(std::filesystem::path(), net.Fetch(), 1),
                          FakeTextures(&live));
  for (int i = 0; i < 10; ++i)
    map.Update(st, true, ViewOn(0.0, 0.0, 500.f), 800, 600, log);
  CHECK(map.Service().FetchCount() == 0);
  REQUIRE(log.size() == 1);
  CHECK(log[0].find("cannot be placed") != std::string::npos);
}

TEST_CASE("The map choice is one undo step, per drawing, saved, and cleared by Remove Location (REQ-363)",
          "[req363]") {
  AppCommandState st = TexasDrawing();
  std::vector<std::string> log;
  REQUIRE(SetOnlineMap(st, DrawingSettings::OnlineMap::UsgsImagery, log));
  CHECK(st.drawingSettings.onlineMap == DrawingSettings::OnlineMap::UsgsImagery);
  REQUIRE(SetOnlineMap(st, DrawingSettings::OnlineMap::UsgsTopo, log));
  REQUIRE(DoUndo(st, log));
  CHECK(st.drawingSettings.onlineMap == DrawingSettings::OnlineMap::UsgsImagery);

  // Saved with the drawing (the ADR-044 trailer's JSON).
  AppCommandState back;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, SerializeGoSurveyJson(st), log));
  CHECK(back.drawingSettings.onlineMap == DrawingSettings::OnlineMap::UsgsImagery);

  // Per drawing tab.
  st.documents.resize(3);
  SaveDocumentToSnapshot(st, 1);
  st.drawingSettings.onlineMap = DrawingSettings::OnlineMap::Off;
  SaveDocumentToSnapshot(st, 2);
  RestoreDocumentFromSnapshot(st, 1);
  CHECK(st.drawingSettings.onlineMap == DrawingSettings::OnlineMap::UsgsImagery);
  RestoreDocumentFromSnapshot(st, 2);
  CHECK(st.drawingSettings.onlineMap == DrawingSettings::OnlineMap::Off);
  RestoreDocumentFromSnapshot(st, 1);

  // Remove Location turns the map off in the same undo step.
  REQUIRE(RemoveGeoLocation(st, log));
  CHECK(st.drawingSettings.onlineMap == DrawingSettings::OnlineMap::Off);
  REQUIRE(DoUndo(st, log));
  CHECK(st.drawingSettings.onlineMap == DrawingSettings::OnlineMap::UsgsImagery);
  CHECK(st.drawingSettings.Geolocated());

  // A drawing with no location cannot turn a map on; Map Off is always allowed.
  AppCommandState plain;
  CHECK_FALSE(SetOnlineMap(plain, DrawingSettings::OnlineMap::UsgsImagery, log));
  CHECK(SetOnlineMap(plain, DrawingSettings::OnlineMap::Off, log));

  // Storage names: unknown → Map Off; each map round-trips.
  CHECK(OnlineMapFromStorageName("bingAerial") == DrawingSettings::OnlineMap::Off);
  for (const OnlineMapInfo& m : kOnlineMaps)
    CHECK(OnlineMapFromStorageName(m.storageName) == m.map);
}

// ===================================================================================================
// REQ-364 (GitHub issue #583 increment 2): Capture Area keeps a piece of the map inside the drawing.
// ===================================================================================================

namespace {

/// Frames the controller until the running Capture Area ends (or a timeout).
void RunCapture(OnlineMapController& map, AppCommandState& st, const Camera& cam, std::vector<std::string>& log) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (st.active == AppCommandState::Kind::GeoCaptureArea && std::chrono::steady_clock::now() < until) {
    map.Update(st, true, cam, 800, 600, log);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

std::unique_ptr<OnlineMapController> Controller(FakeNetwork& net, int* live) {
  return std::make_unique<OnlineMapController>(std::make_unique<MapTileService>(std::filesystem::path(), net.Fetch(), 2),
                                               FakeTextures(live), std::chrono::milliseconds(0));
}

size_t CapturedTileCount(const AppCommandState& st) {
  size_t n = 0;
  for (const DrawingSettings::CapturedArea& a : st.drawingSettings.capturedAreas)
    n += a.tiles.size();
  return n;
}

}  // namespace

TEST_CASE("Capture Area keeps the visible tiles, and they draw offline with Map Off after a reopen (REQ-364)",
          "[req364]") {
  AppCommandState st = TexasDrawing();
  st.drawingSettings.onlineMap = DrawingSettings::OnlineMap::UsgsImagery;
  FakeNetwork net;
  int live = 0;
  std::vector<std::string> log;
  const Camera cam = ViewOn(kEFt - kOriginX, kNFt - kOriginY, 500.f);
  auto map = Controller(net, &live);
  Settle(*map, st, cam, log);
  const size_t shown = map->DrawList().size();
  REQUIRE(shown > 0);

  REQUIRE(StartCaptureMapArea(st, false, log));
  CHECK(st.geoCmdPhase == AppCommandState::GeoCmdPhase::Capturing);
  RunCapture(*map, st, cam, log);
  CHECK(st.active == AppCommandState::Kind::None);
  REQUIRE(st.drawingSettings.capturedAreas.size() == 1);
  const DrawingSettings::CapturedArea& area = st.drawingSettings.capturedAreas[0];
  CHECK(area.map == DrawingSettings::OnlineMap::UsgsImagery);
  CHECK(area.level == map->Level());
  CHECK(area.tiles.size() == shown);  // the visible area is the view's tiles
  for (const DrawingSettings::CapturedTile& t : area.tiles) {
    REQUIRE(t.image);
    CHECK(*t.image == TinyPng());  // kept as served
  }
  CHECK(log.back().find("kept in the drawing") != std::string::npos);

  // Save (the trailer's JSON) → reopen, Map Off, and no network: the captured area still draws, in
  // the place the live tiles had.
  AppCommandState back = TexasDrawing();
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, SerializeGoSurveyJson(st), log));
  REQUIRE(back.drawingSettings.capturedAreas == st.drawingSettings.capturedAreas);
  back.drawingSettings.onlineMap = DrawingSettings::OnlineMap::Off;
  FakeNetwork offline;
  offline.mode = 2;
  int live2 = 0;
  auto map2 = Controller(offline, &live2);
  Settle(*map2, back, cam, log);
  CHECK(map2->Service().FetchCount() == 0);  // decoded from the drawing, never fetched
  REQUIRE(map2->DrawList().size() == area.tiles.size());
  DrawingWgs84Frame frame;
  std::string why;
  REQUIRE(frame.Open(back, &why));
  std::vector<double> expect;
  const DrawingSettings::CapturedTile& t0 = area.tiles[0];
  REQUIRE(PlaceMapTile(frame, area.level, t0.x, t0.y, OnlineMapController::kCellsPerSide, expect, &why));
  bool found = false;
  for (const MapTileDraw& d : map2->DrawList())
    found = found || (d.xyuv && *d.xyuv == expect);
  CHECK(found);
  CHECK(map2->Drawing());  // so the attribution shows (REQ-363 item 9)
}

TEST_CASE("Pick Area keeps only the tiles under the picked rectangle (REQ-364)", "[req364]") {
  AppCommandState st = TexasDrawing();
  st.drawingSettings.onlineMap = DrawingSettings::OnlineMap::UsgsTopo;
  FakeNetwork net;
  int live = 0;
  std::vector<std::string> log;
  const Camera cam = ViewOn(kEFt - kOriginX, kNFt - kOriginY, 2000.f);
  auto map = Controller(net, &live);
  Settle(*map, st, cam, log);
  REQUIRE(map->Level() == 16);

  REQUIRE(StartCaptureMapArea(st, true, log));
  const double cx = kEFt - kOriginX, cy = kNFt - kOriginY;
  REQUIRE(SubmitGeoCommandPoint(st, cx - 20.0, cy - 20.0, log));
  CHECK_FALSE(SubmitGeoCommandPoint(st, cx - 20.0, cy + 50.0, log));  // not a rectangle: pick again
  REQUIRE(SubmitGeoCommandPoint(st, cx + 20.0, cy + 20.0, log));
  RunCapture(*map, st, cam, log);
  REQUIRE(st.drawingSettings.capturedAreas.size() == 1);
  // A 40 ft square lies in one level-16 tile, or at most four where it straddles tile edges.
  const DrawingSettings::CapturedArea& area = st.drawingSettings.capturedAreas[0];
  CHECK(area.level == 16);
  CHECK(area.tiles.size() >= 1);
  CHECK(area.tiles.size() <= 4);
  CHECK(area.tiles.size() < map->DrawList().size());  // fewer than the visible view
}

TEST_CASE("Capture Area refuses more than 256 tiles, and a failed fetch keeps nothing (REQ-364)", "[req364]") {
  AppCommandState st = TexasDrawing();
  st.drawingSettings.onlineMap = DrawingSettings::OnlineMap::UsgsImagery;
  FakeNetwork net;
  int live = 0;
  std::vector<std::string> log;
  const Camera cam = ViewOn(kEFt - kOriginX, kNFt - kOriginY, 500.f);
  auto map = Controller(net, &live);
  Settle(*map, st, cam, log);
  REQUIRE(map->Level() == 16);

  // 20 km square at level 16 (~530 m tiles): over a thousand tiles.
  REQUIRE(StartCaptureMapArea(st, true, log));
  const double cx = kEFt - kOriginX, cy = kNFt - kOriginY;
  REQUIRE(SubmitGeoCommandPoint(st, cx - 33000.0, cy - 33000.0, log));
  REQUIRE(SubmitGeoCommandPoint(st, cx + 33000.0, cy + 33000.0, log));
  RunCapture(*map, st, cam, log);
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK(st.drawingSettings.capturedAreas.empty());
  CHECK(log.back().find("Zoom in or pick a smaller area.") != std::string::npos);

  // A cold controller with the network down: the capture's first failed tile ends it, keeping nothing.
  FakeNetwork down;
  down.mode = 2;
  int live2 = 0;
  auto cold = Controller(down, &live2);
  cold->Update(st, true, cam, 800, 600, log);
  REQUIRE(StartCaptureMapArea(st, false, log));
  RunCapture(*cold, st, cam, log);
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK(st.drawingSettings.capturedAreas.empty());
  // The capture says why it kept nothing (the live map also says, once, that USGS is unreachable).
  bool said = false;
  for (const std::string& l : log)
    said = said || (l.find("nothing was kept") != std::string::npos && l.find("no route to host") != std::string::npos);
  CHECK(said);

  // Esc while capturing keeps nothing.
  FakeNetwork slow;
  slow.mode = 2;
  int live3 = 0;
  auto map3 = Controller(slow, &live3);
  REQUIRE(StartCaptureMapArea(st, false, log));
  CancelActiveCommand(st, log);
  map3->Update(st, true, cam, 800, 600, log);
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK(st.drawingSettings.capturedAreas.empty());
}

TEST_CASE("Captures undo one at a time; Remove Captured Areas is one undo step; Map Off refuses (REQ-364)",
          "[req364]") {
  AppCommandState st = TexasDrawing();
  std::vector<std::string> log;
  CHECK_FALSE(StartCaptureMapArea(st, false, log));  // Map Off: nothing to capture
  CHECK(log.back().find("choose a map first") != std::string::npos);
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK_FALSE(RemoveCapturedMapAreas(st, log));  // nothing to remove: refused, not a silent no-op

  st.drawingSettings.onlineMap = DrawingSettings::OnlineMap::UsgsImagery;
  FakeNetwork net;
  int live = 0;
  auto map = Controller(net, &live);
  const Camera cam = ViewOn(kEFt - kOriginX, kNFt - kOriginY, 500.f);
  Settle(*map, st, cam, log);
  for (int i = 0; i < 2; ++i) {
    REQUIRE(StartCaptureMapArea(st, false, log));
    RunCapture(*map, st, cam, log);
  }
  REQUIRE(st.drawingSettings.capturedAreas.size() == 2);
  REQUIRE(DoUndo(st, log));
  CHECK(st.drawingSettings.capturedAreas.size() == 1);

  REQUIRE(RemoveCapturedMapAreas(st, log));
  CHECK(st.drawingSettings.capturedAreas.empty());
  REQUIRE(DoUndo(st, log));
  CHECK(st.drawingSettings.capturedAreas.size() == 1);

  // Per drawing tab.
  st.documents.resize(3);
  SaveDocumentToSnapshot(st, 1);
  st.drawingSettings.capturedAreas.clear();
  SaveDocumentToSnapshot(st, 2);
  RestoreDocumentFromSnapshot(st, 1);
  CHECK(st.drawingSettings.capturedAreas.size() == 1);
  RestoreDocumentFromSnapshot(st, 2);
  CHECK(st.drawingSettings.capturedAreas.empty());
  CHECK(CapturedTileCount(st) == 0);
}

TEST_CASE("A damaged captured tile is dropped on open and said (REQ-364)", "[req364]") {
  AppCommandState st = TexasDrawing();
  DrawingSettings::CapturedArea a;
  a.map = DrawingSettings::OnlineMap::UsgsImagery;
  a.level = 16;
  a.tiles.push_back({1, 2, std::make_shared<const std::string>(TinyPng())});
  a.tiles.push_back({3, 4, std::make_shared<const std::string>(std::string("\x00\xff\x10", 3))});
  st.drawingSettings.capturedAreas.push_back(a);
  std::string text = SerializeGoSurveyJson(st);
  // Damage the second tile's base64 (an out-of-alphabet character).
  const size_t second = text.rfind("\"image\"");
  REQUIRE(second != std::string::npos);
  const size_t q = text.find('"', text.find(':', second) + 1);
  text[q + 1] = '*';
  AppCommandState back;
  std::vector<std::string> log;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, text, log));
  REQUIRE(back.drawingSettings.capturedAreas.size() == 1);
  CHECK(back.drawingSettings.capturedAreas[0].tiles.size() == 1);
  CHECK(*back.drawingSettings.capturedAreas[0].tiles[0].image == TinyPng());  // base64 round trip, binary-safe
  bool said = false;
  for (const std::string& l : log)
    said = said || l.find("damaged captured map tile") != std::string::npos;
  CHECK(said);
}
