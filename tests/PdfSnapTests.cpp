// REQ-391 clause 5: snapping to the ends and corners of a PDF page's own line work - the nearest-point
// index, and the points read off real pages (plain paths, a scaled path, a rectangle, a form XObject, a
// rotated page and a page box that does not start at zero).

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <catch2/catch_test_macros.hpp>

#include "pdf/PdfAnnotate.hpp"
#include "pdf/PdfDocument.hpp"
#include "pdf/PdfSnap.hpp"
#include "pdf/PdfViewerCore.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace pdfview;

namespace {

std::filesystem::path WriteBytes(const char* name, const std::string& bytes) {
  const auto p = std::filesystem::temp_directory_path() / name;
  std::ofstream f(p, std::ios::binary);
  f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return p;
}

// One page whose /MediaBox and other keys are `pageKeys`, with content `content`; `extra` are more objects
// (numbered from 5) such as a form XObject.
std::string BuildPdf(const std::string& pageKeys, const std::string& content, const std::vector<std::string>& extra = {}) {
  std::vector<std::string> objs = {
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R]/Count 1>>",
      "<</Type/Page/Parent 2 0 R/Contents 4 0 R" + pageKeys + ">>",
      "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream",
  };
  for (const std::string& e : extra)
    objs.push_back(e);
  std::string out = "%PDF-1.4\n";
  std::vector<size_t> off;
  for (size_t i = 0; i < objs.size(); ++i) {
    off.push_back(out.size());
    out += std::to_string(i + 1) + " 0 obj\n" + objs[i] + "\nendobj\n";
  }
  const size_t xref = out.size();
  out += "xref\n0 " + std::to_string(objs.size() + 1) + "\n0000000000 65535 f \n";
  for (size_t o : off) {
    char line[32];
    std::snprintf(line, sizeof(line), "%010zu 00000 n \n", o);
    out += line;
  }
  out += "trailer\n<</Size " + std::to_string(objs.size() + 1) + "/Root 1 0 R>>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
  return out;
}

bool Has(const std::vector<SnapPoint>& pts, float x, float y, float tol = 0.3f) {
  for (const auto& p : pts)
    if (std::fabs(p.x - x) <= tol && std::fabs(p.y - y) <= tol)
      return true;
  return false;
}

std::vector<SnapPoint> Snap(const std::filesystem::path& p, bool* ok = nullptr) {
  auto r = PdfDocument::Open(p);
  REQUIRE(r.doc != nullptr);
  std::vector<SnapPoint> pts;
  const bool good = r.doc->SnapPoints(0, pts, [] { return false; });
  if (ok != nullptr)
    *ok = good;
  r.doc.reset();
  return pts;
}

} // namespace

TEST_CASE("text boxes: the box of a text object is read in the viewer's page coordinates", "[issue732][req393]") {
  // MakeSyntheticPdf puts "Page 1" at (40, h - 40) in 18 pt Helvetica.
  const auto path = WriteBytes("gosurvey_textbox.pdf", MakeSyntheticPdf(1, 5, 0));
  auto r = PdfDocument::Open(path);
  REQUIRE(r.doc != nullptr);
  std::vector<ObjBox> boxes;
  REQUIRE(r.doc->TextBoxes(0, boxes, [] { return false; }));
  REQUIRE(boxes.size() == 1);
  CHECK(std::fabs(boxes[0].x0 - 40.f) < 1.5f);
  CHECK(boxes[0].x1 - boxes[0].x0 > 30.f);
  CHECK(boxes[0].y0 > 792.f - 60.f);
  CHECK(boxes[0].y1 < 792.f - 20.f);
}

