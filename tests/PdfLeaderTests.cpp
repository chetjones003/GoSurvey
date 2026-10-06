// REQ-396 / D-2026-10-06-j: the Leader annotation (an arrow with a boxed note, a FreeText callout) and where a
// note lands. Generated PDFs, no window, no GL.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <catch2/catch_test_macros.hpp>

#include "pdf/PdfAnnotate.hpp"
#include "pdf/PdfDocument.hpp"
#include "pdf/PdfViewerCore.hpp"

#include <algorithm>
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

bool Near(float a, float b, float tol = 0.01f) { return std::fabs(a - b) <= tol; }

Annot Leader(int page, float tipX, float tipY, float boxX, float boxY, const char* text, unsigned color, float thickness) {
  Annot a;
  a.kind = Annot::Kind::Leader;
  a.page = page;
  a.pts = {{tipX, tipY}};
  a.text = text;
  a.color = color;
  a.thickness = thickness;
  a.font = "Helvetica";
  a.fontSize = 12.f;
  float w = 0, h = 0;
  EstimateTextBox(text, a.fontSize, w, h);
  a.x0 = boxX;
  a.y1 = boxY;
  a.x1 = boxX + w + 2.f * kLeaderPad;
  a.y0 = boxY - h - 2.f * kLeaderPad;
  return a;
}

Annot Note(int page, float x, float y, const char* text, float size) {
  Annot a;
  a.kind = Annot::Kind::Text;
  a.page = page;
  a.x0 = x;
  a.y1 = y;
  float w = 0, h = 0;
  EstimateTextBox(text, size, w, h);
  a.x1 = x + w;
  a.y0 = y - h;
  a.text = text;
  a.color = 0;
  a.font = "Helvetica";
  a.fontSize = size;
  return a;
}

// The colour of the pixel at PDF point (x, y) of page `page`, rendered 1 pixel per point.
unsigned PixelAt(PdfDocument& d, int page, float x, float y) {
  const PageSize s = d.Sizes()[static_cast<size_t>(page)];
  Bitmap bm;
  REQUIRE(d.RenderPage(page, static_cast<int>(s.wPt), static_cast<int>(s.hPt), bm, [] { return false; }));
  const int px = static_cast<int>(x), py = static_cast<int>(s.hPt - y);
  const uint8_t* p = bm.bgra.data() + (static_cast<size_t>(py) * bm.w + px) * 4u;
  return (static_cast<unsigned>(p[2]) << 16) | (static_cast<unsigned>(p[1]) << 8) | p[0];
}

} // namespace

TEST_CASE("a leader is a FreeText callout and reads back with the same tip, box, text and style",
          "[pdfannot][req396][issue732]") {
  const auto src = WriteBytes("gs_leader_src.pdf", MakeSyntheticPdf(1, 0, 0));
  const auto dst = TempPath("gs_leader_out.pdf");
  std::filesystem::remove(dst);
  const Annot in = Leader(0, 120.f, 200.f, 260.f, 400.f, "Verify\nin field", 0x0000FF, 2.f);
  const std::string err = SaveAnnotated(src, {in}, dst);
  INFO(err);
  REQUIRE(err.empty());

  const std::string bytes = ReadAll(dst);
  CHECK(bytes.find("/Subtype/FreeText") != std::string::npos);
  CHECK(bytes.find("/IT/FreeTextCallout") != std::string::npos);
  CHECK(bytes.find("/CL[120.0000 200.0000 ") != std::string::npos); // the callout line starts at the arrow tip
  CHECK(bytes.find("/LE/OpenArrow") != std::string::npos);

  const std::vector<Annot> back = ReadAnnotations(dst);
  REQUIRE(back.size() == 1);
  const Annot& b = back[0];
  CHECK(b.kind == Annot::Kind::Leader);
  REQUIRE(b.pts.size() == 1);
  CHECK(Near(b.pts[0].first, 120.f));
  CHECK(Near(b.pts[0].second, 200.f));
  CHECK(Near(b.x0, in.x0));
  CHECK(Near(b.x1, in.x1));
  CHECK(Near(b.y0, in.y0));
  CHECK(Near(b.y1, in.y1));
  CHECK(b.text == in.text);
  CHECK(b.color == in.color);
  CHECK(Near(b.thickness, in.thickness));
  CHECK(b.font == in.font);
  CHECK(Near(b.fontSize, in.fontSize));
  std::filesystem::remove(src);
  std::filesystem::remove(dst);
}

TEST_CASE("a saved leader is drawn: the box edge and the arrow line show", "[pdfannot][req396][issue732]") {
  const auto src = WriteBytes("gs_leader_src2.pdf", MakeSyntheticPdf(1, 0, 0));
  const auto dst = TempPath("gs_leader_out2.pdf");
  std::filesystem::remove(dst);
  const Annot in = Leader(0, 100.f, 100.f, 300.f, 300.f, "Hi", 0xFF0000, 4.f);
  const std::string err = SaveAnnotated(src, {in}, dst);
  INFO(err);
  REQUIRE(err.empty());
  auto d = PdfDocument::Open(dst);
  REQUIRE(d.doc != nullptr);
  const LeaderGeom g = LeaderLine(in);
  CHECK(PixelAt(*d.doc, 0, (g.sx + g.tx) * 0.5f, (g.sy + g.ty) * 0.5f) == 0xFF0000);      // on the arrow line
  CHECK(PixelAt(*d.doc, 0, in.x0 + 1.f, (in.y0 + in.y1) * 0.5f) == 0xFF0000);             // on the box's left edge
  CHECK(PixelAt(*d.doc, 0, 500, 500) == 0xFFFFFF);
  d.doc.reset();
  std::filesystem::remove(src);
  std::filesystem::remove(dst);
}

TEST_CASE("a leader line leaves the box edge nearest the tip", "[pdfannot][req396][issue732]") {
  Annot a = Leader(0, 0.f, 150.f, 100.f, 200.f, "x", 0, 1.f); // tip to the left of the box
  LeaderGeom g = LeaderLine(a);
  CHECK(Near(g.sx, 100.f)); // leaves the left edge, at its middle
  CHECK(Near(g.sy, (a.y0 + a.y1) * 0.5f));
  CHECK(Near(g.tx, 0.f));
  CHECK(Near(g.ty, 150.f));
  a.pts = {{400.f, 180.f}}; // tip to the right
  g = LeaderLine(a);
  CHECK(Near(g.sx, std::max(a.x0, a.x1)));
  a.pts = {{110.f, 600.f}}; // tip above
  g = LeaderLine(a);
  CHECK(Near(g.sy, std::max(a.y0, a.y1)));
}

TEST_CASE("a note whose box starts at the clicked point is stored with its left at x and its top at y",
          "[pdfannot][req396][issue732]") {
  const auto src = WriteBytes("gs_note_src.pdf", MakeSyntheticPdf(1, 0, 0));
  const auto dst = TempPath("gs_note_out.pdf");
  std::filesystem::remove(dst);
  REQUIRE(SaveAnnotated(src, {Note(0, 222.f, 333.f, "Here", 12.f)}, dst).empty());
  const std::vector<Annot> back = ReadAnnotations(dst);
  REQUIRE(back.size() == 1);
  // The stored rectangle is the text's own bounds (a glyph's side bearing and the line's leading sit inside the
  // box), so it starts a point or two in from the clicked corner - never at the page's left edge.
  CHECK(Near(std::min(back[0].x0, back[0].x1), 222.f, 2.f));
  CHECK(Near(std::max(back[0].y0, back[0].y1), 333.f, 4.f));
  std::filesystem::remove(src);
  std::filesystem::remove(dst);
}
