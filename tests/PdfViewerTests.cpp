// REQ-387 / ADR-067: the PDF viewer's pure core (layout, read-ahead plan, bounded cache) and its
// PDFium wrapper, opened against generated PDFs. No window, no GL.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <catch2/catch_test_macros.hpp>

#include "pdf/PdfDocument.hpp"
#include "pdf/PdfViewerCore.hpp"

#include <algorithm>
#include <fpdf_edit.h>
#include <fpdf_thumbnail.h>
#include <fpdfview.h>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

using namespace pdfview;

namespace {

std::filesystem::path WritePdf(const char* name, const std::string& bytes) {
  const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
  std::ofstream f(p, std::ios::binary);
  f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return p;
}

std::vector<PageSize> Letter(int n) { return std::vector<PageSize>(static_cast<size_t>(n), PageSize{612.f, 792.f}); }

} // namespace

TEST_CASE("Layout stacks pages and finds the page at a y", "[pdfview][req387]") {
  const Layout l = Layout::Build(Letter(10), 10.f);
  REQUIRE(l.PageCount() == 10);
  CHECK(l.top[0] == 0.f);
  CHECK(l.top[1] == 802.f);
  CHECK(l.totalHeight == 10 * 792.f + 9 * 10.f);
  CHECK(l.PageAt(0.f) == 0);
  CHECK(l.PageAt(801.f) == 0); // inside the gap: the page above
  CHECK(l.PageAt(802.f) == 1);
  CHECK(l.PageAt(1e9f) == 9);
  CHECK(Layout::Build({}, 10.f).PageAt(5.f) == -1);
}

TEST_CASE("VisiblePages returns only pages meeting the view", "[pdfview][req387]") {
  const Layout l = Layout::Build(Letter(100), 10.f);
  const VisibleRange v = VisiblePages(l, 802.f * 20 + 100.f, 1000.f);
  REQUIRE_FALSE(v.Empty());
  CHECK(v.first == 20);
  CHECK(v.last == 21);
  const VisibleRange top = VisiblePages(l, 0.f, 500.f);
  CHECK(top.first == 0);
  CHECK(top.last == 0);
  CHECK(VisiblePages(Layout{}, 0.f, 500.f).Empty());
}

TEST_CASE("PlanRequests: visible first, then ahead of the scroll direction, then behind", "[pdfview][req387]") {
  const VisibleRange vis{10, 11};
  const auto none = [](int) { return Have{}; };
  const auto plan = PlanRequests(vis, 500, +1, 4, 24, none);
  REQUIRE(plan.size() > 6);
  // Every visible page's stand-in precedes any sharp render.
  CHECK(plan[0].level == Level::StandIn);
  CHECK(plan[1].level == Level::StandIn);
  std::set<int> firstTwo{plan[0].page, plan[1].page};
  CHECK(firstTwo == std::set<int>{10, 11});
  CHECK(plan[2].level == Level::Display);
  CHECK(plan[3].level == Level::Display);
  // After the visible four requests come the pages below (scrolling down): 12, 13, ...
  CHECK(plan[4].page == 12);
  // Pages behind appear, but only after every page ahead.
  size_t lastAhead = 0, firstBehind = plan.size();
  for (size_t i = 0; i < plan.size(); ++i) {
    if (plan[i].page > 11)
      lastAhead = i;
    if (plan[i].page < 10 && i < firstBehind)
      firstBehind = i;
  }
  CHECK(lastAhead < firstBehind);

  const auto up = PlanRequests(vis, 500, -1, 4, 24, none);
  CHECK(up[4].page == 9); // scrolling up reads ahead above

  // Pages already satisfied are left out; everything satisfied means nothing to do.
  const auto all = [](int) { return Have{true, true}; };
  CHECK(PlanRequests(vis, 500, +1, 4, 24, all).empty());
  // A page that left the window is simply not requested (the worker drops it, REQ-387 clause 4).
  const auto moved = PlanRequests(VisibleRange{300, 300}, 500, +1, 4, 24, none);
  for (const RenderRequest& r : moved)
    CHECK(r.page >= 298);
}

