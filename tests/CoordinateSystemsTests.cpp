// REQ-358 (GitHub issue #582 increment 2): the CS-MAP wrapper (src/geo/) against the shipped
// dictionary — the category catalogue, the Texas systems, code lookup, survey-grade grid <-> lat/long
// against an NGS datasheet, NAD27 -> NAD83 through the shipped NADCON grid — and the zone as drawing
// data: trailer persistence, per-tab isolation, a relabel that moves no coordinate.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "geo/CoordinateSystems.hpp"

namespace {

void LoadShippedDictionary() {
  if (!geo::DictionariesLoaded())
    REQUIRE(geo::LoadDictionaries(GOSURVEY_CSMAP_DICTIONARY_DIR));
}

bool Contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

double Dms(double d, double m, double s) { return d + m / 60.0 + s / 3600.0; }

constexpr double kUsFootMeters = 1200.0 / 3937.0;
constexpr double kMetersPerDegreeLat = 110852.0;  // at 30 deg N; only used to express a tolerance

// NGS datasheet, PID AG9976 "UNIV OF TEXAS TOWER SEC", TX/TRAVIS (retrieved 2026-09-29):
//   NAD 83(1993) POSITION- 30 17 10.51249(N) 097 44 21.71739(W)   ADJUSTED
//   SPC TX C - 3,071,595.000   949,528.007   MT
//   SPC TX C -10,077,391.26  3,115,243.14   sFT
// NAD 83(1993) is the Texas HARN, so the zone is HARN/TX.TX-C (meters) / HARN/TX.TX-CF (US feet).
const double kAg9976Lat = Dms(30, 17, 10.51249);
const double kAg9976Lon = -Dms(97, 44, 21.71739);
constexpr double kAg9976N = 3071595.000;
constexpr double kAg9976E = 949528.007;
constexpr double kAg9976NFt = 10077391.26;
constexpr double kAg9976EFt = 3115243.14;

}  // namespace

TEST_CASE("The shipped dictionary loads and a bad folder is reported (REQ-358)", "[req358]") {
  REQUIRE_FALSE(geo::LoadDictionaries("C:/no/such/folder"));
  CHECK_FALSE(geo::DictionariesLoaded());
  CHECK_FALSE(geo::DictionaryError().empty());
  CHECK(geo::Categories().empty());
  CHECK_FALSE(geo::GridToLatLong("HARN/TX.TX-C", kAg9976E, kAg9976N).ok);

  REQUIRE(geo::LoadDictionaries(GOSURVEY_CSMAP_DICTIONARY_DIR));
  CHECK(geo::DictionariesLoaded());
  CHECK(geo::DictionaryError().empty());
}

TEST_CASE("Categories are the dictionary's, Lat Longs first (REQ-358)", "[req358]") {
  LoadShippedDictionary();
  const std::vector<std::string>& cats = geo::Categories();
  // category.asc of CS-MAP r3078 holds 246 categories, "Lat Longs" first.
  REQUIRE(cats.size() == 246);
  CHECK(cats.front() == "Lat Longs");
  CHECK(Contains(cats, "Afghanistan"));
  CHECK(Contains(cats, "Zimbabwe"));
  CHECK(Contains(cats, "USA, Texas"));
  CHECK(Contains(cats, "Canada, Provinces"));
  CHECK(Contains(cats, "USA, Gulf of Mexico"));
  CHECK(Contains(geo::CoordinateSystemsIn("Lat Longs"), "LL84"));
}

