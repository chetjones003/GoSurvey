#pragma once

#include <string>
#include <string_view>

namespace StringUtil {

/// Leading / trailing ASCII whitespace removed (survey + CAD token hygiene).
[[nodiscard]] std::string trimCopy(std::string_view s);

/// Lowercase mapping for ASCII letters only; rest copied unchanged (command aliases, layer names).
[[nodiscard]] std::string toLowerAsciiCopy(std::string_view s);

} // namespace StringUtil
