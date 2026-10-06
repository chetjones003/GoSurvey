// REQ-390 / ADR-067 addendum 2: PDF page scale - the maths, the standard /VP Measure text, reading a scale
// out of other programs' files, and writing one with Save As. Generated PDFs, no window, no GL.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <catch2/catch_test_macros.hpp>

#include "pdf/PdfAnnotate.hpp"
#include "pdf/PdfDocument.hpp"
#include "pdf/PdfMeasure.hpp"
#include "pdf/PdfRaw.hpp"
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

bool Close(double a, double b, double rel = 1e-9) { return std::fabs(a - b) <= rel * std::max(1.0, std::fabs(b)); }

size_t Count(const std::string& hay, const std::string& needle) {
  size_t n = 0;
  for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
    ++n;
  return n;
}

const PageScale* Preset(const char* label) {
  for (const PageScale& s : PresetScales())
    if (s.label == label)
      return &s;
  return nullptr;
}

// A small PDF in the way Acrobat and Bluebeam write them: the page tree lives in an object stream and the
// cross-reference is a stream, so none of it is readable as plain text.
std::string ObjStmPdf(const std::string& pageExtra) {
  const std::string o2 = "<</Type/Pages/Kids[3 0 R]/Count 1>>";
  const std::string o3 = "<</Type/Page/Parent 2 0 R/MediaBox[0 0 612 792]" + pageExtra + ">>";
  const std::string hdr = "2 0 3 " + std::to_string(o2.size() + 1) + " ";
  const std::string stm = hdr + o2 + " " + o3;
  std::string out = "%PDF-1.5\n%\xE2\xE3\xCF\xD3\n";
  const size_t off1 = out.size();
  out += "1 0 obj\n<</Type/Catalog/Pages 2 0 R>>\nendobj\n";
  const size_t off4 = out.size();
  out += "4 0 obj\n<</Type/ObjStm/N 2/First " + std::to_string(hdr.size()) + "/Length " + std::to_string(stm.size()) +
         ">>\nstream\n" + stm + "\nendstream\nendobj\n";
  const size_t off5 = out.size();
  std::string rows;
  const auto put = [&](int type, size_t a, int b) {
    rows.push_back(static_cast<char>(type));
    for (int s = 24; s >= 0; s -= 8)
      rows.push_back(static_cast<char>((a >> s) & 0xFF));
    rows.push_back(static_cast<char>((b >> 8) & 0xFF));
    rows.push_back(static_cast<char>(b & 0xFF));
  };
  put(0, 0, 65535);
  put(1, off1, 0);
  put(2, 4, 0); // object 2 is entry 0 of object stream 4
  put(2, 4, 1);
  put(1, off4, 0);
  put(1, off5, 0);
  out += "5 0 obj\n<</Type/XRef/Size 6/W[1 4 2]/Root 1 0 R/Length " + std::to_string(rows.size()) + ">>\nstream\n" + rows +
         "\nendstream\nendobj\nstartxref\n" + std::to_string(off5) + "\n%%EOF\n";
  return out;
}

const char* kAcrobatVp =
    "/VP[<</Type/Viewport/BBox[0 0 612 792]/Measure<</Type/Measure/Subtype/RL/R(1/8 in = 1 ft)"
    "/X[<</Type/NumberFormat/U(ft)/C 0.111111111/F/D/D 100>>]/D[<</Type/NumberFormat/U(ft)/C 1/F/D/D 100>>]"
    "/A[<</Type/NumberFormat/U(sq ft)/C 1/F/D/D 100>>]/O[0 0]>>>>]";

} // namespace

