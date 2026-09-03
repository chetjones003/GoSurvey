// REQ-079 / ADR-030 — .gs format version handling and forward migration.
//
// The chaining logic is tested with SYNTHETIC step tables rather than only through whichever
// migrations happen to exist. Today there are none, so testing only the production table would
// test nothing at all — and the composition is precisely the part that must still be correct in
// three years when a v1 file meets a v7 build.

#include <catch2/catch_test_macros.hpp>

#include "io/GsMigrate.hpp"

#include <string>
#include <vector>

using nlohmann::json;

namespace {

// Records the order steps ran in, so composition can be asserted rather than inferred.
bool AppendMark(json& doc, const char* mark)
{
  if (!doc.contains("trail") || !doc["trail"].is_array())
    doc["trail"] = json::array();
  doc["trail"].push_back(mark);
  return true;
}

bool V1toV2(json& doc, std::string&) { return AppendMark(doc, "1->2"); }
bool V2toV3(json& doc, std::string&) { return AppendMark(doc, "2->3"); }
bool V3toV4(json& doc, std::string&) { return AppendMark(doc, "3->4"); }

bool AlwaysFails(json&, std::string& err)
{
  err = "field cannot be represented in the new format";
  return false;
}

const GsMigrationStep kChain[] = {
    {1, "one to two", &V1toV2},
    {2, "two to three", &V2toV3},
    {3, "three to four", &V3toV4},
};

}  // namespace

TEST_CASE("A file at the current version needs no migration", "[gsmigrate]")
{
  json doc = json::object();
  std::vector<std::string> log;
  std::string err;

  REQUIRE(ApplyGsMigrations(doc, 4, 4, kChain, 3, log, err));
  CHECK(err.empty());
  CHECK(log.empty());               // nothing reported, because nothing happened
  CHECK_FALSE(doc.contains("trail"));
}

TEST_CASE("A one-version-old file runs exactly one step", "[gsmigrate]")
{
  json doc = json::object();
  std::vector<std::string> log;
  std::string err;

  REQUIRE(ApplyGsMigrations(doc, 3, 4, kChain, 3, log, err));
  REQUIRE(doc["trail"].size() == 1);
  CHECK(doc["trail"][0] == "3->4");
  CHECK(log.size() == 1);
}

// The heart of ADR-030 (c): a drawing several versions old is carried forward by composing steps
// that were each written against exactly one change.
TEST_CASE("Migrations compose in ascending order across several versions", "[gsmigrate]")
{
  json doc = json::object();
  std::vector<std::string> log;
  std::string err;

  REQUIRE(ApplyGsMigrations(doc, 1, 4, kChain, 3, log, err));
  REQUIRE(doc["trail"].size() == 3);
  CHECK(doc["trail"][0] == "1->2");
  CHECK(doc["trail"][1] == "2->3");
  CHECK(doc["trail"][2] == "3->4");
  CHECK(log.size() == 3);
}

TEST_CASE("A file from a newer build is refused, and says so precisely", "[gsmigrate]")
{
  json doc = json::object();
  std::vector<std::string> log;
  std::string err;

  CHECK_FALSE(ApplyGsMigrations(doc, 9, 4, kChain, 3, log, err));
  // The message must point at the real cause — a downgrade — not at file corruption.
  CHECK(err.find("newer version") != std::string::npos);
  CHECK(err.find("9") != std::string::npos);
  CHECK(err.find("4") != std::string::npos);
}

TEST_CASE("A missing or nonsensical version is refused as malformed", "[gsmigrate]")
{
  json doc = json::object();
  std::vector<std::string> log;
  std::string err;

  CHECK_FALSE(ApplyGsMigrations(doc, 0, 4, kChain, 3, log, err));
  CHECK_FALSE(err.empty());
  CHECK_FALSE(ApplyGsMigrations(doc, -3, 4, kChain, 3, log, err));
  CHECK_FALSE(err.empty());
}

TEST_CASE("A gap in the chain fails loudly rather than skipping a step", "[gsmigrate]")
{
  // 1->2 and 3->4 exist, but nothing upgrades 2->3.
  const GsMigrationStep gapped[] = {
      {1, "one to two", &V1toV2},
      {3, "three to four", &V3toV4},
  };
  json doc = json::object();
  std::vector<std::string> log;
  std::string err;

  // Skipping the missing step would hand the typed loader a document in a shape it does not
  // expect — silent misreads instead of a refusal.
  CHECK_FALSE(ApplyGsMigrations(doc, 1, 4, gapped, 2, log, err));
  CHECK(err.find("no migration available") != std::string::npos);
  CHECK(err.find("2") != std::string::npos);
}

TEST_CASE("A failing step reports which version pair failed and why", "[gsmigrate]")
{
  const GsMigrationStep breaking[] = {
      {1, "one to two", &V1toV2},
      {2, "cannot be represented", &AlwaysFails},
  };
  json doc = json::object();
  std::vector<std::string> log;
  std::string err;

  CHECK_FALSE(ApplyGsMigrations(doc, 1, 3, breaking, 2, log, err));
  CHECK(err.find("2") != std::string::npos);
  CHECK(err.find("cannot be represented") != std::string::npos);
  // The first step still ran and is still reported — the log says how far it got.
  CHECK(log.size() == 1);
}

TEST_CASE("The production entry point accepts a current-version document", "[gsmigrate]")
{
  json doc = json::object();
  std::vector<std::string> log;
  std::string err;

  REQUIRE(MigrateGsDocument(doc, kGsFormatVersion, kGsFormatVersion, log, err));
  CHECK(log.empty());
  CHECK(err.empty());

  CHECK_FALSE(MigrateGsDocument(doc, kGsFormatVersion + 1, kGsFormatVersion, log, err));  // newer file
  CHECK_FALSE(err.empty());
}

TEST_CASE("The production migration chain reaches every version up to the current one", "[gsmigrate]")
{
  // Every solid-geometry bump so far (ellipse edges, procedural edges, NURBS faces) is a pure
  // relabel: an older document has none of the new geometry, so it carries forward byte-identically
  // and the chain must simply not have a gap.
  json doc = json::object();
  std::vector<std::string> log;
  std::string err;

  REQUIRE(MigrateGsDocument(doc, 1, kGsFormatVersion, log, err));
  CHECK(err.empty());
  CHECK(static_cast<int>(log.size()) == kGsFormatVersion - 1);
}
