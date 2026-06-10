#pragma once

#include "TraverseCalc.hpp"

#include <string>

/// Parse an Autodesk/Civil 3D .fbk (Field Book) file and populate \p td.
/// Handles NEZ (known point), STN (instrument setup), BS (backsight), and
/// F1 VA / F2 VA (face-1/face-2 observations) records. Angles are packed DMS
/// (D.MMSS, e.g. 317.59551 = 317°59'55.1"); distances are plain decimal.
/// Face observations are averaged during import. Replaces \p td on success.
/// Returns true on success. On failure sets \p errorMsg and returns false.
bool FbkImport(const char* path, TraverseData& td, std::string& errorMsg);
