#pragma once

#include "GoSurvey/Pdf/PdfAttach.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <nlohmann/json.hpp>

inline nlohmann::json PdfPlacementToJson(const PdfAttachment& a, double originX, double originY) {
  return {{"page", a.pageIndex}, {"x", originX + a.insertX}, {"y", originY + a.insertY},
          {"scale", a.scale}, {"rotationDeg", a.rotationDeg}, {"dpi", a.rasterDpi},
          {"pageWidthPts", a.pageWidthPts}, {"pageHeightPts", a.pageHeightPts},
          {"snapEnabled", a.snapEnabled}, {"snapLines", a.snapLines},
          {"snapCircles", a.snapCircles}, {"snapText", a.snapText},
          {"layer", a.layer}, {"fade", a.fade}, {"showBackground", a.showBackground}};
}

inline std::optional<PdfAttachment> PdfPlacementFromJson(const nlohmann::json& j, std::string path,
                                                         double originX, double originY) {
  if (!j.is_object() || path.empty() || path.find('\0') != std::string::npos)
    return std::nullopt;
  try {
    PdfAttachment a;
    a.filePath = std::move(path);
    const auto page = j.find("page");
    if (page != j.end()) {
      if (!page->is_number_integer())
        return std::nullopt;
      const double index = page->get<double>();
      if (index < 0.0 || index > std::numeric_limits<int>::max())
        return std::nullopt;
      a.pageIndex = static_cast<int>(index);
    }
    const double x = j.value("x", originX) - originX;
    const double y = j.value("y", originY) - originY;
    constexpr double maxCoordinate = std::numeric_limits<float>::max();
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > maxCoordinate || std::abs(y) > maxCoordinate)
      return std::nullopt;
    a.insertX = static_cast<float>(x);
    a.insertY = static_cast<float>(y);
    a.scale = j.value("scale", a.scale);
    a.rotationDeg = j.value("rotationDeg", a.rotationDeg);
    a.rasterDpi = j.value("dpi", a.rasterDpi);
    a.pageWidthPts = j.value("pageWidthPts", a.pageWidthPts);
    a.pageHeightPts = j.value("pageHeightPts", a.pageHeightPts);
    a.snapEnabled = j.value("snapEnabled", a.snapEnabled);
    a.snapLines = j.value("snapLines", a.snapLines);
    a.snapCircles = j.value("snapCircles", a.snapCircles);
    a.snapText = j.value("snapText", a.snapText);
    a.layer = j.value("layer", a.layer);
    a.fade = j.value("fade", a.fade);
    a.showBackground = j.value("showBackground", a.showBackground);
    if (!std::isfinite(a.scale) || a.scale <= 0.f || !std::isfinite(a.rotationDeg) ||
        !std::isfinite(a.rasterDpi) || a.rasterDpi <= 0.f ||
        !std::isfinite(a.pageWidthPts) || a.pageWidthPts < 0.f ||
        !std::isfinite(a.pageHeightPts) || a.pageHeightPts < 0.f || !std::isfinite(a.fade))
      return std::nullopt;
    a.fade = std::clamp(a.fade, 0.f, 1.f);
    a.needsReload = true;
    return a;
  } catch (const nlohmann::json::exception&) {
    return std::nullopt;
  }
}

inline nlohmann::json PdfAttachmentsToJson(const std::vector<PdfAttachment>& attachments) {
  auto records = nlohmann::json::array();
  for (const auto& attachment : attachments) {
    auto record = PdfPlacementToJson(attachment, 0.0, 0.0);
    record["path"] = attachment.filePath;
    records.push_back(std::move(record));
  }
  return records;
}

inline std::vector<PdfAttachment> PdfAttachmentsFromJson(const nlohmann::json& records,
                                                        std::vector<std::string>& log) {
  std::vector<PdfAttachment> attachments;
  if (!records.is_array())
    return attachments;
  for (const auto& record : records) {
    const auto path = record.find("path");
    auto attachment = path != record.end() && path->is_string()
        ? PdfPlacementFromJson(record, path->get<std::string>(), 0.0, 0.0) : std::nullopt;
    if (attachment)
      attachments.push_back(std::move(*attachment));
    else
      log.push_back("Skipped an invalid PDF underlay placement.");
  }
  return attachments;
}
