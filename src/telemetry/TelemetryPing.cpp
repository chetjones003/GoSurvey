#include "TelemetryPing.hpp"

#include <sstream>
#include <random>

std::string BuildTelemetryJson(const TelemetryPayload& payload) {
  std::ostringstream oss;
  oss << "{"
      << "\"installId\":\"" << payload.installId << "\","
      << "\"event\":\"" << payload.event << "\","
      << "\"version\":\"" << payload.version << "\","
      << "\"channel\":\"" << payload.channel << "\","
      << "\"os\":\"" << payload.os << "\","
      << "\"email\":\"" << payload.email << "\""
      << "}";
  return oss.str();
}

std::string GenerateInstallId() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);

  std::ostringstream oss;
  for (int i = 0; i < 32; ++i) {
    oss << std::hex << dis(gen);
  }
  return oss.str();
}

std::string DecideEventToSend(const std::string& installId) {
  return installId.empty() ? "install" : "active";
}
