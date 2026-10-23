#include "io/CadPointCloudLaz.hpp"

#include "laszip_api.h"  // third_party/laszip/dll/laszip_api.h — vendored LASzip's public C API

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <system_error>

namespace pointcloud_laz {

namespace {

/// Minimum point-data-record byte size for point data record formats 0-3, same table
/// `CadPointCloudLas.cpp` uses — kept here too since this reader/writer never calls into the LAS
/// TU (the two formats' PRs are independent increments; see `CadPointCloudLaz.hpp`'s top note).
std::size_t MinRecordSizeForFormat(int format) {
  switch (format) {
    case 0: return 20;
    case 1: return 28;
    case 2: return 26;
    case 3: return 34;
    default: return static_cast<std::size_t>(-1);
  }
}

/// Always runs `laszip_destroy` on the handle `laszip_create` returned, whether the function
/// returns early on an error or falls through to a normal close — a raw `laszip_destroy` call at
/// every return site would be one more thing a future edit could forget.
struct LaszipHandleGuard {
  laszip_POINTER handle = nullptr;
  ~LaszipHandleGuard() {
    if (handle) laszip_destroy(handle);
  }
};

/// `laszip_get_error` hands back a pointer into the handle's own internal buffer (overwritten by
/// the next API call) — copied into a `std::string` immediately, never retained past this call.
std::string LaszipErrorOr(laszip_POINTER handle, const std::string &fallback) {
  laszip_CHAR *err = nullptr;
  if (handle && laszip_get_error(handle, &err) == 0 && err && err[0] != '\0')
    return std::string(err);
  return fallback;
}

}  // namespace

bool StreamLazFile(const std::string &pathUtf8, std::int64_t chunkPointBudget,
                    const std::function<bool(const PointChunk &)> &onChunk,
                    std::string *errorMessage) {
  const auto fail = [&](const std::string &msg) {
    if (errorMessage) *errorMessage = msg;
    return false;
  };
  if (pathUtf8.empty()) return fail("LAZ import: empty file path");

  laszip_POINTER reader = nullptr;
  if (laszip_create(&reader) != 0 || reader == nullptr)
    return fail("LAZ import: could not create a LASzip reader");
  LaszipHandleGuard guard{reader};

  laszip_BOOL isCompressed = 0;
  if (laszip_open_reader(reader, pathUtf8.c_str(), &isCompressed) != 0)
    return fail("LAZ import: " + LaszipErrorOr(reader, "could not open '" + pathUtf8 + "'"));

  laszip_header_struct *header = nullptr;
  if (laszip_get_header_pointer(reader, &header) != 0 || header == nullptr)
    return fail("LAZ import: could not read '" + pathUtf8 + "'s header");

  const int format = static_cast<int>(header->point_data_format);
  if (format > 10) {
    laszip_close_reader(reader);
    return fail("LAZ import: '" + pathUtf8 + "' uses point data record format " +
                std::to_string(format) +
                ", which this reader does not support (only formats 0-10 are supported)");
  }

  const std::int64_t pointCount = header->number_of_point_records != 0
                                       ? static_cast<std::int64_t>(header->number_of_point_records)
                                       : static_cast<std::int64_t>(header->extended_number_of_point_records);
  if (pointCount < 0) {
    laszip_close_reader(reader);
    return fail("LAZ import: '" + pathUtf8 + "' declares an invalid point count");
  }

  laszip_point_struct *point = nullptr;
  if (laszip_get_point_pointer(reader, &point) != 0 || point == nullptr) {
    laszip_close_reader(reader);
    return fail("LAZ import: could not read '" + pathUtf8 + "'s point structure");
  }

  // Formats 2/3/5/7/8/10 carry RGB; the rest (0/1/4/6/9) do not. GPS time (1/3/4/5/6-10), NIR
  // (8/10), and wave packets (4/5/9/10) are decoded by LASzip into `point` but discarded here —
  // REQ-171's cloud schema has no field for them, same treatment formats 1/3's GPS time always
  // got.
  const bool hasColor = (format == 2 || format == 3 || format == 5 || format == 7 ||
                         format == 8 || format == 10);
  const double scaleX = header->x_scale_factor, scaleY = header->y_scale_factor,
               scaleZ = header->z_scale_factor;
  const double offsetX = header->x_offset, offsetY = header->y_offset, offsetZ = header->z_offset;

  const std::int64_t budget = std::max<std::int64_t>(1, chunkPointBudget);
  const std::int64_t reserveN = std::max<std::int64_t>(0, std::min(budget, pointCount));
  PointChunk chunk;
  chunk.pointsXyz.reserve(static_cast<std::size_t>(reserveN) * 3);
  if (hasColor) chunk.colorsRgb.reserve(static_cast<std::size_t>(reserveN) * 3);
  chunk.intensity.reserve(static_cast<std::size_t>(reserveN));

  for (std::int64_t i = 0; i < pointCount; ++i) {
    if (laszip_read_point(reader) != 0) {
      const std::string msg = "LAZ import: '" + pathUtf8 +
                               "' is truncated or corrupt while reading point data (" +
                               LaszipErrorOr(reader, "decompression failed") + ")";
      laszip_close_reader(reader);
      return fail(msg);
    }

    chunk.pointsXyz.push_back(static_cast<double>(point->X) * scaleX + offsetX);
    chunk.pointsXyz.push_back(static_cast<double>(point->Y) * scaleY + offsetY);
    chunk.pointsXyz.push_back(static_cast<double>(point->Z) * scaleZ + offsetZ);
    chunk.intensity.push_back(static_cast<float>(point->intensity) / 65535.0f);
    if (hasColor) {
      chunk.colorsRgb.push_back(static_cast<float>(point->rgb[0]) / 65535.0f);
      chunk.colorsRgb.push_back(static_cast<float>(point->rgb[1]) / 65535.0f);
      chunk.colorsRgb.push_back(static_cast<float>(point->rgb[2]) / 65535.0f);
    }

    const bool chunkFull = static_cast<std::int64_t>(chunk.pointsXyz.size() / 3) >= budget;
    const bool isLast = (i == pointCount - 1);
    if (chunkFull || isLast) {
      if (!chunk.pointsXyz.empty() && !onChunk(chunk)) {
        laszip_close_reader(reader);
        return fail("point-cloud cache build aborted mid-stream");
      }
      chunk.pointsXyz.clear();
      chunk.colorsRgb.clear();
      chunk.intensity.clear();
    }
  }

  if (laszip_close_reader(reader) != 0)
    return fail("LAZ import: " + LaszipErrorOr(reader, "could not close '" + pathUtf8 + "'"));

  return true;
}

std::int64_t QuickPointCountEstimate(const std::string &pathUtf8) {
  if (pathUtf8.empty()) return 0;

  laszip_POINTER reader = nullptr;
  if (laszip_create(&reader) != 0 || reader == nullptr) return 0;
  LaszipHandleGuard guard{reader};

  laszip_BOOL isCompressed = 0;
  if (laszip_open_reader(reader, pathUtf8.c_str(), &isCompressed) != 0) return 0;

  laszip_header_struct *header = nullptr;
  if (laszip_get_header_pointer(reader, &header) != 0 || header == nullptr) {
    laszip_close_reader(reader);
    return 0;
  }
  const std::int64_t pointCount = header->number_of_point_records != 0
                                       ? static_cast<std::int64_t>(header->number_of_point_records)
                                       : static_cast<std::int64_t>(header->extended_number_of_point_records);
  laszip_close_reader(reader);
  return pointCount > 0 ? pointCount : 0;
}

ReadResult ReadLazFile(const std::string &pathUtf8) {
  ReadResult out;
  std::string err;
  const bool ok = StreamLazFile(
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
    failure.errorMessage = "LAZ import: '" + pathUtf8 + "' contains zero points";
    return failure;
  }
  out.ok = true;
  return out;
}

WriteResult WriteLazFile(const std::string &pathUtf8, const std::vector<double> &pointsXyz,
                          const std::vector<float> &colorsRgb,
                          const std::vector<float> &intensity) {
  WriteResult res;
  const std::int64_t n = static_cast<std::int64_t>(pointsXyz.size() / 3);
  if (n <= 0) {
    res.errorMessage = "LAZ export: no points to write";
    return res;
  }
  const bool hasColor = !colorsRgb.empty();
  const bool hasIntensity = !intensity.empty();
  if (hasColor && colorsRgb.size() != static_cast<std::size_t>(n) * 3) {
    res.errorMessage = "LAZ export: colour array size does not match the point count";
    return res;
  }
  if (hasIntensity && intensity.size() != static_cast<std::size_t>(n)) {
    res.errorMessage = "LAZ export: intensity array size does not match the point count";
    return res;
  }
  if (static_cast<std::uint64_t>(n) >
      static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())) {
    res.errorMessage = "LAZ export: too many points for this writer's 32-bit point-count field";
    return res;
  }

