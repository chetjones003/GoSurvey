#pragma once
// Public IO seam for format-neutral 3D model import (glTF/GLB and STL).

#include <cstdint>
#include <string>
#include <vector>

namespace modelimport {

struct Options {
  double unitScale = 1.0;
  double insertX = 0.0;
  double insertY = 0.0;
  double insertZ = 0.0;
};

struct Part {
  std::string name;
  int indexBegin = 0;
  int indexCount = 0;
  float r = 0.78f;
  float g = 0.78f;
  float b = 0.78f;
};

struct Result {
  bool ok = false;
  std::string error;
  std::vector<std::string> skipped;

  std::vector<float> vertsXyz;
  std::vector<float> normalsXyz;
  std::vector<std::uint32_t> indices;
  std::vector<Part> parts;

  [[nodiscard]] int triangleCount() const { return static_cast<int>(indices.size() / 3); }
  [[nodiscard]] int vertexCount() const { return static_cast<int>(vertsXyz.size() / 3); }
};

} // namespace modelimport

namespace gltf {
using ImportOptions = modelimport::Options;
using ImportedPart = modelimport::Part;
using ImportResult = modelimport::Result;

[[nodiscard]] ImportResult ImportGltfFile(const std::string& pathUtf8, const ImportOptions& opt);
} // namespace gltf

namespace stl {
[[nodiscard]] modelimport::Result ImportStlFile(const std::string& pathUtf8,
                                                const modelimport::Options& opt);
} // namespace stl
