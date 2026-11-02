#pragma once

#include "GsIo.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// These tests need the full DWG-trailer document payload. A user-facing .gst is intentionally
// narrower and must not be used as a shortcut for arbitrary document round trips.
inline bool SaveGoSurveyDocumentJsonForTest(const AppCommandState& state, const std::filesystem::path& path,
                                            std::vector<std::string>& log) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    log.push_back("Could not open test payload for write.");
    return false;
  }
  file << SerializeGoSurveyJson(state);
  if (!file) {
    log.push_back("Could not finish writing test payload.");
    return false;
  }
  return true;
}

inline bool LoadGoSurveyDocumentJsonForTest(AppCommandState& state, const std::filesystem::path& path,
                                            std::vector<std::string>& log) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    log.push_back("Could not open test payload for read.");
    return false;
  }
  const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return LoadGoSurveyFromJsonUtf8(state, json, log);
}
