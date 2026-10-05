// REQ-378 (issue #696 P5) — the pure half of Add Drawing to Project: the preview counts, the
// per-conflict choices, and the rules the added drawing gets. No window, no drawing.

#include "ProjectAddDrawing.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace {

SurveyPoint Pt(int id, double e, double n, double z, const char* desc = "EG") {
  SurveyPoint p;
  p.id = id;
  p.easting = e;
  p.northing = n;
  p.elevation = z;
  p.description = desc;
  return p;
}

projpts::Db DbWith(std::initializer_list<SurveyPoint> pts, const char* source = "Drawings/Old.dwg") {
  projpts::Db db;
  for (const SurveyPoint& p : pts)
    db.points.push_back({p, source});
  return db;
}

}  // namespace

TEST_CASE("req378 summary counts new, identical and differing numbers", "[req378]") {
  const projpts::Db db = DbWith({Pt(1, 10, 10, 1), Pt(2, 20, 20, 2), Pt(3, 30, 30, 3)});
  const std::vector<SurveyPoint> in = {Pt(1, 10, 10, 1),   // identical
                                       Pt(2, 99, 20, 2),   // differs
                                       Pt(3, 30, 30, 9),   // differs
                                       Pt(4, 40, 40, 4),   // new
                                       Pt(5, 50, 50, 5)};  // new
  const projadd::Summary s = projadd::Analyze(db, in);
  CHECK(s.total == 5);
  CHECK(s.fresh == 2);
  CHECK(s.identical == 1);
  CHECK(s.differing == 2);
  CHECK(s.Existing() == 3);
  REQUIRE(s.conflicts.size() == 2);
  CHECK(s.conflicts[0].id == 2);
  CHECK(s.conflicts[0].existing.easting == 20.0);
  CHECK(s.conflicts[0].incoming.easting == 99.0);
  CHECK(s.conflicts[1].id == 3);
}

TEST_CASE("req378 analysing changes nothing and a repeated number counts once", "[req378]") {
  const projpts::Db db = DbWith({Pt(1, 10, 10, 1)});
  const std::vector<SurveyPoint> in = {Pt(7, 1, 1, 1), Pt(7, 2, 2, 2), Pt(1, 10, 10, 1)};
  const projadd::Summary s = projadd::Analyze(db, in);
  CHECK(s.total == 2);
  CHECK(s.duplicates == 1);
  CHECK(db.points.size() == 1);
  CHECK(db.revision == 0);
  CHECK_FALSE(db.dirty);
}

TEST_CASE("req378 new points are added tagged with the source drawing, identical ones are shared", "[req378]") {
  projpts::Db db = DbWith({Pt(1, 10, 10, 1)});
  const std::vector<SurveyPoint> in = {Pt(1, 10, 10, 1), Pt(2, 20, 20, 2)};
  const projadd::Outcome o = projadd::Apply(&db, in, {}, "Drawings/New.dwg", 5.0);
  CHECK(o.added == 1);
  CHECK(o.shared == 1);
  REQUIRE(db.points.size() == 2);
  CHECK(db.points[0].sourceDrawing == "Drawings/Old.dwg");  // an identical point keeps its own tag
  CHECK(db.points[1].sourceDrawing == "Drawings/New.dwg");
  CHECK(o.shownIds == std::vector<int>{1, 2});
  CHECK(db.dirty);
  CHECK(db.revision == 1);
  CHECK(db.dirtySince == 5.0);
}

