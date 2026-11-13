#include <catch2/catch_test_macros.hpp>

#include <GoSurvey/Core/StringUtil.hpp>

TEST_CASE("String utilities trim whitespace and lowercase ASCII", "[core][string]") {
  CHECK(StringUtil::trimCopy(" \tGoSurvey Core\r\n") == "GoSurvey Core");
  CHECK(StringUtil::trimCopy(" \t\r\n").empty());
  CHECK(StringUtil::toLowerAsciiCopy("GoSurvey CORE 123") == "gosurvey core 123");
}
