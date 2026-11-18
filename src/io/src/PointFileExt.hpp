#pragma once

// REQ-083 — which extension a saved point file gets.
//
// `.csv` and `.txt` are two spellings of one comma-delimited point file, so a name the user
// already spelled with either is written as typed and only a name carrying neither picks up a
// default. That rule is one `if`, but it is the one part of REQ-083 with a real failure mode
// (`points.txt.csv`), and its home — `WinFileDialogs.cpp` — is Win32-only and cannot be linked by
// the Catch2 target. So it lives here, pure, for the same reason `io/SurveyCsvValidate.hpp` exists.
//
// It is in `util/` rather than beside that header in `io/` because its caller is Platform, the
// bottom layer: a Platform → IO include would be an upward dependency (architecture §11
// invariant 1). `util/` depends on nothing and is legal from anywhere.

#include <cctype>
#include <string_view>

namespace pointfile {

/// True when \p name ends with \p ext, comparing ASCII letters case-insensitively (so `POINTS.TXT`
/// counts). A name shorter than the extension simply does not end with it.
[[nodiscard]] inline bool EndsWithIgnoreCaseAscii(std::string_view name, std::string_view ext) {
  if (name.size() < ext.size())
    return false;
  const std::string_view tail = name.substr(name.size() - ext.size());
  for (size_t i = 0; i < ext.size(); ++i) {
    const unsigned char a = static_cast<unsigned char>(tail[i]);
    const unsigned char b = static_cast<unsigned char>(ext[i]);
    if (std::tolower(a) != std::tolower(b))
      return false;
  }
  return true;
}

/// The extension to append to \p typedName in the Export points save dialog — empty when the name
/// already ends in `.csv` or `.txt` and must be left exactly as typed.
///
/// \p txtFilterChosen says the user selected the Text (*.txt) filter, which is the only signal the
/// common dialog gives about which of the two they meant for an extension-less name.
///
/// Only those two extensions count. `points.dat` is not a point file this project writes, so it
/// still gets a default appended rather than being trusted — matching too loosely would hand the
/// user a file whose name says one thing and whose contents say another.
[[nodiscard]] inline std::string_view ExtensionToAppend(std::string_view typedName, bool txtFilterChosen) {
  if (EndsWithIgnoreCaseAscii(typedName, ".csv") || EndsWithIgnoreCaseAscii(typedName, ".txt"))
    return {};
  return txtFilterChosen ? ".txt" : ".csv";
}

} // namespace pointfile
