#pragma once

#include <cstdint>
#include <string>
#include <string_view>

/// AutoCAD-style colour storage helpers. Canonical index colours are stored as
/// `"ACI:N"` (N = 1..255). True colour stays `"#RRGGBB"`. `"ByLayer"` /
/// `"ByBlock"` are unchanged. Legacy palette names (`Red`, `White`, …) still
/// parse for older drawings.

[[nodiscard]] bool CadColorIsByLayer(std::string_view storage);
[[nodiscard]] bool CadColorIsByBlock(std::string_view storage);

/// Writes the ACI index (1..255) when the storage maps to a palette colour
/// (including legacy names and `#RRGGBB` via nearest-index lookup).
[[nodiscard]] bool CadColorTryGetAci(const std::string& storage, int* outAci);

[[nodiscard]] std::string CadColorStorageFromAci(int aci);
[[nodiscard]] std::string CadColorStorageFromRgbPacked(uint32_t rgbPacked);

/// User-facing label: `"Color 7"`, `"By Layer"`, `"By Block"`.
[[nodiscard]] std::string CadColorDisplayLabel(const std::string& storage);

/// Resolve storage to linear RGB in 0..1 (ignores transparency).
void CadColorResolveRgb(const std::string& storage, float defaultR, float defaultG, float defaultB,
                        float* outRgb3);

/// Effective colour for an entity attribute, resolving ByLayer through \p layerColor.
[[nodiscard]] std::string CadColorEffectiveStorage(const std::string& entityColor,
                                                   const std::string& layerColor);

/// Quick Select / filter equality: same canonical storage or same ACI index.
[[nodiscard]] bool CadColorStorageMatches(const std::string& a, const std::string& b);
