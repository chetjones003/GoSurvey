#include "CadCommandsInternal.hpp"

#include <fstream>

#include <GoSurvey/UI/FileDialogs.hpp>

void RunVolReport(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string verb = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(args));
  const bool wantTable = (verb == "table");
  std::string text = st.lastVolumeReportText;
  const int p = st.displayLinearPrecision;
  const SurfaceVolumeResult* numbers = nullptr;
  if (st.volumeDashboard.hasResult)
    numbers = &st.volumeDashboard.lastResult;
  else if (st.hasLastVolumeResult)
    numbers = &st.lastVolumeResult;
  if (text.empty() && numbers) {
    RememberVolumeResult(st, *numbers, st.lastVolumeBaseName, st.lastVolumeComparisonName);
    text = st.lastVolumeReportText;
  }
  if (!numbers && text.empty() && st.volumeDashboard.rows.empty()) {
    log.push_back(wantTable ? "VOLREPORT TABLE — no volume result to insert."
                            : "VOLREPORT — no volume result to insert.");
    return;
  }
  PushUndoSnapshot(st, wantTable ? "VOLTABLE" : "VOLREPORT");
  if (wantTable) {
    CadTable tbl;
    tbl.cols = 2;
    tbl.cells = {"Item", "Value"};
    if (numbers)
      AppendVolumeTableItemValue(*numbers, p, &tbl.cells);
    for (const auto& row : st.volumeDashboard.rows) {
      if (!row.hasResult)
        continue;
      tbl.cells.push_back(row.label.empty() ? std::string("Row") : row.label);
      tbl.cells.push_back(FormatVolumeYd3(row.result.netFt3, p));
    }
    if (const TextStyle* ts = ActiveTextStyle(st))
      tbl.fontFamily = ts->fontFamily;
    tbl.plottedHeightInches = st.drawing.defaultPlottedTextHeightInches;
    CadTableFitToContent(&tbl, st.drawing.modelUnitsPerPlottedInch);
    tbl.insX = 0.f;
    tbl.insY = tbl.height;
    tbl.insZ = CadCommitElevation(st);
    st.drawing.cadTables.push_back(std::move(tbl));
    AppendNewObjectAttrs(st, st.drawing.cadTableAttrs, st.drawing.cadTables.size(), ObjectLayerKind::Table, {});  // REQ-361
    EnsureAttrCounts(st);
    BumpCadGpuCache(st);
    log.push_back("VOLREPORT TABLE — TABLE inserted.");
    return;
  }
  CadAnnotation ann;
  ann.kind = CadAnnotation::Kind::Mtext;
  ann.boxMinX = 0.f;
  ann.boxMinY = 0.f;
  ann.boxMaxX = 40.f;
  ann.boxMaxY = 12.f;
  ann.insX = 0.f;
  ann.insY = 0.f;
  ann.text = text;
  ann.plottedHeightInches = st.drawing.defaultPlottedTextHeightInches;
  ann.insZ = CadCommitElevation(st);
  StampActiveTextStyleOnNewText(st, ann);
  st.drawing.cadAnnotations.push_back(std::move(ann));
  EnsureAttrCounts(st);
  BumpCadGpuCache(st);
  log.push_back("VOLREPORT — MTEXT inserted.");
}

void RunVolCsv(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  std::string path = StringUtil::trimCopy(args);
  if (path.empty()) {
    char buf[1024] = {};
    if (!BrowseSaveFileCsvUtf8(buf, sizeof(buf), "volumes.csv") || buf[0] == '\0') {
      log.push_back("VOLCSV — cancelled.");
      return;
    }
    path = buf;
  }
  const bool haveLive = st.volumeDashboard.hasResult;
  if (!haveLive && st.volumeDashboard.rows.empty() && !st.hasLastVolumeResult) {
    log.push_back("VOLCSV — no volume result to export.");
    return;
  }
  std::ofstream f(path);
  if (!f) {
    log.push_back("VOLCSV — could not write \"" + path + "\".");
    return;
  }
  const int p = st.displayLinearPrecision;
  f << "label,base,comparison,cut_yd3,fill_yd3,net_yd3,cut_area_ft2,fill_area_ft2,common_area_ft2\n";
  auto emit = [&](const std::string& label, const std::string& base, const std::string& comparison,
                  const SurfaceVolumeResult& r) {
    f << '"' << label << '"' << ',' << '"' << base << '"' << ',' << '"' << comparison << '"' << ','
      << FormatVolumeYd3(r.cutFt3, p) << ',' << FormatVolumeYd3(r.fillFt3, p) << ',' << FormatVolumeYd3(r.netFt3, p)
      << ',' << FormatLinear(r.cutAreaFt2, p) << ',' << FormatLinear(r.fillAreaFt2, p) << ','
      << FormatLinear(r.commonAreaFt2, p) << '\n';
  };
  for (const auto& row : st.volumeDashboard.rows) {
    if (row.hasResult)
      emit(row.label.empty() ? "row" : row.label, SurfaceNameForId(st, row.baseSurfaceId),
           SurfaceNameForId(st, row.comparisonSurfaceId), row.result);
  }
  if (haveLive)
    emit("live", SurfaceNameForId(st, st.volumeDashboard.baseSurfaceId),
         SurfaceNameForId(st, st.volumeDashboard.comparisonSurfaceId), st.volumeDashboard.lastResult);
  else if (st.hasLastVolumeResult)
    emit("volumes", st.lastVolumeBaseName, st.lastVolumeComparisonName, st.lastVolumeResult);
  log.push_back("VOLCSV — wrote \"" + path + "\".");
}
