// Coverage for DXF record composition (REQ-052 / TASK-031).
//
// These exist because of a real shipped defect: the TEXT emitter wrote group 73 without the second
// AcDbText subclass marker, and AutoCAD responded by discarding the ENTIRE drawing
// ("Unexpected DXF group code: 73"). Every GoSurvey DXF containing single-line text was rejected.
// The tests below pin the subclass structure so that cannot recur silently.

#include "DxfEntityEmit.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

DxfTextRecord MakeRecord() {
  DxfTextRecord r;
  r.handleHex = "1F4";
  r.ownerHandleHex = "1D";
  r.layer = "SURVEY";
  r.linetype = "BYLAYER";
  r.colorAci = "7";
  r.lineweight370 = "-1";
  r.x = "10.000000";
  r.y = "23.750000";
  r.z = "0.0";
  r.height = "6.250000";
  r.text = "MON FOUND";
  r.rotationDeg = "0.000000";
  return r;
}

/// Index of the \p nth (1-based) pair with \p code, or -1.
int IndexOf(const std::vector<DxfOutPair>& p, int code, int nth = 1) {
  int seen = 0;
  for (size_t i = 0; i < p.size(); ++i) {
    if (p[i].code == code && ++seen == nth)
      return static_cast<int>(i);
  }
  return -1;
}

int IndexOfPair(const std::vector<DxfOutPair>& p, int code, const std::string& value, int nth = 1) {
  int seen = 0;
  for (size_t i = 0; i < p.size(); ++i) {
    if (p[i].code == code && p[i].value == value && ++seen == nth)
      return static_cast<int>(i);
  }
  return -1;
}

int CountPair(const std::vector<DxfOutPair>& p, int code, const std::string& value) {
  int n = 0;
  for (const auto& q : p)
    if (q.code == code && q.value == value)
      ++n;
  return n;
}

} // namespace

TEST_CASE("TEXT record declares AcDbText twice and puts group 73 in the second subclass",
          "[dxf][emit]") {
  std::vector<DxfOutPair> p;
  DxfAppendTextRecord(MakeRecord(), &p);

  SECTION("the record opens as a TEXT entity") {
    REQUIRE(p.size() > 2);
    REQUIRE(p[0].code == 0);
    REQUIRE(p[0].value == "TEXT");
  }

  SECTION("AcDbText is declared exactly twice") {
    // The regression: one marker is what AutoCAD rejected.
    REQUIRE(CountPair(p, 100, "AcDbText") == 2);
    REQUIRE(CountPair(p, 100, "AcDbEntity") == 1);
  }

  SECTION("group 73 follows the SECOND AcDbText marker") {
    const int secondMarker = IndexOfPair(p, 100, "AcDbText", 2);
    const int g73 = IndexOf(p, 73);
    REQUIRE(secondMarker >= 0);
    REQUIRE(g73 >= 0);
    REQUIRE(g73 > secondMarker);
  }

  SECTION("subclass markers are ordered AcDbEntity then AcDbText then AcDbText") {
    const int ent = IndexOfPair(p, 100, "AcDbEntity");
    const int t1 = IndexOfPair(p, 100, "AcDbText", 1);
    const int t2 = IndexOfPair(p, 100, "AcDbText", 2);
    REQUIRE(ent < t1);
    REQUIRE(t1 < t2);
  }

  SECTION("group 7 (text style) is an AcDbText property, not an AcDbEntity one") {
    // It sat in the AcDbEntity block before the fix.
    const int t1 = IndexOfPair(p, 100, "AcDbText", 1);
    const int t2 = IndexOfPair(p, 100, "AcDbText", 2);
    const int g7 = IndexOf(p, 7);
    REQUIRE(g7 > t1);
    REQUIRE(g7 < t2);
  }

  SECTION("entity properties sit inside the AcDbEntity block") {
    const int ent = IndexOfPair(p, 100, "AcDbEntity");
    const int t1 = IndexOfPair(p, 100, "AcDbText", 1);
    for (const int code : {8, 6, 62, 370}) {
      const int at = IndexOf(p, code);
      REQUIRE(at > ent);
      REQUIRE(at < t1);
    }
  }

  SECTION("geometry precedes the text value, which precedes rotation") {
    REQUIRE(IndexOf(p, 10) < IndexOf(p, 40));
    REQUIRE(IndexOf(p, 40) < IndexOf(p, 1));
    REQUIRE(IndexOf(p, 1) < IndexOf(p, 50));
  }

  SECTION("handle and owner precede the first subclass marker") {
    REQUIRE(IndexOf(p, 5) < IndexOfPair(p, 100, "AcDbEntity"));
    REQUIRE(IndexOf(p, 330) < IndexOfPair(p, 100, "AcDbEntity"));
  }
}