  // Scale/offset chosen from the actual data — identical formula to `WriteLasFile`'s (see
  // CadPointCloudLaz.hpp's top note on why this matters for the LAS/LAZ cross-check).
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
  constexpr laszip_U16 kHeaderSize = 227;  // LAS 1.2 Public Header Block size

  laszip_POINTER writer = nullptr;
  if (laszip_create(&writer) != 0 || writer == nullptr) {
    res.errorMessage = "LAZ export: could not create a LASzip writer";
    return res;
  }
  LaszipHandleGuard guard{writer};

  laszip_header_struct header{};
  header.version_major = 1;
  header.version_minor = 2;
  header.header_size = kHeaderSize;
  // laszip_write_header adds the LASzip VLR's own byte size to offset_to_point_data (and bumps
  // number_of_variable_length_records by 1) itself when compress=true — this must be the value
  // WITHOUT that VLR, i.e. header_size + this writer's own (zero) VLRs (see
  // third_party/laszip/VENDORED.md's build note / laszip_dll.cpp's laszip_prepare_vlrs_for_write).
  header.offset_to_point_data = kHeaderSize;
  header.number_of_variable_length_records = 0;
  header.point_data_format = static_cast<laszip_U8>(format);
  header.point_data_record_length = static_cast<laszip_U16>(recordLength);
  header.number_of_point_records = static_cast<laszip_U32>(n);
  header.x_scale_factor = scaleX;
  header.y_scale_factor = scaleY;
  header.z_scale_factor = scaleZ;
  header.x_offset = offsetX;
  header.y_offset = offsetY;
  header.z_offset = offsetZ;
  header.max_x = maxX; header.min_x = minX;
  header.max_y = maxY; header.min_y = minY;
  header.max_z = maxZ; header.min_z = minZ;
  std::memcpy(header.generating_software, "GoSurvey", 8);

