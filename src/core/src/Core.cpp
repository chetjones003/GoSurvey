#include "CorePrivate.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include <GoSurvey/Core/AngleFormat.hpp>
#include <GoSurvey/Core/NumFormat.hpp>
#include <GoSurvey/Core/StringUtil.hpp>

namespace GoSurvey::Core::detail {

int ClampDisplayPrecision(int precision) { return std::clamp(precision, 0, 12); }

double NormalizeAngle360(double degrees) {
  degrees = std::fmod(degrees, 360.0);
  if (degrees < 0.0) degrees += 360.0;
  return degrees;
}

std::string BuildFloatFormat(int decimalPlaces) {
  char format[8];
  std::snprintf(format, sizeof(format), "%%.%df", decimalPlaces);
  return format;
}

}  // namespace GoSurvey::Core::detail

int DisplayPrecisionClamp(int precision) {
  return GoSurvey::Core::detail::ClampDisplayPrecision(precision);
}

std::string DisplayFloatFmt(int precision) {
  return GoSurvey::Core::detail::BuildFloatFormat(DisplayPrecisionClamp(precision));
}

std::string FormatLinear(double value, int precision) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), DisplayFloatFmt(precision).c_str(), value);
  return buffer;
}

std::string FormatVolumeYd3(double volumeFt3, int precision) {
  return FormatLinear(volumeFt3 / kCubicFeetPerCubicYard, precision) + " yd3";
}

namespace anglefmt_detail {

double Normalize360(double degrees) {
  return GoSurvey::Core::detail::NormalizeAngle360(degrees);
}

std::string FormatDms(double degrees, int secDecimals, bool wrap360) {
  secDecimals = std::clamp(secDecimals, 0, 6);
  degrees = Normalize360(degrees);
  int integerDegrees = static_cast<int>(std::floor(degrees + 1e-9));
  double minutesFloat = (degrees - integerDegrees) * 60.0;
  if (minutesFloat < 0.0) minutesFloat = 0.0;
  int integerMinutes = static_cast<int>(std::floor(minutesFloat + 1e-9));
  double seconds = (minutesFloat - integerMinutes) * 60.0;
  if (seconds < 0.0) seconds = 0.0;
  const double rollover = 60.0 - 0.5 * std::pow(10.0, -secDecimals);
  if (integerMinutes >= 60) { integerMinutes = 0; integerDegrees = wrap360 ? (integerDegrees + 1) % 360 : integerDegrees + 1; }
  if (seconds >= rollover) {
    seconds = 0.0;
    if (++integerMinutes >= 60) { integerMinutes = 0; integerDegrees = wrap360 ? (integerDegrees + 1) % 360 : integerDegrees + 1; }
  }
  char format[24];
  const int secondsWidth = secDecimals == 0 ? 2 : secDecimals + 3;
  std::snprintf(format, sizeof(format), "%%d\xc2\xb0%%02d'%%0%d.%df\"", secondsWidth, secDecimals);
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), format, integerDegrees, integerMinutes, seconds);
  return buffer;
}

std::string FormatDecimalDeg(double degrees, int decimals) {
  decimals = std::clamp(decimals, 0, 8);
  const std::string format = GoSurvey::Core::detail::BuildFloatFormat(decimals);
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), format.c_str(), degrees);
  return std::string(buffer) + "\xc2\xb0";
}

}  // namespace anglefmt_detail

std::string FormatBearing(double canonicalCwNorthDeg, const AngleDisplaySettings& settings) {
  const double bearing = anglefmt_detail::Normalize360(canonicalCwNorthDeg);
  if (settings.type == AngleDisplayType::Surveyor) {
    if (bearing <= 90.0) return "N " + anglefmt_detail::FormatDms(bearing, settings.precision, false) + " E";
    if (bearing <= 180.0) return "S " + anglefmt_detail::FormatDms(180.0 - bearing, settings.precision, false) + " E";
    if (bearing <= 270.0) return "S " + anglefmt_detail::FormatDms(bearing - 180.0, settings.precision, false) + " W";
    return "N " + anglefmt_detail::FormatDms(360.0 - bearing, settings.precision, false) + " W";
  }
  const double measured = anglefmt_detail::Normalize360(
      settings.clockwise ? (bearing - settings.baseDeg) : (settings.baseDeg - bearing));
  if (settings.type == AngleDisplayType::DecimalDegrees)
    return anglefmt_detail::FormatDecimalDeg(measured, settings.precision);
  return anglefmt_detail::FormatDms(measured, settings.precision, true);
}

std::string FormatSweptAngle(double angleDeg, const AngleDisplaySettings& settings) {
  double angle = std::fabs(angleDeg);
  angle = std::fmod(angle, 360.0);
  if (angle > 180.0) angle = 360.0 - angle;
  if (settings.type == AngleDisplayType::DecimalDegrees)
    return anglefmt_detail::FormatDecimalDeg(angle, settings.precision);
  return anglefmt_detail::FormatDms(angle, settings.precision, false);
}

namespace StringUtil {

std::string trimCopy(std::string_view value) {
  size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
  size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
  return std::string(value.substr(begin, end - begin));
}

std::string toLowerAsciiCopy(std::string_view value) {
  std::string result(value);
  for (char& character : result)
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  return result;
}

}  // namespace StringUtil
