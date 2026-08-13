#pragma once

#include "util/modelimport.hpp"

#include <string>
#include <vector>

/// Converts a DWG's 3D content into a mesh by driving an external AutoCAD (REQ-065 / ADR-024).
///
/// **Why this exists at all.** A Plant 3D drawing stores its piping as `AcPp*` CUSTOM OBJECTS,
/// which resolve only with Autodesk's Plant 3D object enabler — not licensable to an independent
/// application. GoSurvey therefore cannot read that geometry from the DWG however good its own
/// codec becomes (ADR-026 Context). What *can* be done is ask an installed AutoCAD to hand the
/// geometry over in a neutral form, which is exactly ADR-024's converter-route pattern applied to
/// 3D content instead of 2D.
///
/// The conversion is: EXPLODE the custom objects (the enabler emits plain 3D solids) → STLOUT
/// tessellates them → GoSurvey reads the STL. What survives is geometry, position and scale. What
/// does not is per-object colour and naming, because STL carries neither — reported, not hidden.
namespace dwgmesh {

struct ConvertOptions {
  int facetRes = 10;        ///< AutoCAD FACETRES, 1..10. Higher is smoother; 10 is its maximum.
  int timeoutMs = 600000;   ///< A large plant model genuinely takes minutes to explode.
};

struct ConvertResult {
  bool ok = false;
  std::string error;             ///< Specific reason; never a bare "failed" (REQ-201).
  std::string converterName;     ///< Which tool did it, for the log.
  int solidCount = 0;
  modelimport::Result model;     ///< Valid only when \c ok.
};

/// True when a converter capable of this is installed. ODA File Converter can translate DWG→DXF but
/// cannot tessellate solids, so only an AutoCAD `accoreconsole` qualifies — the distinction matters
/// for the message the user gets.
[[nodiscard]] bool ConversionAvailable(std::string* whyNotOut = nullptr);

/// Runs the conversion. Blocking, and slow enough on a real model that the caller should say so
/// before starting.
[[nodiscard]] ConvertResult ConvertDwgToMesh(const std::string& dwgPathUtf8, const modelimport::Options& imp,
                                             const ConvertOptions& conv = {});

} // namespace dwgmesh
