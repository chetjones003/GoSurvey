#include "io/CadPointCloudE57.hpp"

#include <E57SimpleData.h>
#include <E57SimpleReader.h>

#include <algorithm>
#include <cstdint>

namespace pointcloud_e57 {

namespace {

/// Appends one scan's points into the accumulating result, converting E57's per-scan buffers into
/// GoSurvey's flat interleaved layout. Points E57 itself marks invalid (`cartesianInvalidState`
/// or `isColorInvalid`/`isIntensityInvalid`) are skipped rather than stored as zeros — a silent
/// zero would be a wrong coordinate, not a missing one (REQ-201).
void AppendScan(e57::Reader &reader, std::int64_t scanIndex, e57::Data3D &header,
                 ReadResult &out) {
  if (header.pointCount == 0) return;

  e57::Data3DPointsData_t<double> buffers(header);
  e57::CompressedVectorReader vectorReader =
      reader.SetUpData3DPointsData(scanIndex, header.pointCount, buffers);

  const bool hasColor =
      header.pointFields.colorRedField && header.pointFields.colorGreenField &&
      header.pointFields.colorBlueField;
  const bool hasIntensity = header.pointFields.intensityField;

  std::size_t readOffset = 0;
  unsigned readCount = 0;
  while ((readCount = vectorReader.read()) > 0) {
    for (unsigned i = 0; i < readCount; ++i) {
      const std::size_t idx = readOffset + i;
      const bool cartesianValid =
          !header.pointFields.cartesianInvalidStateField || buffers.cartesianInvalidState[idx] == 0;
      if (!cartesianValid) continue;

      out.pointsXyz.push_back(buffers.cartesianX[idx]);
      out.pointsXyz.push_back(buffers.cartesianY[idx]);
      out.pointsXyz.push_back(buffers.cartesianZ[idx]);

      if (hasColor) {
        const bool colorValid =
            !header.pointFields.isColorInvalidField || buffers.isColorInvalid[idx] == 0;
        if (colorValid) {
          out.colorsRgb.push_back(static_cast<float>(buffers.colorRed[idx]) / 255.0f);
          out.colorsRgb.push_back(static_cast<float>(buffers.colorGreen[idx]) / 255.0f);
          out.colorsRgb.push_back(static_cast<float>(buffers.colorBlue[idx]) / 255.0f);
        } else {
          out.colorsRgb.push_back(0.0f);
          out.colorsRgb.push_back(0.0f);
          out.colorsRgb.push_back(0.0f);
        }
      }
      if (hasIntensity) {
        const bool intensityValid =
            !header.pointFields.isIntensityInvalidField || buffers.isIntensityInvalid[idx] == 0;
        out.intensity.push_back(intensityValid ? static_cast<float>(buffers.intensity[idx]) : 0.0f);
      }
    }
    readOffset += readCount;
  }
  vectorReader.close();
}

/// Streams one scan through `onChunk` in bounded-size pieces. Unlike `AppendScan` above, the E57
/// read buffers themselves are sized to `chunkPointBudget`, not `header.pointCount` — this is the
/// one line that actually bounds memory (`Data3DPointsData_t(header)` would allocate a buffer for
/// every point in the scan up front, which is the exact behaviour `ReadE57File`/`AppendScan` above
/// accept and `StreamE57File` exists to avoid).
///
/// Returns `false` (via `okOut`) if `onChunk` aborts the stream (its own error, e.g. a disk write
/// failure downstream) or a read fails; the caller distinguishes these by checking `*errorMessage`.
bool StreamScan(e57::Reader &reader, std::int64_t scanIndex, e57::Data3D &header,
                 std::int64_t chunkPointBudget, const std::function<bool(const PointChunk &)> &onChunk,
                 std::string *errorMessage) {
  if (header.pointCount == 0) return true;

  const bool hasColor = header.pointFields.colorRedField && header.pointFields.colorGreenField &&
                        header.pointFields.colorBlueField;
  const bool hasIntensity = header.pointFields.intensityField;
  const bool hasCartesianInvalid = header.pointFields.cartesianInvalidStateField;
  const bool hasColorInvalid = header.pointFields.isColorInvalidField;
  const bool hasIntensityInvalid = header.pointFields.isIntensityInvalidField;

  const std::size_t bufCap = static_cast<std::size_t>(std::max<std::int64_t>(1, chunkPointBudget));
  std::vector<double> bx(bufCap), by(bufCap), bz(bufCap), bIntensity(hasIntensity ? bufCap : 0);
  std::vector<std::int8_t> bCartInvalid(hasCartesianInvalid ? bufCap : 0);
  std::vector<std::int8_t> bColorInvalid(hasColorInvalid ? bufCap : 0);
  std::vector<std::int8_t> bIntensityInvalid(hasIntensityInvalid ? bufCap : 0);
  std::vector<std::uint16_t> bRed(hasColor ? bufCap : 0), bGreen(hasColor ? bufCap : 0),
      bBlue(hasColor ? bufCap : 0);

  e57::Data3DPointsData_t<double> buffers;
  buffers.cartesianX = bx.data();
  buffers.cartesianY = by.data();
  buffers.cartesianZ = bz.data();
  if (hasCartesianInvalid) buffers.cartesianInvalidState = bCartInvalid.data();
  if (hasIntensity) buffers.intensity = bIntensity.data();
  if (hasIntensityInvalid) buffers.isIntensityInvalid = bIntensityInvalid.data();
  if (hasColor) {
    buffers.colorRed = bRed.data();
    buffers.colorGreen = bGreen.data();
    buffers.colorBlue = bBlue.data();
  }
  if (hasColorInvalid) buffers.isColorInvalid = bColorInvalid.data();

  e57::CompressedVectorReader vectorReader = reader.SetUpData3DPointsData(scanIndex, bufCap, buffers);

  PointChunk chunk;
  unsigned readCount = 0;
  bool aborted = false;
  while (!aborted && (readCount = vectorReader.read()) > 0) {
    for (unsigned i = 0; i < readCount; ++i) {
      const bool cartesianValid = !hasCartesianInvalid || bCartInvalid[i] == 0;
      if (!cartesianValid) continue;

      chunk.pointsXyz.push_back(bx[i]);
      chunk.pointsXyz.push_back(by[i]);
      chunk.pointsXyz.push_back(bz[i]);

      if (hasColor) {
        const bool colorValid = !hasColorInvalid || bColorInvalid[i] == 0;
        if (colorValid) {
          chunk.colorsRgb.push_back(static_cast<float>(bRed[i]) / 255.0f);
          chunk.colorsRgb.push_back(static_cast<float>(bGreen[i]) / 255.0f);
          chunk.colorsRgb.push_back(static_cast<float>(bBlue[i]) / 255.0f);
        } else {
          chunk.colorsRgb.insert(chunk.colorsRgb.end(), 3, 0.0f);
        }
      }
      if (hasIntensity) {
        const bool intensityValid = !hasIntensityInvalid || bIntensityInvalid[i] == 0;
        chunk.intensity.push_back(intensityValid ? static_cast<float>(bIntensity[i]) : 0.0f);
      }
    }
    if (!chunk.pointsXyz.empty()) {
      if (!onChunk(chunk)) {
        aborted = true;
        if (errorMessage) *errorMessage = "point-cloud cache build aborted mid-stream";
      }
      chunk.pointsXyz.clear();
      chunk.colorsRgb.clear();
      chunk.intensity.clear();
    }
  }
  vectorReader.close();
  return !aborted;
}

}  // namespace

std::int64_t QuickPointCountEstimate(const std::string &pathUtf8) {
  if (pathUtf8.empty()) return 0;
  try {
    e57::Reader reader(pathUtf8, e57::ReaderOptions{});
    if (!reader.IsOpen()) return 0;
    const std::int64_t scanCount = reader.GetData3DCount();
    std::int64_t total = 0;
    for (std::int64_t scan = 0; scan < scanCount; ++scan) {
      e57::Data3D header;
      if (reader.ReadData3D(scan, header)) total += static_cast<std::int64_t>(header.pointCount);
    }
    reader.Close();
    return total;
  } catch (...) {
    return 0;
  }
}

bool StreamE57File(const std::string &pathUtf8, std::int64_t chunkPointBudget,
                    const std::function<bool(const PointChunk &)> &onChunk,
                    std::string *errorMessage) {
  const auto fail = [&](const std::string &msg) {
    if (errorMessage) *errorMessage = msg;
    return false;
  };
  if (pathUtf8.empty()) return fail("E57 import: empty file path");

  try {
    e57::Reader reader(pathUtf8, e57::ReaderOptions{});
    if (!reader.IsOpen()) return fail("E57 import: could not open '" + pathUtf8 + "'");

    const std::int64_t scanCount = reader.GetData3DCount();
    if (scanCount <= 0) {
      reader.Close();
      return fail("E57 import: '" + pathUtf8 + "' contains no Data3D scans");
    }

    for (std::int64_t scan = 0; scan < scanCount; ++scan) {
      e57::Data3D header;
      if (!reader.ReadData3D(scan, header)) {
        reader.Close();
        return fail("E57 import: '" + pathUtf8 + "' scan " + std::to_string(scan) +
                    " header unreadable");
      }
      if (!StreamScan(reader, scan, header, chunkPointBudget, onChunk, errorMessage)) {
        reader.Close();
        return false;
      }
    }
    reader.Close();
    return true;
  } catch (const e57::E57Exception &ex) {
    return fail("E57 import: '" + pathUtf8 + "' " + std::string(ex.what()));
  } catch (const std::exception &ex) {
    return fail("E57 import: '" + pathUtf8 + "' " + std::string(ex.what()));
  }
}

ReadResult ReadE57File(const std::string &pathUtf8) {
  ReadResult out;
  if (pathUtf8.empty()) {
    out.errorMessage = "E57 import: empty file path";
    return out;
  }

  try {
    e57::Reader reader(pathUtf8, e57::ReaderOptions{});
    if (!reader.IsOpen()) {
      out.errorMessage = "E57 import: could not open '" + pathUtf8 + "'";
      return out;
    }

    const std::int64_t scanCount = reader.GetData3DCount();
    if (scanCount <= 0) {
      out.errorMessage = "E57 import: '" + pathUtf8 + "' contains no Data3D scans";
      reader.Close();
      return out;
    }

    for (std::int64_t scan = 0; scan < scanCount; ++scan) {
      e57::Data3D header;
      if (!reader.ReadData3D(scan, header)) {
        out.errorMessage =
            "E57 import: '" + pathUtf8 + "' scan " + std::to_string(scan) + " header unreadable";
        reader.Close();
        out.pointsXyz.clear();
        out.colorsRgb.clear();
        out.intensity.clear();
        return out;
      }
      AppendScan(reader, scan, header, out);
    }
    reader.Close();

    if (out.pointsXyz.empty()) {
      out.errorMessage = "E57 import: '" + pathUtf8 + "' contains zero valid points";
      return out;
    }

    out.ok = true;
    return out;
  } catch (const e57::E57Exception &ex) {
    ReadResult failure;
    failure.errorMessage = "E57 import: '" + pathUtf8 + "' " + ex.what();
    return failure;
  } catch (const std::exception &ex) {
    ReadResult failure;
    failure.errorMessage = "E57 import: '" + pathUtf8 + "' " + ex.what();
    return failure;
  }
}

}  // namespace pointcloud_e57
