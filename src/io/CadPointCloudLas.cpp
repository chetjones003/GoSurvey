#include "io/CadPointCloudLas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>

namespace pointcloud_las {

namespace {

constexpr std::size_t kMinHeaderSize = 227;             // LAS 1.2 Public Header Block size.
constexpr std::size_t kExtendedCountHeaderSize = 375;    // LAS 1.4 — gates the 64-bit point count.
constexpr std::size_t kHeaderReadCap = 375;

template <typename T>
T ReadLE(const unsigned char *buf, std::size_t offset) {
  T v{};
  std::memcpy(&v, buf + offset, sizeof(T));
  return v;
}

template <typename T>
void WriteLE(std::vector<unsigned char> &buf, std::size_t offset, const T &v) {
  std::memcpy(buf.data() + offset, &v, sizeof(T));
}

/// Minimum point-data-record byte size per the ASPRS LAS 1.4 spec, formats 0-10
/// (D-2026-10-15-b). Formats 0-5 share a 20-byte "legacy" core (X,Y,Z,Intensity, then
/// ReturnByte/Classification/ScanAngleRank/UserData/PointSourceID); formats 6-10 share a
/// different 30-byte "extended" core (X,Y,Z,Intensity, then ReturnInfo/ClassFlags/Classification/
/// UserData/ScanAngle(int16)/PointSourceID/GPStime) — X, Y, Z, and Intensity sit at the same
/// byte offsets (0/4/8/12) in both cores, which is what lets `StreamLasFile` decode them with one
/// format-invariant code path; only the colour offset (see `ColorOffsetForFormat`) and the total
/// record length vary.
std::size_t MinRecordSizeForFormat(int format) {
  switch (format) {
    case 0: return 20;
    case 1: return 28;             // legacy core + GPS time(8)
    case 2: return 26;             // legacy core + RGB(6)
    case 3: return 34;             // legacy core + GPS time(8) + RGB(6)
    case 4: return 57;             // format 1 + wave packet(29)
    case 5: return 63;             // format 3 + wave packet(29)
    case 6: return 30;             // extended core (GPS time mandatory, no colour)
    case 7: return 36;             // extended core + RGB(6)
    case 8: return 38;             // extended core + RGB(6) + NIR(2)
    case 9: return 59;             // extended core + wave packet(29)
    case 10: return 67;            // extended core + RGB(6) + NIR(2) + wave packet(29)
    default: return static_cast<std::size_t>(-1);
  }
}

/// Byte offset of the first RGB uint16 within a point data record, or -1 when `format` carries no
/// colour. Formats 2/3/5 share the legacy core's RGB offset (20 for format 2 since it has no GPS
/// time; 28 for 3/5, which both have GPS time ahead of RGB). Formats 7/8/10 share the extended
/// core's fixed 30-byte core, so RGB always starts right after it, at offset 30.
int ColorOffsetForFormat(int format) {
  switch (format) {
    case 2: return 20;
    case 3:
    case 5: return 28;
    case 7:
    case 8:
    case 10: return 30;
    default: return -1;
  }
}

struct ParsedHeader {
  std::uint16_t headerSize = 0;
  std::uint32_t offsetToPointData = 0;
  int pointDataFormat = 0;
  std::uint16_t pointDataRecordLength = 0;
  std::int64_t pointCount = 0;  // resolved legacy-or-extended count
  double scaleX = 1, scaleY = 1, scaleZ = 1;
  double offsetX = 0, offsetY = 0, offsetZ = 0;
};

/// Parses the Public Header Block from the already-open stream `in` (positioned at the start of
/// the file). On success `in`'s read position is left wherever the header read left it — callers
/// seek explicitly to `out->offsetToPointData` before reading point records, so this does not
/// matter.
bool ParseHeader(const std::string &pathUtf8, std::ifstream &in, std::uint64_t fileSize,
                  ParsedHeader *out, std::string *err) {
  const auto fail = [&](const std::string &msg) {
    if (err) *err = msg;
    return false;
  };

  const std::size_t toRead =
      static_cast<std::size_t>(std::min<std::uint64_t>(fileSize, kHeaderReadCap));
  if (toRead < kMinHeaderSize)
    return fail("LAS import: '" + pathUtf8 + "' is too small to hold a LAS header");

  std::vector<unsigned char> buf(toRead);
  in.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(toRead));
  if (!in) return fail("LAS import: '" + pathUtf8 + "' header could not be read");

