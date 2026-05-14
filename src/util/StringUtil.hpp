#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace StringUtil {

/// Leading / trailing ASCII whitespace removed (survey + CAD token hygiene).
[[nodiscard]] inline std::string trimCopy(std::string_view s) {
  size_t a = 0;
  while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
    ++a;
  size_t b = s.size();
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    --b;
  return std::string(s.substr(a, b - a));
}

/// Lowercase mapping for ASCII letters only; rest copied unchanged (command aliases, layer names).
[[nodiscard]] inline std::string toLowerAsciiCopy(std::string_view s) {
  std::string out;
  out.resize(s.size());
  for (size_t i = 0; i < s.size(); ++i)
    out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
  return out;
}

} // namespace StringUtil
