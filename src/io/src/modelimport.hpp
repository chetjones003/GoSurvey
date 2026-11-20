#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// Format-neutral result of importing a 3D model (REQ-065).
///
/// These types started life inside the glTF reader. They moved here when a second format (STL, via
/// the DWG conversion route) needed the same shape: one importer's result type is a detail, two
/// importers sharing one is an interface, and the `gltf` names remain as aliases so nothing that
/// already spoke them had to change.
namespace modelimport {

/// Placement of the imported model in the drawing's LOCAL coordinate frame.
///
/// Applied in double throughout: a model dropped at state-plane coordinates would lose REQ-101
/// precision if the offset were added after narrowing to float (the local-storage invariant).
struct Options {
  double unitScale = 1.0;  ///< Multiplies source units. Model authoring units (inches, millimetres)
                           ///< rarely match a survey drawing's feet, and nothing in the file is
                           ///< reliable enough to assume.
  double insertX = 0.0;
  double insertY = 0.0;
  double insertZ = 0.0;
};

/// One imported object. Named so a model keeps its structure rather than arriving as one blob.
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
  /// Specific reason the file was rejected. Never a generic "failed" (REQ-201): the caller shows
  /// this to the user, and "unsupported" is not a diagnosis.
  std::string error;
  /// Features present in the file that this importer does not bring in — stated, never dropped
  /// in silence.
  std::vector<std::string> skipped;

  std::vector<float> vertsXyz;
  std::vector<float> normalsXyz;
  std::vector<std::uint32_t> indices;
  std::vector<Part> parts;

  [[nodiscard]] int triangleCount() const { return static_cast<int>(indices.size() / 3); }
  [[nodiscard]] int vertexCount() const { return static_cast<int>(vertsXyz.size() / 3); }
};

} // namespace modelimport