  if (laszip_set_header(writer, &header) != 0) {
    res.errorMessage = "LAZ export: " + LaszipErrorOr(writer, "could not set the LAS header");
    return res;
  }

  if (laszip_open_writer(writer, pathUtf8.c_str(), /*compress=*/1) != 0) {
    res.errorMessage = "LAZ export: " + LaszipErrorOr(writer, "could not create '" + pathUtf8 + "'");
    return res;
  }

  laszip_point_struct *point = nullptr;
  if (laszip_get_point_pointer(writer, &point) != 0 || point == nullptr) {
    laszip_close_writer(writer);
    std::error_code rmEc;
    std::filesystem::remove(std::filesystem::u8path(pathUtf8), rmEc);
    res.errorMessage = "LAZ export: could not access the point buffer";
    return res;
  }

  bool rangeError = false;
  bool writeFailed = false;
  for (std::int64_t i = 0; i < n && !rangeError && !writeFailed; ++i) {
    const double x = pointsXyz[static_cast<std::size_t>(i) * 3 + 0];
    const double y = pointsXyz[static_cast<std::size_t>(i) * 3 + 1];
    const double z = pointsXyz[static_cast<std::size_t>(i) * 3 + 2];
    const double rawXd = static_cast<double>(std::llround((x - offsetX) / scaleX));
    const double rawYd = static_cast<double>(std::llround((y - offsetY) / scaleY));
    const double rawZd = static_cast<double>(std::llround((z - offsetZ) / scaleZ));
    if (std::abs(rawXd) > static_cast<double>((std::numeric_limits<std::int32_t>::max)()) ||
        std::abs(rawYd) > static_cast<double>((std::numeric_limits<std::int32_t>::max)()) ||
        std::abs(rawZd) > static_cast<double>((std::numeric_limits<std::int32_t>::max)())) {
      rangeError = true;
      break;
    }

    std::memset(point, 0, sizeof(*point));
    point->X = static_cast<laszip_I32>(rawXd);
    point->Y = static_cast<laszip_I32>(rawYd);
    point->Z = static_cast<laszip_I32>(rawZd);
    const float intenF = hasIntensity ? intensity[static_cast<std::size_t>(i)] : 0.0f;
    point->intensity = static_cast<laszip_U16>(
        std::clamp(std::lround(static_cast<double>(intenF) * 65535.0), 0L, 65535L));
    if (hasColor) {
      const auto toU16 = [](float v) -> laszip_U16 {
        return static_cast<laszip_U16>(
            std::clamp(std::lround(static_cast<double>(v) * 65535.0), 0L, 65535L));
      };
      point->rgb[0] = toU16(colorsRgb[static_cast<std::size_t>(i) * 3 + 0]);
      point->rgb[1] = toU16(colorsRgb[static_cast<std::size_t>(i) * 3 + 1]);
      point->rgb[2] = toU16(colorsRgb[static_cast<std::size_t>(i) * 3 + 2]);
    }

    if (laszip_write_point(writer) != 0) {
      writeFailed = true;
      break;
    }
  }

  if (rangeError || writeFailed) {
    const std::string msg = rangeError
        ? "LAZ export: a point's coordinate is out of range for this writer's int32 encoding"
        : ("LAZ export: " + LaszipErrorOr(writer, "disk write failed"));
    laszip_close_writer(writer);
    std::error_code rmEc;
    std::filesystem::remove(std::filesystem::u8path(pathUtf8), rmEc);
    res.errorMessage = msg;
    return res;
  }

  if (laszip_close_writer(writer) != 0) {
    const std::string msg =
        "LAZ export: " + LaszipErrorOr(writer, "could not finish writing '" + pathUtf8 + "'");
    std::error_code rmEc;
    std::filesystem::remove(std::filesystem::u8path(pathUtf8), rmEc);
    res.errorMessage = msg;
    return res;
  }

  res.ok = true;
  return res;
}

}  // namespace pointcloud_laz
