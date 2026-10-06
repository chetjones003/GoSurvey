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

#include <cmath>
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

bool Has(const std::vector<std::pair<float, float>>& pts, float x, float y, float tol = 0.3f) {
  for (const auto& p : pts)
    if (std::fabs(p.first - x) <= tol && std::fabs(p.second - y) <= tol)
      return true;
  return false;
}

std::vector<std::pair<float, float>> Snap(const std::filesystem::path& p, bool* ok = nullptr) {
  auto r = PdfDocument::Open(p);
  REQUIRE(r.doc != nullptr);
  std::vector<std::pair<float, float>> pts;
  const bool good = r.doc->SnapPoints(0, pts, [] { return false; });
  if (ok != nullptr)
    *ok = good;
  r.doc.reset();
  return pts;
}

} // namespace

TEST_CASE("SnapIndex finds the nearest point within the radius", "[pdfsnap][req391][issue732]") {
  SnapIndex idx;
  idx.Build({{10.f, 10.f}, {50.f, 50.f}, {52.f, 50.f}, {-30.f, -40.f}, {1000.f, 1000.f}});
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
  std::vector<std::pair<float, float>> sp;
  REQUIRE(r.doc->SnapPoints(0, sp, [] { return false; }));
  REQUIRE(sp.size() == 2);
  Bitmap bm;
  REQUIRE(r.doc->RenderPage(0, 100, 200, bm, [] { return false; }));
  for (const auto& q : sp) {
    const int px = static_cast<int>(q.first), py = static_cast<int>(sz.hPt - q.second);
    // Within a couple of pixels of the point there is dark ink (the 4 pt stroke's end).
    bool ink = false;
    for (int dy = -2; dy <= 2; ++dy)
      for (int dx = -2; dx <= 2; ++dx) {
        const int x = px + dx, y = py + dy;
        if (x >= 0 && y >= 0 && x < bm.w && y < bm.h && bm.bgra[(static_cast<size_t>(y) * bm.w + x) * 4u] < 100)
          ink = true;
      }
    INFO("snap point " << q.first << "," << q.second);
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
  std::vector<std::pair<float, float>> pts;
  CHECK_FALSE(r.doc->SnapPoints(0, pts, [] { return true; })); // cancelled
  CHECK(pts.empty());
  REQUIRE(r.doc->SnapPoints(0, pts, [] { return false; }, 100)); // bounded
  CHECK(pts.size() == 100);
  CHECK_FALSE(r.doc->SnapPoints(5, pts, [] { return false; })); // no such page
  r.doc.reset();
  std::filesystem::remove(p);
}
