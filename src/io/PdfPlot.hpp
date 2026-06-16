#pragma once

#include <string>
#include <vector>

struct AppCommandState;

// Plot one or more paper layouts to a vector PDF — one page per layout, sized to each layout's paper
// size, with each viewport's model geometry emitted as vector paths clipped to the viewport rect at the
// viewport's scale. Geometry/viewports on non-plottable (or off/frozen) layers are excluded.
// REQ-029 (single) / REQ-030 (batch); ADR-007 (vector PDF via PDFium). Returns true on success.
bool PlotLayoutsToPdf(const AppCommandState& st, const std::vector<int>& layoutIndices, const char* pathUtf8,
                      std::vector<std::string>& log);
