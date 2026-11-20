#pragma once

#include <cmath>
#include <cstdlib>
#include <map>
#include <string>

// Additional drawing-owned style settings. Unknown keys survive IO; malformed numeric values
// use the caller's existing setting rather than becoming zero through an unchecked conversion.
inline std::string StyleProperty(const std::map<std::string, std::string>& properties,
                                 const std::string& key, const std::string& fallback = {}) {
  const auto it = properties.find(key);
  return it == properties.end() ? fallback : it->second;
}

inline double StyleNumber(const std::map<std::string, std::string>& properties,
                          const std::string& key, double fallback) {
  const auto it = properties.find(key);
  if (it == properties.end() || it->second.empty()) return fallback;
  char* end = nullptr;
  const double value = std::strtod(it->second.c_str(), &end);
  return end != it->second.c_str() && *end == '\0' && std::isfinite(value) ? value : fallback;
}

inline bool StyleFlag(const std::map<std::string, std::string>& properties,
                      const std::string& key, bool fallback = false) {
  return StyleNumber(properties, key, fallback ? 1 : 0) != 0;
}
