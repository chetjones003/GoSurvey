// REQ-350 / TASK-275 — the Pipe Fittings palette's pure filtering and grouping layer.
//
// No GL and no ImGui here on purpose: everything that decides WHICH parts a tab shows is a function
// over `CadBlockLibraryEntry`, so the rules can be pinned without a window. What the thumbnail LOOKS
// like is not testable here (and `project.md`'s anti-requirements rule out framebuffer goldens) —
// that is a by-eye check.

#include "CadBlocks.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

CadBlockLibraryEntry Part(const char* name, CadPipePartType type, const char* size,
                          CadPipePressureClass cls = CadPipePressureClass::None) {
  CadBlockLibraryEntry e;
  e.name = name;
  e.path = std::string("resources/blocks/fittings/") + name + ".sat";
  e.imported = false;
  e.isFitting = true;
  e.partType = type;
  e.nominalSize = size;
  e.pressureClass = cls;
  return e;
}

/// An ordinary, non-piping library block — a matchline, a north arrow, anything in
/// `resources/blocks/` that is not a fitting.
CadBlockLibraryEntry PlainBlock(const char* name) {
  CadBlockLibraryEntry e;
  e.name = name;
  e.path = std::string("resources/blocks/") + name + ".dxf";
  e.isFitting = false;
  return e;
}

std::vector<std::string> NamesOf(const std::vector<CadBlockLibraryEntry>& rows) {
  std::vector<std::string> out;
  out.reserve(rows.size());
  for (const CadBlockLibraryEntry& e : rows)
    out.push_back(e.name);
  return out;
}

} // namespace

TEST_CASE("Every part type files under exactly one palette category", "[issue486][req350][palette]") {
  CHECK(CadPipePaletteCategoryOf(CadPipePartType::Elbow90) == CadPipePaletteCategory::Fittings);
  CHECK(CadPipePaletteCategoryOf(CadPipePartType::Elbow45) == CadPipePaletteCategory::Fittings);
  CHECK(CadPipePaletteCategoryOf(CadPipePartType::Tee) == CadPipePaletteCategory::Fittings);
  CHECK(CadPipePaletteCategoryOf(CadPipePartType::Cross) == CadPipePaletteCategory::Fittings);
  CHECK(CadPipePaletteCategoryOf(CadPipePartType::Reducer) == CadPipePaletteCategory::Fittings);
  CHECK(CadPipePaletteCategoryOf(CadPipePartType::Coupling) == CadPipePaletteCategory::Fittings);
  CHECK(CadPipePaletteCategoryOf(CadPipePartType::Flange) == CadPipePaletteCategory::Flanges);
  CHECK(CadPipePaletteCategoryOf(CadPipePartType::Valve) == CadPipePaletteCategory::Valves);
  CHECK(CadPipePaletteCategoryOf(CadPipePartType::Nozzle) == CadPipePaletteCategory::Nozzles);
  CHECK(CadPipePaletteCategoryOf(CadPipePartType::Cap) == CadPipePaletteCategory::Other);
  CHECK(CadPipePaletteCategoryOf(CadPipePartType::Other) == CadPipePaletteCategory::Other);
  // Total by construction: an untagged part answers a real category rather than falling off the end.
  CHECK(CadPipePaletteCategoryOf(CadPipePartType::None) == CadPipePaletteCategory::Other);
}

TEST_CASE("The new nozzle part type round-trips through its tag", "[issue486][req350][palette]") {
  CHECK(CadPipePartTypeTag(CadPipePartType::Nozzle) == "nozzle");
  CHECK(ParseCadPipePartType("nozzle") == CadPipePartType::Nozzle);
  // Adding it must not have disturbed any existing tag.
  CHECK(ParseCadPipePartType("flange") == CadPipePartType::Flange);
  CHECK(ParseCadPipePartType("other") == CadPipePartType::Other);
  // An unknown tag — including one from a future version — still reads as "not a piping part".
  CHECK(ParseCadPipePartType("wye") == CadPipePartType::None);
  CHECK(ParseCadPipePartType("") == CadPipePartType::None);
}

