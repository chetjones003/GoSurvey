#include "io/CadPointCloudPtx.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>

namespace pointcloud_ptx {

namespace {

std::string TrimCopy(const std::string &s) {
  std::size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
  return s.substr(b, e - b);
}

void StripTrailingCr(std::string &line) {
  if (!line.empty() && line.back() == '\r') line.pop_back();
}

std::vector<std::string> SplitWhitespace(const std::string &line) {
  std::vector<std::string> toks;
  std::istringstream iss(line);
  std::string t;
  while (iss >> t) toks.push_back(t);
  return toks;
}

bool ParseDoubleToken(const std::string &tok, double *out) {
  const char *b = tok.c_str();
  const char *e = b + tok.size();
  const auto r = std::from_chars(b, e, *out);
  return r.ec == std::errc() && r.ptr == e;
}

bool ParseIntToken(const std::string &tok, int *out) {
  const char *b = tok.c_str();
  const char *e = b + tok.size();
  const auto r = std::from_chars(b, e, *out);
  return r.ec == std::errc() && r.ptr == e;
}

/// One successfully-tokenized data line, same shapes `CadPointCloudPts.cpp` parses, minus the
/// bare 3-token case: a PTX data line is always 4 or 7 tokens (it always carries intensity).
struct ParsedLine {
  double x = 0, y = 0, z = 0;
  double intensity = 0;
  bool hasColor = false;
  int r = 0, g = 0, b = 0;
};

bool ParseDataLine(const std::string &line, ParsedLine *out, std::string *err) {
  const std::vector<std::string> toks = SplitWhitespace(line);
  if (toks.size() != 4 && toks.size() != 7) {
    if (err)
      *err = "PTX import: a data line has " + std::to_string(toks.size()) +
             " token(s); expected 4 (X Y Z Intensity) or 7 (X Y Z Intensity R G B)";
    return false;
  }
  double xyz[3];
  for (int i = 0; i < 3; ++i) {
    if (!ParseDoubleToken(toks[static_cast<std::size_t>(i)], &xyz[i])) {
      if (err) *err = "PTX import: a data line has a coordinate that is not a valid number ('" +
                       toks[static_cast<std::size_t>(i)] + "')";
      return false;
    }
  }
  out->x = xyz[0];
  out->y = xyz[1];
  out->z = xyz[2];

  double inten = 0.0;
  if (!ParseDoubleToken(toks[3], &inten)) {
    if (err)
      *err = "PTX import: a data line has an intensity value that is not a valid number ('" +
             toks[3] + "')";
    return false;
  }
  out->intensity = inten;

  if (toks.size() == 7) {
    int rgb[3];
    for (std::size_t i = 0; i < 3; ++i) {
      if (!ParseIntToken(toks[4 + i], &rgb[i])) {
        if (err)
          *err = "PTX import: a data line has an R/G/B value that is not a valid integer ('" +
                 toks[4 + i] + "')";
        return false;
      }
    }
    out->hasColor = true;
    out->r = rgb[0];
    out->g = rgb[1];
    out->b = rgb[2];
  }
  return true;
}

/// A scan block's parsed 10-line header: `ncols`/`nrows` plus the 4x4 setup transform. The
/// scanner registered position/axes rows (3 lines + 3 lines = 1+3 header lines, by PTX
/// convention) are validated for shape but never read into this struct — they are a redundant,
/// informational restatement of the same transform the 4x4 already carries in full, and this
/// reader/writer treats the 4x4 as the single source of truth (see `CadPointCloudPtx.hpp`'s
/// header comment on the row/column convention used).
struct ScanHeader {
  long long ncols = 0;
  long long nrows = 0;
  /// Row-major 4x4, `m[row][col]`. `P_world = M * [x y z 1]^T` — translation in column 3 of rows
  /// 0-2; row 3 is parsed but not used.
  double m[4][4] = {};
};

/// Reads one non-blank, trimmed line. Returns false (EOF) without setting `*err` — that is the
/// normal "no more scan blocks" end condition, distinguished from a genuine mid-header truncation
/// by the caller (which only calls this between blocks, never mid-header).
bool NextNonBlankLine(std::istream &in, std::string *out) {
  std::string line;
  while (std::getline(in, line)) {
    StripTrailingCr(line);
    std::string trimmed = TrimCopy(line);
    if (!trimmed.empty()) {
      *out = trimmed;
      return true;
    }
  }
  return false;
}

/// Reads exactly `count` whitespace-separated double tokens from `line` into `out`. A line with
/// any other token count, or a token that fails to parse, is malformed.
bool ParseExactDoubles(const std::string &line, int count, double *out, const char *what,
                       std::string *err) {
  const std::vector<std::string> toks = SplitWhitespace(line);
  if (static_cast<int>(toks.size()) != count) {
    if (err)
      *err = std::string("PTX import: a scan's ") + what + " line has " +
             std::to_string(toks.size()) + " token(s); expected " + std::to_string(count);
    return false;
  }
  for (int i = 0; i < count; ++i) {
    if (!ParseDoubleToken(toks[static_cast<std::size_t>(i)], &out[i])) {
      if (err)
        *err = std::string("PTX import: a scan's ") + what + " line has a value that is not a " +
               "valid number ('" + toks[static_cast<std::size_t>(i)] + "')";
      return false;
    }
  }
  return true;
}

/// Reads one scan block's 10-line header (`ncols`, `nrows`, scanner position, 3 scanner axis
/// rows, then the 4x4 transform), given `firstLine` (the already-read, non-blank `ncols` line).
/// Returns false with `*err` set on any malformed/truncated header line — every such failure is
/// REQ-172's "a header count that doesn't match... is refused" / malformed-file case.
bool ReadScanHeader(std::istream &in, const std::string &firstLine, ScanHeader *out,
                    std::string *err) {
  {
    // Parsed directly into a `long long` (not via ParseIntToken, which is `int`-sized) — ncols
    // can legitimately exceed INT_MAX for a very wide scan, so it must not be silently truncated.
    const std::vector<std::string> toks = SplitWhitespace(firstLine);
    long long ncols = 0;
    bool valid = toks.size() == 1;
    if (valid) {
      const char *b = toks[0].c_str();
      const char *e = b + toks[0].size();
      const auto r = std::from_chars(b, e, ncols);
      valid = r.ec == std::errc() && r.ptr == e && ncols > 0;
    }
    if (!valid) {
      if (err)
        *err = "PTX import: a scan's grid column count ('ncols') is not a valid positive integer";
      return false;
    }
    out->ncols = ncols;
  }
  {
    std::string nrowsLine;
    if (!std::getline(in, nrowsLine)) {
      if (err) *err = "PTX import: file is truncated inside a scan header (missing 'nrows')";
      return false;
    }
    StripTrailingCr(nrowsLine);
    const std::string trimmed = TrimCopy(nrowsLine);
    const std::vector<std::string> toks = SplitWhitespace(trimmed);
    long long v = 0;
    if (toks.size() != 1) {
      if (err)
        *err = "PTX import: a scan's grid row count ('nrows') is not a valid positive integer";
      return false;
    }
    const char *b = toks[0].c_str();
    const char *e = b + toks[0].size();
    const auto r = std::from_chars(b, e, v);
    if (r.ec != std::errc() || r.ptr != e || v <= 0) {
      if (err)
        *err = "PTX import: a scan's grid row count ('nrows') is not a valid positive integer";
      return false;
    }
    out->nrows = v;
  }

  // Scanner registered position (1 line, 3 doubles) + registered X/Y/Z axes (3 lines, 3 doubles
  // each) — parsed for shape validation only (see ScanHeader's comment); discarded otherwise.
  double discard3[3];
  const char *rowNames[4] = {"scanner position", "scanner X-axis", "scanner Y-axis",
                             "scanner Z-axis"};
  for (int i = 0; i < 4; ++i) {
    std::string line;
    if (!std::getline(in, line)) {
      if (err)
        *err = std::string("PTX import: file is truncated inside a scan header (missing the ") +
               rowNames[i] + " line)";
      return false;
    }
    StripTrailingCr(line);
    if (!ParseExactDoubles(TrimCopy(line), 3, discard3, rowNames[i], err)) return false;
  }

  // The 4x4 setup transform, 4 lines of 4 doubles each.
  const char *matRowNames[4] = {"transform row 0", "transform row 1", "transform row 2",
                                "transform row 3"};
  for (int row = 0; row < 4; ++row) {
    std::string line;
    if (!std::getline(in, line)) {
      if (err)
        *err = std::string("PTX import: file is truncated inside a scan header (missing the ") +
               matRowNames[row] + ")";
      return false;
    }
    StripTrailingCr(line);
    if (!ParseExactDoubles(TrimCopy(line), 4, out->m[row], matRowNames[row], err)) return false;
  }
  return true;
}

/// Applies `h`'s 4x4 setup transform to local point (x,y,z): `P_world = M * [x y z 1]^T` —
/// translation in column 3 of rows 0-2 (see `CadPointCloudPtx.hpp`'s header comment).
void ApplyTransform(const ScanHeader &h, double x, double y, double z, double *outX, double *outY,
                    double *outZ) {
  *outX = h.m[0][0] * x + h.m[0][1] * y + h.m[0][2] * z + h.m[0][3];
  *outY = h.m[1][0] * x + h.m[1][1] * y + h.m[1][2] * z + h.m[1][3];
  *outZ = h.m[2][0] * x + h.m[2][1] * y + h.m[2][2] * z + h.m[2][3];
}

}  // namespace

std::int64_t QuickPointCountEstimate(const std::string &pathUtf8) {
  if (pathUtf8.empty()) return 0;
  std::ifstream in(std::filesystem::u8path(pathUtf8), std::ios::binary);
  if (!in) return 0;
  std::string firstLine;
  if (!NextNonBlankLine(in, &firstLine)) return 0;
  const std::vector<std::string> toks = SplitWhitespace(firstLine);
  if (toks.size() != 1) return 0;
  long long ncols = 0;
  {
    const char *b = toks[0].c_str();
    const char *e = b + toks[0].size();
    const auto r = std::from_chars(b, e, ncols);
    if (r.ec != std::errc() || r.ptr != e || ncols <= 0) return 0;
  }
  std::string nrowsLine;
  if (!std::getline(in, nrowsLine)) return 0;
  StripTrailingCr(nrowsLine);
  const std::string trimmed = TrimCopy(nrowsLine);
  const std::vector<std::string> nrowsToks = SplitWhitespace(trimmed);
  if (nrowsToks.size() != 1) return 0;
  long long nrows = 0;
  const char *b = nrowsToks[0].c_str();
  const char *e = b + nrowsToks[0].size();
  const auto r = std::from_chars(b, e, nrows);
  if (r.ec != std::errc() || r.ptr != e || nrows <= 0) return 0;
  return static_cast<std::int64_t>(ncols) * static_cast<std::int64_t>(nrows);
}

bool StreamPtxFile(const std::string &pathUtf8, std::int64_t chunkPointBudget,
                   const std::function<bool(const PointChunk &)> &onChunk,
                   std::string *errorMessage) {
  const auto fail = [&](const std::string &msg) {
    if (errorMessage) *errorMessage = msg;
    return false;
  };
  if (pathUtf8.empty()) return fail("PTX import: empty file path");

  std::ifstream in(std::filesystem::u8path(pathUtf8), std::ios::binary);
  if (!in) return fail("PTX import: could not open '" + pathUtf8 + "'");

  // The whole-file schema (intensity always present; colour decided from the first data line
  // across ALL scans) is decided once and held fixed, same "decide from the first read, then stay
  // fixed" tolerance `StreamPtsFile`/`BuildFromE57` already document.
  bool schemaDecided = false;
  bool hasColor = false;

  PointChunk chunk;
  const std::int64_t budget = std::max<std::int64_t>(1, chunkPointBudget);
  bool aborted = false;
  std::string abortErr;
  long long scanCount = 0;

  const auto flushIfFull = [&]() -> bool {
    if (static_cast<std::int64_t>(chunk.pointsXyz.size() / 3) < budget) return true;
    if (!onChunk(chunk)) return false;
    chunk.pointsXyz.clear();
    chunk.colorsRgb.clear();
    chunk.intensity.clear();
    return true;
  };

  std::string firstLine;
  while (NextNonBlankLine(in, &firstLine)) {
    ScanHeader header;
    std::string headerErr;
    if (!ReadScanHeader(in, firstLine, &header, &headerErr)) return fail(headerErr);
    ++scanCount;

    const long long declared = header.ncols * header.nrows;
    std::string line;
    for (long long i = 0; i < declared; ++i) {
      if (!std::getline(in, line)) {
        return fail("PTX import: '" + pathUtf8 + "' is truncated — a scan declared " +
                   std::to_string(declared) + " grid cell(s) (" + std::to_string(header.ncols) +
                   " x " + std::to_string(header.nrows) + ") but only " + std::to_string(i) +
                   " data line(s) are present");
      }
      StripTrailingCr(line);

      ParsedLine parsed;
      std::string lineErr;
      if (!ParseDataLine(line, &parsed, &lineErr)) return fail(lineErr);

      // "No return" sentinel: raw (pre-transform) x=y=z=0 and intensity=0 — still a counted grid
      // cell, never an emitted point (CadPointCloudPtx.hpp's header comment).
      const bool noReturn =
          parsed.x == 0.0 && parsed.y == 0.0 && parsed.z == 0.0 && parsed.intensity == 0.0;
      if (noReturn) continue;

      if (!schemaDecided) {
        hasColor = parsed.hasColor;
        schemaDecided = true;
      }

      double wx, wy, wz;
      ApplyTransform(header, parsed.x, parsed.y, parsed.z, &wx, &wy, &wz);

      chunk.pointsXyz.push_back(wx);
      chunk.pointsXyz.push_back(wy);
      chunk.pointsXyz.push_back(wz);
      chunk.intensity.push_back(static_cast<float>(parsed.intensity));
      if (hasColor) {
        if (parsed.hasColor) {
          chunk.colorsRgb.push_back(static_cast<float>(std::clamp(parsed.r, 0, 255)) / 255.0f);
          chunk.colorsRgb.push_back(static_cast<float>(std::clamp(parsed.g, 0, 255)) / 255.0f);
          chunk.colorsRgb.push_back(static_cast<float>(std::clamp(parsed.b, 0, 255)) / 255.0f);
        } else {
          chunk.colorsRgb.insert(chunk.colorsRgb.end(), 3, 0.0f);
        }
      }

      if (!flushIfFull()) {
        aborted = true;
        abortErr = "point-cloud cache build aborted mid-stream";
        break;
      }
    }
    if (aborted) break;
  }
  if (aborted) return fail(abortErr);

  if (!chunk.pointsXyz.empty()) {
    if (!onChunk(chunk)) return fail("point-cloud cache build aborted mid-stream");
  }

  if (scanCount == 0) return fail("PTX import: '" + pathUtf8 + "' is empty (no scan blocks)");

  return true;
}

ReadResult ReadPtxFile(const std::string &pathUtf8) {
  ReadResult out;
  std::string err;
  const bool ok = StreamPtxFile(
      pathUtf8, (std::numeric_limits<std::int64_t>::max)() / 4,
      [&](const PointChunk &chunk) -> bool {
        out.pointsXyz.insert(out.pointsXyz.end(), chunk.pointsXyz.begin(), chunk.pointsXyz.end());
        out.colorsRgb.insert(out.colorsRgb.end(), chunk.colorsRgb.begin(), chunk.colorsRgb.end());
        out.intensity.insert(out.intensity.end(), chunk.intensity.begin(), chunk.intensity.end());
        return true;
      },
      &err);
  if (!ok) {
    ReadResult failure;
    failure.errorMessage = err;
    return failure;
  }
  if (out.pointsXyz.empty()) {
    ReadResult failure;
    failure.errorMessage = "PTX import: '" + pathUtf8 + "' contains zero valid points";
    return failure;
  }
  out.ok = true;
  return out;
}

WriteResult WritePtxFile(const std::string &pathUtf8, const std::vector<double> &pointsXyz,
                         const std::vector<float> &colorsRgb,
                         const std::vector<float> &intensity) {
  WriteResult res;
  const std::int64_t n = static_cast<std::int64_t>(pointsXyz.size() / 3);
  if (n <= 0) {
    res.errorMessage = "PTX export: no points to write";
    return res;
  }
  const bool hasColor = !colorsRgb.empty();
  const bool hasIntensity = !intensity.empty();
  if (hasColor && colorsRgb.size() != static_cast<std::size_t>(n) * 3) {
    res.errorMessage = "PTX export: colour array size does not match the point count";
    return res;
  }
  if (hasIntensity && intensity.size() != static_cast<std::size_t>(n)) {
    res.errorMessage = "PTX export: intensity array size does not match the point count";
    return res;
  }

  const std::filesystem::path path = std::filesystem::u8path(pathUtf8);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    res.errorMessage = "PTX export: could not create '" + pathUtf8 + "'";
    return res;
  }

