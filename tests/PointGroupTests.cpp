// Point-group membership rules (REQ-067) and the raw-description fallback (REQ-066).
//
// Every case below restates one of REQ-067's acceptance conditions. Two are load-bearing and would
// both "pass" under a plausible wrong implementation, so they are called out explicitly:
//   * an all-empty rule must resolve to NOTHING, not to every point — "no filter" read as "match
//     everything" is how a whole drawing silently ends up in a surface;
//   * two filled criteria must UNION, not intersect — the user chose that explicitly
//     (decision log 2026-08-15), and under intersection a hand-picked point outside the id range
//     would be dropped from the group it was deliberately added to.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "survey/PointGroupRule.hpp"

using json = nlohmann::json;

namespace {

/// Just enough of a survey point for rule evaluation.
struct Pt {
  int id;
  std::string desc;
  std::string raw;
};

/// Resolve a rule over a point set, the way the state-facing resolver will.
std::vector<int> Resolve(const PointGroupRule& rule, const std::vector<Pt>& pts) {
  const std::vector<PointIdRange> ranges = ParseIdRanges(rule.idRangesText);
  std::vector<int> out;
  for (const Pt& p : pts)
    if (PointMatchesRule(rule, ranges, p.id, p.desc, p.raw))
      out.push_back(p.id);
  return out;
}

/// A small survey: existing ground, top of curb, and a couple of control points.
std::vector<Pt> Sample() {
  return {
      {1, "EG", "EG"},     {2, "EG", "EG"},       {3, "TOP CURB", "TC"},
      {4, "TOP CURB", "TC"}, {5, "CONTROL", "CP"}, {20, "EG", "EG"},
      {25, "CONTROL", "CP"}, {30, "EG", "EG"},
  };
}

} // namespace

// ---------------------------------------------------------------- wildcard

TEST_CASE("Wildcard matching is case-insensitive and supports * and ?", "[pointgroup]") {
  REQUIRE(WildcardMatchCI("EG*", "EG"));
  REQUIRE(WildcardMatchCI("EG*", "EG SHOT"));
  REQUIRE(WildcardMatchCI("eg*", "EG"));       // pattern case ignored
  REQUIRE(WildcardMatchCI("EG*", "eg"));       // text case ignored
  REQUIRE(WildcardMatchCI("*CURB", "TOP CURB"));
  REQUIRE(WildcardMatchCI("TC?", "TC1"));
  REQUIRE(WildcardMatchCI("*", "anything"));
  REQUIRE(WildcardMatchCI("*", ""));

  REQUIRE_FALSE(WildcardMatchCI("EG", "EGG"));      // no implicit trailing wildcard
  REQUIRE_FALSE(WildcardMatchCI("EG*", "TOP EG"));  // no implicit leading wildcard
  REQUIRE_FALSE(WildcardMatchCI("TC?", "TC"));      // ? needs exactly one character
}

TEST_CASE("An empty pattern matches nothing, never everything", "[pointgroup]") {
  // The distinction the whole feature turns on: an unused criterion contributes no members.
  REQUIRE_FALSE(WildcardMatchCI("", "EG"));
  REQUIRE_FALSE(WildcardMatchCI("", ""));
}

TEST_CASE("A pathological pattern still terminates", "[pointgroup]") {
  // Backtracking matcher, no recursion: a long text against many stars must not hang or overflow.
  const std::string text(2000, 'a');
  REQUIRE(WildcardMatchCI("*a*a*a*a*b", text) == false);
  REQUIRE(WildcardMatchCI("*a*a*a*a*", text));
}

// ---------------------------------------------------------------- id ranges

TEST_CASE("Id ranges include both endpoints and exclude the gaps", "[pointgroup]") {
  const std::vector<PointIdRange> r = ParseIdRanges("1-10, 20-30");
  REQUIRE(IdInRanges(r, 1));   // low endpoint included
  REQUIRE(IdInRanges(r, 10));  // high endpoint included
  REQUIRE(IdInRanges(r, 20));
  REQUIRE(IdInRanges(r, 30));
  for (int id : {11, 15, 19, 31, 0})
    REQUIRE_FALSE(IdInRanges(r, id));
}

