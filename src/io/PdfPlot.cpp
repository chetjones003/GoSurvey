#include "PdfPlot.hpp"

#include "CadCommands.hpp"
#include "PaperSpace.hpp"
#include "ShxFont.hpp"           // shared lower-layer SHX stroke geometry (ADR-022)
#include "MtextRichFormat.hpp"   // MtextRichFlattenToPlain (already used by DxfIo)
#include "ColorContrast.hpp"     // background-adaptive white/black (REQ-048 refinement)
#include "PlotFont.hpp"          // pure font-name/encoding helpers for TTF plot text (REQ-049)
#include "CadFontName.hpp"
#include "CadDimStroke.hpp"
#include "util/cadtable.hpp"

#include <cctype>

#include <fpdfview.h>
#include <fpdf_edit.h>
#include <fpdf_save.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <map>
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

// Resolve a TrueType family name (REQ-049) to its .ttf file under the Windows Fonts directory, probing the
// pure candidate list (alias, then de-spaced) from PlotFont.hpp. Returns "" if none exists (caller then
// substitutes a base-14 font). The Fonts path matches the FontRegistry / CadCommands convention.
std::string ResolveTtfPath(const std::string& family) {
  const fs::path fonts = "C:/Windows/Fonts";
  for (const std::string& fn : plotfont::TtfCandidates(family)) {
    const fs::path p = fonts / fn;
    if (fs::exists(p))
      return p.u8string();
  }
  return {};
}

}  // namespace

