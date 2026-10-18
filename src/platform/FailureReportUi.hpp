#pragma once

#include <string>

namespace failureReportUi {

void ShowReportDialog(const wchar_t* windowTitle, const std::string& reportUtf8);

/// REQ-425 §3: the GitHub new-issue address for a report — "GoSurvey report (<version>)" as the
/// title and the report as the body, cut to fit a URL (the full text goes on the clipboard).
std::string GitHubIssueUrlForReport(const std::string& reportUtf8);

/// REQ-425 §3: what "Send This Report" tells the user, given which of its two steps worked.
std::string SendOutcomeMessage(bool copiedToClipboard, bool openedBrowser);

}  // namespace failureReportUi