TEST_CASE("scale maths: calibration, presets and typed ratios agree", "[pdfmeasure][req390][issue732]") {
  // 100 pt apart on the sheet is 50 ft in reality.
  const PageScale cal = ScaleFromCalibration(100.0, 50.0, Unit::Foot);
  REQUIRE(cal.Valid());
  CHECK(Close(cal.PointsToReal(100.0), 50.0));
  CHECK(Close(cal.PointsToReal(250.0), 125.0, 1e-4)); // within 0.01 %
  CHECK(Close(cal.SqPointsToReal(100.0 * 40.0), 50.0 * 20.0));

  const PageScale* arch = Preset("1/8\" = 1'-0\"");
  REQUIRE(arch != nullptr);
  CHECK(Close(arch->PointsToReal(9.0), 1.0)); // 1/8 in = 9 pt is one foot
  const PageScale* eng = Preset("1\" = 20'");
  REQUIRE(eng != nullptr);
  CHECK(Close(eng->PointsToReal(72.0), 20.0));
  const PageScale* metric = Preset("1:100");
  REQUIRE(metric != nullptr);
  CHECK(Close(metric->PointsToReal(72.0 / 25.4), 0.1)); // 1 mm on the sheet is 100 mm = 0.1 m
  CHECK(metric->realUnit == Unit::Metre);

  // A typed ratio "1 in = 20 ft" is the same scale as the preset and as the matching calibration.
  PageScale typed;
  typed.pageValue = 1;
  typed.pageUnit = Unit::Inch;
  typed.realValue = 20;
  typed.realUnit = Unit::Foot;
  CHECK(Close(typed.RealPerPoint(), eng->RealPerPoint()));
  CHECK(Close(ScaleFromCalibration(72.0, 20.0, Unit::Foot).RealPerPoint(), typed.RealPerPoint()));
  CHECK(typed.RatioText() == "1 in = 20 ft");

  // Refused input.
  CHECK_FALSE(ScaleFromCalibration(0.0, 10.0, Unit::Foot).Valid());
  CHECK_FALSE(ScaleFromCalibration(100.0, -1.0, Unit::Foot).Valid());
}

TEST_CASE("units parse from the names and marks other programs use", "[pdfmeasure][req390][issue732]") {
  Unit u;
  for (const char* t : {"ft", "FT", "foot", "Feet", "'"}) {
    REQUIRE(ParseUnit(t, u));
    CHECK(u == Unit::Foot);
  }
  REQUIRE(ParseUnit("\"", u));
  CHECK(u == Unit::Inch);
  REQUIRE(ParseUnit("Meters", u));
  CHECK(u == Unit::Metre);
  CHECK_FALSE(ParseUnit("furlong", u));
  for (Unit x : AllUnits()) { // every unit survives its own label
    Unit back;
    REQUIRE(ParseUnit(UnitLabel(x), back));
    CHECK(back == x);
  }
}

TEST_CASE("the viewport text round-trips and third-party scales are read", "[pdfmeasure][req390][issue732]") {
  const PageScale s = *Preset("1\" = 20'");
  const std::string vp = BuildViewport(s, 0, 0, 612, 792);
  PageScale back;
  std::string why;
  REQUIRE(ParseViewport("<</Type/Page" + vp + ">>", back, why));
  CHECK(why.empty());
  CHECK(Close(back.RealPerPoint(), s.RealPerPoint()));
  CHECK(back.realUnit == Unit::Foot);
  CHECK(back.RatioText() == "1\" = 20'"); // the label travels in /R

  // Acrobat-style entry.
  PageScale a;
  REQUIRE(ParseViewport(std::string("<</Type/Page") + kAcrobatVp + ">>", a, why));
  CHECK(Close(a.PointsToReal(9.0), 1.0, 1e-6));
  CHECK(a.realUnit == Unit::Foot);

  // No measure data: not an error, just no scale.
  CHECK_FALSE(ParseViewport("<</Type/Page/MediaBox[0 0 10 10]>>", a, why));
  CHECK(why.empty());
  // Measure data that cannot be used: refused with a reason.
  CHECK_FALSE(ParseViewport("<</VP[<</Measure<</Type/Measure/Subtype/RL/X[<</U(furlong)/C 2>>]>>>>]>>", a, why));
  CHECK_FALSE(why.empty());
  CHECK_FALSE(ParseViewport("<</VP[<</Measure<</Type/Measure/Subtype/GEO/GPTS[0 0 1 1]>>>>]>>", a, why));
  CHECK_FALSE(why.empty());
  CHECK_FALSE(ParseViewport("<</VP[<</Measure<</Type/Measure/Subtype/RL/X[<</U(ft)/C 0>>]>>>>]>>", a, why));
  CHECK_FALSE(why.empty());
}

