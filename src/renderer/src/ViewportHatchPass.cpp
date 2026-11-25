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

void ViewportRenderer::DrawSolidFilledRegions(const SolidFillPassInput& in) {
  const CadExtendedGeometryInput* extended = in.extended;
  const std::vector<CadFilledRegion>* filledRegions = in.filledRegions;
  const std::vector<EntityAttributes>* filledRegionAttrs = in.filledRegionAttrs;
  const std::vector<CadLayerRow>* drawingLayers = in.drawingLayers;
  if (!filledRegions || filledRegions->empty())
    return;

  glUseProgram(lineProgram_);
  glUniformMatrix4fv(in.locMvp, 1, GL_FALSE, in.mvp);
  glBindBuffer(GL_ARRAY_BUFFER, vboLines_);
  glBindVertexArray(vaoLines_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);
  glDisable(GL_BLEND);
  glEnable(GL_STENCIL_TEST);
  std::vector<float> fan;
  // Draw order: every opaque fill first, then translucent fills back to front. A blend only mixes with
  // what is already in the frame, so a translucent fill drawn before an opaque one behind it would be
  // painted over; sorting makes the result independent of entity order.
  struct FillOrder { size_t index; bool translucent; double depth; };
  std::vector<FillOrder> fillOrder;
  fillOrder.reserve(filledRegions->size());
  {
    for (size_t fi = 0; fi < filledRegions->size(); ++fi) {
      const CadFilledRegion& fr = (*filledRegions)[fi];
      float alphaRgba[4] = {0.85f, 0.85f, 0.85f, 1.f};
      if (filledRegionAttrs && fi < filledRegionAttrs->size()) {
        const EntityAttributes& attr = (*filledRegionAttrs)[fi];
        const CadLayerRow* lr = LookupLayerRowCi(drawingLayers, attr.layer.empty() ? std::string("0") : attr.layer);
        ResolveEntityRgbaForViewport(attr, lr, 0.85f, 0.85f, 0.85f, alphaRgba);
      }
      double cx = 0., cy = 0., cz = 0.;
      const size_t vertexCount = fr.vertsXyz.size() / 3;
      for (size_t v = 0; v < vertexCount; ++v) {
        cx += fr.vertsXyz[v * 3 + 0];
        cy += fr.vertsXyz[v * 3 + 1];
        cz += fr.vertsXyz[v * 3 + 2];
      }
      if (vertexCount > 0) {
        cx /= static_cast<double>(vertexCount);
        cy /= static_cast<double>(vertexCount);
        cz /= static_cast<double>(vertexCount);
      }
      // Distance along the view direction; larger is farther from the eye.
      const double depth =
          (cx - in.viewAnchorX) * in.camFwdX + (cy - in.viewAnchorY) * in.camFwdY + cz * in.camFwdZ;
      fillOrder.push_back({fi, alphaRgba[3] < 0.999f, depth});
    }
    std::stable_sort(fillOrder.begin(), fillOrder.end(), [](const FillOrder& a, const FillOrder& b) {
      if (a.translucent != b.translucent) return !a.translucent;
      return a.translucent && a.depth > b.depth;
    });
  }
  for (const FillOrder& order : fillOrder) {
    const size_t fi = order.index;
    const CadFilledRegion& fr = (*filledRegions)[fi];
    if (fr.loopStart.empty() || fr.vertsXyz.size() < 9)  // < 3 vertices × 3 floats
      continue;
    if (!fr.isSolid())
      continue;  // line-pattern hatches are drawn as clipped lines in the viewport overlay (REQ-043)
    // REQ-084 (d): a fill isolated out is not drawn.
    if (filledRegionAttrs && fi < filledRegionAttrs->size() &&
        CadEntityIdHidden(extended ? extended->hiddenEntityIds : nullptr, (*filledRegionAttrs)[fi].id))
      continue;
    if (!CadAnnotativeVisibleAtActiveScale(
            fr.annotative, fr.annotativeVisibleScaleNames,
            extended ? extended->annotationScales : nullptr,
            extended ? extended->currentAnnotationScaleIndex : -1,
            extended ? extended->annotativeViewport : nullptr,
            extended ? extended->drawingModelUnitsPerPlottedInch : 0.f))
      continue;
    fan.clear();
    double mnx = 1e300, mxx = -1e300, mny = 1e300, mxy = -1e300;
    // The stencil cover quad spans the region's XY bounds and so needs one elevation; a filled
    // region is planar in practice, so the mean vertex Z is that plane and is exact whenever the
    // loops share an elevation (REQ-058).
    double zSum = 0.;
    int zCount = 0;
    auto vx = [&](int p) { return fr.vertsXyz[static_cast<size_t>(p) * 3 + 0]; };
    auto vy = [&](int p) { return fr.vertsXyz[static_cast<size_t>(p) * 3 + 1]; };
    auto vz = [&](int p) { return fr.vertsXyz[static_cast<size_t>(p) * 3 + 2]; };
    for (size_t li = 0; li < fr.loopStart.size(); ++li) {
      const int begin = fr.loopStart[li];
      const int cnt = fr.loopCount(li);
      if (cnt < 3)
        continue;
      for (int v = 1; v + 1 < cnt; ++v) {
        fan.push_back(vx(begin) - static_cast<float>(in.viewAnchorX));
        fan.push_back(vy(begin) - static_cast<float>(in.viewAnchorY));
        fan.push_back(vz(begin));
        fan.push_back(vx(begin + v) - static_cast<float>(in.viewAnchorX));
        fan.push_back(vy(begin + v) - static_cast<float>(in.viewAnchorY));
        fan.push_back(vz(begin + v));
        fan.push_back(vx(begin + v + 1) - static_cast<float>(in.viewAnchorX));
        fan.push_back(vy(begin + v + 1) - static_cast<float>(in.viewAnchorY));
        fan.push_back(vz(begin + v + 1));
      }
      for (int v = 0; v < cnt; ++v) {
        mnx = std::min(mnx, static_cast<double>(vx(begin + v)));
        mxx = std::max(mxx, static_cast<double>(vx(begin + v)));
        mny = std::min(mny, static_cast<double>(vy(begin + v)));
        mxy = std::max(mxy, static_cast<double>(vy(begin + v)));
        zSum += static_cast<double>(vz(begin + v));
        ++zCount;
      }
    }
    if (fan.empty())
      continue;
    // Pass 1: parity into stencil (no color write).
    //
    // Depth WRITES are off for this pass even when depth testing is on (REQ-064). The fan covers
    // the whole loop including area that even-odd parity will subtract — the holes in a hatch with
    // islands — so letting it write depth would occlude geometry seen through those holes. Only
    // pass 2, which the stencil confines to the region's true area, writes depth.
    const GLboolean wantDepthWrite = in.depthOn ? GL_TRUE : GL_FALSE;
    glDepthMask(GL_FALSE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(fan.size() * sizeof(float)), fan.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(fan.size() / 3));
    glDepthMask(wantDepthWrite);
    // Pass 2: fill the odd-parity region, resetting stencil to 0 as the covering quad draws.
    float rgba[4] = {0.85f, 0.85f, 0.85f, 1.f};
    if (filledRegionAttrs && fi < filledRegionAttrs->size()) {
      const EntityAttributes& attr = (*filledRegionAttrs)[fi];
      const CadLayerRow* lr = LookupLayerRowCi(drawingLayers, attr.layer.empty() ? std::string("0") : attr.layer);
      ResolveEntityRgbaForViewport(attr, lr, 0.85f, 0.85f, 0.85f, rgba);
    }
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
    // Entity / layer transparency: the stencil confines the cover quad to each pixel once, so a plain
    // alpha blend is exact. A see-through fill must not write depth, or it hides what lies behind it.
    const bool translucent = rgba[3] < 0.999f;
    if (translucent) {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glDepthMask(GL_FALSE);
    }
    const float qx0 = static_cast<float>(mnx - in.viewAnchorX), qy0 = static_cast<float>(mny - in.viewAnchorY);
    const float qx1 = static_cast<float>(mxx - in.viewAnchorX), qy1 = static_cast<float>(mxy - in.viewAnchorY);
    const float qz = (zCount > 0) ? static_cast<float>(zSum / static_cast<double>(zCount)) : 0.f;

    if (in.shadeSurfaces) {
      // Shaded: the same stencil-confined quad, through the lit program. A filled region is
      // planar and parallel to XY (ADR-025), so its normal is +Z — which is why this dims as the
      // view orbits toward edge-on, and is the visible proof that lighting is live before REQ-063
      // brings surfaces with varied normals.
      const float sq[36] = {qx0, qy0, qz, 0.f, 0.f, 1.f, qx1, qy0, qz, 0.f, 0.f, 1.f,
                            qx1, qy1, qz, 0.f, 0.f, 1.f, qx0, qy0, qz, 0.f, 0.f, 1.f,
                            qx1, qy1, qz, 0.f, 0.f, 1.f, qx0, qy1, qz, 0.f, 0.f, 1.f};
      glUseProgram(shadedProgram_);
      glUniformMatrix4fv(glGetUniformLocation(shadedProgram_, "uMVP"), 1, GL_FALSE, in.mvp);
      glUniform4f(glGetUniformLocation(shadedProgram_, "uColor"), rgba[0], rgba[1], rgba[2], rgba[3]);
      glUniform3f(glGetUniformLocation(shadedProgram_, "uViewDir"), static_cast<float>(in.camFwdX),
                  static_cast<float>(in.camFwdY), static_cast<float>(in.camFwdZ));
      glUniform1f(glGetUniformLocation(shadedProgram_, "uAmbient"), in.shadedAmbient);
      glBindVertexArray(vaoShaded_);
      glBindBuffer(GL_ARRAY_BUFFER, vboShaded_);
      glBufferData(GL_ARRAY_BUFFER, sizeof(sq), sq, GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES, 0, 6);
      // Back to the flat program and its buffer for the next region's parity pass.
      glUseProgram(lineProgram_);
      glUniformMatrix4fv(in.locMvp, 1, GL_FALSE, in.mvp);
      glBindBuffer(GL_ARRAY_BUFFER, vboLines_);
      glBindVertexArray(vaoLines_);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);
    } else {
      glUniform4f(in.locCol, rgba[0], rgba[1], rgba[2], rgba[3]);
      const float quad[18] = {qx0, qy0, qz, qx1, qy0, qz, qx1, qy1, qz,
                              qx0, qy0, qz, qx1, qy1, qz, qx0, qy1, qz};
      glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    if (translucent) {
      glDisable(GL_BLEND);
      glDepthMask(wantDepthWrite);
    }
  }
  glDisable(GL_STENCIL_TEST);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glBindVertexArray(0);
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
