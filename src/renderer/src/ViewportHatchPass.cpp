// Patterned hatch linework (REQ-043), split out of ViewportRenderer.cpp to keep that file inside its
// size budget. The pattern's clipped linework is depth-tested like any other geometry at the hatch's
// elevation. The segments come pre-clipped from the hatch display cache; this pass only colours,
// filters and uploads them, and only when something it reads has changed.
#include <GoSurvey/Renderer/ViewportRenderer.hpp>
#include "ViewportRendererInternal.hpp"
#include <GL/glew.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

void MulMat4(const float* a, const float* b, float* out) {
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      out[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                       a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
    }
  }
}

void TranslateMat(float tx, float ty, float tz, float* m) {
  std::memset(m, 0, sizeof(float) * 16);
  m[0] = m[5] = m[10] = m[15] = 1.f;
  m[12] = tx;
  m[13] = ty;
  m[14] = tz;
}

const CadLayerRow* LookupLayerRowCi(const std::vector<CadLayerRow>* layers, const std::string& layerName) {
  if (!layers)
    return nullptr;
  for (const auto& r : *layers) {
    if (r.name.size() != layerName.size())
      continue;
    bool eq = true;
    for (size_t i = 0; i < r.name.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(r.name[i])) !=
          std::tolower(static_cast<unsigned char>(layerName[i]))) {
        eq = false;
        break;
      }
    }
    if (eq)
      return &r;
  }
  return nullptr;
}

void ViewportRenderer::ReleaseHatchGpu() {
  if (hatchVbo_) glDeleteBuffers(1, &hatchVbo_);
  if (hatchVao_) glDeleteVertexArrays(1, &hatchVao_);
  hatchVbo_ = hatchVao_ = 0;
  hatchVertCount_ = 0;
  hatchSig_ = 0;
  hatchUpload_.clear();
  hatchUpload_.shrink_to_fit();
}