TEST_CASE("Id range text tolerates single ids, spacing and stray commas", "[pointgroup]") {
  const std::vector<PointIdRange> r = ParseIdRanges("  1-500 ,1200,  1400-1450 , ");
  REQUIRE(IdInRanges(r, 1));
  REQUIRE(IdInRanges(r, 500));
  REQUIRE(IdInRanges(r, 1200));
  REQUIRE(IdInRanges(r, 1400));
  REQUIRE(IdInRanges(r, 1450));
  REQUIRE_FALSE(IdInRanges(r, 501));
  REQUIRE_FALSE(IdInRanges(r, 1199));
  REQUIRE_FALSE(IdInRanges(r, 1451));
}

TEST_CASE("A reversed range is normalised rather than dropped", "[pointgroup]") {
  const std::vector<PointIdRange> r = ParseIdRanges("30-25");
  REQUIRE(IdInRanges(r, 25));
  REQUIRE(IdInRanges(r, 27));
  REQUIRE(IdInRanges(r, 30));
}

TEST_CASE("Unparseable range tokens are reported, not absorbed", "[pointgroup]") {
  // REQ-201: junk input must be visible. It must also not match everything or nothing-by-accident —
  // the good tokens beside it keep working.
  std::vector<std::string> bad;
  const std::vector<PointIdRange> r = ParseIdRanges("1-10, abc, 20-30, 12xyz", &bad);
  REQUIRE(bad.size() == 2);
  REQUIRE(bad[0] == "abc");
  REQUIRE(bad[1] == "12xyz");
  REQUIRE(IdInRanges(r, 5));
  REQUIRE(IdInRanges(r, 25));
  REQUIRE_FALSE(IdInRanges(r, 15));
}

TEST_CASE("Empty range text yields no ranges", "[pointgroup]") {
  REQUIRE(ParseIdRanges("").empty());
  REQUIRE(ParseIdRanges("   ").empty());
  REQUIRE(ParseIdRanges(",,,").empty());
}

// ---------------------------------------------------------------- resolution

TEST_CASE("A description rule resolves to exactly the matching points", "[pointgroup]") {
  PointGroupRule rule;
  rule.descriptionMatch = "EG*";
  REQUIRE(Resolve(rule, Sample()) == std::vector<int>{1, 2, 20, 30});
}

TEST_CASE("A raw-description rule is independent of an edited description", "[pointgroup][req066]") {
  // The reason REQ-066 exists: the office rewrites the description, the field code stands.
  std::vector<Pt> pts = {{1, "Existing ground at NW corner", "EG"}, {2, "Top of curb", "TC"}};
  PointGroupRule byRaw;
  byRaw.rawDescriptionMatch = "EG";
  REQUIRE(Resolve(byRaw, pts) == std::vector<int>{1});

  PointGroupRule byDesc;
  byDesc.descriptionMatch = "EG";
  REQUIRE(Resolve(byDesc, pts).empty());  // the description no longer says EG
}

TEST_CASE("A raw-description rule falls back to description for pre-REQ-066 points", "[pointgroup][req066]") {
  // A drawing written before the field existed has no raw code. Matching on raw must still find
  // those points rather than silently skipping every one of them.
  std::vector<Pt> legacy = {{1, "EG", /*raw*/ ""}, {2, "TC", /*raw*/ ""}};
  PointGroupRule rule;
  rule.rawDescriptionMatch = "EG";
  REQUIRE(Resolve(rule, legacy) == std::vector<int>{1});
}

TEST_CASE("An all-empty rule resolves to NOTHING, not to every point", "[pointgroup]") {
  // The dangerous default. "No filter" must not mean "the whole drawing".
  PointGroupRule rule;
  REQUIRE(rule.empty());
  REQUIRE(Resolve(rule, Sample()).empty());
}

TEST_CASE("Two filled criteria UNION rather than intersect", "[pointgroup]") {
  // User decision, 2026-08-15. Ids 1-10 OR description EG* — so 20 and 30 come in on description
  // alone, and 3/4/5 come in on id alone.
  PointGroupRule rule;
  rule.idRangesText = "1-10";
  rule.descriptionMatch = "EG*";
  const std::vector<int> got = Resolve(rule, Sample());
  REQUIRE(got == std::vector<int>{1, 2, 3, 4, 5, 20, 30});
  // Under intersection this would have been {1, 2} — assert we are not that.
  REQUIRE(got.size() > 2);
}

