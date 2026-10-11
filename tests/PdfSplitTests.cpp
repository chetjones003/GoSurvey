// REQ-389 / ADR-067: splitting a PDF into a new PDF of chosen pages. Generated PDFs, no window, no GL.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <catch2/catch_test_macros.hpp>

#include "pdf/PdfDocument.hpp"
#include "pdf/PdfSplit.hpp"
#include "pdf/PdfViewerCore.hpp"

#include <fpdf_text.h>
#include <fpdfview.h>

#include <atomic>
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

// The text on page `page` of an open document ("Page N" for MakeSyntheticPdf).
std::string PageText(const std::filesystem::path& file, int page) {
  const std::string bytes = ReadAll(file);
  std::lock_guard<std::recursive_mutex> lock(PdfiumMutex());
  FPDF_InitLibrary();
  FPDF_DOCUMENT d = FPDF_LoadMemDocument(bytes.data(), static_cast<int>(bytes.size()), nullptr);
  REQUIRE(d != nullptr);
  FPDF_PAGE p = FPDF_LoadPage(d, page);
  REQUIRE(p != nullptr);
  FPDF_TEXTPAGE tp = FPDFText_LoadPage(p);
  std::u16string u;
  u.resize(64);
  const int n = FPDFText_GetText(tp, 0, 60, reinterpret_cast<unsigned short*>(u.data()));
  std::string s;
  for (int i = 0; i + 1 < n; ++i)
    s += static_cast<char>(u[static_cast<size_t>(i)]);
  FPDFText_ClosePage(tp);
  FPDF_ClosePage(p);
  FPDF_CloseDocument(d);
  return s;
}

} // namespace

TEST_CASE("ParsePageList reads pages and ranges in the order typed", "[pdfsplit][req389][issue732]") {
  const PageListResult r = ParsePageList("1-5, 9, 12-20", 20);
  REQUIRE(r.error.empty());
  const std::vector<int> want = {0, 1, 2, 3, 4, 8, 11, 12, 13, 14, 15, 16, 17, 18, 19};
  CHECK(r.pages == want);

  const PageListResult order = ParsePageList(" 9 ,2 - 3,9", 20);
  REQUIRE(order.error.empty());
  const std::vector<int> want2 = {8, 1, 2, 8};
  CHECK(order.pages == want2); // typed order kept, a duplicate repeated, spaces allowed
}

TEST_CASE("ParsePageList refuses every bad input with a message", "[pdfsplit][req389][issue732]") {
  for (const char* bad : {"", "   ", "0", "21", "1-21", "9-5", "abc", "1,,2", "1,", ",1", "-3", "3-", "1 2", "1-2-3", "1.5",
                          "99999999999"}) {
    const PageListResult r = ParsePageList(bad, 20);
    INFO("input: \"" << bad << "\"");
    CHECK_FALSE(r.error.empty());
    CHECK(r.pages.empty());
  }
}

TEST_CASE("SplitPdf keeps the chosen pages in order with their size and text", "[pdfsplit][req389][issue732]") {
  // 20 pages; every 5 pages the sheet flips between portrait and landscape.
  const auto src = WriteBytes("gs_split_src.pdf", MakeSyntheticPdf(20, 5, 5));
  const auto dst = TempPath("gs_split_out.pdf");
  std::filesystem::remove(dst);
  const PageListResult pl = ParsePageList("1-5, 9, 12-20", 20);
  REQUIRE(pl.error.empty());
  std::atomic<int> progress{0};
  REQUIRE(SplitPdf(src, pl.pages, dst, &progress).empty());
  CHECK(progress.load() == 15);

  auto a = PdfDocument::Open(src);
  auto b = PdfDocument::Open(dst);
  REQUIRE(a.doc != nullptr);
  REQUIRE(b.doc != nullptr);
  REQUIRE(b.doc->PageCount() == 15);
  for (int i = 0; i < 15; ++i) {
    const int from = pl.pages[static_cast<size_t>(i)];
    CHECK(b.doc->Sizes()[static_cast<size_t>(i)].wPt == a.doc->Sizes()[static_cast<size_t>(from)].wPt);
    CHECK(b.doc->Sizes()[static_cast<size_t>(i)].hPt == a.doc->Sizes()[static_cast<size_t>(from)].hPt);
    CHECK(PageText(dst, i) == "Page " + std::to_string(from + 1));
  }
  a.doc.reset();
  b.doc.reset();
  std::filesystem::remove(src);
  std::filesystem::remove(dst);
}

TEST_CASE("SplitPdf leaves the source untouched and refuses to replace it", "[pdfsplit][req389][issue732]") {
  const auto src = WriteBytes("gs_split_src2.pdf", MakeSyntheticPdf(6, 3, 0));
  const std::string before = ReadAll(src);
  const auto dst = TempPath("gs_split_out2.pdf");
  std::filesystem::remove(dst);

  CHECK(SplitPdf(src, {0, 2}, dst).empty());
  CHECK(ReadAll(src) == before);                 // byte-for-byte the same
  CHECK(std::filesystem::exists(dst));

  const std::string refused = SplitPdf(src, {0}, src);
  CHECK_FALSE(refused.empty());
  CHECK(ReadAll(src) == before);                 // still unchanged after the refused attempt

  CHECK_FALSE(SplitPdf(src, {}, TempPath("gs_split_none.pdf")).empty());
  CHECK_FALSE(std::filesystem::exists(TempPath("gs_split_none.pdf")));
  CHECK_FALSE(SplitPdf(src, {6}, TempPath("gs_split_oob.pdf")).empty());
  CHECK_FALSE(std::filesystem::exists(TempPath("gs_split_oob.pdf")));
  CHECK_FALSE(SplitPdf(TempPath("gs_split_missing.pdf"), {0}, TempPath("gs_split_m.pdf")).empty());

  // No temporary left behind.
  CHECK_FALSE(std::filesystem::exists(TempPath("gs_split_out2.pdf.gssplit.tmp")));
  std::filesystem::remove(src);
  std::filesystem::remove(dst);
}
