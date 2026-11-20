#pragma once

namespace geometry {

// Integrates linearly varying signed curvature. The initial tangent is +Y;
// positive curvature turns toward +X. Outputs use the existing bounded midpoint rule.
void IntegrateClothoidLocal(double startCurvature, double endCurvature, double length,
                           double travel, double* dx, double* dy, double* heading);

}  // namespace geometry