TEST_CASE("PlanRequests clamps to the document", "[pdfview][req387]") {
  const auto none = [](int) { return Have{}; };
  for (const RenderRequest& r : PlanRequests(VisibleRange{0, 0}, 3, -1, 6, 24, none)) {
    CHECK(r.page >= 0);
    CHECK(r.page < 3);
  }
  CHECK(PlanRequests(VisibleRange{}, 3, 1, 6, 24, none).size() == 0);
}

TEST_CASE("PageCache never exceeds its cap while every page of a 500-page file is walked", "[pdfview][req387]") {
  const size_t pageBytes = 1000000;
  PageCache cache(40 * pageBytes);
  for (int p = 0; p < 500; ++p) {
    PageCache::Entry e;
    e.page = p;
    e.level = Level::Display;
    e.scaleKey = 24;
    e.bytes = pageBytes;
    e.handle = static_cast<uint32_t>(p + 1);
    cache.Put(e, p, VisibleRange{p, p});
    REQUIRE(cache.Bytes() <= cache.Cap());
  }
  CHECK(cache.Count() == 40);
  CHECK(cache.Find(499, Level::Display) != nullptr);
}

TEST_CASE("PageCache evicts the page farthest from the viewport first and spares visible pages", "[pdfview][req387]") {
  PageCache cache(3 * 100);
  auto put = [&](int page, int center, VisibleRange keep) {
    PageCache::Entry e;
    e.page = page;
    e.level = Level::Display;
    e.bytes = 100;
    e.handle = static_cast<uint32_t>(page + 1);
    return cache.Put(e, center, keep);
  };
  put(10, 12, {12, 12});
  put(11, 12, {12, 12});
  put(50, 12, {12, 12});
  const auto removed = put(12, 12, {12, 12});
  REQUIRE(removed.size() == 1);
  CHECK(removed[0].page == 50); // farthest from page 12
  CHECK(cache.Find(12, Level::Display) != nullptr);

  // A visible page is never evicted, even when it is the farthest.
  PageCache c2(100);
  PageCache::Entry a;
  a.page = 1;
  a.bytes = 100;
  c2.Put(a, 90, {1, 1});
  PageCache::Entry b;
  b.page = 90;
  b.bytes = 100;
  const auto r2 = c2.Put(b, 90, {1, 1});
  CHECK(r2.empty()); // over cap, but nothing evictable besides the entry just inserted and the visible page
  CHECK(c2.Count() == 2);
}

TEST_CASE("PageCache replaces a same-level entry and reports the old one", "[pdfview][req387]") {
  PageCache cache(1000);
  PageCache::Entry e;
  e.page = 3;
  e.level = Level::Display;
  e.scaleKey = 16;
  e.bytes = 100;
  e.handle = 7;
  cache.Put(e, 3, {3, 3});
  e.scaleKey = 24;
  e.handle = 8;
  const auto removed = cache.Put(e, 3, {3, 3});
  REQUIRE(removed.size() == 1);
  CHECK(removed[0].handle == 7);
  CHECK(cache.Bytes() == 100);
  CHECK_FALSE(cache.HaveFor(3, 16).displayAtKey);
  CHECK(cache.HaveFor(3, 24).displayAtKey);
  CHECK(cache.Best(3)->handle == 8);
}

TEST_CASE("ReadAheadThatFits never plans more sharp pages than the cache holds", "[pdfview][req387]") {
  const size_t cap = 256ull * 1024 * 1024;
  CHECK(ReadAheadThatFits(cap, 3'400'000, 2, 6) == 6);        // letter page, normal zoom: full window
  CHECK(ReadAheadThatFits(cap, 50'000'000, 1, 6) == 1);       // 36x48 sheet: only a couple fit
  CHECK(ReadAheadThatFits(cap, 80'000'000, 2, 6) == 0);       // visible pages alone fill the cap
  for (size_t bytes : {1'000'000ull, 20'000'000ull, 60'000'000ull}) {
    const int vis = 2;
    const int ra = ReadAheadThatFits(cap, bytes, vis, 6);
    const bool fits = static_cast<size_t>(vis + ra + ra / 2) * bytes <= cap - cap / 3;
    CHECK((fits || ra == 0));
  }
}

