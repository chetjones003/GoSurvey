#pragma once

#include <string>
#include <vector>

// Pure font-name/encoding helpers for the PDF plot's TrueType text path (REQ-049). Kept header-only and
// free of PDFium/filesystem so the tricky logic — family→file mapping, base-14 substitution, and UTF-8→
// UTF-16 encoding — is unit-testable (the OrthoConstrain/ColorContrast precedent). PdfPlot pairs the file
// candidates with the Windows Fonts directory and PDFium; those parts stay in the .cpp.
namespace plotfont {

inline std::string LowerAscii(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  return s;
}

// Candidate .ttf file names for a family, best first: a known alias (title-block families whose file name
// differs from the family), then the family with spaces stripped + ".ttf". PdfPlot probes these under the
// Windows Fonts directory and embeds the first that exists; if none do it falls back to StandardSubstitute.
// .ttc collections are intentionally not produced here (FPDFText_LoadFont expects a single TrueType face).
inline std::vector<std::string> TtfCandidates(const std::string& family) {
  const std::string lf = LowerAscii(family);
  std::vector<std::string> out;
  struct Alias { const char* family; const char* file; };
  static const Alias kAlias[] = {
      {"arial", "arial.ttf"},          {"arial black", "ariblk.ttf"},
      {"arial narrow", "arialn.ttf"},  {"times new roman", "times.ttf"},
      {"courier new", "cour.ttf"},     {"calibri", "calibri.ttf"},
      {"consolas", "consola.ttf"},     {"verdana", "verdana.ttf"},
      {"tahoma", "tahoma.ttf"},        {"georgia", "georgia.ttf"},
      {"segoe ui", "segoeui.ttf"},     {"comic sans ms", "comic.ttf"},
      {"trebuchet ms", "trebuc.ttf"},  {"palatino linotype", "pala.ttf"},
      {"lucida console", "lucon.ttf"}, {"impact", "impact.ttf"},
  };
  for (const Alias& a : kAlias)
    if (lf == a.family) {
      out.emplace_back(a.file);
      break;
    }
  std::string noSpace;
  for (char c : lf)
    if (c != ' ')
      noSpace += c;
  if (!noSpace.empty())
    out.emplace_back(noSpace + ".ttf");
  return out;
}

// Closest base-14 standard PDF font name (no spaces, per FPDFText_LoadStandardFont) for a family that could
// not be embedded — serif/mono detected by keyword, else sans-serif Helvetica.
inline const char* StandardSubstitute(const std::string& family) {
  const std::string lf = LowerAscii(family);
  auto has = [&](const char* k) { return lf.find(k) != std::string::npos; };
  if (has("courier") || has("consol") || has("mono") || has("lucida console"))
    return "Courier";
  if (has("times") || has("serif") || has("georgia") || has("cambria") || has("garamond") || has("roman") ||
      has("palatino"))
    return "Times-Roman";
  return "Helvetica";
}

// UTF-8 → UTF-16LE (null-terminated) for FPDFText_SetText. BMP-only; a stray/invalid byte or an
// above-BMP code point becomes U+FFFD.
inline std::vector<unsigned short> Utf8ToUtf16(const std::string& s) {
  std::vector<unsigned short> out;
  out.reserve(s.size() + 1);
  size_t i = 0;
  const size_t n = s.size();
  while (i < n) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    unsigned int cp;
    if (c < 0x80) {
      cp = c;
      i += 1;
    } else if ((c >> 5) == 0x6 && i + 1 < n) {
      cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
      i += 2;
    } else if ((c >> 4) == 0xE && i + 2 < n) {
      cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
           (static_cast<unsigned char>(s[i + 2]) & 0x3F);
      i += 3;
    } else {
      cp = 0xFFFD;
      i += 1;
    }
    if (cp > 0xFFFF)
      cp = 0xFFFD;  // outside the BMP — best-effort replacement
    out.push_back(static_cast<unsigned short>(cp));
  }
  out.push_back(0);
  return out;
}

}  // namespace plotfont