TEST_CASE("SnapIndex finds the nearest point within the radius", "[pdfsnap][req391][issue732]") {
  SnapIndex idx;
  idx.Build({{10.f, 10.f, 1.f}, {50.f, 50.f, 1.f}, {52.f, 50.f, 1.f}, {-30.f, -40.f, 1.f}, {1000.f, 1000.f, 1.f}});
  SnapIndex::Pt out;
  REQUIRE(idx.Nearest(11.f, 9.f, 5.f, out));
  CHECK(out == SnapIndex::Pt(10.f, 10.f));
  REQUIRE(idx.Nearest(51.7f, 50.f, 5.f, out)); // the nearer of two
  CHECK(out == SnapIndex::Pt(52.f, 50.f));
  REQUIRE(idx.Nearest(-31.f, -41.f, 3.f, out)); // negative coordinates
  CHECK(out == SnapIndex::Pt(-30.f, -40.f));
  CHECK_FALSE(idx.Nearest(30.f, 30.f, 5.f, out)); // nothing that close
  CHECK_FALSE(idx.Nearest(10.f, 10.f, 0.f, out));
  CHECK(idx.Size() == 5);
  SnapIndex empty;
  CHECK_FALSE(empty.Nearest(0.f, 0.f, 10.f, out));
}

TEST_CASE("an end of a real line beats a stray vertex that is a little nearer", "[pdfsnap][req391][issue732]") {
  SnapIndex idx;
  // A stray vertex (weight 1) 1.0 from the pointer and a line end (weight 3) 3.5 away; radius 10.
  idx.Build({{101.f, 100.f, 1.f}, {96.5f, 100.f, 3.f}});
  SnapIndex::Pt out;
  REQUIRE(idx.Nearest(100.f, 100.f, 10.f, out));
  CHECK(out == SnapIndex::Pt(96.5f, 100.f)); // two steps of weight = 3.0 of extra closeness: 3.5 - 3.0 < 1.0
  // But not one that is much farther: the stray vertex wins when the end is 9 away.
  SnapIndex far;
  far.Build({{101.f, 100.f, 1.f}, {91.f, 100.f, 3.f}});
  REQUIRE(far.Nearest(100.f, 100.f, 10.f, out));
  CHECK(out == SnapIndex::Pt(101.f, 100.f));
  // Equal weights: the nearest.
  SnapIndex same;
  same.Build({{103.f, 100.f, 3.f}, {98.f, 100.f, 3.f}});
  REQUIRE(same.Nearest(100.f, 100.f, 10.f, out));
  CHECK(out == SnapIndex::Pt(98.f, 100.f));
  // Merging: a cluster a few pixels wide offers only its strongest point.
  SnapIndex cluster;
  cluster.Build({{100.f, 100.f, 1.f}, {102.f, 100.f, 2.f}, {140.f, 100.f, 1.f}});
  REQUIRE(cluster.Nearest(100.f, 100.f, 10.f, out));
  CHECK(out == SnapIndex::Pt(100.f, 100.f));             // without merging the nearest wins
  REQUIRE(cluster.Nearest(100.f, 100.f, 10.f, out, 4.f)); // with a 4-point merge radius the stronger one 2 away wins
  CHECK(out == SnapIndex::Pt(102.f, 100.f));
  REQUIRE(cluster.Nearest(100.f, 100.f, 10.f, out, 1.f)); // zoomed in (1 point on screen): both are on offer again
  CHECK(out == SnapIndex::Pt(100.f, 100.f));
  // A far, unrelated weak point is not touched by the merge.
  REQUIRE(cluster.Nearest(140.f, 100.f, 10.f, out, 4.f));
  CHECK(out == SnapIndex::Pt(140.f, 100.f));
  // Outside the radius nothing is offered, whatever its weight.
  SnapIndex none;
  none.Build({{150.f, 100.f, 3.f}});
  CHECK_FALSE(none.Nearest(100.f, 100.f, 10.f, out));
}

