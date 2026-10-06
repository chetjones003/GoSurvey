// REQ-387 / ADR-067: the PDF viewer's pure core (layout, read-ahead plan, bounded cache) and its
// PDFium wrapper, opened against generated PDFs. No window, no GL.

#include <catch2/catch_test_macros.hpp>

#include "pdf/PdfDocument.hpp"
#include "pdf/PdfViewerCore.hpp"

#include <chrono>
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
