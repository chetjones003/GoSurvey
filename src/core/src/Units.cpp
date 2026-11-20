#include <GoSurvey/Core/Units.hpp>

namespace GoSurvey::Core {

double MetersPerInsUnit(int insUnits, bool usSurveyFoot) {
  constexpr double kUsSurveyFootMeters = 1200.0 / 3937.0;
  constexpr double kInternationalFootMeters = 0.3048;
  switch (insUnits) {
    case 1: return 0.0254;
    case 2: return usSurveyFoot ? kUsSurveyFootMeters : kInternationalFootMeters;
    case 4: return 0.001;
    case 6: return 1.0;
    default: return 0.0;
  }
}

}  // namespace GoSurvey::Core
