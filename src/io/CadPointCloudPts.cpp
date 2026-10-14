#include "io/CadPointCloudPts.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>

namespace pointcloud_pts {

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

/// One successfully-tokenized data line. `hasIntensity`/`hasColor` reflect THIS line's own token
/// count (3/4/7) — the caller projects it onto the whole-file schema decided from the first data
/// line (see `StreamPtsFile`'s header comment on mixed-shape handling).
struct ParsedLine {
  double x = 0, y = 0, z = 0;
  bool hasIntensity = false;
  double intensity = 0;
  bool hasColor = false;
  int r = 0, g = 0, b = 0;
};

/// Returns false (with `*err` set) for a line that is not exactly 3, 4, or 7 whitespace-separated
/// numeric tokens — the only malformed-line shape REQ-172's PTS acceptance names.
bool ParseDataLine(const std::string &line, ParsedLine *out, std::string *err) {
  const std::vector<std::string> toks = SplitWhitespace(line);
  if (toks.size() != 3 && toks.size() != 4 && toks.size() != 7) {
    if (err)
      *err = "PTS import: a data line has " + std::to_string(toks.size()) +
             " token(s); expected 3 (X Y Z), 4 (X Y Z Intensity), or 7 (X Y Z Intensity R G B)";
    return false;
  }
  double xyz[3];
  for (int i = 0; i < 3; ++i) {
    if (!ParseDoubleToken(toks[static_cast<std::size_t>(i)], &xyz[i])) {
      if (err) *err = "PTS import: a data line has a coordinate that is not a valid number ('" +
                       toks[static_cast<std::size_t>(i)] + "')";
      return false;
    }
  }
  out->x = xyz[0];
  out->y = xyz[1];
  out->z = xyz[2];

  if (toks.size() >= 4) {
    double inten = 0.0;
    if (!ParseDoubleToken(toks[3], &inten)) {
      if (err)
        *err = "PTS import: a data line has an intensity value that is not a valid number ('" +
               toks[3] + "')";
      return false;
    }
    out->hasIntensity = true;
    out->intensity = inten;
  }
  if (toks.size() == 7) {
    int rgb[3];
    for (int i = 0; i < 3; ++i) {
      if (!ParseIntToken(toks[static_cast<std::size_t>(4 + i)], &rgb[i])) {
        if (err)
          *err = "PTS import: a data line has an R/G/B value that is not a valid integer ('" +
                 toks[static_cast<std::size_t>(4 + i)] + "')";
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

}  // namespace

std::int64_t QuickPointCountEstimate(const std::string &pathUtf8) {
  if (pathUtf8.empty()) return 0;
  std::error_code ec;
  std::ifstream in(std::filesystem::u8path(pathUtf8), std::ios::binary);
  if (!in) return 0;
  std::string headerLine;
  if (!std::getline(in, headerLine)) return 0;
  StripTrailingCr(headerLine);
  const std::string trimmed = TrimCopy(headerLine);
  long long count = 0;
  const char *b = trimmed.c_str();
  const char *e = b + trimmed.size();
  const auto r = std::from_chars(b, e, count);
  if (r.ec != std::errc() || r.ptr != e || count <= 0) return 0;
  return static_cast<std::int64_t>(count);
}

bool StreamPtsFile(const std::string &pathUtf8, std::int64_t chunkPointBudget,
                    const std::function<bool(const PointChunk &)> &onChunk,
                    std::string *errorMessage) {
  const auto fail = [&](const std::string &msg) {
    if (errorMessage) *errorMessage = msg;
    return false;
  };
  if (pathUtf8.empty()) return fail("PTS import: empty file path");

  std::ifstream in(std::filesystem::u8path(pathUtf8), std::ios::binary);
  if (!in) return fail("PTS import: could not open '" + pathUtf8 + "'");

  std::string headerLine;
  if (!std::getline(in, headerLine))
    return fail("PTS import: '" + pathUtf8 + "' is empty (missing the point-count header line)");
  StripTrailingCr(headerLine);
  const std::string trimmedHeader = TrimCopy(headerLine);
  long long declaredCount = 0;
  {
    const char *b = trimmedHeader.c_str();
    const char *e = b + trimmedHeader.size();
    const auto r = std::from_chars(b, e, declaredCount);
    if (r.ec != std::errc() || r.ptr != e)
      return fail("PTS import: '" + pathUtf8 + "' header line is not a valid integer point count");
  }
  if (declaredCount <= 0)
    return fail("PTS import: '" + pathUtf8 + "' header point count must be positive, found " +
               std::to_string(declaredCount));

  // The whole-file schema is decided ONCE, from the first data line's own shape, and every
  // subsequent line is projected onto it (missing channels default-filled, extra channels
  // dropped) — the same "decide from the first non-empty read, then stay fixed" tolerance
  // `pointcloudcache::BuildFromE57` already documents for an E57 with inconsistent per-scan
  // schemas. This is what lets the reader "accept all three [PTS] shapes per-line" (REQ-172)
  // without needing a second pass over the file.
  bool schemaDecided = false;
  bool hasIntensity = false;
  bool hasColor = false;

  PointChunk chunk;
  const std::int64_t budget = std::max<std::int64_t>(1, chunkPointBudget);
  bool aborted = false;
  std::string abortErr;

  std::string line;
  long long linesRead = 0;
  for (; linesRead < declaredCount; ++linesRead) {
    if (!std::getline(in, line)) {
      return fail("PTS import: '" + pathUtf8 + "' is truncated — header declared " +
                 std::to_string(declaredCount) + " point(s) but only " +
                 std::to_string(linesRead) + " data line(s) are present");
    }
    StripTrailingCr(line);

    ParsedLine parsed;
    std::string lineErr;
    if (!ParseDataLine(line, &parsed, &lineErr)) return fail(lineErr);

    if (!schemaDecided) {
      hasIntensity = parsed.hasIntensity;
      hasColor = parsed.hasColor;
      schemaDecided = true;
    }

    chunk.pointsXyz.push_back(parsed.x);
    chunk.pointsXyz.push_back(parsed.y);
    chunk.pointsXyz.push_back(parsed.z);
    if (hasIntensity)
      chunk.intensity.push_back(parsed.hasIntensity ? static_cast<float>(parsed.intensity) : 0.0f);
    if (hasColor) {
      if (parsed.hasColor) {
        chunk.colorsRgb.push_back(std::clamp(parsed.r, 0, 255) / 255.0f);
        chunk.colorsRgb.push_back(std::clamp(parsed.g, 0, 255) / 255.0f);
        chunk.colorsRgb.push_back(std::clamp(parsed.b, 0, 255) / 255.0f);
      } else {
        chunk.colorsRgb.insert(chunk.colorsRgb.end(), 3, 0.0f);
      }
    }

    if (static_cast<std::int64_t>(chunk.pointsXyz.size() / 3) >= budget) {
      if (!onChunk(chunk)) {
        aborted = true;
        abortErr = "point-cloud cache build aborted mid-stream";
        break;
      }
      chunk.pointsXyz.clear();
      chunk.colorsRgb.clear();
      chunk.intensity.clear();
    }
  }
  if (aborted) return fail(abortErr);

  if (!chunk.pointsXyz.empty()) {
    if (!onChunk(chunk)) return fail("point-cloud cache build aborted mid-stream");
  }

  // Any further non-blank content past the declared count is itself a header/data mismatch
  // (REQ-172: "a header count that doesn't match the number of data lines actually present").
  // A lone trailing blank line (common end-of-file newline) is tolerated.
  while (std::getline(in, line)) {
    StripTrailingCr(line);
    if (!TrimCopy(line).empty())
      return fail("PTS import: '" + pathUtf8 + "' has more data lines than its header declared (" +
                 std::to_string(declaredCount) + ")");
  }

  return true;
}

ReadResult ReadPtsFile(const std::string &pathUtf8) {
  ReadResult out;
  std::string err;
  const bool ok = StreamPtsFile(
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
    failure.errorMessage = "PTS import: '" + pathUtf8 + "' contains zero points";
    return failure;
  }
  out.ok = true;
  return out;
}

WriteResult WritePtsFile(const std::string &pathUtf8, const std::vector<double> &pointsXyz,
                          const std::vector<float> &colorsRgb,
                          const std::vector<float> &intensity) {
  WriteResult res;
  const std::int64_t n = static_cast<std::int64_t>(pointsXyz.size() / 3);
  if (n <= 0) {
    res.errorMessage = "PTS export: no points to write";
    return res;
  }
  const bool hasColor = !colorsRgb.empty();
  const bool hasIntensity = !intensity.empty();
  if (hasColor && colorsRgb.size() != static_cast<std::size_t>(n) * 3) {
    res.errorMessage = "PTS export: colour array size does not match the point count";
    return res;
  }
  if (hasIntensity && intensity.size() != static_cast<std::size_t>(n)) {
    res.errorMessage = "PTS export: intensity array size does not match the point count";
    return res;
  }

  const std::filesystem::path path = std::filesystem::u8path(pathUtf8);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    res.errorMessage = "PTS export: could not create '" + pathUtf8 + "'";
    return res;
  }

  const auto writeLine = [&](const char *buf, int len) {
    return len > 0 && static_cast<bool>(out.write(buf, len));
  };

  char line[256];
  int len = std::snprintf(line, sizeof(line), "%lld\n", static_cast<long long>(n));
  if (!writeLine(line, len)) {
    res.errorMessage = "PTS export: disk write failed";
    out.close();
    std::error_code rmEc;
    std::filesystem::remove(path, rmEc);
    return res;
  }

  // A PTS file is ONE consistent line shape for its whole body — 3-token when the source cloud
  // carries neither colour nor intensity, else 7-token with a documented default for whichever of
  // the two channels the source cloud does not actually have.
  const bool sevenToken = hasColor || hasIntensity;
  bool writeFailed = false;
  for (std::int64_t i = 0; i < n && !writeFailed; ++i) {
    const double x = pointsXyz[static_cast<std::size_t>(i) * 3 + 0];
    const double y = pointsXyz[static_cast<std::size_t>(i) * 3 + 1];
    const double z = pointsXyz[static_cast<std::size_t>(i) * 3 + 2];
    if (!sevenToken) {
      len = std::snprintf(line, sizeof(line), "%.6f %.6f %.6f\n", x, y, z);
    } else {
      const double inten =
          hasIntensity ? static_cast<double>(intensity[static_cast<std::size_t>(i)]) : 1.0;
      int r = 255, g = 255, b = 255;
      if (hasColor) {
        const auto toByte = [](float v) {
          const int iv = static_cast<int>(std::lround(static_cast<double>(v) * 255.0));
          return std::clamp(iv, 0, 255);
        };
        r = toByte(colorsRgb[static_cast<std::size_t>(i) * 3 + 0]);
        g = toByte(colorsRgb[static_cast<std::size_t>(i) * 3 + 1]);
        b = toByte(colorsRgb[static_cast<std::size_t>(i) * 3 + 2]);
      }
      len = std::snprintf(line, sizeof(line), "%.6f %.6f %.6f %.4f %d %d %d\n", x, y, z, inten, r,
                          g, b);
    }
    if (!writeLine(line, len)) writeFailed = true;
  }
  out.flush();
  if (writeFailed || !out) {
    res.errorMessage = "PTS export: disk write failed";
    out.close();
    std::error_code rmEc;
    std::filesystem::remove(path, rmEc);
    return res;
  }
  out.close();
  res.ok = true;
  return res;
}

}  // namespace pointcloud_pts
