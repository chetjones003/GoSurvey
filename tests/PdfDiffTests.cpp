// REQ-393 (and REQ-392 clause 3): automatic alignment of two sheets, finding what was added / removed / changed
// by what is drawn, and writing the findings as markups on a Save As copy of the revision.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pdf/PdfAlign.hpp"
#include "pdf/PdfAnnotate.hpp"
#include "pdf/PdfDiff.hpp"
#include "pdf/PdfDocument.hpp"
#include "pdf/PdfViewerCore.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace pdfalign;
using Catch::Approx;

namespace {

constexpr double kPpp = 1.0; // one pixel per point

struct Seg {
  double x0, y0, x1, y1;
};

pdfview::Bitmap Blank(int w, int h) {
  pdfview::Bitmap b;
  b.w = w;
  b.h = h;
  b.bgra.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 255);
  return b;
}

/// Draw each segment (page points, y up) about 2 px thick.
void Draw(pdfview::Bitmap& b, double hPt, const std::vector<Seg>& segs, double ppp = kPpp) {
  for (const Seg& s : segs) {
    const double len = std::hypot(s.x1 - s.x0, s.y1 - s.y0);
    const int steps = std::max(1, static_cast<int>(len * ppp * 2));
    for (int i = 0; i <= steps; ++i) {
      const double t = static_cast<double>(i) / steps;
      const double x = (s.x0 + (s.x1 - s.x0) * t) * ppp, y = (hPt - (s.y0 + (s.y1 - s.y0) * t)) * ppp;
      for (int dy = -1; dy <= 0; ++dy)
        for (int dx = -1; dx <= 0; ++dx) {
          const int px = static_cast<int>(std::floor(x)) + dx, py = static_cast<int>(std::floor(y)) + dy;
          if (px < 0 || py < 0 || px >= b.w || py >= b.h)
            continue;
          uint8_t* p = &b.bgra[(static_cast<size_t>(py) * static_cast<size_t>(b.w) + static_cast<size_t>(px)) * 4u];
          p[0] = p[1] = p[2] = 0;
        }
    }
  }
}

/// A repeatable pseudo-random sheet of line work, well inside a w x h page.
std::vector<Seg> RandomSheet(unsigned seed, double w, double h, int n) {
  unsigned s = seed;
  const auto rnd = [&s] {
    s = s * 1664525u + 1013904223u;
    return static_cast<double>((s >> 8) & 0xFFFF) / 65535.0;
  };
  std::vector<Seg> out;
  for (int i = 0; i < n; ++i) {
    const double x = 60 + rnd() * (w - 120), y = 60 + rnd() * (h - 120);
    const double len = 30 + rnd() * 120;
    const bool horizontal = rnd() < 0.5;
    out.push_back({x, y, std::min(w - 40, horizontal ? x + len : x), std::min(h - 40, horizontal ? y : y + len)});
  }
  return out;
}

std::vector<Seg> Moved(const std::vector<Seg>& in, const Transform& t) {
  std::vector<Seg> out;
  for (const Seg& s : in) {
    const Pt a = t.Apply({s.x0, s.y0}), b = t.Apply({s.x1, s.y1});
    out.push_back({a.x, a.y, b.x, b.y});
  }
  return out;
}

constexpr double kW = 800, kH = 600;

pdfview::Bitmap Sheet(const std::vector<Seg>& segs) {
  pdfview::Bitmap b = Blank(static_cast<int>(kW * kPpp), static_cast<int>(kH * kPpp));
  Draw(b, kH, segs);
  return b;
}

bool Contains(const pdfdiff::Region& r, double x, double y) { return x >= r.x0 - 0.5 && x <= r.x1 + 0.5 && y >= r.y0 - 0.5 && y <= r.y1 + 0.5; }

std::filesystem::path TempPath(const char* name) { return std::filesystem::temp_directory_path() / name; }