  const auto writeLine = [&](const char *buf, int len) {
    return len > 0 && static_cast<bool>(out.write(buf, len));
  };
  bool writeFailed = false;
  char line[256];

  // One scan block: ncols == N, nrows == 1 (a degenerate single-row grid listing every point),
  // scanner position at the origin, identity registered axes, identity 4x4 transform — GoSurvey's
  // in-memory cloud has no "scanner setup" concept, so there is nothing non-identity to write
  // (CadPointCloudPtx.hpp's header comment).
  int len = std::snprintf(line, sizeof(line), "%lld\n", static_cast<long long>(n));
  writeFailed = writeFailed || !writeLine(line, len);
  len = std::snprintf(line, sizeof(line), "1\n");
  writeFailed = writeFailed || !writeLine(line, len);
  len = std::snprintf(line, sizeof(line), "0.000000 0.000000 0.000000\n");
  writeFailed = writeFailed || !writeLine(line, len);
  len = std::snprintf(line, sizeof(line), "1.000000 0.000000 0.000000\n");
  writeFailed = writeFailed || !writeLine(line, len);
  len = std::snprintf(line, sizeof(line), "0.000000 1.000000 0.000000\n");
  writeFailed = writeFailed || !writeLine(line, len);
  len = std::snprintf(line, sizeof(line), "0.000000 0.000000 1.000000\n");
  writeFailed = writeFailed || !writeLine(line, len);
  len = std::snprintf(line, sizeof(line), "1.000000 0.000000 0.000000 0.000000\n");
  writeFailed = writeFailed || !writeLine(line, len);
  len = std::snprintf(line, sizeof(line), "0.000000 1.000000 0.000000 0.000000\n");
  writeFailed = writeFailed || !writeLine(line, len);
  len = std::snprintf(line, sizeof(line), "0.000000 0.000000 1.000000 0.000000\n");
  writeFailed = writeFailed || !writeLine(line, len);
  len = std::snprintf(line, sizeof(line), "0.000000 0.000000 0.000000 1.000000\n");
  writeFailed = writeFailed || !writeLine(line, len);

