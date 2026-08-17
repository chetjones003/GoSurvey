#include <catch2/catch_test_macros.hpp>

#include "CadCommands.hpp"
#include "util/docinvariants.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

// REQ-204: every document invariant must have a fixture that DELIBERATELY BREAKS IT and proves the
// check fires.
//
// This file is the acceptance condition, not a nicety. An oracle that has never failed is not known
// to be an oracle — it is a function that returns an empty vector, and it will keep returning one
// after the property it was meant to guard stops holding. The fuzzer's entire value rests on these
// checks being real, so each one is shown failing here before it is trusted in a fuzz run.
//
// The risk is not theoretical. The first version of docinvariants.cpp counted polylines as
// `userPolylineOffsets.size()`, but that array is CSR (N+1 offsets for N polylines), so the
// attr-counts check reported a violation for every valid drawing containing a polyline. A
// false-positive oracle is worse than no oracle: it files garbage and teaches everyone to ignore
// the harness. Hence also the first test below — the clean state must be SILENT.

namespace {

/// Names of the invariants that fired, for order-independent assertions.
std::vector<std::string> Fired(const AppCommandState& st) {
  std::vector<InvariantViolation> v;
  CheckDocumentInvariants(st, &v);
  std::vector<std::string> names;
  names.reserve(v.size());
  for (const InvariantViolation& iv : v)
    names.emplace_back(iv.name);
  return names;
}

bool Contains(const std::vector<std::string>& names, const char* want) {
  return std::find(names.begin(), names.end(), std::string(want)) != names.end();
}

/// A minimal well-formed drawing: one line, one circle, one 3-vertex polyline, each with its
/// attribute row and a unique id. Every test below starts here and breaks exactly one thing, so a
/// firing check can only be caused by that break.
AppCommandState GoodDrawing() {
  AppCommandState st;

  // One line: two XYZ endpoints (stride 6).
  st.userLinesFlat = {0.f, 0.f, 0.f, 100.f, 0.f, 0.f};
  st.userLineAttrs.resize(1);
  st.userLineAttrs[0].id = 1;

  // One circle: cx, cy, z, r (stride 4).
  st.userCirclesCxCyZR = {50.f, 25.f, 0.f, 10.f};
  st.userCircleAttrs.resize(1);
  st.userCircleAttrs[0].id = 2;

  // One polyline, CSR: 3 vertices, offsets {0, 3}.
  st.userPolylineVerts = {0.f, 100.f, 0.f, 50.f, 150.f, 0.f, 100.f, 100.f, 0.f};
  st.userPolylineOffsets = {0, 3};
  st.userPolylineClosed = {0};
  st.userPolylineAttrs.resize(1);
  st.userPolylineAttrs[0].id = 3;

  st.nextEntityId = 4;  // past every id above
  return st;
}

}  // namespace

// ---------------------------------------------------------------------------
// The control: a sound document must produce NO findings.
// ---------------------------------------------------------------------------

TEST_CASE("A default-constructed drawing violates nothing", "[docinvariants]") {
  const AppCommandState st;
  REQUIRE(Fired(st).empty());
}

TEST_CASE("A well-formed drawing violates nothing", "[docinvariants]") {
  const AppCommandState st = GoodDrawing();
  const std::vector<std::string> names = Fired(st);
  INFO("unexpected findings: " << FormatInvariantViolations([&] {
         std::vector<InvariantViolation> v;
         CheckDocumentInvariants(st, &v);
         return v;
       }()));
  REQUIRE(names.empty());
}

// ---------------------------------------------------------------------------
// flat-strides (architecture §11.8)
// ---------------------------------------------------------------------------

TEST_CASE("flat-strides fires on a partial line vertex", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.userLinesFlat.push_back(1.f);  // 7 floats: not a whole stride-6 segment
  REQUIRE(Contains(Fired(st), docinv::kFlatStrides));
}

TEST_CASE("flat-strides fires on a partial circle record", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.userCirclesCxCyZR.push_back(1.f);  // 5 floats against stride 4
  REQUIRE(Contains(Fired(st), docinv::kFlatStrides));
}

TEST_CASE("flat-strides fires on a partial polyline vertex", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.userPolylineVerts.push_back(1.f);  // 10 floats against stride 3
  REQUIRE(Contains(Fired(st), docinv::kFlatStrides));
}

// ---------------------------------------------------------------------------
// finite-coords
// ---------------------------------------------------------------------------

TEST_CASE("finite-coords fires on a NaN line coordinate", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.userLinesFlat[3] = std::numeric_limits<float>::quiet_NaN();
  REQUIRE(Contains(Fired(st), docinv::kFiniteCoords));
}

