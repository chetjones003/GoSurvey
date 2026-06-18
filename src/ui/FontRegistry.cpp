#include "FontRegistry.hpp"

#include <imgui.h>

#include <cctype>
#include <fstream>
#include <string>
#include <unordered_map>

namespace {

ImFont* g_default = nullptr;
// Cache key "family|b|i" → resolved font + which styles it actually carries (so callers can apply faux
// bold/italic). A null font means "tried, unavailable" → use default. Cached to avoid per-frame disk I/O.
struct CachedFont {
  ImFont* font = nullptr;
  bool realBold = false;
  bool realItalic = false;
};
std::unordered_map<std::string, CachedFont> g_cache;

std::string Lower(std::string s) {
  for (char& c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool FileExists(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  return f.good();
}

// A TrueType family with its four Windows variant filenames (any may be empty / missing on a box).
struct Family {
  const char* key;  // lowercased, spaces/extension stripped
  const char* reg;
  const char* bold;
  const char* italic;
  const char* boldItalic;
};

const Family kFamilies[] = {
    {"arial", "arial.ttf", "arialbd.ttf", "ariali.ttf", "arialbi.ttf"},
    {"timesnewroman", "times.ttf", "timesbd.ttf", "timesi.ttf", "timesbi.ttf"},
    {"times", "times.ttf", "timesbd.ttf", "timesi.ttf", "timesbi.ttf"},
    {"couriernew", "cour.ttf", "courbd.ttf", "couri.ttf", "courbi.ttf"},
    {"courier", "cour.ttf", "courbd.ttf", "couri.ttf", "courbi.ttf"},
    {"calibri", "calibri.ttf", "calibrib.ttf", "calibrii.ttf", "calibriz.ttf"},
    {"verdana", "verdana.ttf", "verdanab.ttf", "verdanai.ttf", "verdanaz.ttf"},
    {"tahoma", "tahoma.ttf", "tahomabd.ttf", "tahoma.ttf", "tahomabd.ttf"},
    {"consolas", "consola.ttf", "consolab.ttf", "consolai.ttf", "consolaz.ttf"},
    {"georgia", "georgia.ttf", "georgiab.ttf", "georgiai.ttf", "georgiaz.ttf"},
    {"segoeui", "segoeui.ttf", "segoeuib.ttf", "segoeuii.ttf", "segoeuiz.ttf"},
    {"cambria", "cambria.ttc", "cambriab.ttf", "cambriai.ttf", "cambriaz.ttf"},
    {"comicsansms", "comic.ttf", "comicbd.ttf", "comici.ttf", "comicz.ttf"},
    {"trebuchetms", "trebuc.ttf", "trebucbd.ttf", "trebucit.ttf", "trebucbi.ttf"},
    {"palatinolinotype", "pala.ttf", "palab.ttf", "palai.ttf", "palabi.ttf"},
    {"impact", "impact.ttf", "impact.ttf", "impact.ttf", "impact.ttf"},
};

// SHX stroke-font name → substitute TrueType family key (AutoCAD-style substitution). The third
// field marks SHX fonts that are inherently italic/oblique.
struct ShxMap {
  const char* key;
  const char* familyKey;
  bool italic;
};

const ShxMap kShx[] = {
    {"txt", "consolas", false},      {"monotxt", "consolas", false},
    {"romans", "arial", false},      {"simplex", "arial", false},
    {"isocp", "arial", false},       {"isocpeur", "arial", false},
    {"isocteur", "arial", false},    {"romand", "timesnewroman", false},
    {"romanc", "timesnewroman", false}, {"romant", "timesnewroman", false},
    {"complex", "timesnewroman", false}, {"italic", "arial", true},
    {"italicc", "timesnewroman", true}, {"italict", "timesnewroman", true},
    {"gothice", "timesnewroman", false}, {"gothicg", "timesnewroman", false},
    {"scripts", "calibri", true},    {"scriptc", "calibri", true},
};

const Family* FindFamily(const std::string& key) {
  for (const Family& f : kFamilies)
    if (key == f.key)
      return &f;
  return nullptr;
}

// Resolve (family key, style) → an existing Windows Fonts path, reporting which style was actually found.
std::string ResolvePath(std::string famKey, bool wantBold, bool wantItalic, bool* gotBold, bool* gotItalic) {
  *gotBold = false;
  *gotItalic = false;
  const std::string dir = "C:/Windows/Fonts/";

  // SHX substitution.
  for (const ShxMap& s : kShx) {
    if (famKey == s.key) {
      famKey = s.familyKey;
      if (s.italic)
        wantItalic = true;
      break;
    }
  }

  if (const Family* f = FindFamily(famKey)) {
    auto tryFile = [&](const char* fn, bool b, bool i) -> std::string {
      if (!fn || !*fn)
        return {};
      std::string p = dir + fn;
      if (FileExists(p)) {
        *gotBold = b;
        *gotItalic = i;
        return p;
      }
      return {};
    };
    std::string p;
    if (wantBold && wantItalic) {
      p = tryFile(f->boldItalic, true, true);
      if (p.empty()) p = tryFile(f->bold, true, false);
      if (p.empty()) p = tryFile(f->italic, false, true);
    } else if (wantBold) {
      p = tryFile(f->bold, true, false);
    } else if (wantItalic) {
      p = tryFile(f->italic, false, true);
    }
    if (p.empty())
      p = tryFile(f->reg, false, false);
    if (!p.empty())
      return p;
  }

  // Best-effort: a file named "<family>.ttf" (covers many single-file fonts).
  std::string guess = dir + famKey + ".ttf";
  if (FileExists(guess))
    return guess;
  return {};
}

}  // namespace

namespace FontReg {

void SetDefault(ImFont* f) { g_default = f; }

ImFont* Resolve(const std::string& fontNameOrShx, bool bold, bool italic, bool* outRealBold, bool* outRealItalic) {
  // Normalize: lowercase, drop extension, drop spaces.
  std::string key = Lower(fontNameOrShx);
  if (const auto dot = key.rfind('.'); dot != std::string::npos)
    key = key.substr(0, dot);
  std::string norm;
  for (char c : key)
    if (c != ' ')
      norm += c;

  const std::string cacheKey = norm + (bold ? "|b" : "") + (italic ? "|i" : "");
  if (auto it = g_cache.find(cacheKey); it != g_cache.end()) {
    if (outRealBold) *outRealBold = it->second.realBold;
    if (outRealItalic) *outRealItalic = it->second.realItalic;
    return it->second.font ? it->second.font : g_default;
  }

  bool gotBold = false, gotItalic = false;
  const std::string path = ResolvePath(norm, bold, italic, &gotBold, &gotItalic);
  ImFont* font = nullptr;
  if (!path.empty()) {
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    font = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 0.0f, &cfg);  // 0 = dynamic size (1.92)
  }
  CachedFont cf;
  cf.font = font;
  cf.realBold = gotBold && font != nullptr;
  cf.realItalic = gotItalic && font != nullptr;
  g_cache[cacheKey] = cf;  // cache null too, so we don't retry a missing font every frame
  if (outRealBold) *outRealBold = cf.realBold;
  if (outRealItalic) *outRealItalic = cf.realItalic;
  return font ? font : g_default;
}

}  // namespace FontReg
