#include <catch2/catch_test_macros.hpp>

#include "../src/telemetry/TelemetryPing.hpp"

TEST_CASE("TelemetryPing: GenerateInstallId produces 32 hex characters", "[telemetry]") {
  const std::string id = GenerateInstallId();
  REQUIRE(id.length() == 32);
  for (char c : id) {
    REQUIRE(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
  }
}

TEST_CASE("TelemetryPing: Unique install IDs are different", "[telemetry]") {
  const std::string id1 = GenerateInstallId();
  const std::string id2 = GenerateInstallId();
  REQUIRE(id1 != id2);
}

TEST_CASE("TelemetryPing: DecideEventToSend returns install when ID is empty", "[telemetry]") {
  REQUIRE(DecideEventToSend("") == "install");
}

// REQ-080 amended 2026-08-23 (D-2026-08-23-f): the 24h throttle is gone by explicit user
// decision — a ping fires every launch, so DecideEventToSend no longer has a "send nothing"
// case at all once an install id exists.
TEST_CASE("TelemetryPing: DecideEventToSend always returns active once an install id exists", "[telemetry]") {
  const std::string id = "abc123def456";
  REQUIRE(DecideEventToSend(id) == "active");
}

TEST_CASE("TelemetryPing: BuildTelemetryJson creates valid JSON", "[telemetry]") {
  TelemetryPayload payload;
  payload.installId = "abc123def456";
  payload.event     = "install";
  payload.version   = "0.5.2";
  payload.channel   = "stable";
  payload.os        = "windows";
  const std::string json = BuildTelemetryJson(payload);

  REQUIRE(json.find("\"installId\":\"abc123def456\"") != std::string::npos);
  REQUIRE(json.find("\"event\":\"install\"") != std::string::npos);
  REQUIRE(json.find("\"version\":\"0.5.2\"") != std::string::npos);
  REQUIRE(json.find("\"channel\":\"stable\"") != std::string::npos);
  REQUIRE(json.find("\"os\":\"windows\"") != std::string::npos);
  // REQ-080 amended (D-2026-08-23-e): email is present but empty for a signed-out user — the
  // default-constructed payload.email is "", not absent.
  REQUIRE(json.find("\"email\":\"\"") != std::string::npos);
}

TEST_CASE("TelemetryPing: BuildTelemetryJson includes the signed-in email when present", "[telemetry]") {
  TelemetryPayload payload;
  payload.installId = "abc123def456";
  payload.event     = "active";
  payload.version   = "0.5.4";
  payload.channel   = "stable";
  payload.os        = "windows";
  payload.email     = "surveyor@example.com";
  const std::string json = BuildTelemetryJson(payload);

  REQUIRE(json.find("\"email\":\"surveyor@example.com\"") != std::string::npos);
}

TEST_CASE("TelemetryPing: BuildTelemetryJson escapes special characters", "[telemetry]") {
  TelemetryPayload payload;
  payload.installId = "id\"with\"quotes";
  payload.event     = "active";
  payload.version   = "1.0";
  payload.channel   = "beta";
  payload.os        = "windows";
  const std::string json = BuildTelemetryJson(payload);

  REQUIRE(json.find("id\"with\"quotes") != std::string::npos);
}