TEST_CASE("Downscale shrinks to the limit and averages the pixels", "[pdfview][req387]") {
  Bitmap src;
  src.w = 8;
  src.h = 4;
  src.bgra.assign(8u * 4u * 4u, 0);
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 8; ++x)
      src.bgra[(static_cast<size_t>(y) * 8 + x) * 4] = x < 4 ? 0 : 200;  // left half black, right half 200 (blue channel)
  const Bitmap d = Downscale(src, 4);
  CHECK(d.w == 4);
  CHECK(d.h == 2);
  CHECK(d.bgra[0] == 0);
  CHECK(d.bgra[(3u) * 4u] == 200);
  CHECK(Downscale(src, 100).w == 8);  // never enlarges
  CHECK(Downscale(Bitmap{}, 10).bgra.empty());
}

TEST_CASE("PlanRequests: drafts first, refinements last, a sharp page needs neither", "[pdfview][req387]") {
  const VisibleRange vis{5, 5};
  Have draft;
  draft.standIn = true;
  draft.standInPartial = true;
  const auto have = [&](int p) { return p == 6 ? draft : Have{}; };
  const auto plan = PlanRequests(vis, 20, +1, 3, 24, have);
  REQUIRE(!plan.empty());
  CHECK(plan.back().refine);        // the unfinished draft of page 7 (index 6) is refined, last
  CHECK(plan.back().page == 6);
  for (size_t i = 0; i + 1 < plan.size(); ++i)
    CHECK_FALSE(plan[i].refine);
  Have sharp = draft;
  sharp.anyDisplay = true;
  const auto plan2 = PlanRequests(vis, 20, +1, 3, 24, [&](int p) { return p == 6 ? sharp : Have{}; });
  for (const RenderRequest& r : plan2)
    CHECK_FALSE((r.refine && r.page == 6));
}

TEST_CASE("ScaleKey quantises a smooth zoom", "[pdfview][req387]") {
  CHECK(ScaleKeyFor(1.333f) == ScaleKeyFor(1.34f));
  CHECK(ScaleKeyFor(0.0001f) >= 2);
  CHECK(PxPerPtFor(ScaleKeyFor(1.5f)) > 1.4f);
}

TEST_CASE("PdfDocument opens a 500-page file and reports sizes without rendering a page", "[pdfview][req387]") {
  const auto path = WritePdf("gs_pdfview_500.pdf", MakeSyntheticPdf(500, 40, 50));
  const auto t0 = std::chrono::steady_clock::now();
  auto r = PdfDocument::Open(path);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  REQUIRE(r.doc != nullptr);
  CHECK(r.doc->PageCount() == 500);
  CHECK(r.doc->Sizes()[0].wPt == 612.f);
  CHECK(r.doc->Sizes()[0].hPt == 792.f);
  CHECK(r.doc->Sizes()[50].wPt == 792.f); // landscape block
  CHECK(r.doc->Sizes()[50].hPt == 612.f);
  CHECK(ms < 1000.0); // generous: the budget itself is measured by BENCH PDFVIEW
  r.doc.reset();      // closes the file; Windows will not delete an open one
  std::filesystem::remove(path);
}

TEST_CASE("PdfDocument renders a page to a non-blank bitmap and can be cancelled", "[pdfview][req387]") {
  const auto path = WritePdf("gs_pdfview_5.pdf", MakeSyntheticPdf(5, 200, 0));
  auto r = PdfDocument::Open(path);
  REQUIRE(r.doc != nullptr);
  Bitmap bm;
  REQUIRE(r.doc->RenderPage(2, 306, 396, bm, [] { return false; }));
  CHECK(bm.w == 306);
  CHECK(bm.h == 396);
  size_t dark = 0;
  for (size_t i = 0; i + 3 < bm.bgra.size(); i += 4)
    if (bm.bgra[i] < 200)
      ++dark;
  CHECK(dark > 100);

  Bitmap cancelled;
  CHECK_FALSE(r.doc->RenderPage(2, 1224, 1584, cancelled, [] { return true; }));
  CHECK(cancelled.bgra.empty());
  CHECK_FALSE(r.doc->RenderPage(99, 10, 10, cancelled, [] { return false; }));
  r.doc.reset();
  std::filesystem::remove(path);
}