  // A PTX scan body is ONE consistent line shape — 4-token when the source cloud carries no
  // colour, else 7-token, with a documented default for intensity (1.0) when the source cloud
  // does not actually carry that channel (PTX has no bare 3-token shape, unlike PTS).
  for (std::int64_t i = 0; i < n && !writeFailed; ++i) {
    const double x = pointsXyz[static_cast<std::size_t>(i) * 3 + 0];
    const double y = pointsXyz[static_cast<std::size_t>(i) * 3 + 1];
    const double z = pointsXyz[static_cast<std::size_t>(i) * 3 + 2];
    const double inten = hasIntensity ? static_cast<double>(intensity[static_cast<std::size_t>(i)]) : 1.0;
    if (!hasColor) {
      len = std::snprintf(line, sizeof(line), "%.6f %.6f %.6f %.4f\n", x, y, z, inten);
    } else {
      const auto toByte = [](float v) {
        const int iv = static_cast<int>(std::lround(static_cast<double>(v) * 255.0));
        return std::clamp(iv, 0, 255);
      };
      const int r = toByte(colorsRgb[static_cast<std::size_t>(i) * 3 + 0]);
      const int g = toByte(colorsRgb[static_cast<std::size_t>(i) * 3 + 1]);
      const int b = toByte(colorsRgb[static_cast<std::size_t>(i) * 3 + 2]);
      len = std::snprintf(line, sizeof(line), "%.6f %.6f %.6f %.4f %d %d %d\n", x, y, z, inten, r,
                          g, b);
    }
    if (!writeLine(line, len)) writeFailed = true;
  }
  out.flush();
  if (writeFailed || !out) {
    res.errorMessage = "PTX export: disk write failed";
    out.close();
    std::error_code rmEc;
    std::filesystem::remove(path, rmEc);
    return res;
  }
  out.close();
  res.ok = true;
  return res;
}

}  // namespace pointcloud_ptx
