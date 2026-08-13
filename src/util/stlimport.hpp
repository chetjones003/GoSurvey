#pragma once

#include "modelimport.hpp"

#include <cstdint>
#include <string>
#include <vector>

/// STL reader (REQ-065, the DWG conversion route).
///
/// STL is deliberately NOT the preferred interchange format — ADR-026 rejected it as the primary
/// route because it carries no colour and no object identity, so a model arrives as one grey part.
/// It is here because it is what AutoCAD can emit without Navisworks, and a grey model you can see
/// beats a coloured one you cannot open.
///
/// Pure and dependency-free, so it is unit tested without a window.
namespace stl {

/// Reads binary or ASCII STL. The format is detected from content, not extension: an ASCII file
/// beginning "solid" is unambiguous, but a BINARY file may also begin with those five bytes in its
/// 80-byte header, so the triangle count is cross-checked against the file length before trusting it.
[[nodiscard]] modelimport::Result ImportStlBytes(const std::vector<std::uint8_t>& bytes,
                                                 const modelimport::Options& opt,
                                                 const std::string& partName = "model");

[[nodiscard]] modelimport::Result ImportStlFile(const std::string& pathUtf8, const modelimport::Options& opt);

} // namespace stl
