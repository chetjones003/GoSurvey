#pragma once
// Helpers shared by ViewportRenderer.cpp and the passes split out of it (ViewportHatchPass.cpp).
#include <string>
#include <vector>

struct CadLayerRow;

/// Column-major 4x4 product: out = a * b.
void MulMat4(const float* a, const float* b, float* out);
/// Column-major 4x4 translation by (tx, ty, tz).
void TranslateMat(float tx, float ty, float tz, float* m);
/// The layer row named \p layerName, compared case-insensitively, or nullptr.
const CadLayerRow* LookupLayerRowCi(const std::vector<CadLayerRow>* layers, const std::string& layerName);
