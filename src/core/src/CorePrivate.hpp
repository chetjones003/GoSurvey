#pragma once

#if !defined(GOSURVEY_CORE_IMPLEMENTATION)
#error "CorePrivate.hpp is private to GoSurvey::Core"
#endif

namespace GoSurvey::Core::detail {

int ClampDisplayPrecision(int precision);
double NormalizeAngle360(double degrees);

}  // namespace GoSurvey::Core::detail