TEST_CASE("the tiny steps of a curve and specks are not snap points; real corners are", "[pdfsnap][req391][issue732]") {
  // A "circle" of radius 5 drawn as 48 straight steps (about 0.65 pt each), a 1 pt speck, a 20 x 10 rectangle
  // and an L whose short leg is 1.8 pt.
  std::string content = "0 0 0 RG\n";
  char buf[96];
  for (int i = 0; i <= 48; ++i) {
    const double a = 6.283185307 * i / 48.0;
    std::snprintf(buf, sizeof(buf), "%.4f %.4f %s\n", 100.0 + 5.0 * std::cos(a), 100.0 + 5.0 * std::sin(a), i == 0 ? "m" : "l");
    content += buf;
  }
  content += "S\n";
  content += "200 200 m 200.5 200.5 l S\n";                   // a speck: under 1.5 pt long
  content += "50 150 20 10 re S\n";                            // corners (50,150) (70,150) (70,160) (50,160)
  content += "250 100 m 250 101.8 l 270 101.8 l S\n";          // an L: its middle vertex is a real corner (a 20 pt leg)
  const auto p = WriteBytes("gs_snap_noise.pdf", BuildPdf("/MediaBox[0 0 400 300]", content));
  const auto pts = Snap(p);
  // The 48 vertices of the circle collapse to its centre and four quadrant points.
  int nearCircle = 0;
  float centreWeight = 0.f;
  for (const auto& q : pts)
    if (std::hypot(q.x - 100.f, q.y - 100.f) < 6.f) {
      ++nearCircle;
      if (std::hypot(q.x - 100.f, q.y - 100.f) < 0.3f)
        centreWeight = q.weight;
    }
  CHECK(nearCircle <= 5);
  CHECK(Has(pts, 100, 100));   // the centre ...
  CHECK(centreWeight == 4.f);  // ... is the strongest point
  CHECK(Has(pts, 105, 100));   // a quadrant
  CHECK(Has(pts, 100, 105));
  CHECK(Has(pts, 95, 100));
  CHECK(Has(pts, 100, 95));
  CHECK_FALSE(Has(pts, 200, 200)); // the speck gives nothing
  for (const auto& corner : {std::pair<float, float>{50, 150}, {70, 150}, {70, 160}, {50, 160}}) {
    INFO("corner " << corner.first << "," << corner.second);
    REQUIRE(Has(pts, corner.first, corner.second));
    for (const auto& q : pts)
      if (std::fabs(q.x - corner.first) < 0.3f && std::fabs(q.y - corner.second) < 0.3f)
        CHECK(q.weight >= 3.f); // the corner of a real line is the strongest kind
  }
  CHECK(Has(pts, 250, 100));
  CHECK(Has(pts, 250, 101.8f)); // the L's bend is kept: one side is a 20 pt line
  CHECK(Has(pts, 270, 101.8f));
  std::filesystem::remove(p);

}

TEST_CASE("a circle drawn as four curves snaps at its centre and quadrants, not its control points", "[pdfsnap][req391][issue732]") {
  // Radius 5 at (100, 100); each quarter is a Bezier with the usual 0.5523 handles (2.76).
  const std::string content =
      "105 100 m 105 102.76 102.76 105 100 105 c 97.24 105 95 102.76 95 100 c 95 97.24 97.24 95 100 95 c 102.76 95 105 97.24 105 100 c S\n"
      "300 300 m 320 300 l 320 310 l 300 310 l h S\n"; // a plain rectangle keeps all four corners
  const auto p = WriteBytes("gs_snap_bezcircle.pdf", BuildPdf("/MediaBox[0 0 400 400]", content));
  const auto pts = Snap(p);
  CHECK(Has(pts, 100, 100));
  CHECK(Has(pts, 105, 100));
  CHECK(Has(pts, 100, 105));
  CHECK(Has(pts, 95, 100));
  CHECK(Has(pts, 100, 95));
  CHECK_FALSE(Has(pts, 105, 102.76f)); // a handle, not a point on the curve
  CHECK_FALSE(Has(pts, 102.76f, 105));
  int nearCircle = 0;
  for (const auto& q : pts)
    if (std::hypot(q.x - 100.f, q.y - 100.f) < 7.f)
      ++nearCircle;
  CHECK(nearCircle == 5);
  CHECK(Has(pts, 300, 300)); // the square is not mistaken for a circle
  CHECK(Has(pts, 320, 300));
  CHECK(Has(pts, 320, 310));
  CHECK(Has(pts, 300, 310));
  std::filesystem::remove(p);
}

