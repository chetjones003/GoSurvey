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
  REQUIRE(DecideEventToSend("", "") == "install");
  REQUIRE(DecideEventToSend("", "2026-08-16") == "install");
}

TEST_CASE("TelemetryPing: DecideEventToSend returns active on first active event", "[telemetry]") {
  const std::string id = "abc123def456";
  REQUIRE(DecideEventToSend(id, "") == "active");
}

TEST_CASE("TelemetryPing: DecideEventToSend returns empty when active ping already sent today", "[telemetry]") {
  const std::string id = "abc123def456";
  const std::string today = GetTodayDateString();
  REQUIRE(DecideEventToSend(id, today) == "");
}

TEST_CASE("TelemetryPing: IsAfter24Hours detects day boundary", "[telemetry]") {
  REQUIRE(IsAfter24Hours("2026-08-15", "2026-08-16"));
  REQUIRE(IsAfter24Hours("2026-08-15", "2026-08-17"));
  REQUIRE(!IsAfter24Hours("2026-08-16", "2026-08-16"));
  REQUIRE(!IsAfter24Hours("2026-08-16", "2026-08-15"));
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

TEST_CASE("TelemetryPing: GetTodayDateString returns YYYY-MM-DD format", "[telemetry]") {
  const std::string date = GetTodayDateString();
  REQUIRE(date.length() == 10);
  REQUIRE(date[4] == '-');
  REQUIRE(date[7] == '-');
  for (int i = 0; i < 10; ++i) {
    if (i != 4 && i != 7) {
      REQUIRE((date[i] >= '0' && date[i] <= '9'));
    }
  }
}

TEST_CASE("TelemetryPing: IsAfter24Hours handles invalid dates gracefully", "[telemetry]") {
  REQUIRE(!IsAfter24Hours("", "2026-08-16"));
  REQUIRE(!IsAfter24Hours("2026-08-16", ""));
  REQUIRE(!IsAfter24Hours("invalid", "2026-08-16"));
  REQUIRE(!IsAfter24Hours("2026-08-16", "invalid"));
}