  if (std::memcmp(buf.data(), "LASF", 4) != 0)
    return fail("LAS import: '" + pathUtf8 + "' does not start with the 'LASF' signature");

  out->headerSize = ReadLE<std::uint16_t>(buf.data(), 94);
  out->offsetToPointData = ReadLE<std::uint32_t>(buf.data(), 96);
  out->pointDataFormat = buf[104];
  out->pointDataRecordLength = ReadLE<std::uint16_t>(buf.data(), 105);
  const std::uint32_t legacyCount = ReadLE<std::uint32_t>(buf.data(), 107);
  out->scaleX = ReadLE<double>(buf.data(), 131);
  out->scaleY = ReadLE<double>(buf.data(), 139);
  out->scaleZ = ReadLE<double>(buf.data(), 147);
  out->offsetX = ReadLE<double>(buf.data(), 155);
  out->offsetY = ReadLE<double>(buf.data(), 163);
  out->offsetZ = ReadLE<double>(buf.data(), 171);

  if (out->pointDataFormat > 10)
    return fail("LAS import: '" + pathUtf8 + "' uses point data record format " +
                std::to_string(out->pointDataFormat) +
                ", which this reader does not support (only formats 0-10 are supported)");

  const std::size_t minRecordSize = MinRecordSizeForFormat(out->pointDataFormat);
  if (out->pointDataRecordLength < minRecordSize)
    return fail("LAS import: '" + pathUtf8 + "' point data record length (" +
                std::to_string(out->pointDataRecordLength) +
                ") is smaller than point data record format " +
                std::to_string(out->pointDataFormat) + "'s minimum (" +
                std::to_string(minRecordSize) + ")");

