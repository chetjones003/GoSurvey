#include "CrashReporter.hpp"

#include <catch2/catch_test_macros.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

TEST_CASE("ExceptionCodeToWireReason maps common Windows codes", "[crash][platform]") {
#ifdef _WIN32
  REQUIRE(crashReporter::ExceptionCodeToWireReason(EXCEPTION_ACCESS_VIOLATION) == "access_violation");
  REQUIRE(crashReporter::ExceptionCodeToWireReason(EXCEPTION_STACK_OVERFLOW) == "stack_overflow");
#endif
  REQUIRE(crashReporter::ExceptionCodeToWireReason(0xDEADBEEF) == "unhandled_exception");
}
