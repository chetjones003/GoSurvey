#pragma once
// Zippy (OpenXLSX) includes miniz_zip.h. The repo vendors miniz as a single miniz.h that already
// contains the zip archive API, so this shim forwards to it. See VENDORED.md.
#include "miniz.h"