TEST_CASE("PdfDocument returns a stated error for a corrupt, empty, missing or encrypted file", "[pdfview][req387]") {
  const auto junk = WritePdf("gs_pdfview_junk.pdf", "this is not a pdf at all, just some text");
  auto a = PdfDocument::Open(junk);
  CHECK(a.doc == nullptr);
  CHECK_FALSE(a.error.empty());
  std::filesystem::remove(junk);

  const auto empty = WritePdf("gs_pdfview_empty.pdf", "");
  auto b = PdfDocument::Open(empty);
  CHECK(b.doc == nullptr);
  CHECK_FALSE(b.error.empty());
  std::filesystem::remove(empty);

  auto c = PdfDocument::Open(std::filesystem::temp_directory_path() / "gs_pdfview_does_not_exist.pdf");
  CHECK(c.doc == nullptr);
  CHECK_FALSE(c.error.empty());

  // A standard-security-handler PDF whose user password is not the empty string.
  const std::string zeros(64, '0');
  const std::string enc =
      "%PDF-1.4\n1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
      "2 0 obj\n<< /Type /Pages /Count 1 /Kids [3 0 R] >>\nendobj\n"
      "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] >>\nendobj\n"
      "4 0 obj\n<< /Filter /Standard /V 1 /R 2 /P -4 /O <" + zeros + "> /U <" + zeros + "> >>\nendobj\n"
      "trailer\n<< /Size 5 /Root 1 0 R /Encrypt 4 0 R /ID [<00112233445566778899aabbccddeeff><00112233445566778899aabbccddeeff>] >>\n"
      "startxref\n0\n%%EOF\n";
  const auto encPath = WritePdf("gs_pdfview_enc.pdf", enc);
  auto d = PdfDocument::Open(encPath);
  CHECK(d.doc == nullptr);
  CHECK(d.error.find("password") != std::string::npos);
  std::filesystem::remove(encPath);
}

// Hidden benchmark (run by name): thumbnail render cost on a real, large construction set.
//   set GOSURVEY_BENCH_PDF to the file, then: GoSurveyTests "thumbnail render cost on a real PDF"
TEST_CASE("thumbnail render cost on a real PDF", "[.thumbbench]") {
  const char* env = std::getenv("GOSURVEY_BENCH_PDF");
  if (env == nullptr)
    SKIP("GOSURVEY_BENCH_PDF not set");
  const auto t0 = std::chrono::steady_clock::now();
  auto r = PdfDocument::Open(std::filesystem::path(env));
  REQUIRE(r.doc != nullptr);
  const double openMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("open: %.0f ms, %d pages\n", openMs, r.doc->PageCount());

  struct Mode {
    const char* name;
    int side;
    int flags;
  };
  constexpr int kNoSmooth = 0x1000 | 0x2000 | 0x4000;  // FPDF_RENDER_NO_SMOOTHTEXT | IMAGE | PATH
  const Mode modes[] = {
      {"288 annot+lcd (current)", 288, 0x01 | 0x02},
      {"288 flags 0", 288, 0},
      {"192 flags 0", 192, 0},
      {"192 no-smooth", 192, kNoSmooth},
      {"160 no-smooth", 160, kNoSmooth},
      {"192 no-smooth+limitedcache", 192, kNoSmooth | 0x200},
  };
  const int n = std::min(r.doc->PageCount(), 40);
  for (const Mode& m : modes) {
    double total = 0, worst = 0;
    for (int p = 0; p < n; ++p) {
      const PageSize s = r.doc->Sizes()[static_cast<size_t>(p)];
      const float k = static_cast<float>(m.side) / std::max(s.wPt, s.hPt);
      Bitmap bm;
      const auto a = std::chrono::steady_clock::now();
      r.doc->RenderPage(p, std::max(1, static_cast<int>(s.wPt * k)), std::max(1, static_cast<int>(s.hPt * k)), bm,
                        [] { return false; }, m.flags);
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
      total += ms;
      worst = std::max(worst, ms);
    }
    std::printf("%-30s %d pages: avg %.1f ms, worst %.1f ms, total %.0f ms\n", m.name, n, total / n, worst, total);
  }
}