bool PlotLayoutsToPdf(AppCommandState& st, const std::vector<int>& layoutIndices, const char* pathUtf8,
                      std::vector<std::string>& log) {
  if (!pathUtf8 || !pathUtf8[0]) {
    log.push_back("PLOT — no output path.");
    return false;
  }
  if (layoutIndices.empty()) {
    log.push_back("PLOT — no layouts selected.");
    return false;
  }

  RefreshSurfaceDisplayGeometry(st);

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

  // TrueType fonts embedded for native sheet text (REQ-049). Loaded once per document, keyed by family, and
  // closed after the save. A family that cannot be embedded from its .ttf degrades to the closest base-14
  // font and is logged once (REQ-201 — never silently dropped).
  std::unordered_map<std::string, FPDF_FONT> fontCache;
  std::unordered_map<std::string, bool> ttfWarned;
  auto plotFont = [&](const std::string& family) -> FPDF_FONT {
    const std::string key = plotfont::LowerAscii(family);
    if (auto it = fontCache.find(key); it != fontCache.end())
      return it->second;
    FPDF_FONT font = nullptr;
    const std::string path = ResolveTtfPath(family);
    if (!path.empty()) {
      std::ifstream f(fs::u8path(path), std::ios::binary | std::ios::ate);
      if (f) {
        const std::streamsize sz = f.tellg();
        f.seekg(0);
        std::vector<uint8_t> bytes(static_cast<size_t>(sz));
        if (sz > 0 && f.read(reinterpret_cast<char*>(bytes.data()), sz))
          font = FPDFText_LoadFont(doc, bytes.data(), static_cast<uint32_t>(bytes.size()), FPDF_FONT_TRUETYPE,
                                   /*cid=*/0);
      }
    }
    if (!font) {  // best-effort base-14 substitute — text still appears, and we note the family once
      font = FPDFText_LoadStandardFont(doc, plotfont::StandardSubstitute(family));
      if (!ttfWarned[key]) {
        log.push_back("PLOT — note: could not embed TrueType font '" + family + "'; substituted " +
                      plotfont::StandardSubstitute(family) + " (REQ-049 debt).");
        ttfWarned[key] = true;
      }
    }
    fontCache[key] = font;
    return font;
  };
  auto closeFonts = [&]() {
    for (auto& [k, f] : fontCache)
      if (f)
        FPDFFont_Close(f);
    fontCache.clear();
  };

  for (int li : layoutIndices) {
    if (li < 0 || static_cast<size_t>(li) >= st.paperLayouts.size())
      continue;
    const PaperLayout& L = st.paperLayouts[static_cast<size_t>(li)];
    const double wPts = static_cast<double>(L.sheetWidthIn()) * kPtPerIn;
    const double hPts = static_cast<double>(L.sheetHeightIn()) * kPtPerIn;
    FPDF_PAGE page = FPDFPage_New(doc, pages, wPts, hPts);
    if (!page)
      continue;

    // Accumulate stroked segments (paper inches) for this page, grouped by stroke color so a per-viewport
    // layer color override (REQ-046) prints in its color; everything else stays black. std::map keeps a
    // deterministic emit order for build reproducibility (REQ-200). Key = 0xRRGGBB, default 0x000000.
    std::map<uint32_t, std::vector<std::array<float, 4>>> segsByColor;
    uint32_t curColor = 0x000000u;  // set per entity before emitting
    auto addSeg = [&](float ax, float ay, float bx, float by) {
      segsByColor[curColor].push_back({ax, ay, bx, by});
    };

    struct VpDimLabel {
      float px = 0.f, py = 0.f, rot = 0.f, Hin = 0.1f;
      std::string text;
      std::string fontFamily;
      uint32_t rgb = 0;
    };
    std::vector<VpDimLabel> vpDimLabels;

    for (const Viewport& vp : L.viewports) {
      // Per-viewport layer color override → packed 0xRRGGBB (0 = no override → black).
      // REQ-046/048: plot stroke color as packed 0xRRGGBB. A per-viewport VP Color override wins;
      // otherwise the entity's TRUE color (entity color, else layer color) — full-color plot (ADR-007
      // amended). \p entityColor is "ByLayer" for entities with none (e.g. survey points).
      auto resolveRgb = [&](const std::string& layer, const std::string& entityColor) -> uint32_t {
        float rgba[4] = {0.f, 0.f, 0.f, 1.f};
        if (const std::string* ov = ViewportLayerColorOverride(vp, layer)) {
          ResolveStoredColorForViewport(*ov, 0.f, 0.f, 0.f, 0.f, rgba);
        } else {
          std::string col = entityColor;
          if (col.empty() || col == "ByLayer") {
            const CadLayerRow* lr = FindDrawingLayerRowCi(st, layer);
            col = (lr && !lr->color.empty() && lr->color != "ByLayer") ? lr->color : "White";
          }
          ResolveStoredColorForViewport(col, 0.f, 0.f, 0.f, 0.f, rgba);
        }
        AdaptWhiteBlackToBackground(&rgba[0], &rgba[1], &rgba[2], true);  // REQ-048: legible on the white page
        return (static_cast<uint32_t>(rgba[0] * 255.f) << 16) | (static_cast<uint32_t>(rgba[1] * 255.f) << 8) |
               static_cast<uint32_t>(rgba[2] * 255.f);
      };
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

      // Lines (REQ-028: also skip layers frozen in THIS viewport, matching the on-screen viewport render).
      for (size_t i = 0; i + 5 < st.userLinesFlat.size(); i += 6) {
        const size_t idx = i / 6;
        if (idx < st.userLineAttrs.size() &&
            (!plottable(st.userLineAttrs[idx].layer) || IsLayerFrozenInViewport(vp, st.userLineAttrs[idx].layer)))
          continue;
        curColor = (idx < st.userLineAttrs.size())
                       ? resolveRgb(st.userLineAttrs[idx].layer, st.userLineAttrs[idx].color)
                       : 0x000000u;
        emitModelSeg(st.userLinesFlat[i] + oX, st.userLinesFlat[i + 1] + oY, st.userLinesFlat[i + 3] + oX,
                     st.userLinesFlat[i + 4] + oY);
      }
      // Polylines.
      for (size_t pi = 0; pi < st.userPolylineOffsets.size(); ++pi) {
        if (pi < st.userPolylineAttrs.size() &&
            (!plottable(st.userPolylineAttrs[pi].layer) || IsLayerFrozenInViewport(vp, st.userPolylineAttrs[pi].layer)))
          continue;
        curColor = (pi < st.userPolylineAttrs.size())
                       ? resolveRgb(st.userPolylineAttrs[pi].layer, st.userPolylineAttrs[pi].color)
                       : 0x000000u;
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
      for (size_t i = 0; i + 3 < st.userCirclesCxCyZR.size(); i += 4) {  // cx,cy,z,r
        const size_t idx = i / 4;
        if (idx < st.userCircleAttrs.size() &&
            (!plottable(st.userCircleAttrs[idx].layer) || IsLayerFrozenInViewport(vp, st.userCircleAttrs[idx].layer)))
          continue;
        curColor = (idx < st.userCircleAttrs.size())
                       ? resolveRgb(st.userCircleAttrs[idx].layer, st.userCircleAttrs[idx].color)
                       : 0x000000u;
        const double cx = st.userCirclesCxCyZR[i] + oX, cy = st.userCirclesCxCyZR[i + 1] + oY;
        const double r = st.userCirclesCxCyZR[i + 3];  // [i+2] is Z — the plot is a flat projection
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
      // Arcs (sampled). REQ-028: skip layers frozen in this viewport (matches the on-screen viewport render).
      for (size_t ai = 0; ai < st.userArcs.size(); ++ai) {
        if (ai < st.userArcAttrs.size() && IsLayerFrozenInViewport(vp, st.userArcAttrs[ai].layer))
          continue;
        curColor = (ai < st.userArcAttrs.size())
                       ? resolveRgb(st.userArcAttrs[ai].layer, st.userArcAttrs[ai].color)
                       : 0x000000u;
        const CadArc& a = st.userArcs[ai];
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
        if (!plottable(sp.layer) || IsLayerFrozenInViewport(vp, sp.layer))
          continue;
        curColor = resolveRgb(sp.layer, "ByLayer");
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
      // Surfaces (REQ-135): stroke the same display-geometry batches as the viewport.
      for (size_t si = 0; si < st.cadSurfaces.size(); ++si) {
        if (!SurfaceVisible(st, si) || si >= st.cadSurfaceAttrs.size())
          continue;
        const EntityAttributes& attr = st.cadSurfaceAttrs[si];
        if (!plottable(attr.layer) || IsLayerFrozenInViewport(vp, attr.layer))
          continue;
        const std::uint64_t id = attr.id;
        auto it = std::find_if(st.surfaceDisplayCache.begin(), st.surfaceDisplayCache.end(),
                               [&](const AppCommandState::SurfaceDisplayCacheEntry& e) { return e.surfaceId == id; });
        if (it == st.surfaceDisplayCache.end())
          continue;
        curColor = resolveRgb(attr.layer, attr.color);
        auto emitSurf = [&](const std::vector<float>& verts) {
          for (size_t i = 0; i + 5 < verts.size(); i += 6)
            emitModelSeg(static_cast<double>(verts[i]) + oX, static_cast<double>(verts[i + 1]) + oY,
                         static_cast<double>(verts[i + 3]) + oX, static_cast<double>(verts[i + 4]) + oY);
        };
        emitSurf(it->triangleEdges);
        emitSurf(it->minorContours);
        emitSurf(it->majorContours);
        emitSurf(it->borderEdges);
        for (const auto& ab : it->arrowLineBuffers)
          emitSurf(ab);
        auto wit = std::find_if(st.surfaceWatershedCache.begin(), st.surfaceWatershedCache.end(),
                                [&](const AppCommandState::SurfaceWatershedCacheEntry& e) {
                                  return e.surfaceId == id;
                                });
        if (wit != st.surfaceWatershedCache.end()) {
          emitSurf(wit->basinOutlines);
        }
      }
      {
        auto emitPreview = [&](const std::vector<float>& verts) {
          for (size_t i = 0; i + 5 < verts.size(); i += 6)
            emitModelSeg(static_cast<double>(verts[i]) + oX, static_cast<double>(verts[i + 1]) + oY,
                         static_cast<double>(verts[i + 3]) + oX, static_cast<double>(verts[i + 4]) + oY);
        };
        emitPreview(st.catchmentPreviewLines);
        emitPreview(st.waterDropPreviewLines);
      }
      CadDimStrokeParams dsp;
      dsp.modelUnitsPerPlottedInch = st.modelUnitsPerPlottedInch;
      dsp.arrowSizeInches = st.activeDimensionStyle.arrowSizeInches;
      dsp.arrowScale = 1.f;
      dsp.arrowType = st.activeDimensionStyle.arrowType;
      for (size_t ai = 0; ai < st.cadAnnotations.size(); ++ai) {
        const CadAnnotation& ann = st.cadAnnotations[ai];
        if (!CadAnnotationIsDimension(ann))
          continue;
        const EntityAttributes* aa =
            (ai < st.cadAnnotationAttrs.size()) ? &st.cadAnnotationAttrs[ai] : nullptr;
        const std::string layer = aa ? (aa->layer.empty() ? std::string("0") : aa->layer) : std::string("0");
        if (!plottable(layer) || IsLayerFrozenInViewport(vp, layer))
          continue;
        curColor = (aa) ? resolveRgb(layer, aa->color) : resolveRgb(layer, "ByLayer");
        CadDimWorldStrokes strokes;
        if (!CadDimBuildWorldStrokes(ann, dsp, &strokes))
          continue;
        for (const CadDimWorldSeg& seg : strokes.segs) {
          float p0x = 0.f, p0y = 0.f, p1x = 0.f, p1y = 0.f;
          m2p(static_cast<double>(seg.x0) + oX, static_cast<double>(seg.y0) + oY, &p0x, &p0y);
          m2p(static_cast<double>(seg.x1) + oX, static_cast<double>(seg.y1) + oY, &p1x, &p1y);
          if (ClipSeg(vx0, vy0, vx1, vy1, p0x, p0y, p1x, p1y))
            addSeg(p0x, p0y, p1x, p1y);
        }
        float lpx = 0.f, lpy = 0.f;
        m2p(static_cast<double>(strokes.labelX) + oX, static_cast<double>(strokes.labelY) + oY, &lpx, &lpy);
        if (lpx >= vx0 && lpx <= vx1 && lpy >= vy0 && lpy <= vy1 && !ann.text.empty()) {
          VpDimLabel lab;
          lab.px = lpx;
          lab.py = lpy;
          lab.rot = strokes.labelRotRad;
          lab.Hin = (std::max)(0.01f, AnnotationPlottedHeightThroughViewport(ann, vp, st.modelUnitsPerPlottedInch));
          lab.text = ann.text;
          lab.fontFamily = ann.fontFamily;
          lab.rgb = curColor;
          vpDimLabels.push_back(std::move(lab));
        }
      }
      for (size_t ai = 0; ai < st.cadAnnotations.size(); ++ai) {
        const CadAnnotation& ann = st.cadAnnotations[ai];
        if (CadAnnotationIsDimension(ann) ||
            (ann.kind != CadAnnotation::Kind::Text && ann.kind != CadAnnotation::Kind::Mtext &&
             ann.kind != CadAnnotation::Kind::Table) ||
            (ann.kind != CadAnnotation::Kind::Table && ann.text.empty()))
          continue;
        const EntityAttributes* aa =
            (ai < st.cadAnnotationAttrs.size()) ? &st.cadAnnotationAttrs[ai] : nullptr;
        const std::string layer = aa ? (aa->layer.empty() ? std::string("0") : aa->layer) : std::string("0");
        if (!plottable(layer) || IsLayerFrozenInViewport(vp, layer))
          continue;
        if (ann.kind == CadAnnotation::Kind::Table && ann.tableCols > 0) {
          std::vector<CadTableCellRect> cells;
          CadTableLayoutCells(ann.boxMinX, ann.boxMinY, ann.boxMaxX, ann.boxMaxY, ann.tableCols, ann.tableCells,
                              &cells);
          for (size_t ci = 0; ci < cells.size() && ci < ann.tableCells.size(); ++ci) {
            float cpx = 0.f, cpy = 0.f;
            m2p(static_cast<double>(cells[ci].x0) + oX, static_cast<double>(cells[ci].y0) + oY, &cpx, &cpy);
            if (cpx < vx0 || cpx > vx1 || cpy < vy0 || cpy > vy1)
              continue;
            VpDimLabel tlab;
            tlab.px = cpx;
            tlab.py = cpy;
            tlab.rot = 0.f;
            tlab.Hin =
                (std::max)(0.01f, AnnotationPlottedHeightThroughViewport(ann, vp, st.modelUnitsPerPlottedInch));
            tlab.text = ann.tableCells[ci];
            tlab.fontFamily = ann.fontFamily;
            tlab.rgb = (aa) ? resolveRgb(layer, aa->color) : resolveRgb(layer, "ByLayer");
            vpDimLabels.push_back(std::move(tlab));
          }
          continue;
        }
        float lpx = 0.f, lpy = 0.f;
        const float wx = (ann.kind == CadAnnotation::Kind::Mtext) ? 0.5f * (ann.boxMinX + ann.boxMaxX) : ann.insX;
        const float wy = (ann.kind == CadAnnotation::Kind::Mtext) ? 0.5f * (ann.boxMinY + ann.boxMaxY) : ann.insY;
        m2p(static_cast<double>(wx) + oX, static_cast<double>(wy) + oY, &lpx, &lpy);
        if (lpx < vx0 || lpx > vx1 || lpy < vy0 || lpy > vy1)
          continue;
        VpDimLabel lab;
        lab.px = lpx;
        lab.py = lpy;
        lab.rot = ann.rotationRad;
        lab.Hin = (std::max)(0.01f, AnnotationPlottedHeightThroughViewport(ann, vp, st.modelUnitsPerPlottedInch));
        lab.text = (ann.kind == CadAnnotation::Kind::Mtext) ? MtextRichFlattenToPlain(ann.text) : ann.text;
        lab.fontFamily = ann.fontFamily;
        lab.rgb = (aa) ? resolveRgb(layer, aa->color) : resolveRgb(layer, "ByLayer");
        vpDimLabels.push_back(std::move(lab));
      }
      for (size_t ti = 0; ti < st.cadTables.size(); ++ti) {
        const CadTable& tbl = st.cadTables[ti];
        const EntityAttributes* ta =
            (ti < st.cadTableAttrs.size()) ? &st.cadTableAttrs[ti] : nullptr;
        const std::string layer = ta ? (ta->layer.empty() ? std::string("0") : ta->layer) : std::string("0");
        if (!plottable(layer) || IsLayerFrozenInViewport(vp, layer))
          continue;
        std::vector<CadTableCellRect> cells;
        CadTableLayoutWorldCells(tbl, &cells);
        for (size_t ci = 0; ci < cells.size() && ci < tbl.cells.size(); ++ci) {
          float cpx = 0.f, cpy = 0.f;
          m2p(static_cast<double>(cells[ci].x0) + oX, static_cast<double>(cells[ci].y1) + oY, &cpx, &cpy);
          if (cpx < vx0 || cpx > vx1 || cpy < vy0 || cpy > vy1)
            continue;
          VpDimLabel tlab;
          tlab.px = cpx;
          tlab.py = cpy;
          tlab.rot = tbl.rotationRad;
          tlab.Hin = (std::max)(0.01f, tbl.plottedHeightInches);
          tlab.text = tbl.cells[ci];
          tlab.fontFamily = tbl.fontFamily;
          tlab.rgb = (ta) ? resolveRgb(layer, ta->color) : resolveRgb(layer, "ByLayer");
          vpDimLabels.push_back(std::move(tlab));
        }
      }
      // Viewport border — only if the viewport's layer is plottable. Always black.
      if (plottable(vp.layer)) {
        curColor = 0x000000u;
        addSeg(vx0, vy0, vx1, vy0);
        addSeg(vx1, vy0, vx1, vy1);
        addSeg(vx1, vy1, vx0, vy1);
        addSeg(vx0, vy1, vx0, vy0);
      }
    }

    // Native paper-space sheet geometry (REQ-049): lines/circles/arcs/ellipses/polylines drawn directly
    // on the sheet — already in paper inches, so emitted straight (no viewport transform). Colored per
    // REQ-048 (entity color, else layer color; per-viewport overrides do not apply to the sheet); layers
    // that are off/frozen/non-plottable are excluded. Native sheet TEXT is pending (see task log C2).
    {
      auto sheetRgb = [&](const std::string& layer, const std::string& entityColor) -> uint32_t {
        std::string col = entityColor;
        if (col.empty() || col == "ByLayer") {
          const CadLayerRow* lr = FindDrawingLayerRowCi(st, layer);
          col = (lr && !lr->color.empty() && lr->color != "ByLayer") ? lr->color : "White";
        }
        float rgba[4] = {0.f, 0.f, 0.f, 1.f};
        ResolveStoredColorForViewport(col, 0.f, 0.f, 0.f, 0.f, rgba);
        AdaptWhiteBlackToBackground(&rgba[0], &rgba[1], &rgba[2], true);  // REQ-048: legible on the white page
        return (static_cast<uint32_t>(rgba[0] * 255.f) << 16) | (static_cast<uint32_t>(rgba[1] * 255.f) << 8) |
               static_cast<uint32_t>(rgba[2] * 255.f);
      };
      auto sheetAttr = [&](const std::vector<EntityAttributes>& v, size_t i) -> const EntityAttributes* {
        return i < v.size() ? &v[i] : nullptr;
      };

      // Solid sheet fills (REQ-049 — closes the ADR-011 plot gap). Every native filled region plots as a
      // solid fill in its resolved entity/layer color, matching the on-screen paper overlay (which
      // scanline-fills every region regardless of pattern). One PDF path per region, all loops closed and
      // filled even-odd so concave outlines and island holes render correctly. Inserted first so fills sit
      // under the linework, as on screen. No REQ-048 white/black adaptation here — fills carry their true
      // color (the overlay does not adapt them either).
      auto fillRgb = [&](size_t fi) -> uint32_t {
        float rgba[4] = {0.85f, 0.85f, 0.85f, 1.f};  // on-screen default when a region has no attrs
        if (fi < L.paperFilledRegionAttrs.size()) {
          const EntityAttributes& fa = L.paperFilledRegionAttrs[fi];
          std::string col = fa.color;
          if (col.empty() || col == "ByLayer") {
            const CadLayerRow* lr = FindDrawingLayerRowCi(st, fa.layer.empty() ? std::string("0") : fa.layer);
            col = (lr && !lr->color.empty() && lr->color != "ByLayer") ? lr->color : "White";
          }
          ResolveStoredColorForViewport(col, 0.f, 0.f, 0.f, 0.f, rgba);
        }
        return (static_cast<uint32_t>(rgba[0] * 255.f) << 16) | (static_cast<uint32_t>(rgba[1] * 255.f) << 8) |
               static_cast<uint32_t>(rgba[2] * 255.f);
      };
      for (size_t fi = 0; fi < L.paperFilledRegions.size(); ++fi) {
        const CadFilledRegion& fr = L.paperFilledRegions[fi];
        if (fr.loopStart.empty() || fr.vertsXyz.size() < 9)  // < 3 vertices × 3 floats
          continue;
        if (fi < L.paperFilledRegionAttrs.size() && !plottable(L.paperFilledRegionAttrs[fi].layer))
          continue;
        FPDF_PAGEOBJECT fillObj = nullptr;
        for (size_t lp = 0; lp < fr.loopStart.size(); ++lp) {
          const int begin = fr.loopStart[lp];
          const int cnt = fr.loopCount(lp);
          if (cnt < 3)
            continue;
          for (int k = 0; k < cnt; ++k) {
            const size_t vi = static_cast<size_t>(begin + k);
            // Paper fills are 2D (ADR-025 (g)) — Z is ignored here, the sheet has no depth.
            const float vx = fr.vertsXyz[vi * 3] * kPtPerIn, vy = fr.vertsXyz[vi * 3 + 1] * kPtPerIn;
            if (!fillObj) {
              fillObj = FPDFPageObj_CreateNewPath(vx, vy);  // first subpath starts here (implicit MoveTo)
              if (!fillObj) break;
            } else if (k == 0) {
              FPDFPath_MoveTo(fillObj, vx, vy);             // start of a subsequent loop
            } else {
              FPDFPath_LineTo(fillObj, vx, vy);
            }
          }
          if (fillObj)
            FPDFPath_Close(fillObj);
        }
        if (!fillObj)
          continue;
        const uint32_t rgb = fillRgb(fi);
        FPDFPageObj_SetFillColor(fillObj, (rgb >> 16) & 0xffu, (rgb >> 8) & 0xffu, rgb & 0xffu, 255u);
        FPDFPath_SetDrawMode(fillObj, FPDF_FILLMODE_ALTERNATE, /*stroke=*/0);
        FPDFPage_InsertObject(page, fillObj);
      }

      // Lines.
      for (size_t i = 0; i + 5 < L.paperLines.size(); i += 6) {
        const EntityAttributes* a = sheetAttr(L.paperLineAttrs, i / 6);
        if (a && !plottable(a->layer)) continue;
        curColor = a ? sheetRgb(a->layer, a->color) : 0x000000u;
        addSeg(L.paperLines[i], L.paperLines[i + 1], L.paperLines[i + 3], L.paperLines[i + 4]);
      }
      // Circles (sampled to a polygon).
      for (size_t i = 0; i + 2 < L.paperCircles.size(); i += 3) {
        const EntityAttributes* a = sheetAttr(L.paperCircleAttrs, i / 3);
        if (a && !plottable(a->layer)) continue;
        curColor = a ? sheetRgb(a->layer, a->color) : 0x000000u;
        const float cx = L.paperCircles[i], cy = L.paperCircles[i + 1], r = L.paperCircles[i + 2];
        constexpr int kSeg = 72;
        for (int k = 0; k < kSeg; ++k) {
          const float t0 = 6.2831853f * static_cast<float>(k) / kSeg, t1 = 6.2831853f * static_cast<float>(k + 1) / kSeg;
          addSeg(cx + r * std::cos(t0), cy + r * std::sin(t0), cx + r * std::cos(t1), cy + r * std::sin(t1));
        }
      }
      // Arcs (sampled).
      for (size_t ai = 0; ai < L.paperArcs.size(); ++ai) {
        const EntityAttributes* a = sheetAttr(L.paperArcAttrs, ai);
        if (a && !plottable(a->layer)) continue;
        curColor = a ? sheetRgb(a->layer, a->color) : 0x000000u;
        const CadArc& arc = L.paperArcs[ai];
        const int kSeg = std::clamp(static_cast<int>(std::fabs(arc.sweepRad) / 0.1f) + 2, 2, 256);
        float pxp = 0, pyp = 0;
        for (int k = 0; k <= kSeg; ++k) {
          const float t = arc.startRad + arc.sweepRad * (static_cast<float>(k) / static_cast<float>(kSeg));
          const float wx = arc.cx + arc.r * std::cos(t), wy = arc.cy + arc.r * std::sin(t);
          if (k > 0) addSeg(pxp, pyp, wx, wy);
          pxp = wx; pyp = wy;
        }
      }
      // Ellipses (sampled).
      for (size_t ei = 0; ei < L.paperEllipses.size(); ++ei) {
        const EntityAttributes* a = sheetAttr(L.paperEllAttrs, ei);
        if (a && !plottable(a->layer)) continue;
        curColor = a ? sheetRgb(a->layer, a->color) : 0x000000u;
        const CadEllipse& e = L.paperEllipses[ei];
        const float mnx = -e.majVy * e.ratio, mny = e.majVx * e.ratio;  // minor axis = perp(major) × ratio
        constexpr int kSeg = 64;
        float pxp = 0, pyp = 0;
        for (int k = 0; k <= kSeg; ++k) {
          const float t = 6.2831853f * (static_cast<float>(k) / static_cast<float>(kSeg));
          const float ct = std::cos(t), stt = std::sin(t);
          const float wx = e.cx + e.majVx * ct + mnx * stt, wy = e.cy + e.majVy * ct + mny * stt;
          if (k > 0) addSeg(pxp, pyp, wx, wy);
          pxp = wx; pyp = wy;
        }
      }
      // Polylines.
      const int nPoly = static_cast<int>(L.paperPolyOffsets.size()) - 1;
      for (int pi = 0; pi < nPoly; ++pi) {
        const EntityAttributes* a = sheetAttr(L.paperPolyAttrs, static_cast<size_t>(pi));
        if (a && !plottable(a->layer)) continue;
        curColor = a ? sheetRgb(a->layer, a->color) : 0x000000u;
        const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
        const int v1 = L.paperPolyOffsets[static_cast<size_t>(pi + 1)];
        for (int vi = v0; vi + 1 < v1; ++vi)
          addSeg(L.paperPolyVerts[static_cast<size_t>(vi * 3)], L.paperPolyVerts[static_cast<size_t>(vi * 3 + 1)],
                 L.paperPolyVerts[static_cast<size_t>((vi + 1) * 3)],
                 L.paperPolyVerts[static_cast<size_t>((vi + 1) * 3 + 1)]);
        if (static_cast<size_t>(pi) < L.paperPolyClosed.size() && L.paperPolyClosed[static_cast<size_t>(pi)] &&
            v1 - v0 >= 2)
          addSeg(L.paperPolyVerts[static_cast<size_t>((v1 - 1) * 3)], L.paperPolyVerts[static_cast<size_t>((v1 - 1) * 3 + 1)],
                 L.paperPolyVerts[static_cast<size_t>(v0 * 3)], L.paperPolyVerts[static_cast<size_t>(v0 * 3 + 1)]);
      }

      // Native sheet TEXT (REQ-049): SHX (stroke) fonts plot faithfully as their strokes via the shared
      // font module (ADR-022); TrueType families embed as real PDF text objects (or a logged base-14
      // substitute). Paper inches, +y up; single-line insertion = top-left (baseline one cap-height below);
      // MTEXT honors its attachment point (see below).
      auto emitShxLine = [&](Shx::Font& font, const std::string& s, float baseX, float baseY, float H, float rot) {
        const float sc = H / font.capHeight();
        const float cr = std::cos(rot), sr = std::sin(rot);
        float penX = 0.f;
        for (unsigned char ch : s) {
          if (ch == '\n') continue;
          const Shx::Glyph* g = font.glyph(ch);
          if (!g) continue;
          for (const auto& stroke : g->strokes) {
            for (size_t k = 0; k + 1 < stroke.size(); ++k) {
              auto mp = [&](const Shx::Vec2& p, float* ox, float* oy) {
                const float gx = (penX + p.x) * sc, gy = p.y * sc;
                *ox = baseX + gx * cr - gy * sr;
                *oy = baseY + gx * sr + gy * cr;
              };
              float x0, y0, x1, y1;
              mp(stroke[k], &x0, &y0);
              mp(stroke[k + 1], &x1, &y1);
              addSeg(x0, y0, x1, y1);
            }
          }
          penX += g->advance;
        }
      };
      // TrueType text (REQ-049): each line becomes a real PDF text object in the embedded font (or a base-14
      // substitute), sized so the cap height ≈ the plotted text height, positioned/rotated by matrix and
      // colored per REQ-048. Baseline origin matches the SHX path (one cap-height below the top-left).
      constexpr float kCapEmRatio = 0.7f;  // nominal cap-height / em — see decision log (2026-07-15)
      auto emitTtfLine = [&](FPDF_FONT font, const std::string& s, float baseXin, float baseYin, float Hin,
                             float rot, uint32_t rgb) {
        if (!font || s.empty())
          return;
        FPDF_PAGEOBJECT to = FPDFPageObj_CreateTextObj(doc, font, Hin * kPtPerIn / kCapEmRatio);
        if (!to)
          return;
        std::vector<unsigned short> u16 = plotfont::Utf8ToUtf16(s);
        FPDFText_SetText(to, u16.data());
        FPDFPageObj_SetFillColor(to, (rgb >> 16) & 0xffu, (rgb >> 8) & 0xffu, rgb & 0xffu, 255u);
        const float cr = std::cos(rot), sr = std::sin(rot);
        FS_MATRIX m{cr, sr, -sr, cr, baseXin * kPtPerIn, baseYin * kPtPerIn};
        FPDFPageObj_SetMatrix(to, &m);
        FPDFPage_InsertObject(page, to);
      };
      // Width (paper inches) of a single TTF line — needed for MTEXT center/right attachment. Builds a
      // throwaway text object, reads its ink bounds, and discards it (side-bearing vs advance is negligible
      // for alignment). Matches the SHX width path (Shx::MeasureWidthPx) so mixed sheets align the same way.
      auto measureTtfWidthIn = [&](FPDF_FONT font, const std::string& s, float Hin) -> float {
        if (!font || s.empty())
          return 0.f;
        FPDF_PAGEOBJECT to = FPDFPageObj_CreateTextObj(doc, font, Hin * kPtPerIn / kCapEmRatio);
        if (!to)
          return 0.f;
        std::vector<unsigned short> u16 = plotfont::Utf8ToUtf16(s);
        FPDFText_SetText(to, u16.data());
        float l = 0.f, b = 0.f, r = 0.f, t = 0.f, w = 0.f;
        if (FPDFPageObj_GetBounds(to, &l, &b, &r, &t))
          w = (r - l) / kPtPerIn;
        FPDFPageObj_Destroy(to);
        return w;
      };
      for (size_t ti = 0; ti < L.paperTexts.size(); ++ti) {
        const CadAnnotation& a = L.paperTexts[ti];
        if (a.text.empty())
          continue;
        const EntityAttributes* at = sheetAttr(L.paperTextAttrs, ti);
        if (at && !plottable(at->layer))
          continue;
        const uint32_t rgb = at ? sheetRgb(at->layer, at->color) : 0x000000u;
        const float H = a.plottedHeightInches < 0.01f ? 0.01f : a.plottedHeightInches;  // avoid win max macro
        const bool wantShx = cadfont::PreferShxStrokes(a.fontFamily, a.text);
        Shx::Font* sfont = wantShx ? Shx::Resolve(a.fontFamily) : nullptr;
        const bool shx = wantShx && sfont && sfont->valid();
        FPDF_FONT tfont = shx ? nullptr : plotFont(a.fontFamily);  // embedded TTF (or base-14 substitute)
        if (!shx && !tfont)
          continue;
        curColor = rgb;  // stroke color for the SHX branch
        auto emitLine = [&](const std::string& s, float bx, float by, float rot) {
          if (shx)
            emitShxLine(*sfont, s, bx, by, H, rot);
          else
            emitTtfLine(tfont, s, bx, by, H, rot, rgb);
        };
        if (a.kind == CadAnnotation::Kind::Mtext) {
          const std::string plain = MtextRichFlattenToPlain(a.text);
          const float lineH = H * 1.4f;
          std::vector<std::string> lines;
          {
            std::string ln;
            for (char ch : plain) {
              if (ch == '\n') { lines.push_back(ln); ln.clear(); }
              else ln += ch;
            }
            lines.push_back(ln);
          }
          // Honor MTEXT attachment (group 71) exactly like the on-screen paper overlay: col 0/1/2 =
          // left/center/right, row 0/1/2 = top/middle/bottom. Without this, center/middle title-block values
          // (e.g. "DRY COOLER") plot at the box's top-left and collide with their labels.
          const int acol = (a.mtextAttach - 1) % 3;
          const int arow = (a.mtextAttach - 1) / 3;
          const float boxW = a.boxMaxX - a.boxMinX;
          const float boxH = a.boxMaxY - a.boxMinY;
          const float blockH = static_cast<float>(lines.size()) * lineH;
          float blockTopY = a.boxMaxY;                                     // top (arow 0)
          if (arow == 1)      blockTopY = a.boxMinY + 0.5f * (boxH + blockH);  // middle
          else if (arow == 2) blockTopY = a.boxMinY + blockH;                  // bottom
          auto lineWidthIn = [&](const std::string& s) -> float {
            if (s.empty()) return 0.f;
            return shx ? Shx::MeasureWidthPx(*sfont, s, H) : measureTtfWidthIn(tfont, s, H);
          };
          float baseY = blockTopY - H;  // baseline of the top line (one cap-height below its top)
          for (const std::string& ln : lines) {
            if (!ln.empty()) {
              float x = a.boxMinX;
              if (acol == 1)      x = a.boxMinX + 0.5f * (boxW - lineWidthIn(ln));
              else if (acol == 2) x = a.boxMaxX - lineWidthIn(ln);
              emitLine(ln, x, baseY, 0.f);
            }
            baseY -= lineH;
          }
        } else {
          emitLine(a.text, a.insX, a.insY - H, a.rotationRad);
        }
      }
      for (const VpDimLabel& lab : vpDimLabels) {
        if (lab.text.empty())
          continue;
        curColor = lab.rgb;
        const float H = lab.Hin < 0.01f ? 0.01f : lab.Hin;
        const bool wantShx = cadfont::PreferShxStrokes(lab.fontFamily, lab.text);
        Shx::Font* sfont = wantShx ? Shx::Resolve(lab.fontFamily) : nullptr;
        const bool shx = wantShx && sfont && sfont->valid();
        FPDF_FONT tfont = shx ? nullptr : plotFont(lab.fontFamily);
        if (!shx && !tfont)
          continue;
        if (shx)
          emitShxLine(*sfont, lab.text, lab.px, lab.py - H, H, lab.rot);
        else
          emitTtfLine(tfont, lab.text, lab.px, lab.py - H, H, lab.rot, lab.rgb);
      }
    }

    // One stroked path object per color group (REQ-046: overridden layers print in their color).
    for (const auto& [color, segs] : segsByColor) {
      if (segs.empty())
        continue;
      FPDF_PAGEOBJECT path = FPDFPageObj_CreateNewPath(segs[0][0] * kPtPerIn, segs[0][1] * kPtPerIn);
      FPDFPath_LineTo(path, segs[0][2] * kPtPerIn, segs[0][3] * kPtPerIn);
      for (size_t i = 1; i < segs.size(); ++i) {
        FPDFPath_MoveTo(path, segs[i][0] * kPtPerIn, segs[i][1] * kPtPerIn);
        FPDFPath_LineTo(path, segs[i][2] * kPtPerIn, segs[i][3] * kPtPerIn);
      }
      FPDFPageObj_SetStrokeColor(path, static_cast<int>((color >> 16) & 0xff), static_cast<int>((color >> 8) & 0xff),
                                 static_cast<int>(color & 0xff), 255);
      FPDFPageObj_SetStrokeWidth(path, 0.5f);
      FPDFPath_SetDrawMode(path, 0 /*no fill*/, 1 /*stroke*/);
      FPDFPage_InsertObject(page, path);
    }
    FPDFPage_GenerateContent(page);
    FPDF_ClosePage(page);
    ++pages;
  }

  if (pages == 0) {
    closeFonts();
    FPDF_CloseDocument(doc);
    log.push_back("PLOT — no plottable layouts.");
    return false;
  }

  std::ofstream os(fs::u8path(std::string(pathUtf8)), std::ios::binary);
  if (!os) {
    closeFonts();
    FPDF_CloseDocument(doc);
    log.push_back(std::string("PLOT — could not open output file: ") + pathUtf8);
    return false;
  }
  PdfWriter writer;
  writer.fw.version = 1;
  writer.fw.WriteBlock = &WriteBlockCb;
  writer.os = &os;
  const FPDF_BOOL ok = FPDF_SaveAsCopy(doc, &writer.fw, 0);
  closeFonts();  // font handles were needed through the save; release before closing the document
  FPDF_CloseDocument(doc);
  os.close();
  if (!ok) {
    log.push_back("PLOT — failed to write the PDF.");
    return false;
  }
  log.push_back("PLOT — wrote " + std::to_string(pages) + " page(s) to " + pathUtf8);
  return true;
}
