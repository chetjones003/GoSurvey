#include "TraverseActions.hpp"
#include <GoSurvey/IO/TraverseReportExport.hpp>

bool ExportTraverseReport(const std::string& format, const std::string& pathUtf8,
                          const std::string& text, std::string& error) {
  if (format == "pdf") return traverserpt::ExportReportPdf(pathUtf8, text, error);
  if (format == "rtf") return traverserpt::ExportReportRtf(pathUtf8, text, error);
  if (format == "txt") return traverserpt::ExportReportTxt(pathUtf8, text, error);
  return false;
}
