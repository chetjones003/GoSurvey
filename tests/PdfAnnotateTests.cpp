// REQ-388 / ADR-067 (e): PDF annotations written through PDFium (Line by the post-save patch, D-2026-10-06-e),
// read back, Save As leaving the source alone, and the undo/redo list. Generated PDFs, no window, no GL.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "pdf/PdfAnnotate.hpp"
#include "pdf/PdfDocument.hpp"
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

bool Near(float a, float b, float tol = 0.01f) { return std::fabs(a - b) <= tol; }

Annot Line(int page, float x0, float y0, float x1, float y1, unsigned color, float thickness) {
  Annot a;
  a.kind = Annot::Kind::Line;
  a.page = page;
  a.x0 = x0;
  a.y0 = y0;
  a.x1 = x1;
  a.y1 = y1;
  a.color = color;
  a.thickness = thickness;
  return a;
}

Annot Shape(Annot::Kind k, int page, float x0, float y0, float x1, float y1, unsigned color, float thickness, bool fill) {
  Annot a = Line(page, x0, y0, x1, y1, color, thickness);
  a.kind = k;
  a.fill = fill;
  return a;
}

Annot Note(int page, float x, float y, const char* text, unsigned color, const char* font, bool bold, bool italic,
           float size) {
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
  a.color = color;
  a.font = font;
  a.bold = bold;
  a.italic = italic;
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

TEST_CASE("each tool writes one annotation of its type with the chosen colour, thickness and font",
          "[pdfannot][req388][issue732]") {
  const auto src = WriteBytes("gs_annot_src.pdf", MakeSyntheticPdf(3, 2, 0));
  const auto dst = TempPath("gs_annot_out.pdf");
  std::filesystem::remove(dst);

  const std::vector<Annot> in = {
      Line(0, 100, 100, 300, 250, 0xFF0000, 3.f),
      Shape(Annot::Kind::Rect, 1, 50, 60, 200, 160, 0x0000FF, 2.f, false),
      Shape(Annot::Kind::Ellipse, 1, 220, 300, 400, 420, 0x00AA00, 4.f, true),
      Note(2, 60, 500, "Hello\nsecond line", 0x800080, "Times", true, true, 18.f),
      Note(2, 60, 300, "Plain", 0x000000, "Helvetica", false, false, 10.f),
      Line(2, 10, 10, 200, 10, 0x123456, 0.5f),
  };
  const std::string err = SaveAnnotated(src, in, dst);
  INFO(err);
  REQUIRE(err.empty());

  const std::vector<Annot> back = ReadAnnotations(dst);
  REQUIRE(back.size() == in.size());
  // Order within a page is the order written; pages ascend.
  const std::vector<int> order = {0, 1, 2, 3, 4, 5};
  for (size_t i = 0; i < in.size(); ++i) {
    const Annot& a = in[static_cast<size_t>(order[i])];
    const Annot& b = back[i];
    INFO("annotation " << i);
    CHECK(b.kind == a.kind);
    CHECK(b.page == a.page);
    CHECK(b.color == a.color);
    if (a.kind == Annot::Kind::Line) {
      CHECK(Near(b.x0, a.x0));
      CHECK(Near(b.y0, a.y0));
      CHECK(Near(b.x1, a.x1));
      CHECK(Near(b.y1, a.y1));
      CHECK(Near(b.thickness, a.thickness));
    } else if (a.kind != Annot::Kind::Text) {
      CHECK(Near(b.x0, std::min(a.x0, a.x1)));
      CHECK(Near(b.x1, std::max(a.x0, a.x1)));
      CHECK(Near(b.y0, std::min(a.y0, a.y1)));
      CHECK(Near(b.y1, std::max(a.y0, a.y1)));
      CHECK(Near(b.thickness, a.thickness));
      CHECK(b.fill == a.fill);
    } else {
      CHECK(b.text == a.text);
      CHECK(b.font == a.font);
      CHECK(b.bold == a.bold);
      CHECK(b.italic == a.italic);
      CHECK(Near(b.fontSize, a.fontSize));
    }
  }
  std::filesystem::remove(src);
  std::filesystem::remove(dst);
}

TEST_CASE("saved annotations are drawn by a PDF reader, in the right place", "[pdfannot][req388][issue732]") {
  const auto src = WriteBytes("gs_annot_src2.pdf", MakeSyntheticPdf(1, 0, 0)); // no page content but the title text
  const auto dst = TempPath("gs_annot_out2.pdf");
  std::filesystem::remove(dst);
  const std::vector<Annot> in = {
      Line(0, 100, 400, 300, 400, 0xFF0000, 6.f),
      Shape(Annot::Kind::Rect, 0, 100, 200, 200, 300, 0x0000FF, 4.f, true),
      Shape(Annot::Kind::Ellipse, 0, 300, 200, 400, 300, 0x00FF00, 4.f, true),
  };
  const std::string err = SaveAnnotated(src, in, dst);
  INFO(err);
  REQUIRE(err.empty());
  auto d = PdfDocument::Open(dst);
  REQUIRE(d.doc != nullptr);
  CHECK(PixelAt(*d.doc, 0, 200, 400) == 0xFF0000);  // on the line
  CHECK(PixelAt(*d.doc, 0, 200, 450) == 0xFFFFFF);  // clear of it
  CHECK(PixelAt(*d.doc, 0, 150, 250) == 0x0000FF);  // inside the filled rectangle
  CHECK(PixelAt(*d.doc, 0, 350, 250) == 0x00FF00);  // inside the filled ellipse
  CHECK(PixelAt(*d.doc, 0, 305, 205) == 0xFFFFFF);  // ellipse corner is outside the curve
  d.doc.reset();
  std::filesystem::remove(src);
  std::filesystem::remove(dst);
}

TEST_CASE("text notes draw, with a standard font and with an embedded TrueType font", "[pdfannot][req388][issue732]") {
  const auto src = WriteBytes("gs_annot_src4.pdf", MakeSyntheticPdf(1, 0, 0));
  const auto dst = TempPath("gs_annot_out4.pdf");
  std::filesystem::remove(dst);
  std::vector<Annot> in = {Note(0, 100, 600, "STANDARD", 0x000000, "Courier", true, false, 28.f)};
  const std::filesystem::path arial = "C:/Windows/Fonts/arial.ttf";
  const bool haveArial = std::filesystem::exists(arial);
  if (haveArial) {
    Annot t = Note(0, 100, 500, "EMBEDDED", 0x000000, "Arial", false, false, 28.f);
    t.fontFile = arial.generic_string();
    in.push_back(t);
  }
  const std::string err = SaveAnnotated(src, in, dst);
  INFO(err);
  REQUIRE(err.empty());
  auto d = PdfDocument::Open(dst);
  REQUIRE(d.doc != nullptr);
  const PageSize s = d.doc->Sizes()[0];
  Bitmap bm;
  REQUIRE(d.doc->RenderPage(0, static_cast<int>(s.wPt), static_cast<int>(s.hPt), bm, [] { return false; }));
  auto darkIn = [&](float x0, float y0, float x1, float y1) { // PDF-space box
    int n = 0;
    for (int y = static_cast<int>(s.hPt - y1); y < static_cast<int>(s.hPt - y0); ++y)
      for (int x = static_cast<int>(x0); x < static_cast<int>(x1); ++x)
        if (bm.bgra[(static_cast<size_t>(y) * bm.w + x) * 4u] < 100)
          ++n;
    return n;
  };
  CHECK(darkIn(100, 570, 300, 610) > 80);
  if (haveArial)
    CHECK(darkIn(100, 470, 300, 510) > 80);
  const std::vector<Annot> back = ReadAnnotations(dst);
  REQUIRE(back.size() == in.size());
  CHECK(back[0].font == "Courier");
  CHECK(back[0].bold);
  d.doc.reset();
  std::filesystem::remove(src);
  std::filesystem::remove(dst);
}

TEST_CASE("Save As leaves the source bytes identical and refuses the source path", "[pdfannot][req388][issue732]") {
  const auto src = WriteBytes("gs_annot_src3.pdf", MakeSyntheticPdf(2, 3, 0));
  const std::string before = ReadAll(src);
  const auto dst = TempPath("gs_annot_out3.pdf");
  std::filesystem::remove(dst);
  const std::vector<Annot> in = {Line(0, 10, 10, 50, 50, 0xFF0000, 1.f)};

  const std::string err = SaveAnnotated(src, in, dst);
  INFO(err);
  REQUIRE(err.empty());
  CHECK(ReadAll(src) == before);
  CHECK(ReadAll(dst) != before);

  CHECK_FALSE(SaveAnnotated(src, in, src).empty());
  CHECK(ReadAll(src) == before);

  // A bad page number fails with a message and writes nothing.
  const auto bad = TempPath("gs_annot_bad.pdf");
  std::filesystem::remove(bad);
  CHECK_FALSE(SaveAnnotated(src, {Line(9, 0, 0, 1, 1, 0, 1.f)}, bad).empty());
  CHECK_FALSE(std::filesystem::exists(bad));
  CHECK_FALSE(std::filesystem::exists(TempPath("gs_annot_out3.pdf.gsannot.tmp")));

  // Saving the saved file again keeps the earlier annotations alongside the new one.
  const auto dst2 = TempPath("gs_annot_out3b.pdf");
  std::filesystem::remove(dst2);
  REQUIRE(SaveAnnotated(dst, {Shape(Annot::Kind::Rect, 1, 5, 5, 40, 40, 0x00FF00, 2.f, false)}, dst2).empty());
  CHECK(ReadAnnotations(dst2).size() == 2);

  std::filesystem::remove(src);
  std::filesystem::remove(dst);
  std::filesystem::remove(dst2);
}

TEST_CASE("undo and redo restore the annotation list exactly", "[pdfannot][req388][issue732]") {
  AnnotSession s;
  CHECK_FALSE(s.Dirty());
  const Annot a = Line(0, 0, 0, 10, 10, 0xFF0000, 1.f);
  const Annot b = Shape(Annot::Kind::Rect, 0, 1, 1, 5, 5, 0x00FF00, 2.f, true);
  s.Add(a);
  s.Add(b);
  CHECK(s.Dirty());
  Annot moved = a;
  moved.x1 = 99.f;
  CHECK(s.Replace(0, moved));
  CHECK(s.Remove(1));
  CHECK(s.Items().size() == 1);

  REQUIRE(s.Undo()); // the removal
  REQUIRE(s.Items().size() == 2);
  CHECK(s.Items()[1] == b);
  REQUIRE(s.Undo()); // the move
  CHECK(s.Items()[0] == a);
  REQUIRE(s.Undo());
  REQUIRE(s.Undo());
  CHECK(s.Items().empty());
  CHECK_FALSE(s.CanUndo());
  CHECK_FALSE(s.Dirty());

  REQUIRE(s.Redo());
  REQUIRE(s.Redo());
  REQUIRE(s.Redo());
  REQUIRE(s.Redo());
  CHECK(s.Items().size() == 1);
  CHECK(s.Items()[0] == moved);
  CHECK_FALSE(s.CanRedo());

  s.Undo();
  s.Add(b); // a new edit clears redo
  CHECK_FALSE(s.CanRedo());

  s.MarkSaved();
  CHECK_FALSE(s.Dirty());
  s.Undo();
  CHECK(s.Dirty());
  CHECK_FALSE(s.Remove(5)); // out of range: no step, no change
  CHECK_FALSE(s.Replace(5, a));
}