  std::uint64_t extendedCount = 0;
  if (out->headerSize >= kExtendedCountHeaderSize && toRead >= 255) {
    extendedCount = ReadLE<std::uint64_t>(buf.data(), 247);
  }
  const std::uint64_t resolvedCount = extendedCount != 0 ? extendedCount : legacyCount;
  if (resolvedCount > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
    return fail("LAS import: '" + pathUtf8 + "' declares more points than this reader can address");
  out->pointCount = static_cast<std::int64_t>(resolvedCount);

  if (out->offsetToPointData < kMinHeaderSize || out->offsetToPointData > fileSize)
    return fail("LAS import: '" + pathUtf8 + "' has an invalid offset to point data");

  const std::uint64_t requiredBytes =
      static_cast<std::uint64_t>(out->offsetToPointData) +
      static_cast<std::uint64_t>(out->pointCount) *
          static_cast<std::uint64_t>(out->pointDataRecordLength);
  if (requiredBytes > fileSize)
    return fail("LAS import: '" + pathUtf8 + "' is truncated — header declares " +
                std::to_string(out->pointCount) +
                " point(s) but the file is too short to hold them");

  return true;
}

}  // namespace

std::int64_t QuickPointCountEstimate(const std::string &pathUtf8) {
  if (pathUtf8.empty()) return 0;
  std::error_code ec;
  const auto fileSize = std::filesystem::file_size(std::filesystem::u8path(pathUtf8), ec);
  if (ec) return 0;
  std::ifstream in(std::filesystem::u8path(pathUtf8), std::ios::binary);
  if (!in) return 0;
  ParsedHeader header;
  std::string err;
  if (!ParseHeader(pathUtf8, in, fileSize, &header, &err)) return 0;
  return header.pointCount > 0 ? header.pointCount : 0;
}

bool StreamLasFile(const std::string &pathUtf8, std::int64_t chunkPointBudget,
                    const std::function<bool(const PointChunk &)> &onChunk,
                    std::string *errorMessage) {
  const auto fail = [&](const std::string &msg) {
    if (errorMessage) *errorMessage = msg;
    return false;
  };
  if (pathUtf8.empty()) return fail("LAS import: empty file path");

  std::error_code ec;
  const auto fileSize = std::filesystem::file_size(std::filesystem::u8path(pathUtf8), ec);
  if (ec) return fail("LAS import: could not open '" + pathUtf8 + "'");

  std::ifstream in(std::filesystem::u8path(pathUtf8), std::ios::binary);
  if (!in) return fail("LAS import: could not open '" + pathUtf8 + "'");

  ParsedHeader header;
  std::string headerErr;
  if (!ParseHeader(pathUtf8, in, fileSize, &header, &headerErr)) return fail(headerErr);

  const int colorOffsetOrNone = ColorOffsetForFormat(header.pointDataFormat);
  const bool hasColor = colorOffsetOrNone >= 0;
  const std::size_t colorOffset = hasColor ? static_cast<std::size_t>(colorOffsetOrNone) : 0;

  in.seekg(static_cast<std::streamoff>(header.offsetToPointData), std::ios::beg);
  if (!in) return fail("LAS import: '" + pathUtf8 + "' point data could not be located");

  const std::int64_t budget = std::max<std::int64_t>(1, chunkPointBudget);
  const std::size_t recSize = header.pointDataRecordLength;
  const std::int64_t blockCap = std::max<std::int64_t>(0, std::min(budget, header.pointCount));

  std::vector<char> block(static_cast<std::size_t>(blockCap) * recSize);
  PointChunk chunk;
  chunk.pointsXyz.reserve(static_cast<std::size_t>(blockCap) * 3);
  if (hasColor) chunk.colorsRgb.reserve(static_cast<std::size_t>(blockCap) * 3);
  chunk.intensity.reserve(static_cast<std::size_t>(blockCap));

  std::int64_t remaining = header.pointCount;
  while (remaining > 0) {
    const std::int64_t thisBlock = std::min(remaining, budget);
    const std::streamsize bytes =
        static_cast<std::streamsize>(thisBlock) * static_cast<std::streamsize>(recSize);
    in.read(block.data(), bytes);
    if (!in)
      return fail("LAS import: '" + pathUtf8 + "' is truncated while reading point data");

    for (std::int64_t i = 0; i < thisBlock; ++i) {
      const char *rec = block.data() + static_cast<std::size_t>(i) * recSize;
      std::int32_t rawX, rawY, rawZ;
      std::memcpy(&rawX, rec + 0, 4);
      std::memcpy(&rawY, rec + 4, 4);
      std::memcpy(&rawZ, rec + 8, 4);
      std::uint16_t rawIntensity;
      std::memcpy(&rawIntensity, rec + 12, 2);

      chunk.pointsXyz.push_back(static_cast<double>(rawX) * header.scaleX + header.offsetX);
      chunk.pointsXyz.push_back(static_cast<double>(rawY) * header.scaleY + header.offsetY);
      chunk.pointsXyz.push_back(static_cast<double>(rawZ) * header.scaleZ + header.offsetZ);
      chunk.intensity.push_back(static_cast<float>(rawIntensity) / 65535.0f);
      if (hasColor) {
        std::uint16_t r, g, b;
        std::memcpy(&r, rec + colorOffset, 2);
        std::memcpy(&g, rec + colorOffset + 2, 2);
        std::memcpy(&b, rec + colorOffset + 4, 2);
        chunk.colorsRgb.push_back(static_cast<float>(r) / 65535.0f);
        chunk.colorsRgb.push_back(static_cast<float>(g) / 65535.0f);
        chunk.colorsRgb.push_back(static_cast<float>(b) / 65535.0f);
      }
    }
    remaining -= thisBlock;

    if (!onChunk(chunk)) return fail("point-cloud cache build aborted mid-stream");
    chunk.pointsXyz.clear();
    chunk.colorsRgb.clear();
    chunk.intensity.clear();
  }

  return true;
}

ReadResult ReadLasFile(const std::string &pathUtf8) {
  ReadResult out;
  std::string err;
  const bool ok = StreamLasFile(
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
    failure.errorMessage = "LAS import: '" + pathUtf8 + "' contains zero points";
    return failure;
  }
  out.ok = true;
  return out;
}

WriteResult WriteLasFile(const std::string &pathUtf8, const std::vector<double> &pointsXyz,
                          const std::vector<float> &colorsRgb,
                          const std::vector<float> &intensity) {
  WriteResult res;
  const std::int64_t n = static_cast<std::int64_t>(pointsXyz.size() / 3);
  if (n <= 0) {
    res.errorMessage = "LAS export: no points to write";
    return res;
  }
  const bool hasColor = !colorsRgb.empty();
  const bool hasIntensity = !intensity.empty();
  if (hasColor && colorsRgb.size() != static_cast<std::size_t>(n) * 3) {
    res.errorMessage = "LAS export: colour array size does not match the point count";
    return res;
  }
  if (hasIntensity && intensity.size() != static_cast<std::size_t>(n)) {
    res.errorMessage = "LAS export: intensity array size does not match the point count";
    return res;
  }
  if (static_cast<std::uint64_t>(n) > static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())) {
    res.errorMessage = "LAS export: too many points for this writer's 32-bit point-count field";
    return res;
  }

