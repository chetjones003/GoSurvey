#include "PdfPlot.hpp"

#include "CadCommands.hpp"
#include "PaperSpace.hpp"
#include "ShxFont.hpp"           // shared lower-layer SHX stroke geometry (ADR-022)
#include "MtextRichFormat.hpp"   // MtextRichFlattenToPlain (already used by DxfIo)

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

    // Accumulate stroked segments (paper inches) for this page, grouped by stroke color so a per-viewport
    // layer color override (REQ-046) prints in its color; everything else stays black. std::map keeps a
    // deterministic emit order for build reproducibility (REQ-200). Key = 0xRRGGBB, default 0x000000.
    std::map<uint32_t, std::vector<std::array<float, 4>>> segsByColor;
    uint32_t curColor = 0x000000u;  // set per entity before emitting
    auto addSeg = [&](float ax, float ay, float bx, float by) {
      segsByColor[curColor].push_back({ax, ay, bx, by});
    };

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
      for (size_t i = 0; i + 2 < st.userCirclesCxCyR.size(); i += 3) {
        const size_t idx = i / 3;
        if (idx < st.userCircleAttrs.size() &&
            (!plottable(st.userCircleAttrs[idx].layer) || IsLayerFrozenInViewport(vp, st.userCircleAttrs[idx].layer)))
          continue;
        curColor = (idx < st.userCircleAttrs.size())
                       ? resolveRgb(st.userCircleAttrs[idx].layer, st.userCircleAttrs[idx].color)
                       : 0x000000u;
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
        return (static_cast<uint32_t>(rgba[0] * 255.f) << 16) | (static_cast<uint32_t>(rgba[1] * 255.f) << 8) |
               static_cast<uint32_t>(rgba[2] * 255.f);
      };
      auto sheetAttr = [&](const std::vector<EntityAttributes>& v, size_t i) -> const EntityAttributes* {
        return i < v.size() ? &v[i] : nullptr;
      };
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
      // font module (ADR-022). TrueType text is not yet plotted — logged once as debt, never silently
      // dropped (REQ-201). Paper inches, +y up; insertion = top-left (baseline one cap-height below).
      auto isShxName = [](const std::string& f) {
        if (f.size() < 4) return false;
        std::string e = f.substr(f.size() - 4);
        for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return e == ".shx";
      };
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
      bool warnedTtfText = false;
      for (size_t ti = 0; ti < L.paperTexts.size(); ++ti) {
        const CadAnnotation& a = L.paperTexts[ti];
        if (a.text.empty())
          continue;
        const EntityAttributes* at = sheetAttr(L.paperTextAttrs, ti);
        if (at && !plottable(at->layer))
          continue;
        if (!isShxName(a.fontFamily)) {
          if (!warnedTtfText) {
            log.push_back("PLOT — note: TrueType sheet text is not yet plotted (SHX text is). See REQ-049 debt.");
            warnedTtfText = true;
          }
          continue;
        }
        Shx::Font* font = Shx::Resolve(a.fontFamily);
        if (!font || !font->valid())
          continue;
        curColor = at ? sheetRgb(at->layer, at->color) : 0x000000u;
        const float H = a.plottedHeightInches < 0.01f ? 0.01f : a.plottedHeightInches;  // avoid win max macro
        if (a.kind == CadAnnotation::Kind::Mtext) {
          const std::string plain = MtextRichFlattenToPlain(a.text);
          const float lineH = H * 1.4f;
          float baseY = a.boxMaxY - H;  // top line baseline (box top, one cap-height down)
          std::string ln;
          auto flushLine = [&]() {
            emitShxLine(*font, ln, a.boxMinX, baseY, H, 0.f);
            baseY -= lineH;
            ln.clear();
          };
          for (char ch : plain) {
            if (ch == '\n') flushLine();
            else ln += ch;
          }
          flushLine();
        } else {
          emitShxLine(*font, a.text, a.insX, a.insY - H, H, a.rotationRad);
        }
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
