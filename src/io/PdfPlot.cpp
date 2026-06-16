#include "PdfPlot.hpp"

#include "CadCommands.hpp"
#include "PaperSpace.hpp"

#include <fpdfview.h>
#include <fpdf_edit.h>
#include <fpdf_save.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr float kPtPerIn = 72.f;  // PDF user space is points (1/72 inch), origin bottom-left, Y up.

// FPDF_FILEWRITE that streams into an ofstream. fw MUST be the first member (PDFium passes &fw back).
struct PdfWriter {
  FPDF_FILEWRITE fw{};
  std::ofstream* os = nullptr;
};
int WriteBlockCb(FPDF_FILEWRITE* pThis, const void* data, unsigned long size) {
  auto* w = reinterpret_cast<PdfWriter*>(pThis);
  w->os->write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
  return w->os->good() ? 1 : 0;
}

// Liang–Barsky clip of segment (x0,y0)-(x1,y1) to the rect [xmin,xmax]x[ymin,ymax]. Updates endpoints;
// returns false if the segment is entirely outside.
bool ClipSeg(float xmin, float ymin, float xmax, float ymax, float& x0, float& y0, float& x1, float& y1) {
  float t0 = 0.f, t1 = 1.f;
  const float dx = x1 - x0, dy = y1 - y0;
  auto clip = [&](float p, float q) {
    if (p == 0.f)
      return q >= 0.f;
    const float r = q / p;
    if (p < 0.f) {
      if (r > t1) return false;
      if (r > t0) t0 = r;
    } else {
      if (r < t0) return false;
      if (r < t1) t1 = r;
    }
    return true;
  };
  if (clip(-dx, x0 - xmin) && clip(dx, xmax - x0) && clip(-dy, y0 - ymin) && clip(dy, ymax - y0)) {
    const float nx0 = x0 + t0 * dx, ny0 = y0 + t0 * dy;
    const float nx1 = x0 + t1 * dx, ny1 = y0 + t1 * dy;
    x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
    return true;
  }
  return false;
}

}  // namespace