  // Scale/offset chosen from the actual data: offset is each axis's midpoint (minimizes the
  // largest |raw| value that has to fit in int32), scale is the finer of a fixed 0.0001
  // (sub-millimeter at any unit the drawing uses) precision target and whatever a larger axis
  // span actually requires to stay inside the int32 range.
  double minX = pointsXyz[0], maxX = pointsXyz[0];
  double minY = pointsXyz[1], maxY = pointsXyz[1];
  double minZ = pointsXyz[2], maxZ = pointsXyz[2];
  for (std::int64_t i = 0; i < n; ++i) {
    const double x = pointsXyz[static_cast<std::size_t>(i) * 3 + 0];
    const double y = pointsXyz[static_cast<std::size_t>(i) * 3 + 1];
    const double z = pointsXyz[static_cast<std::size_t>(i) * 3 + 2];
    minX = std::min(minX, x); maxX = std::max(maxX, x);
    minY = std::min(minY, y); maxY = std::max(maxY, y);
    minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
  }
  const double offsetX = (minX + maxX) * 0.5;
  const double offsetY = (minY + maxY) * 0.5;
  const double offsetZ = (minZ + maxZ) * 0.5;
  constexpr double kPrecisionTarget = 0.0001;
  constexpr double kInt32SafeRange = 2147483000.0;  // just under INT32_MAX, as a safety margin
  const auto axisScale = [&](double halfSpan) {
    return std::max(kPrecisionTarget, halfSpan / kInt32SafeRange);
  };
  const double scaleX = axisScale((maxX - minX) * 0.5);
  const double scaleY = axisScale((maxY - minY) * 0.5);
  const double scaleZ = axisScale((maxZ - minZ) * 0.5);

  const int format = hasColor ? 2 : 0;
  const std::size_t recordLength = MinRecordSizeForFormat(format);
  constexpr std::uint16_t kHeaderSize = static_cast<std::uint16_t>(kMinHeaderSize);
  const std::uint32_t offsetToPointData = kHeaderSize;

  std::vector<unsigned char> header(kHeaderSize, 0);
  std::memcpy(header.data() + 0, "LASF", 4);
  header[24] = 1;  // version major
  header[25] = 2;  // version minor — LAS 1.2 (REQ-172: in-tree reader covers 1.2/1.4, writer emits 1.2)
  const char *generatingSoftware = "GoSurvey";
  std::memcpy(header.data() + 58, generatingSoftware, std::strlen(generatingSoftware));
  WriteLE<std::uint16_t>(header, 94, kHeaderSize);
  WriteLE<std::uint32_t>(header, 96, offsetToPointData);
  WriteLE<std::uint32_t>(header, 100, 0u);  // number of VLRs
  header[104] = static_cast<unsigned char>(format);
  WriteLE<std::uint16_t>(header, 105, static_cast<std::uint16_t>(recordLength));
  WriteLE<std::uint32_t>(header, 107, static_cast<std::uint32_t>(n));
  // bytes 111-130: legacy number of points by return — left zero, not used by REQ-172's acceptance.
  WriteLE<double>(header, 131, scaleX);
  WriteLE<double>(header, 139, scaleY);
  WriteLE<double>(header, 147, scaleZ);
  WriteLE<double>(header, 155, offsetX);
  WriteLE<double>(header, 163, offsetY);
  WriteLE<double>(header, 171, offsetZ);
  WriteLE<double>(header, 179, maxX);
  WriteLE<double>(header, 187, minX);
  WriteLE<double>(header, 195, maxY);
  WriteLE<double>(header, 203, minY);
  WriteLE<double>(header, 211, maxZ);
  WriteLE<double>(header, 219, minZ);