TEST_CASE("A 2in run lists the 2in flanges and not the 4in one", "[issue486][req350][palette]") {
  // The bundled library, as it actually ships after TASK-275's sidecars.
  const std::vector<CadBlockLibraryEntry> lib = {
      Part("2IN_BLIND_FLANGE", CadPipePartType::Flange, "2in"),
      Part("2in_WELD_NECK_FLANGE", CadPipePartType::Flange, "2in"),
      Part("CJ_4in_WELD_NECK_FLANGE", CadPipePartType::Flange, "4in"),
  };
  std::vector<CadBlockLibraryEntry> rows;

  CadPipePaletteCollectRows(lib, "2in", CadPipePressureClass::None, CadPipePaletteCategory::Flanges,
                            &rows);
  CHECK(NamesOf(rows) == std::vector<std::string>{"2IN_BLIND_FLANGE", "2in_WELD_NECK_FLANGE"});

  // The same library, routed at 4in, lists the other part and neither 2in one.
  CadPipePaletteCollectRows(lib, "4in", CadPipePressureClass::None, CadPipePaletteCategory::Flanges,
                            &rows);
  CHECK(NamesOf(rows) == std::vector<std::string>{"CJ_4in_WELD_NECK_FLANGE"});

  // Flanges are not fittings: the Fittings tab is empty for this library at either size.
  CadPipePaletteCollectRows(lib, "2in", CadPipePressureClass::None, CadPipePaletteCategory::Fittings,
                            &rows);
  CHECK(rows.empty());
}

TEST_CASE("Size matching is numeric, not textual", "[issue486][req350][palette]") {
  const std::vector<CadBlockLibraryEntry> lib = {
      Part("SPACED", CadPipePartType::Flange, "2 in"),
      Part("TRAILING_ZERO", CadPipePartType::Flange, "2.0in"),
      Part("PLAIN", CadPipePartType::Flange, "2in"),
      Part("HALF_INCH_SMALLER", CadPipePartType::Flange, "1.5in"),
  };
  std::vector<CadBlockLibraryEntry> rows;

  // All three spellings of two inches are one size...
  CadPipePaletteCollectRows(lib, "2in", CadPipePressureClass::None, CadPipePaletteCategory::Flanges,
                            &rows);
  CHECK(NamesOf(rows) == std::vector<std::string>{"SPACED", "TRAILING_ZERO", "PLAIN"});

  // ...and the run's own label is parsed the same way, so "2.0in" routed finds them too.
  CadPipePaletteCollectRows(lib, " 2.0in ", CadPipePressureClass::None,
                            CadPipePaletteCategory::Flanges, &rows);
  CHECK(rows.size() == 3);

  // A nearby size is NOT offered (REQ-350 (c) — no reducer/branch rule this increment).
  CadPipePaletteCollectRows(lib, "1.5in", CadPipePressureClass::None, CadPipePaletteCategory::Flanges,
                            &rows);
  CHECK(NamesOf(rows) == std::vector<std::string>{"HALF_INCH_SMALLER"});
}

TEST_CASE("An unusable run size offers nothing, never everything", "[issue486][req350][palette]") {
  const std::vector<CadBlockLibraryEntry> lib = {
      Part("A", CadPipePartType::Flange, "2in"),
      Part("B", CadPipePartType::Flange, "4in"),
  };
  std::vector<CadBlockLibraryEntry> rows;
  for (const char* bad : {"", "   ", "flange", "2", "-2in", "1/2in"}) {
    CadPipePaletteCollectRows(lib, bad, CadPipePressureClass::None, CadPipePaletteCategory::Flanges,
                              &rows);
    CHECK(rows.empty());
  }
  // A part whose OWN size is unusable is equally unmatchable, rather than matching anything.
  const std::vector<CadBlockLibraryEntry> untagged = {Part("NO_SIZE", CadPipePartType::Flange, "")};
  CadPipePaletteCollectRows(untagged, "2in", CadPipePressureClass::None,
                            CadPipePaletteCategory::Flanges, &rows);
  CHECK(rows.empty());
}

TEST_CASE("Only tagged piping parts reach the palette", "[issue486][req350][palette]") {
  const std::vector<CadBlockLibraryEntry> lib = {
      PlainBlock("_matchline_EASTING"),
      Part("UNTAGGED_FITTING", CadPipePartType::None, "2in"),
      Part("REAL_FLANGE", CadPipePartType::Flange, "2in"),
  };
  std::vector<CadBlockLibraryEntry> rows;
  // The untagged part would land in Other if it were let through — it is not.
  CadPipePaletteCollectRows(lib, "2in", CadPipePressureClass::None, CadPipePaletteCategory::Other,
                            &rows);
  CHECK(rows.empty());
  CadPipePaletteCollectRows(lib, "2in", CadPipePressureClass::None, CadPipePaletteCategory::Flanges,
                            &rows);
  CHECK(NamesOf(rows) == std::vector<std::string>{"REAL_FLANGE"});
}