bool PlotLayoutsToPdf(const AppCommandState& st, const std::vector<int>& layoutIndices, const char* pathUtf8,
                      std::vector<std::string>& log) {
  if (!pathUtf8 || !pathUtf8[0]) {
    log.push_back("PLOT — no output path.");
    return false;
  }
  if (layoutIndices.empty()) {
    log.push_back("PLOT — no layouts selected.");
    return false;
  }

  // Layer → plottable lookup (excludes off / frozen / non-plottable layers). Unknown layer → plottable.
  std::unordered_map<std::string, bool> layerPlot;
  for (const CadLayerRow& r : st.drawingLayerTable)
    layerPlot[r.name] = r.on && !r.frozen && r.plottable;
  auto plottable = [&](const std::string& layer) {
    auto it = layerPlot.find(layer);
    return it == layerPlot.end() ? true : it->second;
  };

  FPDF_DOCUMENT doc = FPDF_CreateNewDocument();
  if (!doc) {
    log.push_back("PLOT — could not create the PDF document.");
    return false;
  }

  const double oX = st.worldDocumentOriginX;
  const double oY = st.worldDocumentOriginY;
  int pages = 0;

  for (int li : layoutIndices) {
    if (li < 0 || static_cast<size_t>(li) >= st.paperLayouts.size())
      continue;
    const PaperLayout& L = st.paperLayouts[static_cast<size_t>(li)];
    const double wPts = static_cast<double>(L.sheetWidthIn()) * kPtPerIn;
    const double hPts = static_cast<double>(L.sheetHeightIn()) * kPtPerIn;
    FPDF_PAGE page = FPDFPage_New(doc, pages, wPts, hPts);
    if (!page)
      continue;

    // Accumulate all stroked segments (paper inches) for this page into one path object for efficiency.
    std::vector<std::array<float, 4>> segs;  // x0,y0,x1,y1 in paper inches
    auto addSeg = [&](float ax, float ay, float bx, float by) { segs.push_back({ax, ay, bx, by}); };

    for (const Viewport& vp : L.viewports) {
      const float vx0 = vp.paperXIn, vy0 = vp.paperYIn;
      const float vx1 = vp.paperXIn + vp.paperWIn, vy1 = vp.paperYIn + vp.paperHIn;
      const float cxp = (vx0 + vx1) * 0.5f, cyp = (vy0 + vy1) * 0.5f;
      const float s = vp.safeScale();
      auto m2p = [&](double wx, double wy, float* px, float* py) {
        *px = cxp + static_cast<float>((wx - vp.modelCenterX) / static_cast<double>(s));
        *py = cyp + static_cast<float>((wy - vp.modelCenterY) / static_cast<double>(s));
      };
      auto emitModelSeg = [&](double w0x, double w0y, double w1x, double w1y) {
        float p0x, p0y, p1x, p1y;
        m2p(w0x, w0y, &p0x, &p0y);
        m2p(w1x, w1y, &p1x, &p1y);
        if (ClipSeg(vx0, vy0, vx1, vy1, p0x, p0y, p1x, p1y))
          addSeg(p0x, p0y, p1x, p1y);
      };

      // Lines.
      for (size_t i = 0; i + 5 < st.userLinesFlat.size(); i += 6) {
        const size_t idx = i / 6;
        if (idx < st.userLineAttrs.size() && !plottable(st.userLineAttrs[idx].layer))
          continue;
        emitModelSeg(st.userLinesFlat[i] + oX, st.userLinesFlat[i + 1] + oY, st.userLinesFlat[i + 3] + oX,
                     st.userLinesFlat[i + 4] + oY);
      }
      // Polylines.
      for (size_t pi = 0; pi < st.userPolylineOffsets.size(); ++pi) {
        if (pi < st.userPolylineAttrs.size() && !plottable(st.userPolylineAttrs[pi].layer))
          continue;
        const int start = st.userPolylineOffsets[pi];
        const int end = (pi + 1 < st.userPolylineOffsets.size())
                            ? st.userPolylineOffsets[pi + 1]
                            : static_cast<int>(st.userPolylineVerts.size() / 3);
        for (int k = start; k + 1 < end; ++k)
          emitModelSeg(st.userPolylineVerts[static_cast<size_t>(k) * 3] + oX,
                       st.userPolylineVerts[static_cast<size_t>(k) * 3 + 1] + oY,
                       st.userPolylineVerts[static_cast<size_t>(k + 1) * 3] + oX,
                       st.userPolylineVerts[static_cast<size_t>(k + 1) * 3 + 1] + oY);
      }
      // Circles (sampled to a polygon).
      for (size_t i = 0; i + 2 < st.userCirclesCxCyR.size(); i += 3) {
        const size_t idx = i / 3;
        if (idx < st.userCircleAttrs.size() && !plottable(st.userCircleAttrs[idx].layer))
          continue;
        const double cx = st.userCirclesCxCyR[i] + oX, cy = st.userCirclesCxCyR[i + 1] + oY;
        const double r = st.userCirclesCxCyR[i + 2];
        constexpr int kSeg = 72;
        double pxp = 0, pyp = 0;
        for (int k = 0; k <= kSeg; ++k) {
          const double t = 2.0 * 3.14159265358979 * k / kSeg;
          const double wx = cx + r * std::cos(t), wy = cy + r * std::sin(t);
          if (k > 0)
            emitModelSeg(pxp, pyp, wx, wy);
          pxp = wx;
          pyp = wy;
        }
      }
      // Arcs (sampled).
      for (const CadArc& a : st.userArcs) {
        const int kSeg = std::clamp(static_cast<int>(std::fabs(a.sweepRad) / 0.1f) + 2, 2, 256);
        double pxp = 0, pyp = 0;
        for (int k = 0; k <= kSeg; ++k) {
          const float t = a.startRad + a.sweepRad * (static_cast<float>(k) / static_cast<float>(kSeg));
          const double wx = static_cast<double>(a.cx + a.r * std::cos(t)) + oX;
          const double wy = static_cast<double>(a.cy + a.r * std::sin(t)) + oY;
          if (k > 0)
            emitModelSeg(pxp, pyp, wx, wy);
          pxp = wx;
          pyp = wy;
        }
      }
      // Survey-point crosses (fixed 0.05" on the sheet).
      for (const SurveyPoint& sp : st.surveyPoints) {
        if (!plottable(sp.layer))
          continue;
        float pcx, pcy;
        m2p(static_cast<double>(sp.easting) + oX, static_cast<double>(sp.northing) + oY, &pcx, &pcy);
        constexpr float hc = 0.05f;
        float ax = pcx - hc, ay = pcy, bx = pcx + hc, by = pcy;
        if (ClipSeg(vx0, vy0, vx1, vy1, ax, ay, bx, by))
          addSeg(ax, ay, bx, by);
        float cx2 = pcx, cy2 = pcy - hc, dx2 = pcx, dy2 = pcy + hc;
        if (ClipSeg(vx0, vy0, vx1, vy1, cx2, cy2, dx2, dy2))
          addSeg(cx2, cy2, dx2, dy2);
      }
      // Viewport border — only if the viewport's layer is plottable.
      if (plottable(vp.layer)) {
        addSeg(vx0, vy0, vx1, vy0);
        addSeg(vx1, vy0, vx1, vy1);
        addSeg(vx1, vy1, vx0, vy1);
        addSeg(vx0, vy1, vx0, vy0);
      }
    }

    if (!segs.empty()) {
      FPDF_PAGEOBJECT path = FPDFPageObj_CreateNewPath(segs[0][0] * kPtPerIn, segs[0][1] * kPtPerIn);
      FPDFPath_LineTo(path, segs[0][2] * kPtPerIn, segs[0][3] * kPtPerIn);
      for (size_t i = 1; i < segs.size(); ++i) {
        FPDFPath_MoveTo(path, segs[i][0] * kPtPerIn, segs[i][1] * kPtPerIn);
        FPDFPath_LineTo(path, segs[i][2] * kPtPerIn, segs[i][3] * kPtPerIn);
      }
      FPDFPageObj_SetStrokeColor(path, 0, 0, 0, 255);
      FPDFPageObj_SetStrokeWidth(path, 0.5f);
      FPDFPath_SetDrawMode(path, 0 /*no fill*/, 1 /*stroke*/);
      FPDFPage_InsertObject(page, path);
    }
    FPDFPage_GenerateContent(page);
    FPDF_ClosePage(page);
    ++pages;
  }

  if (pages == 0) {
    FPDF_CloseDocument(doc);
    log.push_back("PLOT — no plottable layouts.");
    return false;
  }

  std::ofstream os(fs::u8path(std::string(pathUtf8)), std::ios::binary);
  if (!os) {
    FPDF_CloseDocument(doc);
    log.push_back(std::string("PLOT — could not open output file: ") + pathUtf8);
    return false;
  }
  PdfWriter writer;
  writer.fw.version = 1;
  writer.fw.WriteBlock = &WriteBlockCb;
  writer.os = &os;
  const FPDF_BOOL ok = FPDF_SaveAsCopy(doc, &writer.fw, 0);
  FPDF_CloseDocument(doc);
  os.close();
  if (!ok) {
    log.push_back("PLOT — failed to write the PDF.");
    return false;
  }
  log.push_back("PLOT — wrote " + std::to_string(pages) + " page(s) to " + pathUtf8);
  return true;
}