  const std::filesystem::path path = std::filesystem::u8path(pathUtf8);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    res.errorMessage = "LAS export: could not create '" + pathUtf8 + "'";
    return res;
  }
  out.write(reinterpret_cast<const char *>(header.data()), static_cast<std::streamsize>(header.size()));

  bool rangeError = false;
  bool writeFailed = !out;
  constexpr std::int64_t kBlockPoints = 65536;
  std::vector<unsigned char> block(static_cast<std::size_t>(kBlockPoints) * recordLength);
  for (std::int64_t base = 0; base < n && !writeFailed && !rangeError; base += kBlockPoints) {
    const std::int64_t thisBlock = std::min<std::int64_t>(kBlockPoints, n - base);
    for (std::int64_t i = 0; i < thisBlock && !rangeError; ++i) {
      const std::int64_t srcIdx = base + i;
      const double x = pointsXyz[static_cast<std::size_t>(srcIdx) * 3 + 0];
      const double y = pointsXyz[static_cast<std::size_t>(srcIdx) * 3 + 1];
      const double z = pointsXyz[static_cast<std::size_t>(srcIdx) * 3 + 2];
      const double rawXd = static_cast<double>(std::llround((x - offsetX) / scaleX));
      const double rawYd = static_cast<double>(std::llround((y - offsetY) / scaleY));
      const double rawZd = static_cast<double>(std::llround((z - offsetZ) / scaleZ));
      if (std::abs(rawXd) > static_cast<double>((std::numeric_limits<std::int32_t>::max)()) ||
          std::abs(rawYd) > static_cast<double>((std::numeric_limits<std::int32_t>::max)()) ||
          std::abs(rawZd) > static_cast<double>((std::numeric_limits<std::int32_t>::max)())) {
        rangeError = true;
        break;
      }
      const std::int32_t rawX = static_cast<std::int32_t>(rawXd);
      const std::int32_t rawY = static_cast<std::int32_t>(rawYd);
      const std::int32_t rawZ = static_cast<std::int32_t>(rawZd);
      unsigned char *rec = block.data() + static_cast<std::size_t>(i) * recordLength;
      std::memset(rec, 0, recordLength);
      std::memcpy(rec + 0, &rawX, 4);
      std::memcpy(rec + 4, &rawY, 4);
      std::memcpy(rec + 8, &rawZ, 4);
      const float intenF = hasIntensity ? intensity[static_cast<std::size_t>(srcIdx)] : 0.0f;
      const std::uint16_t intenU16 = static_cast<std::uint16_t>(
          std::clamp(std::lround(static_cast<double>(intenF) * 65535.0), 0L, 65535L));
      std::memcpy(rec + 12, &intenU16, 2);
      if (hasColor) {
        const auto toU16 = [](float v) -> std::uint16_t {
          return static_cast<std::uint16_t>(
              std::clamp(std::lround(static_cast<double>(v) * 65535.0), 0L, 65535L));
        };
        const std::uint16_t r = toU16(colorsRgb[static_cast<std::size_t>(srcIdx) * 3 + 0]);
        const std::uint16_t g = toU16(colorsRgb[static_cast<std::size_t>(srcIdx) * 3 + 1]);
        const std::uint16_t b = toU16(colorsRgb[static_cast<std::size_t>(srcIdx) * 3 + 2]);
        std::memcpy(rec + 20, &r, 2);
        std::memcpy(rec + 22, &g, 2);
        std::memcpy(rec + 24, &b, 2);
      }
    }
    if (!rangeError) {
      out.write(reinterpret_cast<const char *>(block.data()),
                static_cast<std::streamsize>(thisBlock) * static_cast<std::streamsize>(recordLength));
      if (!out) writeFailed = true;
    }
  }

  out.flush();
  if (rangeError || writeFailed || !out) {
    res.errorMessage = rangeError ? "LAS export: a point's coordinate is out of range for this writer's "
                                    "int32 encoding"
                                  : "LAS export: disk write failed";
    out.close();
    std::error_code rmEc;
    std::filesystem::remove(path, rmEc);
    return res;
  }
  out.close();
  res.ok = true;
  return res;
}

}  // namespace pointcloud_las