TEST_CASE("finite-coords fires on an infinite circle radius", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.userCirclesCxCyZR[3] = std::numeric_limits<float>::infinity();
  REQUIRE(Contains(Fired(st), docinv::kFiniteCoords));
}

TEST_CASE("finite-coords fires on a NaN annotation insertion point", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  CadAnnotation a;
  a.insX = std::numeric_limits<float>::quiet_NaN();
  st.cadAnnotations.push_back(a);
  st.cadAnnotationAttrs.resize(1);
  st.cadAnnotationAttrs[0].id = 10;
  st.nextEntityId = 11;
  REQUIRE(Contains(Fired(st), docinv::kFiniteCoords));
}

// A large-but-finite state-plane coordinate is NOT a violation. Worth pinning: 1e12 is exactly the
// kind of value the fuzzer generates on purpose, and an over-eager range check here would report
// every real Texas state-plane drawing as corrupt.
TEST_CASE("finite-coords accepts large state-plane magnitudes", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.userLinesFlat[0] = 1.e12f;
  REQUIRE_FALSE(Contains(Fired(st), docinv::kFiniteCoords));
}

// ---------------------------------------------------------------------------
// entity-ids (REQ-076)
// ---------------------------------------------------------------------------

TEST_CASE("entity-ids fires when two entities share an id", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.userCircleAttrs[0].id = st.userLineAttrs[0].id;  // collision across stores
  REQUIRE(Contains(Fired(st), docinv::kEntityIds));
}

TEST_CASE("entity-ids fires when nextEntityId would reuse a live id", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.nextEntityId = 2;  // but id 3 is in use, so the next entity created collides
  REQUIRE(Contains(Fired(st), docinv::kEntityIds));
}

// id 0 means "not yet assigned" — a legitimate transient state between creating an entity and the
// next EnsureEntityIds sweep, and NOT a violation. If this ever starts failing, the fuzzer will
// report a finding on every freshly drawn entity.
TEST_CASE("entity-ids tolerates unassigned ids", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.userLineAttrs[0].id = 0;
  st.userCircleAttrs[0].id = 0;
  REQUIRE_FALSE(Contains(Fired(st), docinv::kEntityIds));
}

// ---------------------------------------------------------------------------
// attr-counts
// ---------------------------------------------------------------------------

TEST_CASE("attr-counts fires when a geometry array outruns its attributes", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  // A second line's worth of floats, with no matching attribute row.
  st.userLinesFlat.insert(st.userLinesFlat.end(), {0.f, 0.f, 0.f, 1.f, 1.f, 0.f});
  REQUIRE(Contains(Fired(st), docinv::kAttrCounts));
}

TEST_CASE("attr-counts fires when a polyline has no closed-flag", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.userPolylineClosed.clear();
  REQUIRE(Contains(Fired(st), docinv::kAttrCounts));
}

// ---------------------------------------------------------------------------
// polyline-offsets (CSR)
// ---------------------------------------------------------------------------

TEST_CASE("polyline-offsets fires when offsets run backwards", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.userPolylineOffsets = {0, 3, 1};
  st.userPolylineClosed = {0, 0};
  st.userPolylineAttrs.resize(2);
  st.userPolylineAttrs[1].id = 5;
  st.nextEntityId = 6;
  REQUIRE(Contains(Fired(st), docinv::kPolylineOffsets));
}

TEST_CASE("polyline-offsets fires when the last offset misses the vertex count",
          "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.userPolylineOffsets = {0, 2};  // 3 vertices are stored, so the last polyline reads short
  REQUIRE(Contains(Fired(st), docinv::kPolylineOffsets));
}

TEST_CASE("polyline-offsets fires when the array does not start at zero", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.userPolylineOffsets = {1, 3};
  REQUIRE(Contains(Fired(st), docinv::kPolylineOffsets));
}

// ---------------------------------------------------------------------------
// selection-in-range (architecture §11.9)
// ---------------------------------------------------------------------------

TEST_CASE("selection-in-range fires on an index past the end", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.selection.push_back(SelectedEntity{SelectedEntity::Type::LineSeg, 7});  // only 1 line exists
  REQUIRE(Contains(Fired(st), docinv::kSelectionInRange));
}

TEST_CASE("selection-in-range fires on a negative index", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.selection.push_back(SelectedEntity{SelectedEntity::Type::Circle, -1});
  REQUIRE(Contains(Fired(st), docinv::kSelectionInRange));
}