TEST_CASE("Pressure class follows the catalog's exact-then-agnostic precedence",
          "[issue486][req350][palette]") {
  std::vector<CadBlockLibraryEntry> rows;

  SECTION("a run with no class accepts every size match, whatever its tag") {
    const std::vector<CadBlockLibraryEntry> lib = {
        Part("AGNOSTIC", CadPipePartType::Flange, "2in"),
        Part("ONE_FIFTY", CadPipePartType::Flange, "2in", CadPipePressureClass::CS150),
        Part("THREE_HUNDRED", CadPipePartType::Flange, "2in", CadPipePressureClass::CS300),
    };
    CadPipePaletteCollectRows(lib, "2in", CadPipePressureClass::None,
                              CadPipePaletteCategory::Flanges, &rows);
    CHECK(rows.size() == 3);
  }

  SECTION("an exact class wins, and a different class never matches") {
    const std::vector<CadBlockLibraryEntry> lib = {
        Part("AGNOSTIC", CadPipePartType::Flange, "2in"),
        Part("ONE_FIFTY", CadPipePartType::Flange, "2in", CadPipePressureClass::CS150),
        Part("THREE_HUNDRED", CadPipePartType::Flange, "2in", CadPipePressureClass::CS300),
    };
    CadPipePaletteCollectRows(lib, "2in", CadPipePressureClass::CS150,
                              CadPipePaletteCategory::Flanges, &rows);
    CHECK(NamesOf(rows) == std::vector<std::string>{"ONE_FIFTY"});
  }

  SECTION("a class-agnostic part is accepted when nothing carries the exact class") {
    const std::vector<CadBlockLibraryEntry> lib = {
        Part("AGNOSTIC", CadPipePartType::Flange, "2in"),
        Part("THREE_HUNDRED", CadPipePartType::Flange, "2in", CadPipePressureClass::CS300),
    };
    CadPipePaletteCollectRows(lib, "2in", CadPipePressureClass::CS150,
                              CadPipePaletteCategory::Flanges, &rows);
    CHECK(NamesOf(rows) == std::vector<std::string>{"AGNOSTIC"});
  }

  SECTION("precedence is per part type, so a classed elbow cannot hide an untagged tee") {
    const std::vector<CadBlockLibraryEntry> lib = {
        Part("ELBOW_150", CadPipePartType::Elbow90, "2in", CadPipePressureClass::CS150),
        Part("ELBOW_ANY", CadPipePartType::Elbow90, "2in"),
        Part("TEE_ANY", CadPipePartType::Tee, "2in"),
    };
    CadPipePaletteCollectRows(lib, "2in", CadPipePressureClass::CS150,
                              CadPipePaletteCategory::Fittings, &rows);
    // The elbow resolves to its exact-class part; the tee, which has no classed sibling, stays.
    CHECK(NamesOf(rows) == std::vector<std::string>{"ELBOW_150", "TEE_ANY"});
  }
}

TEST_CASE("An empty tab states the size it filtered on", "[issue486][req350][palette]") {
  const std::string flanges = CadPipePaletteEmptyReason(CadPipePaletteCategory::Flanges, "2in");
  CHECK(flanges.find("2in") != std::string::npos);
  CHECK(flanges.find("flanges") != std::string::npos);

  // Category prose is per category, not one generic word.
  CHECK(CadPipePaletteEmptyReason(CadPipePaletteCategory::Other, "4in").find("other parts") !=
        std::string::npos);
  CHECK(CadPipePaletteEmptyReason(CadPipePaletteCategory::Nozzles, "4in").find("nozzles") !=
        std::string::npos);

  // With no size yet the message says THAT, rather than naming an empty size.
  const std::string noSize = CadPipePaletteEmptyReason(CadPipePaletteCategory::Flanges, "");
  CHECK(noSize.find("PIPERUN") != std::string::npos);
}

TEST_CASE("Palette tab labels are present for every category", "[issue486][req350][palette]") {
  for (int i = 0; i < kCadPipePaletteCategoryCount; ++i) {
    const auto c = static_cast<CadPipePaletteCategory>(i);
    CHECK_FALSE(CadPipePaletteCategoryLabel(c).empty());
    CHECK_FALSE(CadPipePaletteCategoryPlural(c).empty());
  }
}
