#include "SurveyCsv.hpp"

#include "CadCommands.hpp"
#include "CadCoordinateFrame.hpp"
#include "SurveyCsvValidate.hpp"
#include "SurveyPoints.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <unordered_set>

#include <filesystem>

namespace fs = std::filesystem;

namespace {

std::string Trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    s.pop_back();
  return s;
}

std::vector<std::string> SplitCsvLine(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  bool inQuotes = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (inQuotes) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {
          cur.push_back('"');
          ++i;
        } else {
          inQuotes = false;
        }
      } else {
        cur.push_back(c);
      }
    } else {
      if (c == '"')
        inQuotes = true;
      else if (c == ',') {
        out.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
  }
  out.push_back(cur);
  return out;
}

bool ParseIntStrict(const std::string& cell, int* out) {
  const std::string t = Trim(cell);
  if (t.empty())
    return false;
  char* end = nullptr;
  const long v = std::strtol(t.c_str(), &end, 10);
  if (!end || end != t.c_str() + static_cast<std::ptrdiff_t>(t.size()))
    return false;
  *out = static_cast<int>(v);
  return true;
}

bool ParseDoubleStrict(const std::string& cell, double* out) {
  const std::string t = Trim(cell);
  if (t.empty())
    return false;
  char* end = nullptr;
  const double v = std::strtod(t.c_str(), &end);
  if (!end || end != t.c_str() + static_cast<std::ptrdiff_t>(t.size()))
    return false;
  if (!std::isfinite(v))
    return false;
  *out = v;
  return true;
}

bool ParseDoubleStrict(const std::string& cell, float* out) {
  double v = 0.0;
  if (!ParseDoubleStrict(cell, &v))
    return false;
  *out = static_cast<float>(v);
  return true;
}

bool SurveyIdExists(const std::vector<SurveyPoint>& pts, int id) {
  for (const auto& p : pts) {
    if (p.id == id)
      return true;
  }
  return false;
}

int MaxSurveyPointId(const std::vector<SurveyPoint>& pts) {
  int m = 0;
  for (const auto& p : pts)
    m = std::max(m, p.id);
  return m;
}

bool LayoutHasPointId(SurveyCsvLayout layout) {
  return layout == SurveyCsvLayout::PENZD_PN || layout == SurveyCsvLayout::PENZD_PE;
}

std::string CsvEscapeField(const std::string& s) {
  bool need = false;
  for (char c : s) {
    if (c == ',' || c == '"' || c == '\n' || c == '\r') {
      need = true;
      break;
    }
  }
  if (!need)
    return s;
  std::string o = "\"";
  for (char c : s) {
    if (c == '"')
      o += "\"\"";
    else
      o += c;
  }
  o += '"';
  return o;
}

bool OpenInput(const char* path, std::ifstream* out) {
  out->open(fs::u8path(std::string(path)), std::ios::binary);
  return static_cast<bool>(*out);
}

bool OpenOutput(const char* path, std::ofstream* out) {
  out->open(fs::u8path(std::string(path)), std::ios::binary);
  return static_cast<bool>(*out);
}