TEST_CASE("TEXT record carries the values it was given", "[dxf][emit]") {
  const DxfTextRecord r = MakeRecord();
  std::vector<DxfOutPair> p;
  DxfAppendTextRecord(r, &p);

  auto valueOf = [&p](int code) -> std::string {
    const int i = IndexOf(p, code);
    return i < 0 ? std::string("<missing>") : p[static_cast<size_t>(i)].value;
  };

  REQUIRE(valueOf(5) == "1F4");
  REQUIRE(valueOf(330) == "1D");
  REQUIRE(valueOf(8) == "SURVEY");
  REQUIRE(valueOf(6) == "BYLAYER");
  REQUIRE(valueOf(62) == "7");
  REQUIRE(valueOf(370) == "-1");
  REQUIRE(valueOf(10) == "10.000000");
  REQUIRE(valueOf(20) == "23.750000");
  REQUIRE(valueOf(40) == "6.250000");
  REQUIRE(valueOf(1) == "MON FOUND");
  REQUIRE(valueOf(7) == "Standard");
  REQUIRE(valueOf(230) == "1.0"); // extrusion defaults to +Z
}

TEST_CASE("group 440 is emitted only when the entity is actually transparent", "[dxf][emit]") {
  SECTION("opaque entities omit 440 entirely") {
    // An explicit "fully opaque" 440 is NOT the same as inheriting ByLayer, so it must be absent.
    DxfTextRecord r = MakeRecord();
    r.hasTransparency = false;
    std::vector<DxfOutPair> p;
    DxfAppendTextRecord(r, &p);
    REQUIRE(IndexOf(p, 440) == -1);
  }

  SECTION("a transparent entity emits 440 inside the AcDbEntity block") {
    DxfTextRecord r = MakeRecord();
    r.hasTransparency = true;
    r.transparency440 = "33554559";
    std::vector<DxfOutPair> p;
    DxfAppendTextRecord(r, &p);
    const int g440 = IndexOf(p, 440);
    REQUIRE(g440 > IndexOfPair(p, 100, "AcDbEntity"));
    REQUIRE(g440 < IndexOfPair(p, 100, "AcDbText", 1));
    REQUIRE(p[static_cast<size_t>(g440)].value == "33554559");
  }
}

TEST_CASE("DxfTransparency440 packs alpha into AutoCAD's 0x02000000 form", "[dxf][emit]") {
  int packed = -1;

  SECTION("opaque returns false and writes nothing") {
    REQUIRE_FALSE(DxfTransparency440(0.f, &packed));
    REQUIRE(packed == -1);
    REQUIRE_FALSE(DxfTransparency440(1.e-6f, &packed)); // below the epsilon
  }

  SECTION("half transparency packs to 0x02000000 | 128") {
    REQUIRE(DxfTransparency440(0.5f, &packed));
    REQUIRE(packed == static_cast<int>(0x02000000u | 128u));
  }

  SECTION("full transparency packs to 0x02000000 | 255") {
    REQUIRE(DxfTransparency440(1.f, &packed));
    REQUIRE(packed == static_cast<int>(0x02000000u | 255u));
  }

  SECTION("out-of-range input is clamped, not wrapped") {
    REQUIRE(DxfTransparency440(5.f, &packed));
    REQUIRE(packed == static_cast<int>(0x02000000u | 255u));
  }

  SECTION("a null out-pointer is tolerated") {
    REQUIRE(DxfTransparency440(0.5f, nullptr));
  }
}

