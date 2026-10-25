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

#ifdef _WIN32
#include "CrashBuildInfo.hpp"

#include <regex>
#include <stdexcept>

namespace {

std::string g_described;

int CaptureRecord(EXCEPTION_POINTERS* ep) {
  // Describe now: the thrown object is destroyed once the handler unwinds.
  g_described = crashReporter::DescribeThrownCppException(ep->ExceptionRecord);
  return EXCEPTION_EXECUTE_HANDLER;
}

void ThrowRuntimeError() { throw std::runtime_error("boom from test"); }

void CaptureThrow(void (*thrower)()) {
  __try {
    thrower();
  } __except (CaptureRecord(GetExceptionInformation())) {
  }
}

struct NotAnStdException {};
void ThrowPlainStruct() { throw NotAnStdException{}; }

}  // namespace

TEST_CASE("PdbIdentityOfModule reads the running module's PDB GUID and age", "[crash][platform]") {
  const std::string id = crashReporter::PdbIdentityOfModule(::GetModuleHandleW(nullptr));
  // A /DEBUG link always carries a CodeView record; the format is 32 hex digits, a comma, the age.
  INFO(id);
  REQUIRE(std::regex_match(id, std::regex("[0-9A-F]{32},[0-9]+")));
}

TEST_CASE("PdbIdentityOfModule reports unavailable for no module or a non-image", "[crash][platform]") {
  REQUIRE(crashReporter::PdbIdentityOfModule(nullptr) == "unavailable");
  const char notAnImage[64] = {};
  REQUIRE(crashReporter::PdbIdentityOfModule(notAnImage) == "unavailable");
}

TEST_CASE("DescribeThrownCppException names the type and what() of a std exception", "[crash][platform]") {
  g_described.clear();
  CaptureThrow(ThrowRuntimeError);
  const std::string text = g_described;
  REQUIRE(text.find("runtime_error") != std::string::npos);
  REQUIRE(text.find("what(): boom from test") != std::string::npos);
}

TEST_CASE("DescribeThrownCppException names a non-std type without a what()", "[crash][platform]") {
  g_described.clear();
  CaptureThrow(ThrowPlainStruct);
  const std::string text = g_described;
  REQUIRE(text.find("NotAnStdException") != std::string::npos);
  REQUIRE(text.find("what():") == std::string::npos);
}

TEST_CASE("DescribeThrownCppException ignores other exceptions and null", "[crash][platform]") {
  EXCEPTION_RECORD av{};
  av.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
  REQUIRE(crashReporter::DescribeThrownCppException(&av).empty());
  REQUIRE(crashReporter::DescribeThrownCppException(nullptr).empty());
}
#endif
