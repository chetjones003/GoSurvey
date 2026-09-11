#include "CadColor.hpp"

#include "DxfColors.hpp"

#include <cctype>
#include <cstdio>
#include <string_view>

namespace {

bool StartsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

int NamedColorToAci(std::string_view name) {
  if (name == "Red")
    return 1;
  if (name == "Yellow")
    return 2;
  if (name == "Green")
    return 3;
  if (name == "Cyan")
    return 4;
  if (name == "Blue")
    return 5;
  if (name == "Magenta")
    return 6;
  if (name == "White")
    return 7;
  if (name == "Gray")
    return 8;
  if (name == "Black")
    return 250;
  if (name == "Orange")
    return 30;
  return -1;
}

bool ParseAciPrefix(const std::string& storage, int* outAci) {
  if (!StartsWith(storage, "ACI:"))
    return false;
  const char* p = storage.c_str() + 4;
  if (!std::isdigit(static_cast<unsigned char>(*p)))
    return false;
  char* end = nullptr;
  const long v = std::strtol(p, &end, 10);
  if (end == p || (end && *end != '\0'))
    return false;
  if (v < 1 || v > 255)
    return false;
  *outAci = static_cast<int>(v);
  return true;
}

uint32_t StorageToRgbPacked(const std::string& storage) {
  int aci = -1;
  if (ParseAciPrefix(storage, &aci))
    return DxfRgbPackedFromAci(aci);
  uint32_t rgb = 0;
  if (DxfColorStringToRgbPacked(storage, &rgb))
    return rgb;
  const int named = NamedColorToAci(storage);
  if (named >= 0)
    return DxfRgbPackedFromAci(named);
  return DxfRgbPackedFromAci(7);
}

} // namespace

bool CadColorIsByLayer(std::string_view storage) {
  return storage == "ByLayer";
}

bool CadColorIsByBlock(std::string_view storage) {
  return storage == "ByBlock";
}

bool CadColorTryGetAci(const std::string& storage, int* outAci) {
  if (outAci == nullptr || storage.empty() || CadColorIsByLayer(storage) || CadColorIsByBlock(storage))
    return false;
  if (ParseAciPrefix(storage, outAci))
    return true;
  const int named = NamedColorToAci(storage);
  if (named >= 0) {
    *outAci = named;
    return true;
  }
  uint32_t rgb = 0;
  if (DxfColorStringToRgbPacked(storage, &rgb)) {
    *outAci = DxfNearestAciFromRgbPacked(rgb);
    return true;
  }
  return false;
}

std::string CadColorStorageFromAci(int aci) {
  if (aci < 1)
    aci = 1;
  if (aci > 255)
    aci = 255;
  char buf[16]{};
  std::snprintf(buf, sizeof(buf), "ACI:%d", aci);
  return std::string(buf);
}

std::string CadColorStorageFromRgbPacked(uint32_t rgbPacked) {
  char buf[16]{};
  DxfRgbPackedToHex(rgbPacked & 0xFFFFFFu, buf, sizeof(buf));
  return std::string(buf);
}

std::string CadColorDisplayLabel(const std::string& storage) {
  if (storage.empty() || storage == "---")
    return "---";
  if (CadColorIsByLayer(storage))
    return "By Layer";
  if (CadColorIsByBlock(storage))
    return "By Block";
  int aci = -1;
  if (CadColorTryGetAci(storage, &aci)) {
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "Color %d", aci);
    return std::string(buf);
  }
  return storage;
}

void CadColorResolveRgb(const std::string& storage, float defaultR, float defaultG, float defaultB,
                        float* outRgb3) {
  if (outRgb3 == nullptr)
    return;
  if (storage.empty() || CadColorIsByLayer(storage)) {
    outRgb3[0] = defaultR;
    outRgb3[1] = defaultG;
    outRgb3[2] = defaultB;
    return;
  }
  const uint32_t rgb = StorageToRgbPacked(storage);
  outRgb3[0] = static_cast<float>((rgb >> 16) & 0xFFu) / 255.f;
  outRgb3[1] = static_cast<float>((rgb >> 8) & 0xFFu) / 255.f;
  outRgb3[2] = static_cast<float>(rgb & 0xFFu) / 255.f;
}

std::string CadColorEffectiveStorage(const std::string& entityColor, const std::string& layerColor) {
  if (entityColor.empty() || CadColorIsByLayer(entityColor))
    return layerColor;
  return entityColor;
}

bool CadColorStorageMatches(const std::string& a, const std::string& b) {
  if (a == b)
    return true;
  if (CadColorIsByLayer(a) || CadColorIsByLayer(b) || CadColorIsByBlock(a) || CadColorIsByBlock(b))
    return false;
  int aciA = -1;
  int aciB = -1;
  if (!CadColorTryGetAci(a, &aciA) || !CadColorTryGetAci(b, &aciB))
    return false;
  return aciA == aciB;
}