TEST_CASE("DxfAppendTextRecord tolerates a null destination", "[dxf][emit]") {
  DxfAppendTextRecord(MakeRecord(), nullptr); // must not crash
  SUCCEED();
}

// ---------------------------------------------------------------------------------------------
// LWPOLYLINE (REQ-052 / REQ-053). RECT stores a rectangle as a 4-vertex closed polyline, so this
// record is the only thing standing between a drawn rectangle and a DXF that contains it. Before
// REQ-053 the exporter had no LWPOLYLINE branch at all and dropped every polyline silently.
// ---------------------------------------------------------------------------------------------

namespace {

DxfLwPolylineRecord MakeRectRecord() {
  DxfLwPolylineRecord r;
  r.handleHex = "2A0";
  r.ownerHandleHex = "1D";
  r.layer = "PARCEL";
  r.linetype = "BYLAYER";
  r.colorAci = "7";
  r.lineweight370 = "-1";
  r.closed = true;
  r.vertices = {{"0.0", "0.0"}, {"100.0", "0.0"}, {"100.0", "50.0"}, {"0.0", "50.0"}};
  return r;
}

} // namespace

TEST_CASE("LWPOLYLINE record carries the vertex count, the closed flag, and every vertex", "[dxf][emit]") {
  std::vector<DxfOutPair> p;
  DxfAppendLwPolylineRecord(MakeRectRecord(), &p);

  SECTION("the record opens as an LWPOLYLINE entity") {
    REQUIRE(p.size() > 2);
    REQUIRE(p[0].code == 0);
    REQUIRE(p[0].value == "LWPOLYLINE");
  }

  SECTION("group 90 states the true vertex count") {
    const int i90 = IndexOf(p, 90);
    REQUIRE(i90 >= 0);
    REQUIRE(p[static_cast<size_t>(i90)].value == "4");
  }

  SECTION("group 90 precedes group 70 and both precede the first vertex") {
    // AutoCAD sizes the vertex array from 90 before it consumes any 10/20 pair.
    const int i90 = IndexOf(p, 90);
    const int i70 = IndexOf(p, 70);
    const int i10 = IndexOf(p, 10);
    REQUIRE(i90 >= 0);
    REQUIRE(i70 > i90);
    REQUIRE(i10 > i70);
  }

  SECTION("a rectangle is closed (group 70 bit 1)") {
    const int i70 = IndexOf(p, 70);
    REQUIRE(i70 >= 0);
    REQUIRE(p[static_cast<size_t>(i70)].value == "1");
  }

  SECTION("every vertex is emitted, in order, as a 10/20 pair") {
    REQUIRE(CountPair(p, 10, "0.0") == 2);      // two corners share x = 0
    REQUIRE(CountPair(p, 10, "100.0") == 2);
    REQUIRE(IndexOfPair(p, 10, "0.0", 1) < IndexOfPair(p, 10, "100.0", 1));
    const int firstY = IndexOf(p, 20);
    REQUIRE(firstY == IndexOf(p, 10) + 1);      // each 20 follows its own 10
  }

  SECTION("the AcDbPolyline subclass marker is present exactly once") {
    REQUIRE(CountPair(p, 100, "AcDbPolyline") == 1);
    REQUIRE(CountPair(p, 100, "AcDbEntity") == 1);
  }

  SECTION("the extrusion vector closes the record") {
    REQUIRE(p.back().code == 230);
    REQUIRE(p.back().value == "1.0");
  }
}

TEST_CASE("An open polyline writes group 70 as 0", "[dxf][emit]") {
  DxfLwPolylineRecord r = MakeRectRecord();
  r.closed = false;
  std::vector<DxfOutPair> p;
  DxfAppendLwPolylineRecord(r, &p);
  const int i70 = IndexOf(p, 70);
  REQUIRE(i70 >= 0);
  REQUIRE(p[static_cast<size_t>(i70)].value == "0");
}

// A 90-of-zero LWPOLYLINE makes AutoCAD reject the whole ENTITIES section, so an empty record must
// produce no output at all rather than a malformed one.
TEST_CASE("A vertex-less LWPOLYLINE emits nothing rather than a zero-vertex record", "[dxf][emit]") {
  DxfLwPolylineRecord r = MakeRectRecord();
  r.vertices.clear();
  std::vector<DxfOutPair> p;
  DxfAppendLwPolylineRecord(r, &p);
  REQUIRE(p.empty());
}