std::string ReadAll(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("automatic alignment finds a known shift, scale and 2 degree rotation", "[issue732][req392][req393]") {
  const std::vector<Seg> base = RandomSheet(7, kW, kH, 90);
  const double th = 2.0 * 3.14159265358979323846 / 180.0;
  Transform truth; // revision -> base
  truth.a = 1.01 * std::cos(th);
  truth.b = 1.01 * std::sin(th);
  truth.tx = 14;
  truth.ty = -9;
  const std::vector<Seg> rev = Moved(base, truth.Inverse());
  const AutoResult r = AutoAlign(Sheet(base), kH, Sheet(rev), kH, kPpp);
  REQUIRE(r.matched);
  CHECK(r.confidence > 0.9);
  for (const Pt q : {Pt{0, 0}, Pt{kW, 0}, Pt{kW, kH}, Pt{0, kH}, Pt{400, 300}}) {
    CHECK(r.xf.Apply(q).x == Approx(truth.Apply(q).x).margin(0.5));
    CHECK(r.xf.Apply(q).y == Approx(truth.Apply(q).y).margin(0.5));
  }
}

TEST_CASE("two sheets with nothing in common report low confidence and claim no match", "[issue732][req392][req393]") {
  const AutoResult r = AutoAlign(Sheet(RandomSheet(1, kW, kH, 90)), kH, Sheet(RandomSheet(99, kW, kH, 90)), kH, kPpp);
  CHECK_FALSE(r.matched);
  CHECK(r.confidence < kLowConfidence);
  CHECK(r.xf.tx == 0.0);
  CHECK(r.xf.a == 1.0);
}

TEST_CASE("identical sheets, and a sheet redrawn with a sub-tolerance wobble, report no change regions", "[issue732][req393]") {
  const std::vector<Seg> base = RandomSheet(3, kW, kH, 90);
  const pdfview::Bitmap a = Sheet(base);
  CHECK(pdfdiff::FindChanges(a, Sheet(base), kPpp, kH, {}).regions.empty());
  // The same sheet drawn 0.7 px off (an anti-aliasing / rounding difference).
  Transform wobble;
  wobble.tx = 0.7;
  wobble.ty = -0.6;
  CHECK(pdfdiff::FindChanges(a, Sheet(Moved(base, wobble)), kPpp, kH, {}).regions.empty());
}

TEST_CASE("one added, one removed and one moved line give an Added, a Removed and a Changed region; a speck is dropped", "[issue732][req393]") {
  const std::vector<Seg> common = {{40, 40, 760, 40}, {40, 560, 760, 560}, {40, 40, 40, 560}, {760, 40, 760, 560}};
  std::vector<Seg> base = common, rev = common;
  base.push_back({100, 450, 260, 450}); // removed in the revision
  rev.push_back({500, 450, 660, 450});  // added in the revision
  base.push_back({100, 150, 260, 150}); // moved 8 pt: close enough to be one region, far enough to be real
  rev.push_back({100, 158, 260, 158});
  rev.push_back({600, 100, 602, 100}); // a speck, 2 pt long
  const pdfdiff::Result r = pdfdiff::FindChanges(Sheet(base), Sheet(rev), kPpp, kH, {});
  REQUIRE(r.regions.size() == 3);
  int added = 0, removed = 0, changed = 0;
  for (const pdfdiff::Region& g : r.regions) {
    if (g.kind == pdfdiff::Kind::Added) {
      ++added;
      CHECK(Contains(g, 580, 450));
    } else if (g.kind == pdfdiff::Kind::Removed) {
      ++removed;
      CHECK(Contains(g, 180, 450));
    } else {
      ++changed;
      CHECK(Contains(g, 180, 154));
    }
  }
  CHECK(added == 1);
  CHECK(removed == 1);
  CHECK(changed == 1);
  CHECK_FALSE(r.regions.empty());
  for (const pdfdiff::Region& g : r.regions)
    CHECK_FALSE(Contains(g, 601, 100)); // the speck is in no region
}

TEST_CASE("a revision shifted by 5 pt reports no changes after automatic alignment, many without it", "[issue732][req393]") {
  const std::vector<Seg> base = RandomSheet(11, kW, kH, 90);
  Transform shift; // base -> revision: everything 5 pt right and 3 up
  shift.tx = 5;
  shift.ty = 3;
  const pdfview::Bitmap baseBmp = Sheet(base), revBmp = Sheet(Moved(base, shift));

  pdfview::Bitmap none; // alignment disabled: the revision laid on at the page corners
  ResampleAligned(revBmp, static_cast<float>(kH), static_cast<float>(kPpp), Transform{}, baseBmp.w, baseBmp.h, static_cast<float>(kPpp), static_cast<float>(kH), none);
  CHECK(pdfdiff::FindChanges(baseBmp, none, kPpp, kH, {}).regions.size() > 5);

  const AutoResult al = AutoAlign(baseBmp, kH, revBmp, kH, kPpp);
  REQUIRE(al.matched);
  pdfview::Bitmap aligned;
  ResampleAligned(revBmp, static_cast<float>(kH), static_cast<float>(kPpp), al.xf, baseBmp.w, baseBmp.h, static_cast<float>(kPpp), static_cast<float>(kH), aligned);
  CHECK(pdfdiff::FindChanges(baseBmp, aligned, kPpp, kH, {}).regions.empty());
}

TEST_CASE("Write changes as markups: one annotation per region, right kind and box, sources untouched", "[issue732][req393]") {
  const auto src = TempPath("gosurvey_diff_src.pdf");
  {
    const std::string bytes = pdfview::MakeSyntheticPdf(1, 20, 0);
    std::ofstream f(src, std::ios::binary);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  const std::string before = ReadAll(src);

  std::vector<pdfdiff::Region> regions(3);
  regions[0].kind = pdfdiff::Kind::Added;
  regions[0].x0 = 100; regions[0].y0 = 200; regions[0].x1 = 160; regions[0].y1 = 230;
  regions[1].kind = pdfdiff::Kind::Removed;
  regions[1].x0 = 300; regions[1].y0 = 400; regions[1].x1 = 340; regions[1].y1 = 420;
  regions[2].kind = pdfdiff::Kind::Changed;
  regions[2].x0 = 50; regions[2].y0 = 60; regions[2].x1 = 90; regions[2].y1 = 75;
  Transform baseToRev; // the revision sits 10 pt to the right
  baseToRev.tx = 10;
  const std::vector<pdfview::Annot> marks = pdfdiff::RegionsToMarkups(regions, 0, baseToRev);
  REQUIRE(marks.size() == 3);

  const auto dst = TempPath("gosurvey_diff_dst.pdf");
  std::filesystem::remove(dst);
  REQUIRE(pdfview::SaveAnnotated(src, marks, dst).empty());
  const std::vector<pdfview::Annot> back = pdfview::ReadAnnotations(dst);
  REQUIRE(back.size() == 3);
  const char* names[3] = {"Added", "Removed", "Changed"};
  for (size_t i = 0; i < 3; ++i) {
    CHECK(back[i].kind == pdfview::Annot::Kind::Rect);
    CHECK(back[i].text.rfind(names[i], 0) == 0);
    CHECK(back[i].x0 == Approx(static_cast<float>(regions[i].x0 + 10)).margin(0.6));
    CHECK(back[i].x1 == Approx(static_cast<float>(regions[i].x1 + 10)).margin(0.6));
    CHECK(back[i].y0 == Approx(static_cast<float>(regions[i].y0)).margin(0.6));
    CHECK(back[i].y1 == Approx(static_cast<float>(regions[i].y1)).margin(0.6));
  }
  CHECK(back[0].color != back[1].color);
  CHECK(back[1].color != back[2].color);
  CHECK(ReadAll(src) == before);
}
