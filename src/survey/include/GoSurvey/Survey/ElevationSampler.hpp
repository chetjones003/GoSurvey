#pragma once

#include <functional>

namespace survey {

/// Synchronously samples a drawing-local XY position, returning false outside provider coverage.
/// The provider owns its surface data; Survey retains no reference after the call.
using ElevationSampler = std::function<bool(double x, double y, double* outElevation)>;

}  // namespace survey
