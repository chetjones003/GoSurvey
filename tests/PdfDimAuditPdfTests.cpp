// REQ-395: the scale audit on a real PDF - PDFium reads the text and strokes of a generated 36 x 24 in sheet,
// the audit matches them, and the whole run is timed against the 5-second target.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <catch2/catch_test_macros.hpp>

#include "pdf/PdfDimAudit.hpp"
#include "pdf/PdfDocument.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace pdfview;

namespace {

bool Never() { return false; }

std::string BuildSheetPdf(const std::string& content) {
  std::vector<std::string> objs = {
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R]/Count 1>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 2592 1728]/Resources<</Font<</F1 5 0 R>>>>/Contents 4 0 R>>",
      "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream",
      "<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>",
  };
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

std::string Num(double v) {
  char b[32];
  std::snprintf(b, sizeof(b), "%.3f", v);
  return b;
}

// \p count horizontal dimensions on a grid across the sheet, drawn at 1 pt = 0.5 ft; the one at \p longIdx is
// drawn 1 % long.
std::string SheetContent(int count, int longIdx) {
  std::string c = "0.5 w\n";
  for (int i = 0; i < count; ++i) {
    const int col = i % 9, row = i / 9; // 9 columns of 280 pt hold the longest (258 pt) dimension
    const double x = 40.0 + col * 280.0, y = 60.0 + row * 50.0;
    const int feet = 40 + (i * 7) % 90; // 40 .. 129 ft: 80 .. 258 pt
    const double drawn = 2.0 * feet * (i == longIdx ? 1.01 : 1.0);
    c += Num(x) + " " + Num(y) + " m " + Num(x + drawn) + " " + Num(y) + " l S\n";
    c += Num(x) + " " + Num(y - 3) + " m " + Num(x) + " " + Num(y + 9) + " l S\n";
    c += Num(x + drawn) + " " + Num(y - 3) + " m " + Num(x + drawn) + " " + Num(y + 9) + " l S\n";
    c += "BT /F1 8 Tf " + Num(x + drawn / 2 - 14) + " " + Num(y + 3) + " Td (" + std::to_string(feet) + "'-0\") Tj ET\n";
  }
  return c;
}

} // namespace

TEST_CASE("the audit of a real PDF sheet finds the consensus and the long dimension", "[pdfdimaudit][req395][issue732]") {
  const auto path = std::filesystem::temp_directory_path() / "gosurvey_dimaudit.pdf";
  {
    const std::string bytes = BuildSheetPdf(SheetContent(280, 33));
    std::ofstream f(path, std::ios::binary);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  auto r = PdfDocument::Open(path);
  REQUIRE(r.doc != nullptr);

  const auto t0 = std::chrono::steady_clock::now();
  std::vector<DimText> texts;
  std::vector<DimSeg> segs;
  REQUIRE(r.doc->AuditPageData(0, texts, segs, Never));
  CHECK(texts.size() == 280);
  CHECK(segs.size() == 280 * 3);
  DimMatchSet set;
  REQUIRE(MatchDimensions(texts, segs, DimMatchParams{}, Never, set));
  const DimAudit a = AuditDimensions(std::move(set), ScaleFromCalibration(100.0, 50.0, Unit::Foot), CheckLimits{});
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("[bench req395] 36 x 24 in sheet, 280 dimensions: read + match + audit %.3f s\n", secs);
  CHECK(secs <= 5.0);

  CHECK(a.set.matches.size() == 280);
  REQUIRE(a.consensusValid);
  CHECK(std::fabs(a.consensusMetresPerPt / (0.5 * 0.3048) - 1.0) < 1e-4);
  REQUIRE(a.offenders.size() == 1);
  CHECK(a.set.matches[static_cast<size_t>(a.offenders[0])].text == std::to_string(40 + (33 * 7) % 90) + "'-0\"");

  std::vector<DimText> t2;
  std::vector<DimSeg> s2;
  CHECK_FALSE(r.doc->AuditPageData(0, t2, s2, [] { return true; })); // cancelled: nothing kept
  CHECK(t2.empty());
  CHECK_FALSE(r.doc->AuditPageData(7, t2, s2, Never)); // no such page
  r.doc.reset();
}