TEST_CASE("a scale is read from a file whose objects are compressed-style streams", "[pdfmeasure][req390][issue732]") {
  const auto p = WriteBytes("gs_measure_objstm.pdf", ObjStmPdf(kAcrobatVp));
  const ScaleRead r = ReadPageScales(p);
  INFO(r.error);
  REQUIRE(r.error.empty());
  REQUIRE(r.scales.count(0) == 1);
  CHECK(Close(r.scales.at(0).PointsToReal(9.0), 1.0, 1e-6));
  CHECK(r.unusable.empty());

  const auto none = WriteBytes("gs_measure_objstm_none.pdf", ObjStmPdf(""));
  const ScaleRead r2 = ReadPageScales(none);
  CHECK(r2.error.empty());
  CHECK(r2.scales.empty());

  const auto bad = WriteBytes("gs_measure_objstm_bad.pdf",
                              ObjStmPdf("/VP[<</Type/Viewport/Measure<</Type/Measure/Subtype/RL/X[<</U(furlong)/C 2>>]>>>>]"));
  const ScaleRead r3 = ReadPageScales(bad);
  CHECK(r3.scales.empty());
  CHECK(r3.unusable.count(0) == 1);

  CHECK_FALSE(ReadPageScales(TempPath("gs_measure_missing.pdf")).error.empty());
  std::filesystem::remove(p);
  std::filesystem::remove(none);
  std::filesystem::remove(bad);
}

TEST_CASE("Save As writes page scales that read back, replace old ones, and leave the source alone",
          "[pdfmeasure][req390][issue732]") {
  const auto src = WriteBytes("gs_measure_src.pdf", MakeSyntheticPdf(3, 2, 0));
  const std::string before = ReadAll(src);
  const auto dst = TempPath("gs_measure_out.pdf");
  std::filesystem::remove(dst);

  std::map<int, PageScale> sc;
  sc[0] = *Preset("1\" = 20'");
  sc[2] = ScaleFromCalibration(100.0, 50.0, Unit::Foot);
  const std::vector<Annot> items = {[] {
    Annot a;
    a.kind = Annot::Kind::Line;
    a.page = 1;
    a.x0 = 10;
    a.y0 = 10;
    a.x1 = 90;
    a.y1 = 60;
    return a;
  }()};
  const std::string err = SaveAnnotated(src, items, dst, sc);
  INFO(err);
  REQUIRE(err.empty());
  CHECK(ReadAll(src) == before);

  // Still a valid, openable PDF with its pages and its annotation.
  auto d = PdfDocument::Open(dst);
  REQUIRE(d.doc != nullptr);
  CHECK(d.doc->PageCount() == 3);
  d.doc.reset();
  CHECK(ReadAnnotations(dst).size() == 1);

  const ScaleRead r = ReadPageScales(dst);
  INFO(r.error);
  REQUIRE(r.error.empty());
  REQUIRE(r.scales.size() == 2);
  CHECK(Close(r.scales.at(0).PointsToReal(72.0), 20.0, 1e-6));
  CHECK(r.scales.at(0).RatioText() == "1\" = 20'");
  INFO("page 2 scale: " << r.scales.at(2).realValue << " " << UnitLabel(r.scales.at(2).realUnit) << " per "
                        << r.scales.at(2).pageValue << " " << UnitLabel(r.scales.at(2).pageUnit));
  CHECK(Close(r.scales.at(2).PointsToReal(100.0), 50.0, 1e-6));
  CHECK(r.scales.count(1) == 0);

  // A second Save As from the saved file: change page 0, clear page 2. No page ends up with two scales.
  const auto dst2 = TempPath("gs_measure_out2.pdf");
  std::filesystem::remove(dst2);
  std::map<int, PageScale> change;
  change[0] = *Preset("1:100");
  change[2] = PageScale{0.0, Unit::Inch, 0.0, Unit::Foot, "", "", 0.0}; // invalid = remove
  const std::string err2 = SaveAnnotated(dst, {}, dst2, change);
  INFO(err2);
  REQUIRE(err2.empty());
  const ScaleRead r2 = ReadPageScales(dst2);
  REQUIRE(r2.error.empty());
  REQUIRE(r2.scales.size() == 1);
  CHECK(Close(r2.scales.at(0).PointsToReal(72.0 / 25.4), 0.1, 1e-6));
  CHECK(r2.scales.at(0).realUnit == Unit::Metre);
  CHECK(Count(ReadAll(dst2), "/Type/Viewport") == 1);

  // A page outside the file is refused and nothing is written.
  const auto bad = TempPath("gs_measure_bad.pdf");
  std::filesystem::remove(bad);
  std::map<int, PageScale> oob;
  oob[9] = *Preset("1:100");
  CHECK_FALSE(SaveAnnotated(src, {}, bad, oob).empty());
  CHECK_FALSE(std::filesystem::exists(bad));

  std::filesystem::remove(src);
  std::filesystem::remove(dst);
  std::filesystem::remove(dst2);
}