TEST_CASE("A hand-picked id stays in the group when it matches nothing else", "[pointgroup]") {
  // This is why union was chosen: under AND, point 25 would be filtered out of the group it was
  // explicitly added to.
  PointGroupRule rule;
  rule.idRangesText = "1-10";
  rule.descriptionMatch = "EG*";
  rule.explicitIds = {25};
  const std::vector<int> got = Resolve(rule, Sample());
  REQUIRE(std::find(got.begin(), got.end(), 25) != got.end());
}

TEST_CASE("An explicit-id group is unchanged by newly imported points", "[pointgroup]") {
  PointGroupRule rule;
  rule.explicitIds = {1, 3};
  std::vector<Pt> pts = Sample();
  const std::vector<int> before = Resolve(rule, pts);
  pts.push_back({99, "EG", "EG"});  // a later import
  REQUIRE(Resolve(rule, pts) == before);
}

TEST_CASE("A rule-based group picks up points imported after it was defined", "[pointgroup]") {
  PointGroupRule rule;
  rule.rawDescriptionMatch = "EG";
  std::vector<Pt> pts = Sample();
  REQUIRE(Resolve(rule, pts) == std::vector<int>{1, 2, 20, 30});
  pts.push_back({99, "EG", "EG"});  // imported later, group never edited
  REQUIRE(Resolve(rule, pts) == std::vector<int>{1, 2, 20, 30, 99});
}

TEST_CASE("A rule that matches nothing resolves to an empty group, not an error", "[pointgroup]") {
  PointGroupRule rule;
  rule.descriptionMatch = "NOSUCHCODE*";
  REQUIRE_FALSE(rule.empty());          // the rule IS configured...
  REQUIRE(Resolve(rule, Sample()).empty());  // ...it simply matches no points
}

// ---------------------------------------------------------------- .gs shape

TEST_CASE("A point group's rule survives .gs serialization", "[pointgroup][gs]") {
  // GsIo.cpp is not linkable here (it pulls the whole command layer), so this pins the JSON shape
  // the saver writes and the reader reads — including the explicit-id array, which is the only
  // non-string field and the one a sloppy round trip would mangle.
  PointGroupRule rule;
  rule.idRangesText = "1-500, 1200, 1400-1450";
  rule.descriptionMatch = "EG*";
  rule.rawDescriptionMatch = "TC?";
  rule.explicitIds = {7, 25, 1000000};

  json o;
  o["name"] = "Existing Ground";
  o["idRanges"] = rule.idRangesText;
  o["descriptionMatch"] = rule.descriptionMatch;
  o["rawDescriptionMatch"] = rule.rawDescriptionMatch;
  o["explicitIds"] = rule.explicitIds;

  const json back = json::parse(o.dump());
  REQUIRE(back.value("name", std::string()) == "Existing Ground");
  REQUIRE(back.value("idRanges", std::string()) == rule.idRangesText);
  REQUIRE(back.value("descriptionMatch", std::string()) == rule.descriptionMatch);
  REQUIRE(back.value("rawDescriptionMatch", std::string()) == rule.rawDescriptionMatch);
  REQUIRE(back["explicitIds"].get<std::vector<int>>() == rule.explicitIds);
}

TEST_CASE("A legacy .gs with no group or rawDescription keys loads as absent", "[pointgroup][gs][req066]") {
  // Mirrors the reader defaults. rawDescription must load EMPTY — never defaulted to a copy of the
  // description, which would make a raw-description rule silently match edited text.
  const json legacyPoint = json::parse(R"({"id":1,"description":"Existing ground","layer":"0"})");
  REQUIRE(legacyPoint.value("rawDescription", std::string()).empty());
  REQUIRE(legacyPoint.value("description", std::string()) == "Existing ground");

  const json legacyDoc = json::parse(R"({"worldDocumentOriginX":0.0})");
  REQUIRE_FALSE(legacyDoc.contains("pointGroups"));  // → no groups, drawing otherwise unchanged
}

TEST_CASE("A deleted point simply stops resolving", "[pointgroup]") {
  // Membership is computed from the current point set, so removal needs no cleanup pass. The stored
  // explicit id may still name the deleted point; it must resolve to nothing rather than to
  // whatever point now sits where it used to.
  PointGroupRule rule;
  rule.explicitIds = {3, 4};
  std::vector<Pt> pts = Sample();
  pts.erase(pts.begin() + 2);  // remove id 3
  REQUIRE(Resolve(rule, pts) == std::vector<int>{4});
}
