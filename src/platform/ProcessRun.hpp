#pragma once

#include <string>
#include <vector>

/// Launches \p exePathUtf8 with \p argsUtf8 and blocks until it exits or \p timeoutMs elapses.
///
/// The child runs with no console window and inherits nothing from the caller beyond the
/// environment. Arguments are quoted for the Windows command-line parser, so callers pass
/// plain (unquoted) paths.
///
/// Returns true only when the process started AND exited within the timeout; \p exitCodeOut
/// then receives its exit code. On timeout the child is terminated and false is returned, so
/// a hung converter can never wedge the UI thread forever.
bool RunProcessAndWait(const std::string& exePathUtf8,
                       const std::vector<std::string>& argsUtf8,
                       const std::string& workingDirUtf8,
                       int timeoutMs,
                       int* exitCodeOut);
