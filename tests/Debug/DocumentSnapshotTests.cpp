#include <catch2/catch_test_macros.hpp>
#include <limits>

#include "util/docinvariants.hpp"

TEST_CASE("Document snapshot checker preserves diagnostic identifiers and detail",
          "[Debug][snapshot]") {
  DocumentDiagnosticSnapshot snapshot;
  snapshot.userLinesFlat = {0.0, 0.0, 0.0, 1.0, 1.0};
  snapshot.userLineAttrs = {{7}};
  snapshot.nextEntityId = 7;
  snapshot.selection = {{0, 1, 0}};
  std::vector<InvariantViolation> violations;
  CheckDocumentInvariants(snapshot, &violations);
  REQUIRE(violations.size() == 4);
  CHECK(std::string(violations[0].name) == docinv::kFlatStrides);
  CHECK(violations[0].detail == "userLinesFlat.size()=5 is not a multiple of 6");
  CHECK(std::string(violations[1].name) == docinv::kAttrCounts);
  CHECK(violations[1].detail == "lines: 0 entities but 1 attribute rows");
  CHECK(std::string(violations[2].name) == docinv::kEntityIds);
  CHECK(violations[2].detail ==
        "userLineAttrs[0] has id 7 but nextEntityId is "
        "7 — the next entity created would reuse it");
  CHECK(std::string(violations[3].name) == docinv::kSelectionInRange);
  CHECK(violations[3].detail == "selection[0] type=0 index=1 but only 0 of that type exist");
}

TEST_CASE("Document snapshot checking appends and accepts a null destination",
          "[Debug][snapshot]") {
  DocumentDiagnosticSnapshot snapshot;
  snapshot.userLinesFlat = {0.0, 0.0, 0.0, 1.0, 1.0, 1.0};
  snapshot.userLineAttrs = {{1}};
  snapshot.nextEntityId = 2;
  snapshot.selection = {{0, 0, 1}};
  std::vector<InvariantViolation> violations{{"prior", "kept", -1}};
  CheckDocumentInvariants(snapshot, &violations);
  REQUIRE(violations.size() == 1);
  CHECK(violations.front().detail == "kept");
  CheckDocumentInvariants(snapshot, nullptr);
}