// Where does the time go for one page of a big construction set? (hidden; same environment variable)
TEST_CASE("where thumbnail time goes on a real PDF", "[.thumbbench2]") {
  const char* env = std::getenv("GOSURVEY_BENCH_PDF");
  if (env == nullptr)
    SKIP("GOSURVEY_BENCH_PDF not set");
  FPDF_InitLibrary();
  FPDF_DOCUMENT doc = FPDF_LoadDocument(env, nullptr);
  REQUIRE(doc != nullptr);
  auto now = [] { return std::chrono::steady_clock::now(); };
  auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  for (int p = 0; p < 12; ++p) {
    const auto t0 = now();
    FPDF_PAGE page = FPDF_LoadPage(doc, p);
    const auto t1 = now();
    const int objs = FPDFPage_CountObjects(page);  // forces the content stream to be parsed
    const auto t2 = now();
    int images = 0, paths = 0, texts = 0;
    for (int i = 0; i < objs; ++i) {
      FPDF_PAGEOBJECT o = FPDFPage_GetObject(page, i);
      const int t = FPDFPageObj_GetType(o);
      images += t == FPDF_PAGEOBJ_IMAGE;
      paths += t == FPDF_PAGEOBJ_PATH;
      texts += t == FPDF_PAGEOBJ_TEXT;
    }
    const bool hasThumb = FPDFPage_GetThumbnailAsBitmap(page) != nullptr;
    // Render 192 px with everything.
    const float w = FPDF_GetPageWidthF(page), h = FPDF_GetPageHeightF(page);
    const float k = 192.f / std::max(w, h);
    const int bw = std::max(1, static_cast<int>(w * k)), bh = std::max(1, static_cast<int>(h * k));
    std::vector<uint8_t> buf(static_cast<size_t>(bw) * bh * 4, 0xFF);
    FPDF_BITMAP bm = FPDFBitmap_CreateEx(bw, bh, FPDFBitmap_BGRA, buf.data(), bw * 4);
    const auto t3 = now();
    FPDF_RenderPageBitmap(bm, page, 0, 0, bw, bh, 0, 0);
    const auto t4 = now();
    // Again on the already-parsed page.
    FPDF_RenderPageBitmap(bm, page, 0, 0, bw, bh, 0, 0);
    const auto t5 = now();
    // Without the images.
    for (int i = objs - 1; i >= 0; --i) {
      FPDF_PAGEOBJECT o = FPDFPage_GetObject(page, i);
      if (FPDFPageObj_GetType(o) == FPDF_PAGEOBJ_IMAGE) {
        FPDFPage_RemoveObject(page, o);
        FPDFPageObj_Destroy(o);
      }
    }
    FPDFPage_GenerateContent(page);
    const auto t6 = now();
    FPDFBitmap_FillRect(bm, 0, 0, bw, bh, 0xFFFFFFFFu);
    FPDF_RenderPageBitmap(bm, page, 0, 0, bw, bh, 0, 0);
    const auto t7 = now();
    std::printf(
        "p%-3d %5.0fx%-5.0f objs %6d (img %d path %d text %d) thumb=%d | load %.0f parse %.0f render %.0f render-again %.0f | "
        "no-images render %.0f\n",
        p + 1, w, h, objs, images, paths, texts, hasThumb ? 1 : 0, ms(t0, t1), ms(t1, t2), ms(t3, t4), ms(t4, t5),
        ms(t6, t7));
    FPDFBitmap_Destroy(bm);
    FPDF_ClosePage(page);
  }
  FPDF_CloseDocument(doc);
}

TEST_CASE("draft thumbnail cost per page on a real PDF", "[.thumbbench3]") {
  const char* env = std::getenv("GOSURVEY_BENCH_PDF");
  if (env == nullptr)
    SKIP("GOSURVEY_BENCH_PDF not set");
  auto r = PdfDocument::Open(std::filesystem::path(env));
  REQUIRE(r.doc != nullptr);
  double total = 0;
  for (int p = 0; p < 10; ++p) {
    const PageSize s = r.doc->Sizes()[static_cast<size_t>(p)];
    const float k = 256.f / std::max(s.wPt, s.hPt);
    Bitmap bm;
    const auto a = std::chrono::steady_clock::now();
    r.doc->RenderPage(p, static_cast<int>(s.wPt * k), static_cast<int>(s.hPt * k), bm, [] { return false; }, 0, 120);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
    total += ms;
    std::printf("draft p%d: %.0f ms (partial=%d)\n", p + 1, ms, bm.partial ? 1 : 0);
  }
  std::printf("10 drafts: %.0f ms\n", total);
}