// Transparency is ByLayer unless the entity actually carries one — an explicit opaque 440 is not the
// same thing, so the group must be absent when the flag is clear (the TEXT record's rule).
TEST_CASE("LWPOLYLINE omits group 440 unless the entity carries transparency", "[dxf][emit]") {
  std::vector<DxfOutPair> opaque;
  DxfAppendLwPolylineRecord(MakeRectRecord(), &opaque);
  REQUIRE(IndexOf(opaque, 440) == -1);

  DxfLwPolylineRecord r = MakeRectRecord();
  r.hasTransparency = true;
  r.transparency440 = "33554559";
  std::vector<DxfOutPair> translucent;
  DxfAppendLwPolylineRecord(r, &translucent);
  const int i440 = IndexOf(translucent, 440);
  REQUIRE(i440 >= 0);
  REQUIRE(translucent[static_cast<size_t>(i440)].value == "33554559");
  // 440 belongs to AcDbEntity, so it must come before the AcDbPolyline marker.
  REQUIRE(i440 < IndexOfPair(translucent, 100, "AcDbPolyline"));
}

// ---------------------------------------------------------------------------
// Elevation (REQ-057 / ADR-025) — Z must reach the wire, and land in the right subclass.
// ---------------------------------------------------------------------------

// TEXT's group 30 is the insertion elevation. Before REQ-057 the exporter hard-coded "0.0", so an
// elevated text round-tripped back flat; this pins that the record's z actually reaches group 30.
TEST_CASE("TEXT emits its elevation as group 30 inside AcDbText", "[dxf][emit][z]") {
  DxfTextRecord r = MakeRecord();
  r.z = "125.75";
  std::vector<DxfOutPair> p;
  DxfAppendTextRecord(r, &p);

  const int i30 = IndexOf(p, 30);
  REQUIRE(i30 >= 0);
  REQUIRE(p[static_cast<size_t>(i30)].value == "125.75");
  // Group 30 is part of the geometry, so it follows the first AcDbText marker — not AcDbEntity.
  REQUIRE(i30 > IndexOfPair(p, 100, "AcDbText", 1));
}

// A flat drawing must still say so explicitly: group 30 is required, not optional, so a reader
// never has to guess an elevation.
TEST_CASE("TEXT still emits group 30 when the elevation is zero", "[dxf][emit][z]") {
  std::vector<DxfOutPair> p;
  DxfAppendTextRecord(MakeRecord(), &p);
  const int i30 = IndexOf(p, 30);
  REQUIRE(i30 >= 0);
  REQUIRE(p[static_cast<size_t>(i30)].value == "0.0");
}

// LWPOLYLINE carries ONE elevation for every vertex (group 38) — there is no per-vertex Z in this
// entity. This pins where 38 sits: after the vertex count and closed flag, before the first vertex,
// and inside AcDbPolyline. A 38 emitted in the AcDbEntity block is rejected by AutoCAD.
TEST_CASE("LWPOLYLINE emits group 38 before the vertices, inside AcDbPolyline", "[dxf][emit][z]") {
  DxfLwPolylineRecord r = MakeRectRecord();
  r.elevation38 = "42.5";
  std::vector<DxfOutPair> p;
  DxfAppendLwPolylineRecord(r, &p);

  const int i38 = IndexOf(p, 38);
  const int iPoly = IndexOfPair(p, 100, "AcDbPolyline");
  const int i90 = IndexOf(p, 90);
  const int i70 = IndexOf(p, 70);
  const int firstVertex = IndexOf(p, 10);

  REQUIRE(i38 >= 0);
  REQUIRE(p[static_cast<size_t>(i38)].value == "42.5");
  REQUIRE(i38 > iPoly);
  REQUIRE(i38 > i90);
  REQUIRE(i38 > i70);
  REQUIRE(i38 < firstVertex);
  // Exactly one elevation for the whole polyline, however many vertices it has.
  REQUIRE(IndexOf(p, 38, 2) == -1);
}
