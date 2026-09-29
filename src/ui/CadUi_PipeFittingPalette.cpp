// REQ-350 / TASK-275 — the Pipe Fittings palette: the library parts that fit the pipe run being
// routed, grouped by category, each with a shaded preview and its name.
//
// This file draws the window and records which parts it wants pictures of. It contains no `gl*` call:
// the thumbnails are rendered by `ViewportRenderer` (ADR-062) and arrive here as opaque texture ids,
// exactly as the viewport's own image already does. Which parts belong on which tab is decided by
// `CadPipePaletteCollectRows` in the command layer, where it is unit-tested without a window.

#include "CadUi.hpp"
#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "CadUiPaletteTabs.hpp"
#include "render/ViewportRenderer.hpp"
#include "util/brep.hpp"

#include "imgui.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

/// Thumbnail edge length, in pixels. Big enough to tell a weld-neck flange from a blind one, small
/// enough that a tab full of parts is a short list rather than a slideshow.
constexpr int kPipeFittingThumbPx = 64;

/// How many thumbnails may be rendered in one frame (ADR-062). Each is a small offscreen pass, and
/// they are one-off — but a library of a hundred matching parts must not turn its first frame into a
/// hundred render passes, so the work is spread over consecutive frames instead. A row waiting for its
/// picture shows a placeholder for a frame or two.
constexpr int kPipeFittingThumbsPerFrame = 4;

/// Connection-port role colours, matching the BEDIT authoring gizmo and the INSERT library pane, so a
/// port means the same thing everywhere it is drawn.
void PortRoleColor(CadBlockConnectionRole role, float* rgba) {
  switch (role) {
    case CadBlockConnectionRole::Outlet:
      rgba[0] = 0.35f; rgba[1] = 0.63f; rgba[2] = 0.94f; break;
    case CadBlockConnectionRole::Branch:
      rgba[0] = 0.94f; rgba[1] = 0.63f; rgba[2] = 0.24f; break;
    case CadBlockConnectionRole::Inlet:
    default:
      rgba[0] = 0.35f; rgba[1] = 0.86f; rgba[2] = 0.47f; break;
  }
  rgba[3] = 1.f;
}

bool Contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

/// The tab the palette is showing, clamped — `pipeFittingPaletteTab` is plain session state and a
/// stale/garbage value must not index past the category list.
CadPipePaletteCategory ActiveCategory(const AppCommandState& cmd) {
  const int t = std::clamp(cmd.pipeFittingPaletteTab, 0, kCadPipePaletteCategoryCount - 1);
  return static_cast<CadPipePaletteCategory>(t);
}

} // namespace