void ViewportRenderer::DrawHatchPatterns(const HatchPassInput& in) {
  const CadExtendedGeometryInput* extended = in.extended;
  const std::vector<CadFilledRegion>* filledRegions = in.filledRegions;
  const std::vector<EntityAttributes>* filledRegionAttrs = in.filledRegionAttrs;
  const double panX = in.panX, panY = in.panY;
  const CadHatchDisplayGeometry* hatchDisplay = extended ? extended->hatchDisplay : nullptr;
  if (hatchDisplay && filledRegions && hatchDisplay->segments.size() == filledRegions->size()) {
    // What the buffer depends on beyond the geometry revision: which hatches show, and in what colour.
    struct HatchDraw { std::size_t index; float rgba[4]; };
    std::vector<HatchDraw> draws;
    std::uint64_t sig = 1469598103934665603ull ^ hatchDisplay->revision;
    const auto mix = [&sig](const void* data, std::size_t size) {
      const auto* bytes = static_cast<const unsigned char*>(data);
      for (std::size_t b = 0; b < size; ++b) { sig ^= bytes[b]; sig *= 1099511628211ull; }
    };
    for (std::size_t fi = 0; fi < filledRegions->size(); ++fi) {
      if (hatchDisplay->segments[fi].size() < 4)
        continue;
      const CadFilledRegion& fr = (*filledRegions)[fi];
      EntityAttributes attr{};
      if (filledRegionAttrs && fi < filledRegionAttrs->size())
        attr = (*filledRegionAttrs)[fi];
      if (CadEntityIdHidden(extended->hiddenEntityIds, attr.id))
        continue;
      const CadLayerRow* lr = LookupLayerRowCi(in.drawingLayers, attr.layer.empty() ? std::string("0") : attr.layer);
      if (lr && (!lr->on || lr->frozen))
        continue;
      if (extended->viewportFrozenLayers != nullptr &&
          std::find(extended->viewportFrozenLayers->begin(), extended->viewportFrozenLayers->end(), attr.layer) !=
              extended->viewportFrozenLayers->end())
        continue;
      if (!CadAnnotativeVisibleAtActiveScale(fr.annotative, fr.annotativeVisibleScaleNames, extended->annotationScales,
                                             extended->currentAnnotationScaleIndex, extended->annotativeViewport,
                                             extended->drawingModelUnitsPerPlottedInch))
        continue;
      HatchDraw draw{fi, {0.85f, 0.85f, 0.85f, 1.f}};
      ResolveEntityRgbaForViewport(attr, lr, 0.85f, 0.85f, 0.85f, draw.rgba);
      mix(&fi, sizeof(fi));
      mix(draw.rgba, sizeof(draw.rgba));
      draws.push_back(draw);
    }
    const double hatchDriftBudget = std::max(in.halfHd * 0.5, 1.e-12);
    const bool anchorDrifted = std::fabs(panX - hatchAnchorX_) > hatchDriftBudget ||
                               std::fabs(panY - hatchAnchorY_) > hatchDriftBudget;
    if (sig != hatchSig_ || anchorDrifted || !hatchVbo_) {
      if (!hatchVao_) {
        glGenVertexArrays(1, &hatchVao_);
        glGenBuffers(1, &hatchVbo_);
        glBindVertexArray(hatchVao_);
        glBindBuffer(GL_ARRAY_BUFFER, hatchVbo_);
        const GLsizei stride = static_cast<GLsizei>(7 * sizeof(float));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(sizeof(float) * 3));
      }
      hatchUpload_.clear();
      for (const HatchDraw& draw : draws) {
        const CadFilledRegion& fr = (*filledRegions)[draw.index];
        const float z = static_cast<float>(fr.vertsXyz[2]);  // a hatch is planar at its boundary's elevation
        const std::vector<double>& segs = hatchDisplay->segments[draw.index];
        // x0,y0,x1,y1 per segment: every coordinate pair is one GL_LINES vertex.
        for (std::size_t s = 0; s + 1 < segs.size(); s += 2) {
          float rx = 0.f, ry = 0.f;
          WorldToViewRelativeFloat(segs[s], segs[s + 1], panX, panY, &rx, &ry);
          hatchUpload_.insert(hatchUpload_.end(), {rx, ry, z, draw.rgba[0], draw.rgba[1], draw.rgba[2], draw.rgba[3]});
        }
      }
      hatchVertCount_ = static_cast<int>(hatchUpload_.size() / 7);
      glBindVertexArray(hatchVao_);
      glBindBuffer(GL_ARRAY_BUFFER, hatchVbo_);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(hatchUpload_.size() * sizeof(float)),
                   hatchUpload_.empty() ? nullptr : hatchUpload_.data(), GL_STATIC_DRAW);
      hatchSig_ = sig;
      hatchAnchorX_ = panX;
      hatchAnchorY_ = panY;
    }
    if (hatchVertCount_ > 0) {
      in.depthForGeometry();
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glUseProgram(vcLineProgram_);
      float hatchModel[16];
      TranslateMat(static_cast<float>(hatchAnchorX_ - panX), static_cast<float>(hatchAnchorY_ - panY), -in.panZf, hatchModel);
      float hatchMvp[16];
      MulMat4(in.projRot, hatchModel, hatchMvp);
      glUniformMatrix4fv(glGetUniformLocation(vcLineProgram_, "uMVP"), 1, GL_FALSE, hatchMvp);
      in.setVcLineClipAnchor(hatchAnchorX_, hatchAnchorY_);
      glLineWidth(in.lineWidth);
      // Pattern lines lie exactly in their hatch's plane, coplanar with the solid fill, the grid and any
      // linework drawn on it; under a depth style that tie z-fights into flickering dashes. A hair of depth
      // range pulls them toward the eye (lines take no polygon offset), still hidden by anything truly in front.
      if (in.depthOn) glDepthRange(0.0, 0.99995);
      glBindVertexArray(hatchVao_);
      glDrawArrays(GL_LINES, 0, hatchVertCount_);
      glBindVertexArray(0);
      if (in.depthOn) glDepthRange(0.0, 1.0);
      in.setVcLineClipAnchor(in.viewAnchorX, in.viewAnchorY);
    }
  } else if (hatchVbo_) {
    ReleaseHatchGpu();
  }
}