TEST_CASE("USA, Texas lists every Texas state-plane system (REQ-358)", "[req358]") {
  LoadShippedDictionary();
  const std::vector<std::string> tx = geo::CoordinateSystemsIn("USA, Texas");
  for (const char* zone : {"N", "NC", "C", "SC", "S"}) {
    const std::string z = zone;
    CHECK(Contains(tx, "TX-" + z));  // NAD27: US Foot only, as published
    CHECK(Contains(tx, "TX83-" + z));
    CHECK(Contains(tx, "TX83-" + z + "F"));
    CHECK(Contains(tx, "HARN/TX.TX-" + z));
    CHECK(Contains(tx, "HARN/TX.TX-" + z + "F"));
    CHECK(Contains(tx, "NSRS07.TX-" + z));
    CHECK(Contains(tx, "NSRS07.TX-" + z + "F"));
    CHECK(Contains(tx, "NSRS11.TX-" + z));
    CHECK(Contains(tx, "NSRS11.TX-" + z + "F"));
  }
  CHECK(Contains(tx, "NAD83.Texas/Lambert"));
  CHECK(Contains(tx, "NAD83.Texas/EqArea"));
}

TEST_CASE("A code shows its details; typing a code finds its category; unknown is refused (REQ-358)",
          "[req358]") {
  LoadShippedDictionary();
  const auto harn = geo::FindCoordinateSystem("HARN/TX.TX-C");
  REQUIRE(harn.has_value());
  CHECK(harn->code == "HARN/TX.TX-C");
  CHECK(harn->description.find("Texas") != std::string::npos);
  CHECK(harn->description.find("Central") != std::string::npos);
  CHECK(harn->projection == "LM");
  CHECK(harn->datum == "HARN/TX");
  CHECK(harn->metersPerUnit == Catch::Approx(1.0));
  CHECK_FALSE(harn->geographic);

  const auto ft = geo::FindCoordinateSystem("TX83-CF");
  REQUIRE(ft.has_value());
  CHECK(ft->metersPerUnit == Catch::Approx(kUsFootMeters).epsilon(1e-12));
  const std::string cat = geo::CategoryOf("TX83-CF");
  REQUIRE_FALSE(cat.empty());
  CHECK(Contains(geo::CoordinateSystemsIn(cat), "TX83-CF"));

  CHECK_FALSE(geo::FindCoordinateSystem("NOSUCH").has_value());
  CHECK(geo::CategoryOf("NOSUCH").empty());

  const auto ll = geo::FindCoordinateSystem("LL84");
  REQUIRE(ll.has_value());
  CHECK(ll->geographic);
}

TEST_CASE("NGS AG9976 converts grid to lat/long and back at survey grade (REQ-358)", "[req358]") {
  LoadShippedDictionary();
  constexpr double kSecTol = 0.00001 / 3600.0;  // 0.00001 arc-second, in degrees
  constexpr double kFtTol = 0.001 * kUsFootMeters;

  // Datasheet grid → lat/long. The datasheet grid is rounded to 1 mm (≤ 0.5 mm ≈ 0.000016″ of
  // latitude), so this direction is checked to the datasheet's own grid precision.
  const geo::GeoResult ll = geo::GridToLatLong("HARN/TX.TX-C", kAg9976E, kAg9976N);
  REQUIRE(ll.ok);
  CHECK(std::abs(ll.y - kAg9976Lat) * kMetersPerDegreeLat <= 0.0005);
  CHECK(std::abs(ll.x - kAg9976Lon) * kMetersPerDegreeLat * std::cos(kAg9976Lat * 3.14159265358979 / 180.0) <=
        0.0005);

  // Datasheet lat/long → grid, meters and US feet: the published values (to their last digit).
  const geo::GeoResult m = geo::LatLongToGrid("HARN/TX.TX-C", kAg9976Lon, kAg9976Lat);
  REQUIRE(m.ok);
  CHECK(std::abs(m.x - kAg9976E) <= 0.0005);
  CHECK(std::abs(m.y - kAg9976N) <= 0.0005);
  const geo::GeoResult f = geo::LatLongToGrid("HARN/TX.TX-CF", kAg9976Lon, kAg9976Lat);
  REQUIRE(f.ok);
  CHECK(std::abs(f.x - kAg9976EFt) <= 0.005);
  CHECK(std::abs(f.y - kAg9976NFt) <= 0.005);

  // And back: lat/long → grid → lat/long returns the datasheet lat/long within 0.00001″, and
  // grid → lat/long → grid returns the grid within 0.001 ft.
  const geo::GeoResult back = geo::GridToLatLong("HARN/TX.TX-C", m.x, m.y);
  REQUIRE(back.ok);
  CHECK(std::abs(back.y - kAg9976Lat) <= kSecTol);
  CHECK(std::abs(back.x - kAg9976Lon) <= kSecTol);
  const geo::GeoResult again = geo::LatLongToGrid("HARN/TX.TX-C", ll.x, ll.y);
  REQUIRE(again.ok);
  CHECK(std::abs(again.x - kAg9976E) <= kFtTol);
  CHECK(std::abs(again.y - kAg9976N) <= kFtTol);
}