void SplitLines(const std::string& blob, std::vector<std::string>* lines) {
  lines->clear();
  std::string cur;
  for (unsigned char uc : blob) {
    const char c = static_cast<char>(uc);
    if (c == '\r')
      continue;
    if (c == '\n') {
      lines->push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  lines->push_back(cur);
}

bool ReadFileLimited(const char* path, std::string* blob, size_t maxBytes, std::vector<std::string>* log) {
  std::ifstream f;
  if (!OpenInput(path, &f)) {
    if (log)
      log->push_back(std::string("Could not open file: ") + path);
    return false;
  }
  std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (content.size() > maxBytes)
    content.resize(maxBytes);
  *blob = std::move(content);
  return true;
}

struct ParseOutcome {
  bool ok = false;
  std::string err;
  SurveyPoint pt{};
  // World-space coordinates as read from the file, in full double precision. Survey points are stored
  // in *local* space (world − worldDocumentOrigin), so the importer converts these before storing.
  double worldE = 0.0;
  double worldN = 0.0;
};

ParseOutcome ParseDataRow(const std::vector<std::string>& cells, SurveyCsvLayout layout, bool hasIdColumn,
                          int* autoIdCounter) {
  ParseOutcome o;
  // REQ-041: elevation (Z) and the trailing description are optional. The required
  // minimum is the ID (if present) plus the two horizontal coordinates: P,N,E for the
  // PENZD layouts, N,E (or E,N) for NEZ/ENZ. A missing Z defaults to 0; a missing
  // description is empty. A Z that *is* present but unparseable is still an error.
  const size_t need = hasIdColumn ? 3 : 2;
  if (cells.size() < need) {
    o.err = "too few columns (need " + std::to_string(need) + ", got " + std::to_string(cells.size()) + ")";
    return o;
  }

  double n = 0.0;
  double e = 0.0;
  float z = 0.f;
  std::string desc;

  if (layout == SurveyCsvLayout::PENZD_PN) {
    if (!ParseIntStrict(cells[0], &o.pt.id)) {
      o.err = "P (ID) is not a valid integer";
      return o;
    }
    if (!ParseDoubleStrict(cells[1], &n)) {
      o.err = "N (northing) is not a valid number";
      return o;
    }
    if (!ParseDoubleStrict(cells[2], &e)) {
      o.err = "E (easting) is not a valid number";
      return o;
    }
    if (cells.size() > 3 && !Trim(cells[3]).empty() && !ParseDoubleStrict(cells[3], &z)) {
      o.err = "Z is not a valid number";
      return o;
    } // else z stays 0 — no elevation provided defaults to 0
    desc = cells.size() > 4 ? Trim(cells[4]) : std::string();
  } else if (layout == SurveyCsvLayout::PENZD_PE) {
    if (!ParseIntStrict(cells[0], &o.pt.id)) {
      o.err = "P (ID) is not a valid integer";
      return o;
    }
    if (!ParseDoubleStrict(cells[1], &e)) {
      o.err = "E (easting) is not a valid number";
      return o;
    }
    if (!ParseDoubleStrict(cells[2], &n)) {
      o.err = "N (northing) is not a valid number";
      return o;
    }
    if (cells.size() > 3 && !Trim(cells[3]).empty() && !ParseDoubleStrict(cells[3], &z)) {
      o.err = "Z is not a valid number";
      return o;
    } // else z stays 0 — no elevation provided defaults to 0
    desc = cells.size() > 4 ? Trim(cells[4]) : std::string();
  } else if (layout == SurveyCsvLayout::NEZ) {
    if (!autoIdCounter) {
      o.err = "internal: missing auto ID";
      return o;
    }
    o.pt.id = (*autoIdCounter)++;
    if (!ParseDoubleStrict(cells[0], &n)) {
      o.err = "N (northing) is not a valid number";
      return o;
    }
    if (!ParseDoubleStrict(cells[1], &e)) {
      o.err = "E (easting) is not a valid number";
      return o;
    }
    if (cells.size() > 2 && !Trim(cells[2]).empty() && !ParseDoubleStrict(cells[2], &z)) {
      o.err = "Z is not a valid number";
      return o;
    } // else z stays 0 — no elevation provided defaults to 0
  } else if (layout == SurveyCsvLayout::ENZ) {
    if (!autoIdCounter) {
      o.err = "internal: missing auto ID";
      return o;
    }
    o.pt.id = (*autoIdCounter)++;
    if (!ParseDoubleStrict(cells[0], &e)) {
      o.err = "E (easting) is not a valid number";
      return o;
    }
    if (!ParseDoubleStrict(cells[1], &n)) {
      o.err = "N (northing) is not a valid number";
      return o;
    }
    if (cells.size() > 2 && !Trim(cells[2]).empty() && !ParseDoubleStrict(cells[2], &z)) {
      o.err = "Z is not a valid number";
      return o;
    } // else z stays 0 — no elevation provided defaults to 0
  } else {
    o.err = "unknown layout";
    return o;
  }

  o.worldE = e;
  o.worldN = n;
  // pt.easting/northing are local-space; the importer fills them from worldE/worldN minus the
  // document origin. Seed with the world values so callers that don't convert still see coordinates.
  o.pt.easting = static_cast<float>(e);
  o.pt.northing = static_cast<float>(n);
  o.pt.elevation = z;
  o.pt.description = desc;
  o.pt.layer = "0";
  o.ok = true;
  return o;
}

void AppendSurveyExportCoord(std::ostringstream& oss, double v) {
  oss << std::fixed << std::setprecision(4) << v;
}

std::string HeaderLine(SurveyCsvLayout layout) {
  switch (layout) {
  case SurveyCsvLayout::PENZD_PN:
    return "P,N,E,Z,D";
  case SurveyCsvLayout::PENZD_PE:
    return "P,E,N,Z,D";
  case SurveyCsvLayout::NEZ:
    return "N,E,Z";
  case SurveyCsvLayout::ENZ:
    return "E,N,Z";
  }
  return {};
}

std::string BuildExportCsvBody(const AppCommandState& st, SurveyCsvLayout layout, bool writeHeader) {
  std::ostringstream oss;
  if (writeHeader)
    oss << HeaderLine(layout) << '\n';
  for (const auto& p : st.surveyPoints) {
    const std::string d = CsvEscapeField(p.description);
    const double e = static_cast<double>(p.easting) + st.worldDocumentOriginX;
    const double n = static_cast<double>(p.northing) + st.worldDocumentOriginY;
    const double z = static_cast<double>(p.elevation);
    switch (layout) {
    case SurveyCsvLayout::PENZD_PN:
      oss << p.id << ',';
      AppendSurveyExportCoord(oss, n);
      oss << ',';
      AppendSurveyExportCoord(oss, e);
      oss << ',';
      AppendSurveyExportCoord(oss, z);
      oss << ',' << d << '\n';
      break;
    case SurveyCsvLayout::PENZD_PE:
      oss << p.id << ',';
      AppendSurveyExportCoord(oss, e);
      oss << ',';
      AppendSurveyExportCoord(oss, n);
      oss << ',';
      AppendSurveyExportCoord(oss, z);
      oss << ',' << d << '\n';
      break;
    case SurveyCsvLayout::NEZ:
      AppendSurveyExportCoord(oss, n);
      oss << ',';
      AppendSurveyExportCoord(oss, e);
      oss << ',';
      AppendSurveyExportCoord(oss, z);
      oss << '\n';
      break;
    case SurveyCsvLayout::ENZ:
      AppendSurveyExportCoord(oss, e);
      oss << ',';
      AppendSurveyExportCoord(oss, n);
      oss << ',';
      AppendSurveyExportCoord(oss, z);
      oss << '\n';
      break;
    }
  }
  return oss.str();
}

} // namespace

SurveyCsvLayout SurveyCsvLayoutFromUiIndex(int idx) {
  switch (idx) {
  default:
  case 0:
    return SurveyCsvLayout::PENZD_PN;
  case 1:
    return SurveyCsvLayout::PENZD_PE;
  case 2:
    return SurveyCsvLayout::NEZ;
  case 3:
    return SurveyCsvLayout::ENZ;
  }
}

void SurveyCsvRefreshImportPreview(AppCommandState& st) {
  st.surveyImportPreviewText.clear();
  st.surveyImportPreviewValidation.clear();
  st.surveyImportFileBlocked = true; // assume blocked until a valid, importable file proves otherwise
  st.surveyImportValidRowCount = 0;
  st.surveyImportBadRowCount = 0;

  if (!st.surveyImportCsvPath[0]) {
    st.surveyImportPreviewValidation = "Pick a CSV file to preview.";
    st.surveyImportPreviewDirty = false;
    return;
  }

  // REQ-041: classify the file before any import so the user gets a specific reason.
  const fs::path path = fs::u8path(std::string(st.surveyImportCsvPath));
  std::error_code ec;
  const bool exists = fs::exists(path, ec) && !ec && fs::is_regular_file(path, ec);
  bool empty = false;
  if (exists) {
    const auto sz = fs::file_size(path, ec);
    empty = !ec && sz == 0;
  }
  bool canOpen = false;
  if (exists && !empty) {
    std::ifstream probe;
    canOpen = OpenInput(st.surveyImportCsvPath, &probe);
  }
  const survey_csv::FileState fstate = survey_csv::ClassifyFileState(exists, empty, canOpen);
  if (fstate != survey_csv::FileState::Ok) {
    st.surveyImportPreviewValidation = std::string("Cannot import — ") + survey_csv::FileStateMessage(fstate);
    st.surveyImportPreviewDirty = false;
    return;
  }

  std::string blob;
  constexpr size_t kPreviewCap = 256 * 1024;
  if (!ReadFileLimited(st.surveyImportCsvPath, &blob, kPreviewCap, nullptr)) {
    // The probe opened the file but the read failed — treat as a file-level block.
    st.surveyImportPreviewValidation =
        std::string("Cannot import — ") + survey_csv::FileStateMessage(survey_csv::FileState::CannotOpen);
    st.surveyImportPreviewDirty = false;
    return;
  }

  std::vector<std::string> lines;
  SplitLines(blob, &lines);

  const SurveyCsvLayout layout = SurveyCsvLayoutFromUiIndex(st.surveyImportCsvLayoutIdx);
  const bool hasId = LayoutHasPointId(layout);
  size_t startRow = 0;
  if (st.surveyImportCsvSkipFirstRow && !lines.empty())
    startRow = 1;

  int previewAuto = MaxSurveyPointId(st.surveyPoints) + 1;
  previewAuto = std::max(previewAuto, st.createPointsNextId);

  int parsedOk = 0;
  int parseFail = 0;
  std::vector<std::string> problems;
  std::vector<survey_csv::RowId> rowsWithId; // parsed rows carrying an explicit ID, in source order
  constexpr int kMaxProblems = 24;

  for (size_t li = startRow; li < lines.size(); ++li) {
    if (Trim(lines[li]).empty())
      continue;
    std::vector<std::string> cells = SplitCsvLine(lines[li]);
    int autoCounter = previewAuto;
    ParseOutcome pr = ParseDataRow(cells, layout, hasId, hasId ? nullptr : &autoCounter);
    if (!pr.ok) {
      ++parseFail;
      if (static_cast<int>(problems.size()) < kMaxProblems)
        problems.push_back("Line " + std::to_string(li + 1) + ": " + pr.err);
    } else {
      ++parsedOk;
      if (hasId)
        rowsWithId.push_back({pr.pt.id, li + 1});
      else
        previewAuto = autoCounter;
    }
  }

  // REQ-041: flag duplicate IDs — within the file and against existing session points —
  // mirroring the importer's skip rules so the valid/skip counts are accurate.
  std::unordered_set<int> sessionIds;
  if (hasId) {
    sessionIds.reserve(st.surveyPoints.size());
    for (const auto& p : st.surveyPoints)
      sessionIds.insert(p.id);
  }
  const survey_csv::DuplicateScan dup = survey_csv::ScanDuplicateIds(rowsWithId, sessionIds);
  for (const auto& m : dup.messages) {
    if (static_cast<int>(problems.size()) < kMaxProblems)
      problems.push_back(m);
  }

  const int duplicateBad = static_cast<int>(dup.badLines.size());
  const int okRows = parsedOk - duplicateBad; // rows that would actually import
  const int badRows = parseFail + duplicateBad;
  st.surveyImportValidRowCount = okRows;
  st.surveyImportBadRowCount = badRows;

  std::ostringstream summ;
  // REQ-041: overall status first. Zero importable rows is a file-level block.
  if (okRows <= 0) {
    st.surveyImportFileBlocked = true;
    summ << "Cannot import — no valid data rows.\n";
  } else {
    st.surveyImportFileBlocked = false;
    summ << "Ready to import — " << okRows << " point(s)";
    if (badRows > 0)
      summ << " (" << badRows << " row(s) will be skipped)";
    summ << ".\n";
  }
  summ << "Layout: " << HeaderLine(layout) << (hasId ? " (with point IDs)" : " (IDs assigned on import)") << '\n';
  if (blob.size() >= kPreviewCap)
    summ << "Preview data truncated at " << kPreviewCap << " bytes; import reads the full file.\n";
  summ << "Non-empty data rows scanned: " << (parsedOk + parseFail) << " — OK: " << okRows << ", problems: " << badRows
       << '\n';
  for (const auto& p : problems)
    summ << p << '\n';
  if (badRows > static_cast<int>(problems.size()))
    summ << "(Additional problem lines omitted from this list.)\n";

  const int kShowLines = 48;
  std::ostringstream raw;
  const int totalShow = std::min(static_cast<int>(lines.size()), kShowLines);
  for (int i = 0; i < totalShow; ++i) {
    raw << lines[static_cast<size_t>(i)];
    raw << '\n';
  }
  if (static_cast<int>(lines.size()) > totalShow)
    raw << "… (" << (lines.size() - static_cast<size_t>(totalShow)) << " more lines)\n";

  st.surveyImportPreviewText = raw.str();
  st.surveyImportPreviewValidation = summ.str();
  st.surveyImportPreviewDirty = false;
}

bool SurveyCsvImportFile(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.surveyImportCsvPath[0]) {
    log.push_back("IMPORTPOINTS — no file path.");
    return false;
  }

  std::ifstream f;
  if (!OpenInput(st.surveyImportCsvPath, &f)) {
    log.push_back(std::string("IMPORTPOINTS — could not open ") + st.surveyImportCsvPath);
    return false;
  }

  const SurveyCsvLayout layout = SurveyCsvLayoutFromUiIndex(st.surveyImportCsvLayoutIdx);
  const bool hasId = LayoutHasPointId(layout);
  int autoId = MaxSurveyPointId(st.surveyPoints) + 1;
  autoId = std::max(autoId, st.createPointsNextId);

  std::string line;
  size_t lineNo = 0;
  int imported = 0;
  int skipped = 0;

  while (std::getline(f, line)) {
    ++lineNo;
    if (st.surveyImportCsvSkipFirstRow && lineNo == 1)
      continue;
    if (Trim(line).empty())
      continue;

    std::vector<std::string> cells = SplitCsvLine(line);
    int counter = autoId;
    ParseOutcome pr = ParseDataRow(cells, layout, hasId, hasId ? nullptr : &counter);
    if (!pr.ok) {
      log.push_back("IMPORTPOINTS line " + std::to_string(lineNo) + ": " + pr.err + " — skipped.");
      ++skipped;
      continue;
    }

    if (hasId && SurveyIdExists(st.surveyPoints, pr.pt.id)) {
      log.push_back("IMPORTPOINTS line " + std::to_string(lineNo) + ": duplicate ID " + std::to_string(pr.pt.id) +
                    " — skipped.");
      ++skipped;
      continue;
    }

    // CSV coordinates are world-space; survey points are stored local (world − document origin). Convert
    // in double precision so points land exactly on existing geometry (e.g. an imported DXF's endpoints).
    pr.pt.easting = static_cast<float>(pr.worldE - st.worldDocumentOriginX);
    pr.pt.northing = static_cast<float>(pr.worldN - st.worldDocumentOriginY);

    st.surveyPoints.push_back(pr.pt);
    EnsureSurveyPointLabelMtext(st, st.surveyPoints.size() - 1, &log);
    ++imported;
    if (!hasId)
      autoId = counter;
  }

  st.createPointsNextId = std::max(st.createPointsNextId, autoId);
  st.selectedSurveyPointIndices.clear();
  if (imported > 0)
    CadCoord::MaybeRebaseLargeCoordinates(st, &log);
  log.push_back("IMPORTPOINTS — imported " + std::to_string(imported) + " point(s); skipped " +
                std::to_string(skipped) + " row(s).");
  st.surveyImportPreviewDirty = true;
  return imported > 0 || skipped > 0;
}

