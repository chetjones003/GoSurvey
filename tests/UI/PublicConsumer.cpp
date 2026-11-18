#include <GoSurvey/UI/CommandBar.hpp>
#include <GoSurvey/UI/FailureReportDialog.hpp>
#include <GoSurvey/UI/FileDialogs.hpp>
#include <GoSurvey/UI/MtextToolbar.hpp>
#include <GoSurvey/UI/RichTextLayout.hpp>

#if __has_include("CommandBar.hpp") || __has_include("FailureReportUi.hpp") || __has_include("FileDialogs.hpp") || \
    __has_include("MtextToolbar.hpp") || __has_include("RichTextLayout.hpp") || __has_include("WinFileDialogs.hpp")
#error UI public consumers must not inherit UI private include roots
#endif

int main() {
  const auto tags = mtexttoolbar::FontRunTags("Arial");
  const richtext::SpanRange span{0, 1};
  std::vector<richtext::Cell> cells;
  richtext::BuildCells("x", std::vector<richtext::SpanRange>{span}, &cells);
  return cmdbar::HistoryAlpha(0.0, 1.0, 1.0) == 1.0f && !tags.open.empty() && cells.size() == 1 ? 0 : 1;
}
