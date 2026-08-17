#include "TelemetryPing.hpp"

#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <random>

std::string BuildTelemetryJson(const TelemetryPayload& payload) {
  std::ostringstream oss;
  oss << "{"
      << "\"installId\":\"" << payload.installId << "\","
      << "\"event\":\"" << payload.event << "\","
      << "\"version\":\"" << payload.version << "\","
      << "\"channel\":\"" << payload.channel << "\","
      << "\"os\":\"" << payload.os << "\""
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

std::string DecideEventToSend(const std::string& installId, const std::string& lastActivePingDate) {
  if (installId.empty()) {
    return "install";
  }

  if (lastActivePingDate.empty()) {
    return "active";
  }

  std::string today = GetTodayDateString();
  if (IsAfter24Hours(lastActivePingDate, today)) {
    return "active";
  }

  return "";
}

std::string GetTodayDateString() {
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);

  std::tm tm_buf;
#ifdef _WIN32
  localtime_s(&tm_buf, &time);
#else
  localtime_r(&time, &tm_buf);
#endif

  std::ostringstream oss;
  oss << std::put_time(&tm_buf, "%Y-%m-%d");
  return oss.str();
}

bool IsAfter24Hours(const std::string& oldDate, const std::string& newDate) {
  if (oldDate.empty() || newDate.empty() || oldDate.length() != 10 || newDate.length() != 10) {
    return false;
  }

  try {
    int oldYear = std::stoi(oldDate.substr(0, 4));
    int oldMonth = std::stoi(oldDate.substr(5, 2));
    int oldDay = std::stoi(oldDate.substr(8, 2));

    int newYear = std::stoi(newDate.substr(0, 4));
    int newMonth = std::stoi(newDate.substr(5, 2));
    int newDay = std::stoi(newDate.substr(8, 2));

    std::tm oldTm = {};
    oldTm.tm_year = oldYear - 1900;
    oldTm.tm_mon = oldMonth - 1;
    oldTm.tm_mday = oldDay;

    std::tm newTm = {};
    newTm.tm_year = newYear - 1900;
    newTm.tm_mon = newMonth - 1;
    newTm.tm_mday = newDay;

    auto oldTime = std::mktime(&oldTm);
    auto newTime = std::mktime(&newTm);

    auto diff = std::difftime(newTime, oldTime);
    return diff >= 86400.0;
  } catch (...) {
    return false;
  }
}