void ServicePipeFittingThumbnails(AppCommandState& cmd, ViewportRenderer& renderer) {
  // Parts edited in BEDIT since the last pass: drop their cached pictures so the next request draws
  // the part as it is now (ADR-062 (c) / TASK-275 ASSUMPTION-1). Cleared even when nothing was cached,
  // since the point is that the name has been dealt with.
  for (const std::string& stale : cmd.pipeFittingThumbStale) {
    renderer.InvalidatePartThumbnail(stale);
    const auto it = std::find(cmd.pipeFittingThumbUnavailable.begin(),
                              cmd.pipeFittingThumbUnavailable.end(), stale);
    if (it != cmd.pipeFittingThumbUnavailable.end())
      cmd.pipeFittingThumbUnavailable.erase(it);  // it may have geometry now
  }
  cmd.pipeFittingThumbStale.clear();

  if (cmd.pipeFittingThumbRequests.empty())
    return;

  int done = 0;
  while (!cmd.pipeFittingThumbRequests.empty() && done < kPipeFittingThumbsPerFrame) {
    const std::string name = cmd.pipeFittingThumbRequests.front();
    cmd.pipeFittingThumbRequests.erase(cmd.pipeFittingThumbRequests.begin());
    ++done;

    // The part's geometry, from the drawing when it is already there, and otherwise imported into a
    // SCRATCH state purely to be pictured.
    //
    // Scratch, and not the drawing, because LISTING a part must not add a block definition to the
    // user's drawing — that is what placing one does. This is also the case that made every row a
    // blank box: the bundled sweep imports `fittings/*.sat` but not `fittings/*.dwg`, so a .dwg
    // fitting is never in `blockDefs` until it is placed, and giving up here left the placeholder
    // showing forever.
    std::unique_ptr<AppCommandState> scratch;  // owns the preview-only import for this iteration
    const std::vector<CadBlockDefinition>* defs = &cmd.blockDefs;
    int di = CadBlockFindDef(cmd.blockDefs, name);
    if (di < 0) {
      const auto row = std::find_if(cmd.pipeFittingLibraryCache.begin(), cmd.pipeFittingLibraryCache.end(),
                                    [&](const CadBlockLibraryEntry& e) { return e.name == name; });
      if (row != cmd.pipeFittingLibraryCache.end()) {
        scratch = std::make_unique<AppCommandState>();
        std::vector<std::string> quiet;  // an unreadable library file is not the command line's business
        if (CadBlocksImportLibraryEntry(*scratch, *row, quiet)) {
          di = CadBlockFindDef(scratch->blockDefs, name);
          defs = &scratch->blockDefs;
        }
      }
    }
    if (di < 0) {
      // No such part, or it could not be read. Remembered so the row is asked about once rather than
      // re-imported every frame; the row still lists by name (ADR-062's degrade-to-name-only).
      if (!Contains(cmd.pipeFittingThumbUnavailable, name))
        cmd.pipeFittingThumbUnavailable.push_back(name);
      continue;
    }
    const CadBlockDefinition& def = (*defs)[static_cast<size_t>(di)];

    // The part at its own origin, unscaled: a preview is a picture of the DEFINITION, not of a
    // placement, so there is no insertion transform to apply.
    CadBlockRef ref;
    ref.defName = def.name;

    std::vector<float> triVerts;
    std::vector<float> triNormals;
    std::vector<float> edgeVerts;

    std::vector<CadBlockWorldSolid> solids;
    CadBlockCollectWorldSolids(*defs, ref, EntityAttributes{}, &solids);
    for (const CadBlockWorldSolid& ws : solids) {
      if (!ws.solid)
        continue;
      brep::Tessellation tess;
      brep::Problem why = brep::Problem::Ok;
      if (brep::Tessellate(*ws.solid, kSolidChordToleranceFt, &tess, &why)) {
        std::vector<float> v;
        std::vector<float> n;
        std::vector<int> faceIds;  // unused here; the thumbnail has no sub-object picking
        ExpandTessellation(tess, &v, &n, &faceIds);
        triVerts.insert(triVerts.end(), v.begin(), v.end());
        triNormals.insert(triNormals.end(), n.begin(), n.end());
      }
      std::vector<double> edges;
      if (brep::TessellateEdges(*ws.solid, kSolidChordToleranceFt, &edges, &why)) {
        for (double e : edges)
          edgeVerts.push_back(static_cast<float>(e));
      }
    }

    // A part may also carry 2D linework — and a 2D-only block carries nothing else, so this is what
    // gives it a picture instead of an empty box (TASK-275 ASSUMPTION-3).
    std::vector<CadBlockWorldSeg> segs;
    CadBlockCollectWorldLines(*defs, ref, EntityAttributes{}, &segs);
    for (const CadBlockWorldSeg& s : segs) {
      edgeVerts.push_back(s.x0);
      edgeVerts.push_back(s.y0);
      edgeVerts.push_back(s.z0);
      edgeVerts.push_back(s.x1);
      edgeVerts.push_back(s.y1);
      edgeVerts.push_back(s.z1);
    }

    std::vector<float> ports;
    ports.reserve(def.connections.size() * 7);
    for (const CadBlockConnection& c : def.connections) {
      float rgba[4];
      PortRoleColor(c.role, rgba);
      ports.push_back(c.x);
      ports.push_back(c.y);
      ports.push_back(c.z);
      ports.insert(ports.end(), rgba, rgba + 4);
    }

    ViewportRenderer::PartThumbnailInput in;
    in.triVerts = &triVerts;
    in.triNormals = &triNormals;
    in.edgeVerts = &edgeVerts;
    in.portMarkers = &ports;
    in.rgba[0] = 0.72f;
    in.rgba[1] = 0.76f;
    in.rgba[2] = 0.82f;
    in.rgba[3] = 1.f;
    if (!renderer.EnsurePartThumbnail(name, in, kPipeFittingThumbPx)) {
      // Nothing drawable, or no GL object to draw into. Remembered, so the row is asked for once
      // rather than re-attempted every frame (ADR-062's degrade-to-name-only consequence).
      if (!Contains(cmd.pipeFittingThumbUnavailable, name))
        cmd.pipeFittingThumbUnavailable.push_back(name);
    }
  }
}

