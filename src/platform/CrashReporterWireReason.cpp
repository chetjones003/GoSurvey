#include "CrashReporter.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace crashReporter {

std::string ExceptionCodeToWireReason(unsigned long exceptionCode) {
#ifdef _WIN32
  switch (exceptionCode) {
  case EXCEPTION_ACCESS_VIOLATION:
    return "access_violation";
  case EXCEPTION_STACK_OVERFLOW:
    return "stack_overflow";
  case EXCEPTION_INT_DIVIDE_BY_ZERO:
    return "int_divide_by_zero";
  case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    return "float_divide_by_zero";
  case EXCEPTION_ILLEGAL_INSTRUCTION:
    return "illegal_instruction";
  case EXCEPTION_IN_PAGE_ERROR:
    return "in_page_error";
  default:
    break;
  }
#else
  (void)exceptionCode;
#endif
  return "unhandled_exception";
}

}  // namespace crashReporter