TEST_CASE("snap points are the ends and corners of the page's own lines", "[pdfsnap][req391][issue732]") {
  // MakeSyntheticPdf draws lines 20,20 -> 33,20 and 57,73 -> 104,49 on page 1.
  const auto syn = WriteBytes("gs_snap_syn.pdf", MakeSyntheticPdf(1, 3, 0));
  bool ok = false;
  auto pts = Snap(syn, &ok);
  CHECK(ok);
  CHECK(Has(pts, 20, 20));
  CHECK(Has(pts, 33, 20));
  CHECK(Has(pts, 57, 73));
  CHECK(Has(pts, 104, 49));
  CHECK_FALSE(Has(pts, 300, 300));
  std::filesystem::remove(syn);

  // A scaled and moved path: 2x and +10,+10.
  const auto cm = WriteBytes("gs_snap_cm.pdf", BuildPdf("/MediaBox[0 0 200 200]", "q 2 0 0 2 10 10 cm 5 5 m 15 5 l S Q"));
  pts = Snap(cm);
  CHECK(Has(pts, 20, 20));
  CHECK(Has(pts, 40, 20));
  CHECK(pts.size() == 2);
  std::filesystem::remove(cm);

  // A rectangle gives its four corners.
  const auto re = WriteBytes("gs_snap_re.pdf", BuildPdf("/MediaBox[0 0 300 300]", "50 60 100 40 re S"));
  pts = Snap(re);
  CHECK(Has(pts, 50, 60));
  CHECK(Has(pts, 150, 60));
  CHECK(Has(pts, 150, 100));
  CHECK(Has(pts, 50, 100));
  std::filesystem::remove(re);

  // A curve gives its end point, not its control points.
  const auto cv = WriteBytes("gs_snap_curve.pdf", BuildPdf("/MediaBox[0 0 300 300]", "10 10 m 20 90 80 90 90 10 c S"));
  pts = Snap(cv);
  CHECK(Has(pts, 10, 10));
  CHECK(Has(pts, 90, 10));
  CHECK_FALSE(Has(pts, 20, 90));
  CHECK_FALSE(Has(pts, 80, 90));
  std::filesystem::remove(cv);
}

TEST_CASE("snap points inside a form XObject follow its matrix", "[pdfsnap][req391][issue732]") {
  const std::string form = "<</Type/XObject/Subtype/Form/BBox[0 0 50 50]/Matrix[1 0 0 1 100 100]/Length 22>>\nstream\n0 0 m 10 0 l S\nendstream";
  const auto p = WriteBytes("gs_snap_form.pdf", BuildPdf("/MediaBox[0 0 300 300]/Resources<</XObject<</F 5 0 R>>>>", "/F Do", {form}));
  const auto pts = Snap(p);
  CHECK(Has(pts, 100, 100));
  CHECK(Has(pts, 110, 100));
  std::filesystem::remove(p);
}

TEST_CASE("snap points land on the drawn line on a rotated page and an offset page box", "[pdfsnap][req391][issue732]") {
  // Page box that starts at (100, 100): the viewer measures from the page's own corner.
  const auto off = WriteBytes("gs_snap_off.pdf", BuildPdf("/MediaBox[100 100 300 200]", "120 120 m 180 120 l S"));
  auto pts = Snap(off);
  CHECK(Has(pts, 20, 20));
  CHECK(Has(pts, 80, 20));
  std::filesystem::remove(off);

  // A page turned 90 degrees: whatever PDFium shows, the snap point must be on the drawn pixels.
  const auto rot = WriteBytes("gs_snap_rot.pdf", BuildPdf("/MediaBox[0 0 200 100]/Rotate 90", "4 w 10 10 m 60 10 l S"));
  auto r = PdfDocument::Open(rot);
  REQUIRE(r.doc != nullptr);
  const PageSize sz = r.doc->Sizes()[0];
  CHECK(sz.wPt == 100.f); // displayed turned: 100 wide, 200 tall
  CHECK(sz.hPt == 200.f);
  std::vector<SnapPoint> sp;
  REQUIRE(r.doc->SnapPoints(0, sp, [] { return false; }));
  REQUIRE(sp.size() == 2);
  Bitmap bm;
  REQUIRE(r.doc->RenderPage(0, 100, 200, bm, [] { return false; }));
  for (const auto& q : sp) {
    const int px = static_cast<int>(q.x), py = static_cast<int>(sz.hPt - q.y);
    // Within a couple of pixels of the point there is dark ink (the 4 pt stroke's end).
    bool ink = false;
    for (int dy = -2; dy <= 2; ++dy)
      for (int dx = -2; dx <= 2; ++dx) {
        const int x = px + dx, y = py + dy;
        if (x >= 0 && y >= 0 && x < bm.w && y < bm.h && bm.bgra[(static_cast<size_t>(y) * bm.w + x) * 4u] < 100)
          ink = true;
      }
    INFO("snap point " << q.x << "," << q.y);
    CHECK(ink);
  }
  r.doc.reset();
  std::filesystem::remove(rot);
}