void DrawPipeFittingPalette(AppCommandState& cmd, std::vector<std::string>& log, ViewportRenderer& renderer) {
  if (!cmd.pipeFittingPaletteOpen)
    return;

  ImGui::SetNextWindowSize(ImVec2(320.f, 520.f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(360.f, 120.f), ImGuiCond_FirstUseEver);
  bool open = cmd.pipeFittingPaletteOpen;
  if (!ImGui::Begin("PIPE FITTINGS", &open, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    cmd.pipeFittingPaletteOpen = open;
    return;
  }
  cmd.pipeFittingPaletteOpen = open;

  const std::string& size = cmd.pipeRunNominalSize;
  const CadPipePressureClass runClass = ParseCadPipePressureClass(cmd.pipeRunPressureClassTag);

  // The size changing is what re-filters the list (REQ-350's live re-filter condition). Thumbnail
  // bookkeeping is reset with it, so a part that could not be pictured at one size is asked for again
  // at another — the library may have been added to in between.
  if (cmd.pipeFittingPaletteShownSize != size) {
    cmd.pipeFittingPaletteShownSize = size;
    cmd.pipeFittingThumbRequests.clear();
    cmd.pipeFittingThumbUnavailable.clear();
    cmd.pipeFittingLibraryCacheValid = false;
  }

  if (size.empty())
    ImGui::TextDisabled("No pipe size set");
  else if (runClass == CadPipePressureClass::None)
    ImGui::Text("Pipe run: %s", size.c_str());
  else
    ImGui::Text("Pipe run: %s  %s", size.c_str(), std::string(CadPipePressureClassTag(runClass)).c_str());
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 58.f);
  if (ImGui::SmallButton("Refresh")) {
    // Re-read the library folders: the one thing the palette cannot notice on its own is a file
    // copied in from outside the application.
    cmd.pipeFittingLibraryCacheValid = false;
    cmd.pipeFittingThumbUnavailable.clear();
  }
  ImGui::Separator();

  const CadPipePaletteCategory category = ActiveCategory(cmd);

  // Reading the library is file I/O, so it happens on the events that can change it — not every
  // frame (§11.7). Filtering the cached rows below is pure and costs nothing worth caching.
  if (!cmd.pipeFittingLibraryCacheValid) {
    CadBlocksCollectLibraryEntries(cmd, &cmd.pipeFittingLibraryCache);
    cmd.pipeFittingLibraryCacheValid = true;
  }
  std::vector<CadBlockLibraryEntry> rows;
  CadPipePaletteCollectRows(cmd.pipeFittingLibraryCache, size, runClass, category, &rows);

  ImGui::BeginChild("##pfpbody", ImVec2(-46.f, 0.f), true);
  if (rows.empty()) {
    ImGui::TextWrapped("%s", CadPipePaletteEmptyReason(category, size).c_str());
  } else {
    const float thumb = static_cast<float>(kPipeFittingThumbPx);
    for (const CadBlockLibraryEntry& e : rows) {
      ImGui::PushID(e.name.c_str());
      const bool clicked =
          ImGui::Selectable("##part", false, ImGuiSelectableFlags_None, ImVec2(0.f, thumb + 8.f));
      const ImVec2 p = ImGui::GetItemRectMin();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const unsigned int tex = renderer.PartThumbnailTexture(e.name);
      const ImVec2 imgMin(p.x + 4.f, p.y + 4.f);
      const ImVec2 imgMax(imgMin.x + thumb, imgMin.y + thumb);
      if (tex != 0) {
        dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(tex)), imgMin, imgMax);
      } else {
        // Placeholder while the render pass catches up — or permanently, for a part with no geometry
        // to picture. Either way the row is fully usable; the picture is an aid, not the content.
        dl->AddRect(imgMin, imgMax, ImGui::GetColorU32(ImGuiCol_Border));
        if (!Contains(cmd.pipeFittingThumbUnavailable, e.name) &&
            !Contains(cmd.pipeFittingThumbRequests, e.name))
          cmd.pipeFittingThumbRequests.push_back(e.name);
      }

      const float th = ImGui::GetTextLineHeight();
      const float textX = imgMax.x + 10.f;
      dl->AddText(ImVec2(textX, p.y + thumb * 0.5f - th), ImGui::GetColorU32(ImGuiCol_Text),
                  e.name.c_str());
      std::string sub(CadPipePartTypeTag(e.partType));
      if (!e.nominalSize.empty())
        sub = e.nominalSize + "  " + sub;
      if (e.pressureClass != CadPipePressureClass::None)
        sub += "  " + std::string(CadPipePressureClassTag(e.pressureClass));
      dl->AddText(ImVec2(textX, p.y + thumb * 0.5f + 2.f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                  sub.c_str());

      if (clicked) {
        (void)CadPipePaletteArmPart(cmd, e, log);  // refusals are logged there (REQ-201)
        // Arming imports the part when it was not in the drawing yet, which changes what the library
        // reading says about it (and gives it geometry to draw a thumbnail from).
        cmd.pipeFittingLibraryCacheValid = false;
      }
      ImGui::PopID();
    }
  }
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("##pfptabs", ImVec2(42.f, 0.f), false);
  for (int i = 0; i < kCadPipePaletteCategoryCount; ++i) {
    const auto c = static_cast<CadPipePaletteCategory>(i);
    CadUiPaletteTabButton(std::string(CadPipePaletteCategoryLabel(c)).c_str(), i,
                          &cmd.pipeFittingPaletteTab);
  }
  ImGui::EndChild();

  ImGui::End();
}
