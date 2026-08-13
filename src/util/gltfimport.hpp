#pragma once

#include "modelimport.hpp"

#include <cstdint>
#include <string>
#include <vector>

/// glTF 2.0 / GLB reader (REQ-065 / ADR-026 (b), (d)).
///
/// Written in-tree rather than vendored, per ADR-026 (d): the subset REQ-065 needs — POSITION,
/// NORMAL, indices, node transforms, names and base colours — is small and the spec is published,
/// while a full library carries texture, animation, skin, sparse-accessor and extension handling
/// that this requirement explicitly excludes. The trigger to reconsider is recorded in the ADR: if
/// this exceeds ~600 lines, or a real file needs sparse accessors or Draco, vendor `cgltf`.
///
/// Pure and GL-free, so it is unit tested without a window. It knows nothing about
/// `AppCommandState`; the caller turns an \ref ImportResult into a `CadMesh`.
namespace gltf {

// The result types are format-neutral and live in `modelimport.hpp` — STL (via the DWG conversion
// route) produces the same shape. These aliases keep the glTF-flavoured names working.
using ImportOptions = modelimport::Options;
using ImportedPart = modelimport::Part;
using ImportResult = modelimport::Result;

/// Reads `.gltf` (JSON, with external or data-URI buffers) or `.glb` (binary container).
///
/// **Nothing is committed on failure.** The result is built entirely in local storage and `ok` is
/// set only at the very end, so a truncated file cannot leave a half-imported model behind
/// (REQ-065 acceptance).
[[nodiscard]] ImportResult ImportGltfFile(const std::string& pathUtf8, const ImportOptions& opt);

/// Same, from bytes already in memory. \p baseDir resolves external buffer URIs; \p isGlb selects
/// the container. Exposed for tests, which build files in memory rather than on disk.
[[nodiscard]] ImportResult ImportGltfBytes(const std::vector<std::uint8_t>& bytes, bool isGlb,
                                           const std::string& baseDir, const ImportOptions& opt);

/// glTF is **Y-up**; a survey drawing is **Z-up**. Every position and normal passes through this.
/// Exposed because it is the single most consequential line in the importer — get it wrong and
/// every model arrives lying on its side, which looks like a modelling error rather than an
/// importer bug.
inline void GltfYUpToCadZUp(double gx, double gy, double gz, double* cx, double* cy, double* cz) {
  *cx = gx;
  *cy = -gz;
  *cz = gy;
}

} // namespace gltf