// The exact case §11.9 exists for: an entity is erased, the array compacts, and a selection index
// that used to be valid now addresses a different entity — or nothing.
TEST_CASE("selection-in-range fires after the selected entity is erased", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.selection.push_back(SelectedEntity{SelectedEntity::Type::Polyline, 0});
  REQUIRE(Fired(st).empty());  // valid before the erase

  st.userPolylineOffsets.clear();
  st.userPolylineVerts.clear();
  st.userPolylineClosed.clear();
  st.userPolylineAttrs.clear();
  REQUIRE(Contains(Fired(st), docinv::kSelectionInRange));
}

TEST_CASE("selection-in-range accepts a valid selection", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.selection.push_back(SelectedEntity{SelectedEntity::Type::LineSeg, 0});
  st.selection.push_back(SelectedEntity{SelectedEntity::Type::Polyline, 0});
  REQUIRE(Fired(st).empty());
}

// ---------------------------------------------------------------------------
// region-loops
// ---------------------------------------------------------------------------

TEST_CASE("region-loops fires on a loop start past the vertex count", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  CadFilledRegion fr;
  fr.vertsXyz = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 0.f};  // 3 vertices
  fr.loopStart = {0, 9};                                        // 9 is a float index, not a vertex
  st.cadFilledRegions.push_back(fr);
  st.cadFilledRegionAttrs.resize(1);
  st.cadFilledRegionAttrs[0].id = 20;
  st.nextEntityId = 21;
  REQUIRE(Contains(Fired(st), docinv::kRegionLoops));
}

TEST_CASE("region-loops fires when the first loop does not start at zero", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  CadFilledRegion fr;
  fr.vertsXyz = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 0.f};
  fr.loopStart = {1};
  st.cadFilledRegions.push_back(fr);
  st.cadFilledRegionAttrs.resize(1);
  st.cadFilledRegionAttrs[0].id = 20;
  st.nextEntityId = 21;
  REQUIRE(Contains(Fired(st), docinv::kRegionLoops));
}

// ---------------------------------------------------------------------------
// survey-label-links (REQ-076: a reference is an id, and a stale id resolves to NOTHING)
// ---------------------------------------------------------------------------

TEST_CASE("survey-label-links fires when a label claims a different point", "[docinvariants]") {
  AppCommandState st = GoodDrawing();

  SurveyPoint p;
  p.id = 101;
  p.labelMtextAnnId = 30;
  st.surveyPoints.push_back(p);

  CadAnnotation a;
  a.kind = CadAnnotation::Kind::Mtext;
  a.surveyPointLabelForId = 999;  // disagrees with the point that points at it
  st.cadAnnotations.push_back(a);
  st.cadAnnotationAttrs.resize(1);
  st.cadAnnotationAttrs[0].id = 30;
  st.nextEntityId = 31;

  REQUIRE(Contains(Fired(st), docinv::kSurveyLabelLinks));
}

// The REQ-076 promise itself: a label that was erased leaves an id resolving to nothing, and that
// is CORRECT, not a violation. Reporting it would punish the very design the requirement asks for.
TEST_CASE("survey-label-links accepts an id that resolves to nothing", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  SurveyPoint p;
  p.id = 101;
  p.labelMtextAnnId = 12345;  // no annotation carries this id — the label was erased
  st.surveyPoints.push_back(p);
  REQUIRE_FALSE(Contains(Fired(st), docinv::kSurveyLabelLinks));
}

TEST_CASE("survey-label-links accepts a consistent point/label pair", "[docinvariants]") {
  AppCommandState st = GoodDrawing();

  SurveyPoint p;
  p.id = 101;
  p.labelMtextAnnId = 30;
  st.surveyPoints.push_back(p);

  CadAnnotation a;
  a.kind = CadAnnotation::Kind::Mtext;
  a.surveyPointLabelForId = 101;
  st.cadAnnotations.push_back(a);
  st.cadAnnotationAttrs.resize(1);
  st.cadAnnotationAttrs[0].id = 30;
  st.nextEntityId = 31;

  REQUIRE(Fired(st).empty());
}

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------

TEST_CASE("FormatInvariantViolations names the invariant and the specifics", "[docinvariants]") {
  AppCommandState st = GoodDrawing();
  st.userLinesFlat.push_back(1.f);

  std::vector<InvariantViolation> v;
  CheckDocumentInvariants(st, &v);
  REQUIRE_FALSE(v.empty());

  const std::string s = FormatInvariantViolations(v);
  REQUIRE(s.find(docinv::kFlatStrides) != std::string::npos);
  REQUIRE(s.find("userLinesFlat") != std::string::npos);
}

TEST_CASE("FormatInvariantViolations is empty for a clean document", "[docinvariants]") {
  REQUIRE(FormatInvariantViolations({}).empty());
}