TEST_CASE("marks saved on a rotated page or an offset page box land where they were drawn", "[pdfsnap][req388][issue732]") {
  struct Case {
    const char* name;
    std::string keys;
    int w, h; // displayed size in points
  };
  const Case cases[] = {
      {"offset", "/MediaBox[100 100 300 200]", 200, 100},
      {"rotated", "/MediaBox[0 0 200 100]/Rotate 90", 100, 200},
      {"rotated270", "/MediaBox[0 0 200 100]/Rotate 270", 100, 200},
      {"plain", "/MediaBox[0 0 200 100]", 200, 100},
  };
  for (const Case& cs : cases) {
    INFO(cs.name);
    const auto src = WriteBytes("gs_map_src.pdf", BuildPdf(cs.keys, "0 0 m 1 1 l S"));
    const auto dst = std::filesystem::temp_directory_path() / "gs_map_out.pdf";
    std::filesystem::remove(dst);
    Annot a;
    a.kind = Annot::Kind::Rect;
    a.page = 0;
    a.x0 = 10;
    a.y0 = 10;
    a.x1 = 40;
    a.y1 = 50;
    a.fill = true;
    a.color = 0x0000FF;
    a.thickness = 1.f;
    const std::string err = SaveAnnotated(src, {a}, dst);
    INFO(err);
    REQUIRE(err.empty());
    auto d = PdfDocument::Open(dst);
    REQUIRE(d.doc != nullptr);
    Bitmap bm;
    REQUIRE(d.doc->RenderPage(0, cs.w, cs.h, bm, [] { return false; }));
    const auto at = [&](float x, float y) { // viewer point -> pixel
      const uint8_t* p = bm.bgra.data() + (static_cast<size_t>(cs.h - y) * bm.w + static_cast<size_t>(x)) * 4u;
      return (static_cast<unsigned>(p[2]) << 16) | (static_cast<unsigned>(p[1]) << 8) | p[0];
    };
    CHECK(at(25, 30) == 0x0000FF); // the middle of the box, where the user drew it
    CHECK(at(60, 30) == 0xFFFFFF); // outside it
    CHECK(at(25, 70) == 0xFFFFFF);
    d.doc.reset();
    std::filesystem::remove(src);
    std::filesystem::remove(dst);
  }
}

TEST_CASE("snap reading can be cancelled and bounded", "[pdfsnap][req391][issue732]") {
  const auto p = WriteBytes("gs_snap_cancel.pdf", MakeSyntheticPdf(1, 400, 0));
  auto r = PdfDocument::Open(p);
  REQUIRE(r.doc != nullptr);
  std::vector<SnapPoint> pts;
  CHECK_FALSE(r.doc->SnapPoints(0, pts, [] { return true; })); // cancelled
  CHECK(pts.empty());
  REQUIRE(r.doc->SnapPoints(0, pts, [] { return false; }, 100)); // bounded
  CHECK(pts.size() == 100);
  CHECK_FALSE(r.doc->SnapPoints(5, pts, [] { return false; })); // no such page
  r.doc.reset();
  std::filesystem::remove(p);
}
