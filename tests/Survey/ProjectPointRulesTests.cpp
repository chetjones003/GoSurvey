// REQ-377 (issue #696 P4) — which points of the project database a drawing shows. Pure: no window,
// no drawing.

#include "ProjectPointRules.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

projpts::Entry E(int id, const char* desc, double z, const char* source) {
  projpts::Entry e;
  e.point.id = id;
  e.point.description = desc;
  e.point.elevation = z;
  e.sourceDrawing = source;
  return e;
}

projpts::Db SampleDb() {
  projpts::Db db;
  db.points = {E(1, "EG", 100.0, "Drawings/EG.dwg"), E(2, "EG", 110.0, "Drawings/EG.dwg"),
               E(3, "FG", 105.0, "Drawings/FG.dwg"), E(4, "FG", 120.0, "Drawings/FG.dwg"),
               E(10, "TREE", 101.0, "Drawings/EG.dwg")};
  return db;
}

std::vector<int> Ids(const std::vector<const projpts::Entry*>& v) {
  std::vector<int> out;
  for (const auto* e : v)
    out.push_back(e->point.id);
  return out;
}

}  // namespace

TEST_CASE("req377 no filters shows every point", "[req377]") {
  const projpts::Db db = SampleDb();
  const projpts::Rules r;
  CHECK(Ids(projpts::Visible(r, nullptr, db)) == std::vector<int>{1, 2, 3, 4, 10});
  CHECK(projpts::HiddenCount(r, nullptr, db) == 0);
}

TEST_CASE("req377 each filter on its own", "[req377]") {
  const projpts::Db db = SampleDb();
  {
    projpts::Rules r;
    r.idRanges = "1-2, 10";
    CHECK(Ids(projpts::Visible(r, nullptr, db)) == std::vector<int>{1, 2, 10});
  }
  {
    projpts::Rules r;
    r.description = "eg*";  // wildcard, case-insensitive
    CHECK(Ids(projpts::Visible(r, nullptr, db)) == std::vector<int>{1, 2});
  }
  {
    projpts::Rules r;
    r.useElevation = true;
    r.elevMin = 105.0;
    r.elevMax = 110.0;
    CHECK(Ids(projpts::Visible(r, nullptr, db)) == std::vector<int>{2, 3});
  }
  {
    projpts::Rules r;
    r.elevMin = 120.0;  // a reversed range still works; the bounds are ignored while the filter is off
    r.elevMax = 100.0;
    CHECK(projpts::Visible(r, nullptr, db).size() == 5);
    r.useElevation = true;
    CHECK(Ids(projpts::Visible(r, nullptr, db)) == std::vector<int>{1, 2, 3, 4, 10});
  }
  {
    projpts::Rules r;
    r.sourceDrawing = "Drawings/FG.dwg";
    CHECK(Ids(projpts::Visible(r, nullptr, db)) == std::vector<int>{3, 4});
  }
  {
    PointGroupRule g;
    g.descriptionMatch = "TREE";
    g.explicitIds = {4};
    projpts::Rules r;
    r.group = "Trees";
    CHECK(Ids(projpts::Visible(r, &g, db)) == std::vector<int>{4, 10});
    // A group that no longer exists matches nothing — never everything.
    CHECK(projpts::Visible(r, nullptr, db).empty());
  }
}

TEST_CASE("req377 combined filters must all match", "[req377]") {
  const projpts::Db db = SampleDb();
  projpts::Rules r;
  r.description = "EG";
  r.useElevation = true;
  r.elevMin = 105.0;
  r.elevMax = 200.0;
  r.sourceDrawing = "Drawings/EG.dwg";
  CHECK(Ids(projpts::Visible(r, nullptr, db)) == std::vector<int>{2});
  CHECK(projpts::HiddenCount(r, nullptr, db) == 4);
}

TEST_CASE("req377 created points are pinned and hidden points win", "[req377]") {
  const projpts::Db db = SampleDb();
  projpts::Rules r;
  r.description = "EG";
  projpts::AddId(&r.shown, 3);  // created here: shown although it does not match
  CHECK(Ids(projpts::Visible(r, nullptr, db)) == std::vector<int>{1, 2, 3});
  projpts::AddId(&r.hidden, 1);  // hide here only
  projpts::AddId(&r.hidden, 3);  // hidden beats pinned
  CHECK(Ids(projpts::Visible(r, nullptr, db)) == std::vector<int>{2});
  projpts::RemoveId(&r.hidden, 3);
  CHECK(Ids(projpts::Visible(r, nullptr, db)) == std::vector<int>{2, 3});
  // The database is untouched by any of this.
  CHECK(db.points.size() == 5);
}

TEST_CASE("req377 an unreadable number range is reported and matches nothing", "[req377]") {
  const projpts::Db db = SampleDb();
  projpts::Rules r;
  r.idRanges = "abc";
  std::vector<std::string> bad;
  CHECK(projpts::Visible(r, nullptr, db, &bad).empty());
  CHECK(bad.size() == 1);
}

TEST_CASE("req377 id lists round-trip through compact text", "[req377]") {
  const std::vector<int> ids = {1, 2, 3, 7, 9, 10, 11, 500};
  CHECK(projpts::IdsToText(ids) == "1-3,7,9-11,500");
  CHECK(projpts::IdsFromText("1-3,7,9-11,500") == ids);
  CHECK(projpts::IdsToText({}).empty());
  CHECK(projpts::IdsFromText("").empty());
  projpts::Rules r;
  CHECK(r.IsDefault());
  projpts::AddId(&r.shown, 5);
  CHECK_FALSE(r.IsDefault());
}
