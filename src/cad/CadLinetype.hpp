#pragma once

#include <string>
#include <vector>

/// Canonical AutoCAD linetype names used in DXF group 6 (mixed case is accepted by most readers).
bool CadLinetypeNameEqCi(const std::string& a, const std::string& b);

/// Normalize user input to a DXF table name (e.g. "dash" → "DASHED"). Unknown names pass through trimmed.
std::string CadCanonicalLinetypeNameForDxf(const std::string& raw);

/// True for Continuous / ByLayer / ByBlock (no dash tessellation in the viewport).
bool CadLinetypeIsSolidCi(const std::string& name);

/// Tessellate a straight segment into GL_LINES vertex format (7 floats per vertex: xyz + rgba) using a dash
/// pattern in **world units** (already scaled by LTSCALE and viewport heuristics).
void CadTessellateLinetypeSegmentVc(float x0, float y0, float z0, float x1, float y1, float z1,
                                    const std::string& linetypeName, float patternScaleWorld, const float rgba[4],
                                    std::vector<float>* outVerts7);

/// Walk a polyline of points (xy pairs, same z) and emit dashed segments between consecutive vertices.
void CadTessellateLinetypePolylineVc(const float* xy, int nPts, float z, const std::string& linetypeName,
                                    float patternScaleWorld, const float rgba[4], std::vector<float>* outVerts7);

/// Multi-segment path; when \p closed, includes edge from last point back to first (for circles).
void CadTessellateLinetypeChainVc(const float* xyPairs, int nPts, float z, bool closed, const std::string& linetypeName,
                                 float patternScaleWorld, const float rgba[4], std::vector<float>* outVerts7);