TEST_CASE("req378 each conflict choice does what it says", "[req378]") {
  projpts::Db db = DbWith({Pt(1, 10, 10, 1), Pt(2, 20, 20, 2), Pt(3, 30, 30, 3), Pt(10, 100, 100, 10)});
  const std::vector<SurveyPoint> in = {Pt(1, 11, 11, 1), Pt(2, 22, 22, 2), Pt(3, 33, 33, 3)};
  const std::map<int, projadd::Choice> choices = {{1, projadd::Choice::Skip},
                                                  {2, projadd::Choice::Overwrite},
                                                  {3, projadd::Choice::Renumber}};
  const projadd::Outcome o = projadd::Apply(&db, in, choices, "Drawings/New.dwg", 1.0);
  CHECK(o.skipped == 1);
  CHECK(o.overwritten == 1);
  CHECK(o.renumbered == 1);
  CHECK(o.added == 0);

  // Skip: the project's point 1 is untouched and the drawing does not show it.
  CHECK(db.points[0].point.easting == 10.0);
  CHECK(db.points[0].sourceDrawing == "Drawings/Old.dwg");
  // Overwrite: point 2 now holds the drawing's data and is tagged with the drawing.
  CHECK(db.points[1].point.easting == 22.0);
  CHECK(db.points[1].sourceDrawing == "Drawings/New.dwg");
  // Renumber: point 3 stays as the project had it; the drawing's one is added above everything (10).
  CHECK(db.points[2].point.easting == 30.0);
  REQUIRE(db.points.size() == 5);
  CHECK(db.points[4].point.id == 11);
  CHECK(db.points[4].point.easting == 33.0);
  CHECK(db.points[4].sourceDrawing == "Drawings/New.dwg");
  REQUIRE(o.renumberMap.size() == 1);
  CHECK(o.renumberMap[0] == std::make_pair(3, 11));
  // The drawing shows what it brought: 2 (overwritten) and 11 (renumbered), not the skipped 1.
  CHECK(o.shownIds == std::vector<int>{2, 11});
}

TEST_CASE("req378 a conflict with no choice is skipped, never overwritten", "[req378]") {
  projpts::Db db = DbWith({Pt(1, 10, 10, 1)});
  const projadd::Outcome o = projadd::Apply(&db, {Pt(1, 99, 99, 9)}, {}, "Drawings/New.dwg", 1.0);
  CHECK(o.skipped == 1);
  CHECK(db.points[0].point.easting == 10.0);
  CHECK(db.revision == 0);  // nothing changed
  CHECK_FALSE(db.dirty);
  CHECK(o.shownIds.empty());
}

TEST_CASE("req378 renumbered points never collide with each other or with the drawing's own numbers", "[req378]") {
  projpts::Db db = DbWith({Pt(1, 1, 1, 1), Pt(2, 2, 2, 2)});
  // The drawing holds 1 and 2 (both conflict) and also 3 (new); renumbering 1 and 2 must avoid 3.
  const std::vector<SurveyPoint> in = {Pt(1, 9, 9, 9), Pt(2, 8, 8, 8), Pt(3, 7, 7, 7)};
  const std::map<int, projadd::Choice> all = {{1, projadd::Choice::Renumber}, {2, projadd::Choice::Renumber}};
  const projadd::Outcome o = projadd::Apply(&db, in, all, "Drawings/New.dwg", 1.0);
  CHECK(o.renumbered == 2);
  CHECK(o.added == 1);
  std::vector<int> ids;
  for (const auto& e : db.points)
    ids.push_back(e.point.id);
  std::sort(ids.begin(), ids.end());
  CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());  // all unique
  CHECK(o.shownIds == std::vector<int>{3, 4, 5});
}

TEST_CASE("req378 the rules show exactly the points the drawing brought", "[req378]") {
  projpts::Db db = DbWith({Pt(1, 1, 1, 1), Pt(2, 2, 2, 2), Pt(3, 3, 3, 3), Pt(4, 4, 4, 4)});
  const projadd::Outcome o = projadd::Apply(&db, {Pt(2, 2, 2, 2), Pt(7, 7, 7, 7), Pt(8, 8, 8, 8)}, {}, "d", 0.0);
  const projpts::Rules r = projadd::RulesShowing(o.shownIds);
  const std::vector<const projpts::Entry*> vis = projpts::Visible(r, nullptr, db);
  std::vector<int> ids;
  for (const projpts::Entry* e : vis)
    ids.push_back(e->point.id);
  CHECK(ids == std::vector<int>{2, 7, 8});
}

TEST_CASE("req378 a drawing with no points keeps the default rules", "[req378]") {
  CHECK(projadd::RulesShowing({}).IsDefault());
}
