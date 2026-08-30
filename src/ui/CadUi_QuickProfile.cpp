// Quick Profile graph (REQ-145). Session UI only — never `.gs`, matching Volume Dashboard.

#include "CadUi.hpp"

#include "CadCommands.hpp"
#include "NumFormat.hpp"

#include <imgui.h>

#include <vector>

void DrawQuickProfileWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  (void)log;
  AppCommandState::QuickProfileState& qp = cmd.quickProfile;
  if (!qp.open)
    return;

  ImGui::SetNextWindowSize(ImVec2(480.f, 280.f), ImGuiCond_FirstUseEver);
  bool open = qp.open;
  if (!ImGui::Begin("Quick Profile", &open)) {
    qp.open = open;
    ImGui::End();
    return;
  }
  qp.open = open;

  if (!qp.hasResult || qp.samples.empty()) {
    ImGui::TextUnformatted("No profile. Use QUICKPROFILE or the Tin Surface Launch Pad.");
    ImGui::End();
    return;
  }

  const int p = cmd.displayLinearPrecision;
  ImGui::Text("Surface: %s", qp.surfaceName.c_str());
  ImGui::Text("Length %s   samples %d (%d on surface)   elev %s to %s", FormatLinear(qp.length, p).c_str(),
              static_cast<int>(qp.samples.size()), qp.onSurfaceCount, FormatLinear(qp.minZ, p).c_str(),
              FormatLinear(qp.maxZ, p).c_str());

  const float hAvail = ImGui::GetContentRegionAvail().y - 8.f;
  const ImVec2 canvas(ImGui::GetContentRegionAvail().x, hAvail > 120.f ? hAvail : 120.f);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##qpcanvas", canvas);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 a = origin;
  const ImVec2 b(origin.x + canvas.x, origin.y + canvas.y);
  dl->AddRectFilled(a, b, IM_COL32(18, 18, 18, 255));
  dl->AddRect(a, b, IM_COL32(80, 80, 80, 255));

  const float padL = 44.f, padR = 12.f, padT = 10.f, padB = 22.f;
  const float x0 = a.x + padL, x1 = b.x - padR, y0 = a.y + padT, y1 = b.y - padB;
  if (x1 <= x0 || y1 <= y0) {
    ImGui::End();
    return;
  }

  const double staMax = std::max(qp.length, 1.0e-6);
  double zLo = qp.minZ;
  double zHi = qp.maxZ;
  if (zHi - zLo < 1.0e-6) {
    zLo -= 1.0;
    zHi += 1.0;
  }
  const auto toX = [&](double sta) {
    return static_cast<float>(x0 + (sta / staMax) * static_cast<double>(x1 - x0));
  };
  const auto toY = [&](double z) {
    return static_cast<float>(y1 - ((z - zLo) / (zHi - zLo)) * static_cast<double>(y1 - y0));
  };

  dl->AddLine(ImVec2(x0, y1), ImVec2(x1, y1), IM_COL32(120, 120, 120, 255));
  dl->AddLine(ImVec2(x0, y0), ImVec2(x0, y1), IM_COL32(120, 120, 120, 255));

  std::vector<ImVec2> poly;
  poly.reserve(qp.samples.size());
  auto flush = [&]() {
    if (poly.size() >= 2)
      dl->AddPolyline(poly.data(), static_cast<int>(poly.size()), IM_COL32(80, 180, 255, 255), 0, 2.f);
    poly.clear();
  };
  for (const SurfaceProfileSample& s : qp.samples) {
    if (!s.onSurface) {
      flush();
      continue;
    }
    poly.push_back(ImVec2(toX(s.station), toY(s.z)));
  }
  flush();

  ImGui::End();
}