TEST_CASE("NAD27 converts to NAD83 through the shipped NADCON grid (REQ-358)", "[req358]") {
  LoadShippedDictionary();
  // NGS NCAT (geodesy.noaa.gov/api/ncat/llh, NADCON 5.0, retrieved 2026-09-29) for this NAD27 point:
  //   NAD27 30.2862535000, -97.7393659000 → NAD83(1986) 30.2864608099, -97.7396494472
  //   (NCAT's own sigma 0.13 m lat / 0.17 m lon).
  // CS-MAP applies NADCON 2 (conus.las/.los); NADCON's stated accuracy in CONUS is about 0.15 m,
  // checked here as 0.20 m to cover the NADCON 2 vs NADCON 5 model difference.
  const geo::GeoResult r = geo::ConvertLatLong("LL27", "LL83", -97.7393659, 30.2862535);
  INFO(r.error);
  REQUIRE(r.ok);
  const double dLatM = (r.y - 30.2864608099) * kMetersPerDegreeLat;
  const double dLonM = (r.x - -97.7396494472) * kMetersPerDegreeLat * std::cos(30.2864608 * 3.14159265358979 / 180.0);
  CHECK(std::abs(dLatM) <= 0.20);
  CHECK(std::abs(dLonM) <= 0.20);
  // The shift itself is tens of meters: a no-op path would fail the check above by far.
  CHECK(std::abs(r.y - 30.2862535) * kMetersPerDegreeLat > 20.0);
}

TEST_CASE("A zone's lat/long converts to WGS 84 along CS-MAP's datum path (REQ-358)", "[req358]") {
  LoadShippedDictionary();
  // AG9976 in the Texas HARN datum → WGS 84 (LL84), and back. HARN and WGS 84 differ by about a
  // meter here; the round trip must close.
  const geo::GeoResult w = geo::ConvertLatLong("HARN/TX.TX-C", "LL84", kAg9976Lon, kAg9976Lat);
  INFO(w.error);
  REQUIRE(w.ok);
  CHECK(std::abs(w.y - kAg9976Lat) * kMetersPerDegreeLat < 5.0);
  CHECK(std::abs(w.x - kAg9976Lon) * kMetersPerDegreeLat < 5.0);
  const geo::GeoResult back = geo::ConvertLatLong("LL84", "HARN/TX.TX-C", w.x, w.y);
  REQUIRE(back.ok);
  CHECK(std::abs(back.y - kAg9976Lat) * kMetersPerDegreeLat < 0.001);
  CHECK(std::abs(back.x - kAg9976Lon) * kMetersPerDegreeLat < 0.001);
}

