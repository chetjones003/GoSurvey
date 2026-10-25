#pragma once

#include <string>

namespace crashReporter {

#ifdef _WIN32
/// REQ-425 §8: the PDB identity of the loaded module at `moduleBase` ("<GUID>,<age>", e.g.
/// "0123456789ABCDEF0123456789ABCDEF,1"), read from its CodeView debug record. Returns
/// "unavailable" when the module carries none. Matches a crash report to the archived PDB.
std::string PdbIdentityOfModule(const void* moduleBase);

/// REQ-425 §9: for a C++ `throw` (exception code 0xE06D7363) describes the thrown type and, when
/// it derives from std::exception, its what() message. `exceptionRecord` is an EXCEPTION_RECORD*.
/// Returns an empty string for any other exception or when the record cannot be read.
/// Allocation-light and guarded against bad pointers: it runs inside the crash handler.
std::string DescribeThrownCppException(const void* exceptionRecord);
#endif

}  // namespace crashReporter
