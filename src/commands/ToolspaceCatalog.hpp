#pragma once

#include "CadCommands.hpp"

#include <string>
#include <vector>

/// Labels for the REQ-142 Toolspace trees. Header-only so Catch2 can check them without linking UI.

[[nodiscard]] inline std::string ToolspaceActiveDrawingName(const AppCommandState& st) {
  // REQ-308: drawingTabs[0] is the Start screen, not a drawing — when it is active the Toolspace
  // still describes the first real drawing rather than showing "Start".
  int idx = st.activeDrawingIdx;
  if (idx < FirstDrawingTabIndex())
    idx = FirstDrawingTabIndex();
  if (idx >= 0 && static_cast<size_t>(idx) < st.drawingTabs.size() &&
      !st.drawingTabs[static_cast<size_t>(idx)].name.empty()) {
    return st.drawingTabs[static_cast<size_t>(idx)].name;
  }
  return "Drawing 1";
}

[[nodiscard]] inline size_t ToolspaceFeatureLineCount(const AppCommandState& st) {
  if (st.featureLineOffsets.empty())
    return 0;
  return st.featureLineOffsets.size() - 1;
}

inline void AppendToolspaceSurfaceFolderLines(const CadSurface& s, std::vector<std::string>* out) {
  if (out == nullptr)
    return;
  out->push_back(s.name);
  out->push_back("Masks");
  for (const CadSurfaceBoundary& b : s.boundaries) {
    if (b.kind != CadBoundaryKind::Mask)
      continue;
    out->push_back(b.name.empty() ? std::string("Mask") : b.name);
  }
  out->push_back("Watersheds");
  out->push_back("Definition");
  out->push_back("Boundaries");
  for (const CadSurfaceBoundary& b : s.boundaries) {
    if (b.kind == CadBoundaryKind::Mask)
      continue;
    out->push_back(b.name.empty() ? std::string("Boundary") : b.name);
  }
  out->push_back("Breaklines");
  out->push_back("Contours");
  out->push_back("Point Files");
  out->push_back("Point Groups");
  for (const std::string& gn : s.sourcePointGroups)
    out->push_back(gn);
  out->push_back("Edits");
}

inline void AppendToolspaceProspectorLines(const AppCommandState& st, std::vector<std::string>* out) {
  if (out == nullptr)
    return;
  out->push_back(ToolspaceActiveDrawingName(st));
  out->push_back("Points");
  out->push_back("Point Groups");
  for (const PointGroup& g : st.pointGroups)
    out->push_back(g.name);
  out->push_back("Surfaces");
  for (const CadSurface& s : st.cadSurfaces)
    AppendToolspaceSurfaceFolderLines(s, out);
  out->push_back("Feature Lines");
  const size_t nFl = ToolspaceFeatureLineCount(st);
  for (size_t i = 0; i < nFl; ++i) {
    if (i < st.featureLineInfo.size() && !st.featureLineInfo[i].name.empty())
      out->push_back(st.featureLineInfo[i].name);
    else
      out->push_back("Feature Line " + std::to_string(i + 1));
  }
}

inline void AppendToolspaceSettingsLines(const AppCommandState& st, std::vector<std::string>* out) {
  if (out == nullptr)
    return;
  out->push_back(ToolspaceActiveDrawingName(st));
  out->push_back("General");
  out->push_back("Text Styles");
  for (const TextStyle& ts : st.textStyles)
    out->push_back(ts.name);
  out->push_back("Layers");
  for (const CadLayerRow& row : st.drawingLayerTable)
    out->push_back(row.name);
  out->push_back("Dimension Style");
  out->push_back("Surface");
  out->push_back("Surface Styles");
  for (const SurfaceStyle& ss : st.surfaceStyles)
    out->push_back(ss.name);
}

[[nodiscard]] inline bool ToolspaceLineIsForbiddenCivil3d(const std::string& line) {
  return line == "Alignments" || line == "Sites" || line == "Pipe Networks" || line == "Parcel" ||
         line == "Grading" || line == "Profile" || line == "Data Shortcuts" ||
         line == "Corridors" || line == "Assemblies" || line == "Catchments" || line == "DEM Files";
}