TEST_CASE("A drawing point converts to grid and lat/long through the zone (REQ-358)", "[req358]") {
  LoadShippedDictionary();
  AppCommandState st;
  CHECK_FALSE(st.drawingSettings.Geolocated());
  CHECK_FALSE(DrawingPointToLatLong(st, 0.0, 0.0).ok);

  // A feet drawing (US survey foot) holding AG9976 in LOCAL coordinates about a world origin.
  st.drawingInsUnits = 2;
  st.drawingSettings.zoneCode = "HARN/TX.TX-CF";
  st.worldDocumentOriginX = 3115000.0;
  st.worldDocumentOriginY = 10077000.0;
  const geo::GeoResult grid = DrawingPointToGrid(st, kAg9976EFt - 3115000.0, kAg9976NFt - 10077000.0);
  REQUIRE(grid.ok);
  CHECK(grid.x == Catch::Approx(kAg9976EFt).margin(1e-6));
  CHECK(grid.y == Catch::Approx(kAg9976NFt).margin(1e-6));
  const geo::GeoResult ll = DrawingPointToLatLong(st, kAg9976EFt - 3115000.0, kAg9976NFt - 10077000.0);
  REQUIRE(ll.ok);
  CHECK(std::abs(ll.y - kAg9976Lat) * kMetersPerDegreeLat <= 0.003);  // sFT datasheet: 0.01 ft

  // The same drawing in a METER zone: feet → meters with the drawing's foot.
  st.drawingSettings.zoneCode = "HARN/TX.TX-C";
  const geo::GeoResult m = DrawingPointToGrid(st, kAg9976EFt - 3115000.0, kAg9976NFt - 10077000.0);
  REQUIRE(m.ok);
  CHECK(m.x == Catch::Approx(kAg9976EFt * kUsFootMeters).margin(1e-6));
  // International foot: 2 ppm different, about 6 ft at 3,115,000 ft.
  st.drawingSettings.footDefinition = DrawingSettings::FootDefinition::International;
  const geo::GeoResult mi = DrawingPointToGrid(st, kAg9976EFt - 3115000.0, kAg9976NFt - 10077000.0);
  REQUIRE(mi.ok);
  CHECK(mi.x == Catch::Approx(kAg9976EFt * 0.3048).margin(1e-6));

  // A zone the dictionary does not know: kept, still geolocated, conversion refused with a reason.
  st.drawingSettings.zoneCode = "NOSUCH";
  CHECK(st.drawingSettings.Geolocated());
  const geo::GeoResult unknown = DrawingPointToGrid(st, 0.0, 0.0);
  CHECK_FALSE(unknown.ok);
  CHECK(unknown.error.find("NOSUCH") != std::string::npos);
}

TEST_CASE("The zone is one undo step, moves no coordinate and survives the trailer (REQ-358)", "[req358]") {
  std::vector<std::string> log;
  AppCommandState st;
  st.userLinesFlat = {1.25, 2.5, 0.0, 10.75, 20.5, 3.0};
  st.userLineAttrs = {EntityAttributes{}};
  const std::vector<double> before = st.userLinesFlat;

  DrawingSettings s = st.drawingSettings;
  s.zoneCode = "HARN/TX.TX-CF";
  REQUIRE(ApplyDrawingSettings(st, st.drawingInsUnits, st.modelUnitsPerPlottedInch, s, log));
  CHECK(st.drawingSettings.zoneCode == "HARN/TX.TX-CF");
  CHECK(st.userLinesFlat == before);  // a relabel (REQ-358 item 3)

  // Trailer round trip, including a code this dictionary does not know (kept verbatim).
  for (const char* code : {"HARN/TX.TX-CF", "SOME.FUTURE-ZONE"}) {
    AppCommandState src;
    src.drawingSettings.zoneCode = code;
    AppCommandState back;
    REQUIRE(LoadGoSurveyFromJsonUtf8(back, SerializeGoSurveyJson(src), log));
    CHECK(back.drawingSettings.zoneCode == code);
    CHECK(back.drawingSettings.Geolocated());
  }

  REQUIRE(DoUndo(st, log));
  CHECK(st.drawingSettings.zoneCode.empty());
}

TEST_CASE("Two drawing tabs keep different zones (REQ-358)", "[req358]") {
  AppCommandState st;
  st.documents.resize(3);
  st.drawingSettings.zoneCode = "HARN/TX.TX-CF";
  SaveDocumentToSnapshot(st, 1);
  st.drawingSettings.zoneCode = "TX83-NF";
  SaveDocumentToSnapshot(st, 2);

  RestoreDocumentFromSnapshot(st, 1);
  CHECK(st.drawingSettings.zoneCode == "HARN/TX.TX-CF");
  RestoreDocumentFromSnapshot(st, 2);
  CHECK(st.drawingSettings.zoneCode == "TX83-NF");
}
