#pragma once

#include <string>
#include <vector>

struct AppCommandState;

/// Writes the drawing's survey points (CgPoints), built TIN surfaces (Surfaces/Surface/Definition) and
/// feature lines (PlanFeatures/PlanFeature/CoordGeom/Line) to a LandXML 1.2 file (issue #1002).
///
/// Coordinates are WORLD, written northing-first as LandXML requires: the document stores X/Y local
/// (world = local + worldDocumentOrigin) and the origin is added back in double. Z is absolute.
/// The `Units` element follows the drawing unit (INSUNITS) and its foot definition; a unitless
/// drawing writes no `Units` rather than claiming one.
///
/// Returns false, logging why and leaving any existing file at \p pathUtf8 untouched, when there is
/// nothing to export or the file cannot be written. Items that cannot be represented (a grid surface,
/// an unbuilt TIN, a feature line with fewer than two vertices, a non-finite coordinate) are skipped
/// and named in \p log (REQ-201).
bool ExportLandXmlFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
