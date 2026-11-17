#pragma once

namespace GoSurvey::Core {

/// Meters represented by a DWG INSUNITS code, using the selected foot definition for feet.
/// Unknown or unitless codes return zero.
[[nodiscard]] double MetersPerInsUnit(int insUnits, bool usSurveyFoot);

}  // namespace GoSurvey::Core
