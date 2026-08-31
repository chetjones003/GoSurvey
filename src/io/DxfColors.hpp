#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

/// AutoCAD 2020 model-space ACI palette (ezdxf DXF_DEFAULT_COLORS); index 1–255.
uint32_t DxfRgbPackedFromAci(int aci);

/// Nearest ACI index (1–255) for an RGB packed as 0xRRGGBB (matches palette Euclidean distance).
int DxfNearestAciFromRgbPacked(uint32_t rgbPacked);

/// Writes "#RRGGBB" (uppercase hex) into \p out[8] including terminator if cap>=8.
void DxfRgbPackedToHex(uint32_t rgbPacked, char* out, size_t cap);

/// Resolve a stored colour string ("#RRGGBB" or a palette name such as "Red"/"White") to an RGB
/// value packed as 0xRRGGBB. Returns false for empty, "ByLayer", "ByBlock", or an unknown name.
bool DxfColorStringToRgbPacked(const std::string& color, uint32_t* rgbPacked);
