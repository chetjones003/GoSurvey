#pragma once

#if !defined(GOSURVEY_CORE_IMPLEMENTATION)
#error "CorePrivate.hpp is private to GoSurvey::Core"
#endif

#include <string>

namespace GoSurvey::Core::detail {

int ClampDisplayPrecision(int precision);
double NormalizeAngle360(double degrees);
std::string BuildFloatFormat(int decimalPlaces);

}  // namespace GoSurvey::Core::detail
