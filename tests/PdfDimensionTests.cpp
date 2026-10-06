// REQ-391 / ADR-067 addendum 2: scaled dimensions - length, polylength, area and perimeter, angle - their
// values, their standard-PDF form on Save As, and what happens when the scale changes or is missing.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <catch2/catch_test_macros.hpp>

#include "pdf/PdfAnnotate.hpp"
#include "pdf/PdfDocument.hpp"
#include "pdf/PdfMeasure.hpp"
#include "pdf/PdfViewerCore.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace pdfview;

namespace {

std::filesystem::path TempPath(const char* name) { return std::filesystem::temp_directory_path() / name; }

std::filesystem::path WriteBytes(const char* name, const std::string& bytes) {
  const auto p = TempPath(name);
  std::ofstream f(p, std::ios::binary);
  f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return p;
}

std::string ReadAll(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

size_t Count(const std::string& hay, const std::string& needle) {
  size_t n = 0;
  for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
    ++n;
  return n;
}

// 1 pt = 0.5 ft.
PageScale HalfFoot() { return ScaleFromCalibration(100.0, 50.0, Unit::Foot); }

Annot Dim(Annot::Kind k, int page, std::vector<std::pair<float, float>> pts, unsigned color = 0xFF0000) {
  Annot a;
  a.kind = k;
  a.page = page;
  a.pts = std::move(pts);
  a.color = color;
  a.thickness = 2.f;
  a.fontSize = 12.f;
  return a;
}

} // namespace

TEST_CASE("dimension values follow the page scale", "[pdfdim][req391][issue732]") {
  const PageScale s = HalfFoot();
  // Length: 100 pt = 50 ft.
  const Annot len = Dim(Annot::Kind::Length, 0, {{0, 0}, {100, 0}});
  CHECK(DimensionLabel(len, s) == "50.00 ft");
  // Polylength of an L: 100 pt + 40 pt = 140 pt = 70 ft.
  const Annot poly = Dim(Annot::Kind::PolyLength, 0, {{0, 0}, {100, 0}, {100, 40}});
  CHECK(DimensionLabel(poly, s) == "70.00 ft");
  // Area of a 100 x 40 pt rectangle: 4000 sq pt = 1000 sq ft (50 ft x 20 ft); perimeter 280 pt = 140 ft.
  const Annot area = Dim(Annot::Kind::Area, 0, {{0, 0}, {100, 0}, {100, 40}, {0, 40}});
  CHECK(DimensionLabel(area, s) == "A = 1000.00 sq ft\nP = 140.00 ft");
  CHECK(PolygonAreaSqPt(area.pts) == 4000.0);
  CHECK(PathLengthPt(area.pts, true) == 280.0);
  // Angle at the middle point.
  CHECK(std::fabs(AngleDegrees({{100, 0}, {0, 0}, {0, 100}}) - 90.0) < 0.01);
  CHECK(std::fabs(AngleDegrees({{100, 0}, {0, 0}, {100, 100}}) - 45.0) < 0.01);
  CHECK(std::fabs(AngleDegrees({{-100, 0}, {0, 0}, {100, 0}}) - 180.0) < 0.01);
  Annot ang = Dim(Annot::Kind::Angle, 0, {{100, 0}, {0, 0}, {100, 100}});
  CHECK(DimensionLabel(ang, s) == "45.00\xC2\xB0");
  // Decimals, and a different scale gives proportionally different values.
  Annot d0 = len;
  d0.decimals = 0;
  CHECK(DimensionLabel(d0, s) == "50 ft");
  CHECK(DimensionLabel(len, ScaleFromCalibration(100.0, 100.0, Unit::Foot)) == "100.00 ft");
  CHECK(DimensionLabel(len, ScaleFromCalibration(100.0, 30.0, Unit::Metre)) == "30.00 m");
  // Completeness.
  CHECK(DimensionComplete(len));
  CHECK_FALSE(DimensionComplete(Dim(Annot::Kind::Area, 0, {{0, 0}, {1, 1}})));
  CHECK_FALSE(DimensionComplete(Dim(Annot::Kind::Angle, 0, {{0, 0}, {1, 1}})));
  CHECK_FALSE(DimensionComplete(Dim(Annot::Kind::Length, 0, {{0, 0}})));
}

TEST_CASE("each dimension is saved as a standard measurement annotation and read back", "[pdfdim][req391][issue732]") {
  const auto src = WriteBytes("gs_dim_src.pdf", MakeSyntheticPdf(2, 0, 0));
  const std::string before = ReadAll(src);
  const auto dst = TempPath("gs_dim_out.pdf");
  std::filesystem::remove(dst);

  const std::vector<Annot> in = {
      Dim(Annot::Kind::Length, 0, {{100, 400}, {300, 400}}, 0xFF0000),
      Dim(Annot::Kind::PolyLength, 0, {{100, 200}, {200, 200}, {200, 260}}, 0x0000FF),
      Dim(Annot::Kind::Area, 1, {{100, 100}, {200, 100}, {200, 140}, {100, 140}}, 0x00AA00),
      Dim(Annot::Kind::Angle, 1, {{300, 100}, {200, 300}, {400, 300}}, 0x800080),
  };
  std::map<int, PageScale> ps;
  ps[0] = HalfFoot();
  ps[1] = HalfFoot();
  const std::string err = SaveAnnotated(src, in, dst, {}, ps);
  INFO(err);
  REQUIRE(err.empty());
  CHECK(ReadAll(src) == before);

  const std::string out = ReadAll(dst);
  CHECK(Count(out, "/Type/Measure") == 3); // length, polylength, area carry it; the angle has no standard form
  CHECK(Count(out, "/Subtype/Line") == 1);
  CHECK(Count(out, "/Subtype/PolyLine") == 2);
  CHECK(Count(out, "/Subtype/Polygon") == 1);

  const std::vector<Annot> back = ReadAnnotations(dst);
  REQUIRE(back.size() == in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    INFO("dimension " << i);
    CHECK(back[i].kind == in[i].kind);
    CHECK(back[i].page == in[i].page);
    CHECK(back[i].color == in[i].color);
    REQUIRE(back[i].pts.size() == in[i].pts.size());
    for (size_t k = 0; k < in[i].pts.size(); ++k) {
      CHECK(std::fabs(back[i].pts[k].first - in[i].pts[k].first) < 0.01f);
      CHECK(std::fabs(back[i].pts[k].second - in[i].pts[k].second) < 0.01f);
    }
    CHECK(std::fabs(back[i].thickness - 2.f) < 0.01f);
    CHECK(back[i].text == DimensionLabel(in[i], HalfFoot())); // the label travels in /Contents
  }
  CHECK(back[0].text == "100.00 ft");
  CHECK(back[2].text == "A = 1000.00 sq ft\nP = 140.00 ft");

  // The file is still an ordinary PDF that opens, and the label is drawn on the page.
  auto d = PdfDocument::Open(dst);
  REQUIRE(d.doc != nullptr);
  const PageSize sz = d.doc->Sizes()[0];
  Bitmap bm;
  REQUIRE(d.doc->RenderPage(0, static_cast<int>(sz.wPt), static_cast<int>(sz.hPt), bm, [] { return false; }));
  const auto pixel = [&](float x, float y) {
    const uint8_t* p = bm.bgra.data() + (static_cast<size_t>(sz.hPt - y) * bm.w + static_cast<size_t>(x)) * 4u;
    return (static_cast<unsigned>(p[2]) << 16) | (static_cast<unsigned>(p[1]) << 8) | p[0];
  };
  CHECK(pixel(150, 400) == 0xFF0000); // on the length line, clear of its label at the middle
  d.doc.reset();

  std::filesystem::remove(src);
  std::filesystem::remove(dst);
}

TEST_CASE("a changed scale changes the saved labels", "[pdfdim][req391][issue732]") {
  const auto src = WriteBytes("gs_dim_src2.pdf", MakeSyntheticPdf(1, 0, 0));
  const std::vector<Annot> in = {Dim(Annot::Kind::Length, 0, {{100, 400}, {300, 400}})}; // 200 pt
  const auto a = TempPath("gs_dim_a.pdf"), b = TempPath("gs_dim_b.pdf");
  std::filesystem::remove(a);
  std::filesystem::remove(b);
  std::map<int, PageScale> half, tenth;
  half[0] = ScaleFromCalibration(100.0, 50.0, Unit::Foot);
  tenth[0] = ScaleFromCalibration(100.0, 10.0, Unit::Foot);
  REQUIRE(SaveAnnotated(src, in, a, {}, half).empty());
  REQUIRE(SaveAnnotated(src, in, b, {}, tenth).empty());
  CHECK(ReadAnnotations(a).at(0).text == "100.00 ft");
  CHECK(ReadAnnotations(b).at(0).text == "20.00 ft");
  std::filesystem::remove(src);
  std::filesystem::remove(a);
  std::filesystem::remove(b);
}

TEST_CASE("a dimension on a page with no scale is refused and nothing is written", "[pdfdim][req391][issue732]") {
  const auto src = WriteBytes("gs_dim_src3.pdf", MakeSyntheticPdf(2, 0, 0));
  const auto dst = TempPath("gs_dim_none.pdf");
  std::filesystem::remove(dst);
  std::map<int, PageScale> onlyPage0;
  onlyPage0[0] = HalfFoot();
  const std::vector<Annot> in = {Dim(Annot::Kind::Length, 1, {{0, 0}, {10, 10}})};
  const std::string err = SaveAnnotated(src, in, dst, {}, onlyPage0);
  CHECK_FALSE(err.empty());
  CHECK(err.find("scale") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(dst));
  // A dimension with too few points is refused too.
  std::map<int, PageScale> both = onlyPage0;
  both[1] = HalfFoot();
  CHECK_FALSE(SaveAnnotated(src, {Dim(Annot::Kind::Area, 1, {{0, 0}, {5, 5}})}, dst, {}, both).empty());
  CHECK_FALSE(std::filesystem::exists(dst));
  // An angle needs no scale.
  CHECK(SaveAnnotated(src, {Dim(Annot::Kind::Angle, 1, {{10, 0}, {0, 0}, {0, 10}})}, dst, {}, {}).empty());
  std::filesystem::remove(src);
  std::filesystem::remove(dst);
}
