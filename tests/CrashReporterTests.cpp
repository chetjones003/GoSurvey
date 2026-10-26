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

#include <cstring>
#include <regex>
#include <stdexcept>

namespace {

int CaptureRecord(EXCEPTION_POINTERS* ep, char* output, size_t outputSize) {
  // Describe now: the thrown object is destroyed once the handler unwinds.
  const std::string description = crashReporter::DescribeThrownCppException(ep->ExceptionRecord);
  if (outputSize > 0) {
    std::strncpy(output, description.c_str(), outputSize - 1);
    output[outputSize - 1] = '\0';
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

void ThrowRuntimeError() { throw std::runtime_error("boom from test"); }

std::exception_ptr CaptureCppException(void (*thrower)()) {
  try {
    thrower();
  } catch (...) {
    return std::current_exception();
  }
  return nullptr;
}

bool CaptureThrow(void (*thrower)(), char* output, size_t outputSize) {
  bool handled = false;
  __try {
    thrower();
  } __except (CaptureRecord(GetExceptionInformation(), output, outputSize)) {
    handled = true;
  }
  return handled;
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
  char described[768] = {};
  REQUIRE(CaptureThrow(ThrowRuntimeError, described, sizeof(described)));
  const std::string text = described;
  REQUIRE(text.find("runtime_error") != std::string::npos);
  REQUIRE(text.find("what(): boom from test") != std::string::npos);
}

TEST_CASE("DescribeThrownCppException names a non-std type without a what()", "[crash][platform]") {
  char described[768] = {};
  REQUIRE(CaptureThrow(ThrowPlainStruct, described, sizeof(described)));
  const std::string text = described;
  REQUIRE(text.find("NotAnStdException") != std::string::npos);
  REQUIRE(text.find("what():") == std::string::npos);
}

TEST_CASE("DescribeThrownCppException ignores other exceptions and null", "[crash][platform]") {
  EXCEPTION_RECORD av{};
  av.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
  REQUIRE(crashReporter::DescribeThrownCppException(&av).empty());
  REQUIRE(crashReporter::DescribeThrownCppException(nullptr).empty());
}

TEST_CASE("DescribeCppException reports a standard exception's type and message", "[crash][platform]") {
  const std::string text = crashReporter::DescribeCppException(CaptureCppException(ThrowRuntimeError));
  REQUIRE(text.find("runtime_error") != std::string::npos);
  REQUIRE(text.find("what(): boom from test") != std::string::npos);
}

TEST_CASE("DescribeCppException reports non-standard exceptions generically", "[crash][platform]") {
  const std::string text = crashReporter::DescribeCppException(CaptureCppException(ThrowPlainStruct));
  REQUIRE(text == "Uncaught C++ exception of a non-standard type.\n");
  REQUIRE(crashReporter::DescribeCppException(nullptr).empty());
}
#endif