TEST_CASE("a scale in a compressed-style file can be replaced by Save As", "[pdfmeasure][req390][issue732]") {
  const auto src = WriteBytes("gs_measure_objstm_src.pdf", ObjStmPdf(kAcrobatVp));
  const auto dst = TempPath("gs_measure_objstm_out.pdf");
  std::filesystem::remove(dst);
  std::map<int, PageScale> change;
  change[0] = *Preset("1\" = 50'");
  const std::string err = SaveAnnotated(src, {}, dst, change);
  INFO(err);
  REQUIRE(err.empty());
  const ScaleRead r = ReadPageScales(dst);
  REQUIRE(r.error.empty());
  REQUIRE(r.scales.count(0) == 1);
  CHECK(Close(r.scales.at(0).PointsToReal(72.0), 50.0, 1e-6));
  CHECK(Count(ReadAll(dst), "/Type/Viewport") == 1);
  std::filesystem::remove(src);
  std::filesystem::remove(dst);
}

TEST_CASE("undo and redo cover page scales and mark the session unsaved", "[pdfmeasure][req390][issue732]") {
  AnnotSession s;
  CHECK_FALSE(s.Dirty());
  std::map<int, PageScale> a;
  a[0] = *Preset("1\" = 20'");
  CHECK(s.SetScales(a));
  CHECK(s.Dirty());
  CHECK(s.Scales().size() == 1);
  CHECK_FALSE(s.SetScales(a)); // no change, no undo step
  REQUIRE(s.Undo());
  CHECK(s.Scales().empty());
  CHECK_FALSE(s.Dirty());
  REQUIRE(s.Redo());
  CHECK(s.Scales().at(0) == a.at(0));
  s.MarkSaved();
  CHECK_FALSE(s.Dirty());
}

TEST_CASE("raw PDF helpers find objects and the page order", "[pdfmeasure][req390][issue732]") {
  const std::string pdf = MakeSyntheticPdf(4, 1, 0);
  const auto objs = raw::ScanObjects(pdf);
  std::vector<int> pages;
  std::string why;
  REQUIRE(raw::PageObjects(pdf, objs, pages, why));
  REQUIRE(pages.size() == 4);
  CHECK(pages[0] == 4); // MakeSyntheticPdf: (page, content) pairs from object 4
  CHECK(pages[3] == 10);
  const std::string withXref = raw::AppendXref(pdf);
  REQUIRE_FALSE(withXref.empty());
  const auto p = WriteBytes("gs_measure_rebuilt.pdf", withXref);
  auto d = PdfDocument::Open(p);
  REQUIRE(d.doc != nullptr);
  CHECK(d.doc->PageCount() == 4);
  d.doc.reset();
  std::filesystem::remove(p);
}