bool SurveyCsvExportFile(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.surveyExportCsvPath[0]) {
    log.push_back("EXPORTPOINTS — no file path.");
    return false;
  }

  const SurveyCsvLayout layout = SurveyCsvLayoutFromUiIndex(st.surveyExportCsvLayoutIdx);
  const std::string body = BuildExportCsvBody(st, layout, st.surveyExportCsvWriteHeader);

  std::ofstream f;
  if (!OpenOutput(st.surveyExportCsvPath, &f)) {
    log.push_back(std::string("EXPORTPOINTS — could not write ") + st.surveyExportCsvPath);
    return false;
  }
  f << body;
  if (!f) {
    log.push_back("EXPORTPOINTS — write failed.");
    return false;
  }

  log.push_back(std::string("EXPORTPOINTS — wrote ") + std::to_string(st.surveyPoints.size()) + " point(s) to " +
                st.surveyExportCsvPath);

  std::string tabTitle = st.surveyExportCsvPath;
  const size_t slash = tabTitle.find_last_of("/\\");
  if (slash != std::string::npos && slash + 1 < tabTitle.size())
    tabTitle = tabTitle.substr(slash + 1);

  st.surveyReportTabs.push_back({tabTitle, body});
  st.surveyReportSelectedTab = static_cast<int>(st.surveyReportTabs.size()) - 1;
  st.surveyReportSelectLatestPending = true;

  return true;
}
