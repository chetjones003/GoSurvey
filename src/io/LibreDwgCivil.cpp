#include "LibreDwgCivil.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace libredwgcivil {
namespace {

bool ReadU32(const unsigned char* bytes, std::size_t size, std::size_t offset, uint32_t* value) {
  if (bytes == nullptr || value == nullptr || offset > size || size - offset < 4)
    return false;
  *value = static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
  return true;
}

bool ReadDouble(const unsigned char* bytes, std::size_t size, std::size_t offset, double* value) {
  if (bytes == nullptr || value == nullptr || offset > size || size - offset < sizeof(double))
    return false;
  std::memcpy(value, bytes + offset, sizeof(*value));
  return std::isfinite(*value);
}

bool ReadBits(const unsigned char* bytes, std::size_t bitCount, std::size_t offset,
              unsigned bitLength, uint32_t* value) {
  if (bytes == nullptr || value == nullptr || offset > bitCount ||
      bitLength > bitCount - offset || bitLength > 32)
    return false;
  uint32_t result = 0;
  for (unsigned bit = 0; bit < bitLength; ++bit) {
    const std::size_t sourceBit = offset + bit;
    result = (result << 1) | ((bytes[sourceBit / 8] >> (7 - sourceBit % 8)) & 1u);
  }
  *value = result;
  return true;
}

class Cursor {
 public:
  Cursor(const unsigned char* bytes, std::size_t count, std::size_t start = 0)
      : position(start), bytes_(bytes), count_(count) {}
  bool Read(unsigned length, uint32_t* value) {
    if (!ReadBits(bytes_, count_, position, length, value)) return false;
    position += length;
    return true;
  }
  bool Short(uint32_t* value) { return Integer(2, true, value); }
  bool Long(uint32_t* value) { return Integer(4, false, value); }
  bool Double(double* value) {
    uint32_t code = 0;
    if (!Read(2, &code)) return false;
    if (code == 1 || code == 2) { *value = code == 1 ? 1.0 : 0.0; return true; }
    if (code != 0) return false;
    uint64_t encoded = 0;
    for (unsigned i = 0; i < 8; ++i) {
      uint32_t octet = 0;
      if (!Read(8, &octet)) return false;
      encoded |= static_cast<uint64_t>(octet) << (i * 8);
    }
    std::memcpy(value, &encoded, sizeof(encoded));
    return std::isfinite(*value);
  }
  bool Text(std::u16string* value) {
    uint32_t length = 0;
    if (!Short(&length) || position > count_ || length > (count_ - position) / 16) return false;
    value->clear();
    value->reserve(length);
    for (uint32_t i = 0; i < length; ++i) {
      uint32_t low = 0, high = 0;
      if (!Read(8, &low) || !Read(8, &high)) return false;
      const auto character = static_cast<char16_t>(low | (high << 8));
      if (character == 0) return false;
      value->push_back(character);
    }
    // Reject malformed UTF-16 rather than silently replacing a drawing-owned style name.
    for (std::size_t i = 0; i < value->size(); ++i) {
      const char16_t character = (*value)[i];
      if (character >= 0xdc00 && character <= 0xdfff) return false;
      if (character >= 0xd800 && character <= 0xdbff) {
        if (++i == value->size() || (*value)[i] < 0xdc00 || (*value)[i] > 0xdfff) return false;
      }
    }
    return true;
  }
  std::size_t position;
 private:
  bool Integer(unsigned octets, bool shortValue, uint32_t* value) {
    uint32_t code = 0;
    if (!Read(2, &code)) return false;
    if (code == 2) { *value = 0; return true; }
    if (code == 1) return Read(8, value);
    if (code == 3) { *value = 256; return shortValue; }
    *value = 0;
    for (unsigned i = 0; i < octets; ++i) {
      uint32_t octet = 0;
      if (!Read(8, &octet)) return false;
      *value |= octet << (i * 8);
    }
    return true;
  }
  const unsigned char* bytes_;
  std::size_t count_;
};

}  // namespace

std::optional<PointStyleRecord> DecodePointStyle(const unsigned char* bytes, std::size_t bitCount,
                                               std::size_t stringStart, std::size_t stringBitCount) {
  if (bytes == nullptr || stringStart > bitCount || stringBitCount > bitCount - stringStart)
    return std::nullopt;
  Cursor data(bytes, stringStart);
  uint32_t version = 0, nameToken = 0;
  if (!data.Short(&version) || version != 1214 || !data.Long(&nameToken)) return std::nullopt;
  PointStyleRecord result;
  if (nameToken == 30) {
    result.name = u"Standard";  // Revision 1214's predefined Standard style-name token.
  } else if ((nameToken & 0x80000000u) != 0) {
    Cursor strings(bytes, stringStart + stringBitCount, stringStart);
    if (!strings.Text(&result.name) || result.name.empty()) return std::nullopt;
  } else {
    return std::nullopt;
  }
  // The six display components in this revision carry this repeated main-stream header. Locate
  // only bounded records in the main stream, require the known common-style prefix and exactly
  // six components, then validate each typed field and the complete geometry suffix. This also
  // rejects the collection object, which shares AECC_POINT_STYLE with its leaf style records.
  constexpr uint32_t componentHeader = 0x54150950;
  if (data.position > stringStart || stringStart - data.position < 336) return std::nullopt;
  const std::size_t firstComponent = data.position + 336;
  std::array<std::size_t, 6> components{};
  std::size_t count = 0;
  for (std::size_t bit = firstComponent - 32; bit <= stringStart && stringStart - bit >= 32; ++bit) {
    uint32_t header = 0;
    if (!ReadBits(bytes, stringStart, bit, 32, &header)) return std::nullopt;
    if (header != componentHeader) continue;
    if (count == components.size()) return std::nullopt;
    components[count++] = bit + 32;
  }
  if (count != components.size() || components.front() != firstComponent) return std::nullopt;
  for (std::size_t i = 0; i < components.size(); ++i) {
    data.position = components[i];
    uint32_t visible = 0, index = 0, flags = 0, weight = 0;
    auto& component = result.components[i];
    if (!data.Read(1, &visible) || !data.Short(&index) || index != 0 ||
        !data.Long(&component.color) || (component.color >> 24) < 0xc0 ||
        (component.color >> 24) > 0xc8 || !data.Read(8, &flags) || flags != 0 ||
        !data.Short(&weight) || !data.Double(&component.linetypeScale) ||
        component.linetypeScale <= 0.0)
      return std::nullopt;
    component.visible = visible != 0;
    component.lineweight = static_cast<int16_t>(static_cast<uint16_t>(weight));
    if (i + 1 < components.size() && data.position > components[i + 1] - 32)
      return std::nullopt;
  }
  if (data.position > stringStart || stringStart - data.position < 54) return std::nullopt;
  data.position += 54;
  uint32_t reserved = 0, marker = 0;
  if (!data.Short(&reserved) || reserved != 0 || !data.Short(&marker) ||
      (marker & ~0x7fu) != 0 || (marker & 0x1fu) > 4 ||
      !data.Double(&result.rotation) || !data.Double(&result.size) || result.size <= 0.0 ||
      !data.Read(8, &result.sizeType) || result.sizeType > 3 ||
      !data.Read(8, &result.markerType) || result.markerType > 2)
    return std::nullopt;
  result.customMarker = marker & 0x1fu;
  result.markerOverlay = marker >> 5;
  for (double& scale : result.fixedScale)
    if (!data.Double(&scale)) return std::nullopt;
  if (!data.Read(8, &reserved) || reserved != 9 ||
      !data.Short(&result.orientation) || result.orientation > 2 ||
      !data.Read(8, &result.display3dType) || result.display3dType > 2 ||
      !data.Double(&result.elevation) || !data.Double(&result.scaleFactor) ||
      result.scaleFactor <= 0.0 || data.position != stringStart)
    return std::nullopt;
  return result;
}

std::optional<std::vector<uint64_t>> DecodeHandles(const unsigned char* bytes, std::size_t bitCount,
    std::size_t start, std::size_t count, uint64_t objectHandle) {
  if (bytes == nullptr || start > bitCount || count > (bitCount - start) / 8) return std::nullopt;
  Cursor data(bytes, bitCount, start);
  std::vector<uint64_t> handles;
  handles.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    uint32_t code = 0, size = 0;
    if (!data.Read(4, &code) || !data.Read(4, &size) || size > 8) return std::nullopt;
    uint64_t value = 0;
    for (uint32_t j = 0; j < size; ++j) {
      uint32_t octet = 0;
      if (!data.Read(8, &octet)) return std::nullopt;
      value = (value << 8) | octet;
    }
    if (code == 6) value = 1;
    if (code == 8) value = 1;
    if (code == 6 || code == 10) {
      if (value > (std::numeric_limits<uint64_t>::max)() - objectHandle) return std::nullopt;
      value += objectHandle;
    } else if (code == 8 || code == 12) {
      if (value > objectHandle) return std::nullopt;
      value = objectHandle - value;
    } else if (code > 5) {
      return std::nullopt;
    }
    handles.push_back(value);
  }
  return handles;
}

static std::optional<std::vector<PointLocation>> DecodePointNodeLocationsImpl(
    const unsigned char* bytes, std::size_t bitCount, std::size_t* end) {
  // Both supplied Civil drawings use this 35-bit revision prefix. The following BL is the
  // record count. Each record contains a BL coordinate flag, three BD coordinates, its BL
  // point number and an 18-bit record suffix. Do not guess at other revisions or suffixes.
  uint32_t signature = 0, revision = 0;
  if (!ReadBits(bytes, bitCount, 0, 16, &signature) || signature != 0x2f81 ||
      !ReadBits(bytes, bitCount, 16, 19, &revision) || revision != 0x9152)
    return std::nullopt;
  std::size_t position = 35;
  auto read = [&](unsigned length, uint32_t* value) {
    if (!ReadBits(bytes, bitCount, position, length, value)) return false;
    position += length;
    return true;
  };
  auto compactLong = [&](uint32_t* value) {
    uint32_t code = 0;
    if (!read(2, &code)) return false;
    if (code == 2) { *value = 0; return true; }
    if (code == 1) return read(8, value);
    if (code != 0) return false;
    *value = 0;
    for (unsigned i = 0; i < 4; ++i) {
      uint32_t octet = 0;
      if (!read(8, &octet)) return false;
      *value |= octet << (i * 8);
    }
    return true;
  };
  auto compactDouble = [&](double* value) {
    uint32_t code = 0;
    if (!read(2, &code)) return false;
    if (code == 1 || code == 2) { *value = code == 1 ? 1.0 : 0.0; return true; }
    if (code != 0) return false;
    uint64_t encoded = 0;
    for (unsigned i = 0; i < 8; ++i) {
      uint32_t octet = 0;
      if (!read(8, &octet)) return false;
      encoded |= static_cast<uint64_t>(octet) << (i * 8);
    }
    std::memcpy(value, &encoded, sizeof(encoded));
    return std::isfinite(*value);
  };
  uint32_t count = 0;
  if (!compactLong(&count) || count > (bitCount - position) / 34)
    return std::nullopt;
  std::vector<PointLocation> points;
  points.reserve(count);
  std::unordered_set<int> identities;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t flag = 0, number = 0, suffix = 0;
    PointLocation point;
    if (!compactLong(&flag) || flag != 1 || !compactDouble(&point.location.x) ||
        !compactDouble(&point.location.y) || !compactDouble(&point.location.z) ||
        !compactLong(&number) || number == 0 ||
        number > static_cast<uint32_t>((std::numeric_limits<int>::max)()) ||
        !read(18, &suffix) || suffix != 0x2004)
      return std::nullopt;
    point.number = static_cast<int>(number);
    if (!identities.insert(point.number).second) return std::nullopt;
    points.push_back(point);
  }
  if (end != nullptr) *end = position;
  return points;
}

std::optional<std::vector<PointLocation>> DecodePointNodeLocations(
    const unsigned char* bytes, std::size_t bitCount) {
  return DecodePointNodeLocationsImpl(bytes, bitCount, nullptr);
}

std::optional<std::vector<std::pair<int, uint64_t>>> DecodePointNodeMarkerStyles(
    const unsigned char* bytes, std::size_t bitCount, std::size_t handleStart,
    std::size_t commonHandles, uint64_t objectHandle) {
  std::size_t position = 0;
  const auto points = DecodePointNodeLocationsImpl(bytes, bitCount, &position);
  if (!points.has_value() || handleStart > bitCount || position > handleStart) return std::nullopt;
  Cursor data(bytes, handleStart, position);
  uint32_t count = 0;
  if (!data.Long(&count) || count > points->size()) return std::nullopt;
  std::unordered_set<int> identities;
  for (const auto& point : *points) identities.insert(point.number);
  std::vector<int> numbers;
  numbers.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t number = 0;
    if (!data.Long(&number) || number > static_cast<uint32_t>((std::numeric_limits<int>::max)()) ||
        identities.erase(static_cast<int>(number)) != 1) return std::nullopt;
    numbers.push_back(static_cast<int>(number));
  }
  if (count > (std::numeric_limits<std::size_t>::max)() - commonHandles) return std::nullopt;
  const auto handles = DecodeHandles(bytes, bitCount, handleStart, commonHandles + count, objectHandle);
  if (!handles.has_value()) return std::nullopt;
  std::vector<std::pair<int, uint64_t>> styles;
  styles.reserve(count);
  for (std::size_t i = 0; i < numbers.size(); ++i)
    styles.emplace_back(numbers[i], (*handles)[commonHandles + i]);
  return styles;
}

std::optional<CadPoint> DecodeCogoPointProxyCenter(const unsigned char* preview,
                                                   std::size_t previewSize) {
  // Civil 3D 2026 writes this preview as a versioned proxy-graphics stream. The first eight
  // bytes contain its total byte count and version. Each following chunk has a byte count and
  // opcode; opcode 6 is a 3D polyline containing an RL vertex count and packed 3RD coordinates.
  if (preview == nullptr || previewSize < 16)
    return std::nullopt;

  uint32_t declaredSize = 0;
  uint32_t version = 0;
  if (!ReadU32(preview, previewSize, 0, &declaredSize) ||
      !ReadU32(preview, previewSize, 4, &version) || declaredSize != previewSize || version != 8)
    return std::nullopt;

  CadPoint center{};
  double minimum[3] = {std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::infinity()};
  double maximum[3] = {-std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()};
  std::size_t vertexCount = 0;

  for (std::size_t offset = 8; offset + 8 <= previewSize;) {
    uint32_t chunkSize = 0;
    uint32_t opcode = 0;
    if (!ReadU32(preview, previewSize, offset, &chunkSize) ||
        !ReadU32(preview, previewSize, offset + 4, &opcode) || chunkSize < 8 ||
        chunkSize > previewSize - offset)
      return std::nullopt;

    if (opcode == 6) {
      uint32_t count = 0;
      if (chunkSize < 12 || !ReadU32(preview, previewSize, offset + 8, &count) ||
          count > (chunkSize - 12) / (3 * sizeof(double)))
        return std::nullopt;
      for (uint32_t vertex = 0; vertex < count; ++vertex) {
        for (unsigned axis = 0; axis < 3; ++axis) {
          double coordinate = 0.0;
          const std::size_t position = offset + 12 +
                                       (static_cast<std::size_t>(vertex) * 3 + axis) * sizeof(double);
          if (!ReadDouble(preview, previewSize, position, &coordinate))
            return std::nullopt;
          minimum[axis] = (std::min)(minimum[axis], coordinate);
          maximum[axis] = (std::max)(maximum[axis], coordinate);
        }
        ++vertexCount;
      }
    }
    offset += chunkSize;
  }

  if (vertexCount == 0 || !std::isfinite(minimum[0]) || !std::isfinite(minimum[1]) ||
      !std::isfinite(minimum[2]) || !std::isfinite(maximum[0]) || !std::isfinite(maximum[1]) ||
      !std::isfinite(maximum[2]))
    return std::nullopt;

  center.x = minimum[0] + (maximum[0] - minimum[0]) * 0.5;
  center.y = minimum[1] + (maximum[1] - minimum[1]) * 0.5;
  center.z = minimum[2] + (maximum[2] - minimum[2]) * 0.5;
  return center;
}

std::optional<int> DecodeCogoPointNumber(const unsigned char* unknownBits,
                                         std::size_t unknownBitCount) {
  uint32_t signature = 0;
  uint32_t code = 0;
  if (!ReadBits(unknownBits, unknownBitCount, 0, 16, &signature) || signature != 0x2f81 ||
      !ReadBits(unknownBits, unknownBitCount, 18, 2, &code))
    return std::nullopt;

  uint32_t number = 0;
  if (code == 0) {
    uint32_t serialized = 0;
    if (!ReadBits(unknownBits, unknownBitCount, 20, 32, &serialized))
      return std::nullopt;
    // DWG's RL is byte-little-endian even though its containing bit stream is read MSB first.
    number = ((serialized & 0x000000ffu) << 24) | ((serialized & 0x0000ff00u) << 8) |
             ((serialized & 0x00ff0000u) >> 8) | ((serialized & 0xff000000u) >> 24);
  } else if (code == 1) {
    if (!ReadBits(unknownBits, unknownBitCount, 20, 8, &number))
      return std::nullopt;
  } else if (code == 2) {
    number = 0;
  } else {
    return std::nullopt;
  }

  if (number == 0 || number > static_cast<uint32_t>((std::numeric_limits<int>::max)()))
    return std::nullopt;
  return static_cast<int>(number);
}

std::optional<std::vector<std::vector<CadPoint>>> DecodeProxyGraphicPolylines(
    const unsigned char* preview, std::size_t previewSize) {
  // The stream is the same versioned vector format used by the COGO marker preview. Chunk sizes
  // make unknown graphics operations safely skippable while preserving the polyline vertices that
  // GoSurvey can display without the Civil 3D runtime.
  if (preview == nullptr || previewSize < 8)
    return std::nullopt;

  // The second header word is the number of chunks that follow (every preview in the supplied
  // drawings agrees); 8 is also accepted, as the first drawings' previews were validated against it.
  uint32_t declaredSize = 0;
  uint32_t declaredChunks = 0;
  if (!ReadU32(preview, previewSize, 0, &declaredSize) ||
      !ReadU32(preview, previewSize, 4, &declaredChunks) || declaredSize != previewSize)
    return std::nullopt;

  std::vector<std::vector<CadPoint>> polylines;
  uint32_t chunks = 0;
  for (std::size_t offset = 8; offset < previewSize; ++chunks) {
    uint32_t chunkSize = 0;
    uint32_t opcode = 0;
    if (previewSize - offset < 8 || !ReadU32(preview, previewSize, offset, &chunkSize) ||
        !ReadU32(preview, previewSize, offset + 4, &opcode) || chunkSize < 8 ||
        chunkSize > previewSize - offset)
      return std::nullopt;

    if (opcode == 6) {
      uint32_t count = 0;
      if (chunkSize < 12 || !ReadU32(preview, previewSize, offset + 8, &count) ||
          count > (chunkSize - 12) / (3 * sizeof(double)))
        return std::nullopt;
      std::vector<CadPoint> vertices;
      vertices.reserve(count);
      for (uint32_t vertex = 0; vertex < count; ++vertex) {
        CadPoint point{};
        double* axes[] = {&point.x, &point.y, &point.z};
        for (unsigned axis = 0; axis < 3; ++axis) {
          const std::size_t position = offset + 12 +
              (static_cast<std::size_t>(vertex) * 3 + axis) * sizeof(double);
          if (!ReadDouble(preview, previewSize, position, axes[axis]))
            return std::nullopt;
        }
        vertices.push_back(point);
      }
      if (vertices.size() >= 2)
        polylines.push_back(std::move(vertices));
    }
    offset += chunkSize;
  }
  if (declaredChunks != 8 && declaredChunks != chunks) return std::nullopt;
  return polylines;
}

namespace {

// AECC_TIN_SURFACE revision this decoder understands: a 16-bit signature and a 19-bit revision at
// the start of the payload. The same prefix is in the supplied Civil 3D drawing and in surfaces
// created by the Civil API, so a different prefix means an unverified layout.
constexpr uint32_t kTinSignature = 0x5430;
constexpr uint32_t kTinRevision = 0x718ea;
// Each vertex is 3 doubles (192 bits) plus a 64-bit separator; the first four are the surface's
// superstructure corners. After the last vertex's coordinates come a compact triangle count and
// the triangle records. A triangle record is eight 32-bit words: three vertex indices, three
// neighbour triangle indices (0xffffffff = none), a flag word and 0xffffffff.
constexpr std::size_t kTinRecordBits = 256;
constexpr std::size_t kTinCoordinateBits = 192;
constexpr std::size_t kTinMinVertices = 7;  // four corners and at least one triangle's worth of points
// Bit 0 hides a triangle (superstructure or outside the boundaries). The other defined bits
// (0x38) are edge attributes that do not affect display.
constexpr uint32_t kTinKnownFlags = 0x39;
constexpr uint32_t kTinHiddenFlag = 0x01;
constexpr uint32_t kTinNone = 0xffffffffu;
// The name's compact length follows the triangle table by a fixed 29-bit run of counters.
constexpr std::size_t kTinNameOffset = 29;

bool ReadOctet(const unsigned char* bytes, std::size_t bitCount, std::size_t offset, uint32_t* value) {
  if (offset > bitCount || bitCount - offset < 8) return false;
  const std::size_t index = offset / 8;
  const unsigned shift = static_cast<unsigned>(offset % 8);
  uint32_t window = static_cast<uint32_t>(bytes[index]) << 8;
  if (shift != 0) window |= bytes[index + 1];
  *value = (window >> (8 - shift)) & 0xffu;
  return true;
}

bool ReadLittleU32(const unsigned char* bytes, std::size_t bitCount, std::size_t offset, uint32_t* value) {
  *value = 0;
  for (unsigned i = 0; i < 4; ++i) {
    uint32_t octet = 0;
    if (!ReadOctet(bytes, bitCount, offset + i * 8, &octet)) return false;
    *value |= octet << (i * 8);
  }
  return true;
}

bool ReadLittleDouble(const unsigned char* bytes, std::size_t bitCount, std::size_t offset, double* value) {
  uint64_t encoded = 0;
  for (unsigned i = 0; i < 8; ++i) {
    uint32_t octet = 0;
    if (!ReadOctet(bytes, bitCount, offset + i * 8, &octet)) return false;
    encoded |= static_cast<uint64_t>(octet) << (i * 8);
  }
  std::memcpy(value, &encoded, sizeof(encoded));
  return std::isfinite(*value);
}

// A survey coordinate is zero or at a magnitude that is neither denormal garbage nor astronomical;
// misaligned reads of the surrounding integer data almost never satisfy this for three doubles.
bool PlausibleCoordinate(double value) {
  const double magnitude = std::fabs(value);
  return value == 0.0 || (magnitude >= 1.e-6 && magnitude <= 1.e10);
}

bool PlausibleVertexAt(const unsigned char* bytes, std::size_t bitCount, std::size_t offset) {
  for (unsigned axis = 0; axis < 3; ++axis) {
    double value = 0.0;
    if (!ReadLittleDouble(bytes, bitCount, offset + axis * 64, &value) || !PlausibleCoordinate(value))
      return false;
  }
  return true;
}

std::optional<TinSurfaceRecord> ReadTinTables(const unsigned char* bytes, std::size_t bitCount,
                                              std::size_t firstVertex, std::size_t vertexCount) {
  Cursor header(bytes, bitCount, firstVertex + (vertexCount - 1) * kTinRecordBits + kTinCoordinateBits);
  uint32_t triangleCount = 0;
  if (!header.Long(&triangleCount) || triangleCount == 0 || triangleCount > 2 * vertexCount ||
      header.position > bitCount || (bitCount - header.position) / kTinRecordBits < triangleCount)
    return std::nullopt;
  const std::size_t tableStart = header.position;

  std::vector<double> allVertices(vertexCount * 3);
  for (std::size_t i = 0; i < vertexCount; ++i)
    for (unsigned axis = 0; axis < 3; ++axis)
      if (!ReadLittleDouble(bytes, bitCount, firstVertex + i * kTinRecordBits + axis * 64,
                            &allVertices[i * 3 + axis]))
        return std::nullopt;

  struct Triangle { uint32_t vertex[3]; bool hidden; };
  std::vector<Triangle> triangles(triangleCount);
  for (uint32_t t = 0; t < triangleCount; ++t) {
    uint32_t word[8] = {};
    for (unsigned i = 0; i < 8; ++i)
      if (!ReadLittleU32(bytes, bitCount, tableStart + t * kTinRecordBits + i * 32, &word[i]))
        return std::nullopt;
    for (unsigned i = 0; i < 3; ++i) {
      if (word[i] >= vertexCount) return std::nullopt;
      if (word[3 + i] != kTinNone && word[3 + i] >= triangleCount) return std::nullopt;
    }
    if (word[0] == word[1] || word[1] == word[2] || word[0] == word[2] ||
        (word[6] & ~kTinKnownFlags) != 0 || word[7] != kTinNone)
      return std::nullopt;
    triangles[t] = {{word[0], word[1], word[2]}, (word[6] & kTinHiddenFlag) != 0};
  }

  // Keep only the vertices a visible triangle uses, in source order.
  std::vector<uint32_t> remap(vertexCount, kTinNone);
  TinSurfaceRecord result;
  for (const Triangle& triangle : triangles) {
    if (triangle.hidden) { ++result.hiddenTriangles; continue; }
    for (unsigned i = 0; i < 3; ++i) remap[triangle.vertex[i]] = 0;
  }
  uint32_t next = 0;
  for (std::size_t i = 0; i < vertexCount; ++i) {
    if (remap[i] == kTinNone) continue;
    remap[i] = next++;
    result.vertsXyz.insert(result.vertsXyz.end(), allVertices.begin() + static_cast<std::ptrdiff_t>(i * 3),
                           allVertices.begin() + static_cast<std::ptrdiff_t>(i * 3 + 3));
  }
  if (next == 0) return std::nullopt;
  for (const Triangle& triangle : triangles) {
    if (triangle.hidden) continue;
    for (unsigned i = 0; i < 3; ++i) result.indices.push_back(remap[triangle.vertex[i]]);
  }

  Cursor name(bytes, bitCount, tableStart + static_cast<std::size_t>(triangleCount) * kTinRecordBits +
                                   kTinNameOffset);
  if (!name.Text(&result.name)) result.name.clear();
  return result;
}

// Component records share the point style's repeated main-stream header. A surface style carries
// 25 of them; the first twelve are the plan components in the Civil API's display-type order.
constexpr uint32_t kStyleComponentHeader = 0x54150950;
constexpr std::size_t kStyleComponentCount = 25;
// Every contour value in a surface style is preceded by the tag 0x460102. Ten tagged values appear
// in a fixed order: depression interval, a pair holding the minor interval and the base elevation
// interval, two 100s, a 1, the depression tick length, a 0, the major interval and a 0 (verified
// against nine styles). Styles written by Civil 3D's copy command store that pair as (minor, base);
// the original styles store (base, minor). A base interval of 0 is the only case seen, so the minor
// interval is the one non-zero member of the pair, and a pair with two non-zero members is refused.
constexpr std::size_t kStyleValueCount = 10;
constexpr std::size_t kStyleIntervalPairIndex = 1;
constexpr std::size_t kStyleMajorIndex = 8;
// The same tag family (0x4601TT) introduces every field; TT is the field's type. The smoothing
// factor is the first type-1 (compact short) value, and the smoothing flag is the bit 7 after the
// end of the fifth of the seven type-3 tags (verified on 22 styles and variants in both layouts).
constexpr uint32_t kStyleTagFamily = 0x4601;
constexpr uint32_t kStyleShortType = 1, kStyleValueType = 2, kStyleGroupType = 3;
constexpr std::size_t kStyleGroupCount = 7, kStyleSmoothGroup = 4, kStyleSmoothFlagOffset = 7;

}  // namespace

std::optional<double> DecodeDrawingScale(const unsigned char* bytes, std::size_t bitCount) {
  uint32_t signature = 0;
  double scale = 0;
  if (!ReadBits(bytes, bitCount, 0, 32, &signature) || signature != 0x5434d150 ||
      !ReadLittleDouble(bytes, bitCount, 131, &scale) || scale <= 0 || scale > 1.e9)
    return std::nullopt;
  return scale;
}

std::optional<PointTableRecord> DecodePointTable(const unsigned char* bytes, std::size_t bitCount,
                                               std::size_t stringStart) {
  constexpr std::size_t kMatricesBits = 2 * 16 * 64, kFooterBits = 19;
  uint32_t signature = 0, footer = 0, rowTag = 0;
  if (bytes == nullptr || stringStart > bitCount || stringStart < kMatricesBits + kFooterBits + 130 ||
      !ReadBits(bytes, bitCount, 0, 32, &signature) || signature != 0x54254150 ||
      !ReadBits(bytes, bitCount, stringStart - kFooterBits, 19, &footer) ||
      footer != 0b0001011111000000100u) return std::nullopt;
  const std::size_t matrices = stringStart - kMatricesBits - kFooterBits;
  const std::size_t rowEnd = matrices - 78;
  double gap = 0;
  if (!ReadBits(bytes, stringStart, rowEnd, 14, &rowTag) || rowTag != 0x340c ||
      !ReadLittleDouble(bytes, stringStart, rowEnd + 14, &gap) || gap < 0 || gap > 1.e6)
    return std::nullopt;
  PointTableRecord result;
  bool found = false;
  for (const std::size_t shortBits : {2u, 10u, 18u}) {
    for (const std::size_t longBits : {2u, 10u, 34u}) {
      const std::size_t start = rowEnd - shortBits - longBits;
      uint32_t header = 0, capacity = 0, first = 0;
      Cursor rows(bytes, rowEnd, start);
      if (start < 8 || !ReadBits(bytes, stringStart, start - 8, 8, &header) || header != 0x2a ||
          !rows.Short(&capacity) || rows.position != rowEnd - longBits || !rows.Long(&first) ||
          rows.position != rowEnd || capacity == 0 || capacity > 65535 || first > 10000000) continue;
      if (found) return std::nullopt;
      found = true;
      result.maximumRows = capacity;
      result.firstRow = first;
    }
  }
  if (!found) return std::nullopt;
  for (std::size_t i = 0; i < 16; ++i) {
    double base = 0;
    if (!ReadLittleDouble(bytes, stringStart, matrices + i * 64, &result.transform[i]) ||
        !ReadLittleDouble(bytes, stringStart, matrices + (16 + i) * 64, &base) ||
        base != (i % 5 == 0 ? 1.0 : 0.0)) return std::nullopt;
  }
  if (result.transform[12] != 0 || result.transform[13] != 0 || result.transform[14] != 0 ||
      result.transform[15] != 1) return std::nullopt;
  return result;
}

std::optional<TableStyleRecord> DecodeTableStyle(const unsigned char* bytes, std::size_t bitCount,
                                               std::size_t stringStart, std::size_t stringBitCount) {
  if (bytes == nullptr || stringStart > bitCount || stringBitCount > bitCount - stringStart)
    return std::nullopt;
  const auto name = DecodeStyleName(bytes, bitCount, stringStart, stringBitCount);
  if (!name || name->empty()) return std::nullopt;
  Cursor strings(bytes, stringStart + stringBitCount, stringStart);
  std::vector<std::u16string> texts;
  while (strings.position < stringStart + stringBitCount) {
    std::u16string text;
    if (!strings.Text(&text)) return std::nullopt;
    texts.push_back(std::move(text));
  }
  if (texts.size() < 3 || texts.back().empty()) return std::nullopt;
  TableStyleRecord result;
  result.name = *name;
  result.title = texts.back();
  std::size_t begin = texts.size() - 1;
  while (begin >= 2 && texts[begin - 1].size() >= 4 && texts[begin - 1].compare(0, 2, u"<[") == 0 &&
         texts[begin - 1].compare(texts[begin - 1].size() - 2, 2, u"]>") == 0)
    begin -= 2;
  const std::size_t columns = (texts.size() - 1 - begin) / 2;
  if (columns == 0 || columns > 256) return std::nullopt;
  // Each column has the same typed property header. Its compact width follows the final
  // fourteen header bits. The last column is followed by the three text heights. Controlled
  // Civil saves changing widths and each height independently establish both boundaries.
  std::vector<std::size_t> widths;
  unsigned displays = 0;
  std::size_t lastDisplay = 0;
  for (std::size_t p = 0; p + 32 <= stringStart; ++p) {
    uint32_t tag = 0;
    if (ReadBits(bytes, stringStart, p, 32, &tag) && tag == 0x54150950) {
      ++displays;
      lastDisplay = p;
    }
  }
  if (displays != 22) return std::nullopt;
  std::vector<std::size_t> componentStarts;
  componentStarts.reserve(kTableStyleDisplayCount);
  for (std::size_t p = 0; p + 32 <= stringStart; ++p) {
    uint32_t header = 0;
    if (ReadBits(bytes, stringStart, p, 32, &header) && header == kStyleComponentHeader)
      componentStarts.push_back(p + 32);
  }
  if (componentStarts.size() != kTableStyleDisplayCount) return std::nullopt;
  const auto componentEnd = [&](std::size_t i) {
    return i + 1 < componentStarts.size() ? componentStarts[i + 1] - 32 : lastDisplay + 512;
  };
  const auto scanResolvedColor = [&](std::size_t from, std::size_t to, uint32_t* color) {
    if (!color || to <= from) return false;
    const std::size_t limit = std::min(to, bitCount);
    bool found = false;
    for (std::size_t bit = from; bit + 32 <= limit; ++bit) {
      uint32_t candidate = 0;
      if (!ReadBits(bytes, bitCount, bit, 32, &candidate)) continue;
      if ((candidate >> 24) != 0xc3 || (candidate & 0xffffffu) == 0) continue;
      *color = candidate;
      found = true;
    }
    return found;
  };
  for (std::size_t i = 0; i < kTableStyleDisplayCount; ++i) {
    Cursor component(bytes, stringStart, componentStarts[i]);
    uint32_t visible = 0, index = 0, flags = 0, weight = 0;
    auto& out = result.displayComponents[i];
    if (!component.Read(1, &visible) || !component.Short(&index) || index != 0 ||
        !component.Long(&out.color) || (out.color >> 24) < 0xc0 || (out.color >> 24) > 0xc8 ||
        !component.Read(8, &flags) || flags != 0 || !component.Short(&weight))
      return std::nullopt;
    out.visible = visible != 0;
    uint32_t resolved = out.color;
    if (scanResolvedColor(componentStarts[i], componentEnd(i), &resolved))
      out.color = resolved;
  }
  const auto findStreamColor = [&](uint32_t aci) {
    const uint32_t target = 0xc3000000u | aci;
    const std::size_t byteLimit = bitCount / 8;
    for (std::size_t offset = 0; offset + 4 <= byteLimit; ++offset) {
      uint32_t value = 0;
      if (ReadU32(bytes, byteLimit, offset, &value) && value == target) return value;
    }
    for (std::size_t p = 0; p + 32 <= bitCount; ++p) {
      uint32_t value = 0;
      if (ReadBits(bytes, bitCount, p, 32, &value) && value == target) return value;
    }
    return 0u;
  };
  if (const uint32_t grid = findStreamColor(1))
    result.displayComponents[kTableStyleDataSeparatorDisplay].color = grid;
  if (const uint32_t border = findStreamColor(5))
    result.displayComponents[kTableStyleBorderDisplay].color = border;
  if (const uint32_t dataText = findStreamColor(150))
    result.displayComponents[kTableStyleDataTextDisplay].color = dataText;
  for (std::size_t p = lastDisplay + 32; p + 38 <= stringStart; ++p) {
    uint32_t high = 0, low = 0;
    if (ReadBits(bytes, stringStart, p, 24, &high) && high == 0x10 &&
        ReadBits(bytes, stringStart, p + 24, 14, &low) && low == 0x1505)
      widths.push_back(p + 38);
  }
  if (widths.size() != columns) return std::nullopt;
  Cursor data(bytes, stringStart);
  for (std::size_t i = 0; i < columns; ++i) {
    TableColumnRecord column;
    column.header = texts[begin + i * 2];
    column.expression = texts[begin + i * 2 + 1];
    data.position = widths[i];
    if (!data.Double(&column.width) || column.width < 0 || column.width > 1.e6)
      return std::nullopt;
    result.columns.push_back(std::move(column));
  }
  if (!data.Double(&result.dataTextHeight) || !data.Double(&result.headerTextHeight) ||
      !data.Double(&result.titleTextHeight) || result.dataTextHeight <= 0 ||
      result.headerTextHeight <= 0 || result.titleTextHeight <= 0 ||
      result.dataTextHeight > 100 || result.headerTextHeight > 100 || result.titleTextHeight > 100)
    return std::nullopt;
  return result;
}

std::optional<SurfaceElevationLabelRecord> DecodeSurfaceElevationLabel(const unsigned char* bytes,
                                                                       std::size_t bitCount) {
  // Signature 0x5425, revision 0x20a84. The raw (x, y) pair sits at bit 347; the compact (x, y, z)
  // point, whose z is the elevation Civil 3D computed on the surface, follows a 2-bit code at bit 811.
  constexpr uint32_t kSignature = 0x5425, kRevision = 0x20a84;
  constexpr std::size_t kRawPair = 347, kCompactPoint = 811;
  uint32_t signature = 0, revision = 0;
  if (bytes == nullptr || !ReadBits(bytes, bitCount, 0, 16, &signature) || signature != kSignature ||
      !ReadBits(bytes, bitCount, 16, 19, &revision) || revision != kRevision)
    return std::nullopt;
  double rawX = 0.0, rawY = 0.0;
  if (!ReadLittleDouble(bytes, bitCount, kRawPair, &rawX) ||
      !ReadLittleDouble(bytes, bitCount, kRawPair + 64, &rawY))
    return std::nullopt;
  Cursor point(bytes, bitCount, kCompactPoint);
  SurfaceElevationLabelRecord result;
  if (!point.Double(&result.location.x) || !point.Double(&result.location.y) ||
      !point.Double(&result.location.z))
    return std::nullopt;
  if (std::fabs(result.location.x - rawX) > 1.e-6 || std::fabs(result.location.y - rawY) > 1.e-6)
    return std::nullopt;
  return result;
}

std::optional<CadPoint> DecodeSurfaceContourLabel(const unsigned char* bytes, std::size_t bitCount,
                                                  const unsigned char* preview, std::size_t previewSize) {
  // Signature 0x5425, revision 0x20a84 (shared with the elevation label). Raw doubles: the elevation
  // at bit 559 (repeated at 659), then x at 757 and y at 821.
  constexpr uint32_t kSignature = 0x5425, kRevision = 0x20a84;
  constexpr std::size_t kElevation = 559, kElevationRepeat = 659, kX = 757, kY = 821;
  uint32_t signature = 0, revision = 0;
  if (bytes == nullptr || !ReadBits(bytes, bitCount, 0, 16, &signature) || signature != kSignature ||
      !ReadBits(bytes, bitCount, 16, 19, &revision) || revision != kRevision)
    return std::nullopt;
  CadPoint point{};
  double repeat = 0.0;
  if (!ReadLittleDouble(bytes, bitCount, kX, &point.x) || !ReadLittleDouble(bytes, bitCount, kY, &point.y) ||
      !ReadLittleDouble(bytes, bitCount, kElevation, &point.z) ||
      !ReadLittleDouble(bytes, bitCount, kElevationRepeat, &repeat) || repeat != point.z)
    return std::nullopt;
  // The preview is a closed four-corner rectangle at the label's elevation, centred on the point.
  const auto outlines = DecodeProxyGraphicPolylines(preview, previewSize);
  if (!outlines.has_value() || outlines->size() != 1 || outlines->front().size() != 5) return std::nullopt;
  const auto& corners = outlines->front();
  double centreX = 0.0, centreY = 0.0;
  for (std::size_t i = 0; i < 4; ++i) {
    if (corners[i].z != point.z) return std::nullopt;
    centreX += corners[i].x / 4.0;
    centreY += corners[i].y / 4.0;
  }
  if (std::fabs(centreX - point.x) > 1.e-6 || std::fabs(centreY - point.y) > 1.e-6) return std::nullopt;
  return point;
}

std::optional<std::vector<PointDescription>> DecodePointNodeDescriptions(
    const unsigned char* bytes, std::size_t bitCount, std::size_t stringStart,
    std::size_t stringBitCount) {
  if (bytes == nullptr || stringStart > bitCount || stringBitCount > bitCount - stringStart)
    return std::nullopt;
  std::size_t position = 0;
  const auto points = DecodePointNodeLocationsImpl(bytes, stringStart, &position);
  if (!points.has_value()) return std::nullopt;
  std::unordered_set<int> owned;
  for (const auto& point : *points) owned.insert(point.number);

  // After the coordinates: the point numbers again, a zero and a list of (number, token) pairs whose
  // tokens are all strings (the supplied drawings use "$*" for every entry), two zeros, then the
  // per-point descriptions in the same pair form.
  Cursor data(bytes, stringStart, position);
  uint32_t count = 0, zero = 0, otherCount = 0;
  if (!data.Long(&count) || count > points->size()) return std::nullopt;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t number = 0;
    if (!data.Long(&number) || owned.count(static_cast<int>(number)) != 1) return std::nullopt;
  }
  if (!data.Long(&zero) || zero != 0 || !data.Long(&otherCount) || otherCount > stringStart) return std::nullopt;
  std::size_t otherLiterals = 0;
  for (uint32_t i = 0; i < otherCount; ++i) {
    uint32_t key = 0, token = 0;
    if (!data.Long(&key) || !data.Long(&token)) return std::nullopt;
    if ((token & 0x80000000u) != 0) ++otherLiterals;
  }
  uint32_t zeroA = 0, zeroB = 0, descriptionCount = 0;
  if (!data.Long(&zeroA) || !data.Long(&zeroB) || zeroA != 0 || zeroB != 0 ||
      !data.Long(&descriptionCount) || descriptionCount > points->size())
    return std::nullopt;
  std::vector<PointDescription> result;
  result.reserve(descriptionCount);
  std::unordered_set<int> seen;
  std::size_t literals = 0;
  for (uint32_t i = 0; i < descriptionCount; ++i) {
    uint32_t number = 0, token = 0;
    if (!data.Long(&number) || !data.Long(&token) || owned.count(static_cast<int>(number)) != 1 ||
        !seen.insert(static_cast<int>(number)).second)
      return std::nullopt;
    PointDescription description;
    description.number = static_cast<int>(number);
    description.literal = (token & 0x80000000u) != 0;
    // Civil's predefined string vocabulary is shared with label property modifiers: 2449 is
    // the case-preserving "CP" token, also used as a raw survey code. It consumes no TU entry.
    if (token == 2449) description.text = u"CP";
    literals += description.literal ? 1 : 0;
    result.push_back(std::move(description));
  }

  // String stream: one empty string, one string per entry of the first list, then one per literal
  // description, in list order.
  Cursor strings(bytes, stringStart + stringBitCount, stringStart);
  std::u16string text;
  if (!strings.Text(&text) || !text.empty()) return std::nullopt;
  for (std::size_t i = 0; i < otherLiterals; ++i)
    if (!strings.Text(&text)) return std::nullopt;
  for (PointDescription& description : result) {
    if (!description.literal) continue;
    if (!strings.Text(&description.text)) return std::nullopt;
  }
  (void)literals;
  return result;
}

std::optional<PointGroupRecord> DecodePointGroup(const unsigned char* bytes, std::size_t bitCount,
                                                 std::size_t stringStart, std::size_t stringBitCount,
                                                 const std::unordered_set<int>& knownPoints) {
  if (bytes == nullptr || stringStart > bitCount || stringBitCount > bitCount - stringStart)
    return std::nullopt;
  Cursor data(bytes, stringStart);
  uint32_t version = 0, nameToken = 0;
  if (!data.Short(&version) || version != 1214 || !data.Long(&nameToken)) return std::nullopt;
  PointGroupRecord result;
  if ((nameToken & 0x80000000u) != 0) {
    Cursor strings(bytes, stringStart + stringBitCount, stringStart);
    if (!strings.Text(&result.name) || result.name.empty()) return std::nullopt;
  } else if (nameToken == 0x17) {
    result.name = u"Surface";  // Built-in name tokens, each seen once in the supplied drawings.
  } else if (nameToken == 0x21) {
    result.name = u"_All Points";
  } else if (nameToken == 0x25) {
    result.name = u"Alignment";
  } else {
    return std::nullopt;
  }

  // The priority is a compact BL, not a fixed ten-bit field: priority zero uses two bits.
  // Reversing Civil's DrawOrder changes this field and shifts the following membership list.
  Cursor flags(bytes, stringStart, data.position + 157);
  uint32_t labelOverride = 0, markerOverride = 0;
  if (!flags.Read(1, &labelOverride) || !flags.Read(1, &markerOverride)) return std::nullopt;
  result.labelStyleOverride = labelOverride != 0;
  result.markerStyleOverride = markerOverride != 0;
  Cursor list(bytes, stringStart, data.position + 168);
  uint32_t count = 0;
  if (!list.Long(&result.displayPriority) || !list.Long(&count) || count > knownPoints.size() ||
      (stringStart - (std::min)(stringStart, list.position)) / 32 < count)
    return result;
  std::unordered_set<int> unique;
  std::vector<int> members;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t number = 0;
    if (!ReadLittleU32(bytes, stringStart, list.position + static_cast<std::size_t>(i) * 32, &number) ||
        knownPoints.count(static_cast<int>(number)) != 1 || !unique.insert(static_cast<int>(number)).second)
      return result;
    members.push_back(static_cast<int>(number));
  }
  result.members = std::move(members);
  result.membersDecoded = true;
  return result;
}

std::optional<std::u16string> DecodeStyleName(const unsigned char* bytes, std::size_t bitCount,
                                            std::size_t stringStart, std::size_t stringBitCount) {
  if (bytes == nullptr || stringStart > bitCount || stringBitCount > bitCount - stringStart)
    return std::nullopt;
  Cursor data(bytes, stringStart);
  uint32_t version = 0, token = 0;
  if (!data.Short(&version) || version != 1214 || !data.Long(&token)) return std::nullopt;
  if (token == 30) return u"Standard";
  if ((token & 0x80000000u) == 0) return std::nullopt;
  Cursor strings(bytes, stringStart + stringBitCount, stringStart);
  std::u16string name;
  if (!strings.Text(&name) || name.empty()) return std::nullopt;
  return name;
}

std::optional<LabelStyleRecord> DecodeLabelStyle(const unsigned char* bytes, std::size_t bitCount,
                                                std::size_t stringStart, std::size_t stringBitCount) {
  const auto name = DecodeStyleName(bytes, bitCount, stringStart, stringBitCount);
  if (!name.has_value()) return std::nullopt;
  std::optional<bool> visible;
  // The boolean property precedes the tagged layer/text-style references. A presence bit forty
  // bits before the property suffix distinguishes an override from the inherited true default.
  // Controlled Civil saves (false -> true) change only the value bit, leaving this suffix intact.
  constexpr uint32_t suffix32 = 0x90016400;
  for (std::size_t p = 40; p + 39 <= stringStart; ++p) {
    uint32_t first = 0, tail = 0, header = 0, overrideValue = 0, value = 0;
    if (!ReadBits(bytes, stringStart, p, 32, &first) || first != suffix32 ||
        !ReadBits(bytes, stringStart, p + 32, 7, &tail) || tail != 8) continue;
    if (!ReadBits(bytes, stringStart, p - 9, 8, &header) || header != 0x5a ||
        !ReadBits(bytes, stringStart, p - 40, 1, &overrideValue) ||
        !ReadBits(bytes, stringStart, p - 1, 1, &value) || visible.has_value()) return std::nullopt;
    visible = overrideValue == 0 || value != 0;
  }
  if (!visible.has_value()) return std::nullopt;
  return LabelStyleRecord{*name, *visible};
}

std::optional<double> DecodeLabelTextHeight(const unsigned char* bytes, std::size_t bitCount,
                                            std::size_t stringStart) {
  if (bytes == nullptr || stringStart > bitCount) return std::nullopt;
  std::optional<double> height;
  for (std::size_t p = 64; p + 64 <= stringStart; ++p) {
    double value = 0.0;
    if (!ReadLittleDouble(bytes, bitCount, p, &value) || value < 0.001 || value > 0.05) continue;
    if (height.has_value() && std::fabs(*height - value) > 1.e-9) continue;
    height = value;
  }
  return height;
}

std::optional<std::u16string> DecodeLabelTextContent(const unsigned char* bytes, std::size_t bitCount,
                                                   std::size_t stringStart) {
  if (bytes == nullptr || stringStart > bitCount) return std::nullopt;
  Cursor revision(bytes, stringStart);
  uint32_t version = 0;
  if (!revision.Short(&version) || version != 1214) return std::nullopt;
  std::optional<std::u16string> content;
  // Content's attribute identifier is 0xa4fe, followed by a BS byte count and null-terminated
  // UTF-16 in the MAIN stream, rather than the object's separate TU name/description stream.
  for (std::size_t p = revision.position; p + 18 <= stringStart; ++p) {
    uint32_t tag = 0;
    if (!ReadBits(bytes, stringStart, p, 16, &tag) || tag != 0xa4fe) continue;
    Cursor text(bytes, stringStart, p + 16);
    uint32_t length = 0;
    if (!text.Short(&length) || length < 2 || length % 2 != 0 ||
        text.position > stringStart || length > (stringStart - text.position) / 8) continue;
    std::u16string value;
    for (uint32_t i = 0; i < length / 2; ++i) {
      uint32_t low = 0, high = 0;
      if (!text.Read(8, &low) || !text.Read(8, &high)) return std::nullopt;
      const char16_t ch = static_cast<char16_t>(low | (high << 8));
      if ((ch == 0) != (i + 1 == length / 2)) return std::nullopt;
      if (ch != 0) value.push_back(ch);
    }
    if (content.has_value()) return std::nullopt;
    content = std::move(value);
  }
  return content;
}

std::vector<uint64_t> DecodeHandleList(const unsigned char* bytes, std::size_t bitCount,
                                       std::size_t start, uint64_t objectHandle) {
  std::vector<uint64_t> handles;
  for (std::size_t count = 1; count <= 256; ++count) {
    auto next = DecodeHandles(bytes, bitCount, start, count, objectHandle);
    if (!next.has_value()) break;
    handles = std::move(*next);
  }
  return handles;
}

std::optional<SurfaceStyleRecord> DecodeSurfaceStyle(const unsigned char* bytes, std::size_t bitCount,
                                                     std::size_t stringStart,
                                                     std::size_t stringBitCount) {
  if (bytes == nullptr || stringStart > bitCount || stringBitCount > bitCount - stringStart)
    return std::nullopt;
  Cursor data(bytes, stringStart);
  uint32_t version = 0, nameToken = 0;
  if (!data.Short(&version) || version != 1214 || !data.Long(&nameToken)) return std::nullopt;
  SurfaceStyleRecord result;
  if (nameToken == 30) {
    result.name = u"Standard";
  } else if (nameToken == 0xcd8) {
    result.name = u"Design";  // Built-in name token (the string stream holds only its description).
  } else if ((nameToken & 0x80000000u) != 0) {
    Cursor strings(bytes, stringStart + stringBitCount, stringStart);
    if (!strings.Text(&result.name) || result.name.empty()) return std::nullopt;
  } else {
    return std::nullopt;
  }

  std::vector<std::size_t> headers;
  std::vector<std::size_t> tags;
  std::vector<std::size_t> shortTags;
  std::vector<std::size_t> groupTags;
  const std::size_t tagsStart = data.position;  // a tag cannot begin inside the name header
  for (std::size_t bit = 0; stringStart - bit >= 32; ++bit) {
    uint32_t window = 0;
    if (!ReadBits(bytes, stringStart, bit, 32, &window)) return std::nullopt;
    if (window == kStyleComponentHeader) headers.push_back(bit + 32);
    if ((window >> 16) != kStyleTagFamily || bit < tagsStart) continue;
    const uint32_t type = (window >> 8) & 0xffu;
    if (type == kStyleValueType) tags.push_back(bit + 24);
    else if (type == kStyleShortType) shortTags.push_back(bit + 24);
    else if (type == kStyleGroupType) groupTags.push_back(bit + 24);
  }
  if (headers.size() != kStyleComponentCount || tags.size() != kStyleValueCount ||
      tags.back() > headers.front())
    return std::nullopt;

  for (std::size_t i = 0; i < result.components.size(); ++i) {
    data.position = headers[i];
    uint32_t visible = 0, index = 0, flags = 0, weight = 0;
    uint32_t color = 0;
    double linetypeScale = 0.0;
    if (!data.Read(1, &visible) || !data.Short(&index) || index != 0 || !data.Long(&color) ||
        (color >> 24) < 0xc0 || (color >> 24) > 0xc8 || !data.Read(8, &flags) || flags != 0 ||
        !data.Short(&weight) || !data.Double(&linetypeScale) || linetypeScale <= 0.0)
      return std::nullopt;
    result.components[i].visible = visible != 0;
    result.components[i].color = color;
    result.components[i].lineweight = static_cast<int16_t>(static_cast<uint16_t>(weight));
  }

  std::array<double, kStyleValueCount> values{};
  for (std::size_t i = 0; i < tags.size(); ++i) {
    Cursor value(bytes, stringStart, tags[i]);
    if (!value.Double(&values[i])) return std::nullopt;
  }
  const double first = values[kStyleIntervalPairIndex], second = values[kStyleIntervalPairIndex + 1];
  if ((first == 0.0) == (second == 0.0)) return std::nullopt;
  result.minorInterval = first != 0.0 ? first : second;
  result.majorInterval = values[kStyleMajorIndex];
  if (!(result.minorInterval > 0.0) || !(result.majorInterval > 0.0)) return std::nullopt;

  if (groupTags.size() == kStyleGroupCount && !shortTags.empty() && shortTags.front() < tags.front()) {
    uint32_t flag = 0, code = 0, factor = 0;
    Cursor factorValue(bytes, stringStart, shortTags.front());
    if (ReadBits(bytes, stringStart, groupTags[kStyleSmoothGroup] + kStyleSmoothFlagOffset, 1, &flag) &&
        factorValue.Read(2, &code) && code == 1 && factorValue.Read(8, &factor) && factor >= 1 &&
        factor <= 32) {
      result.smoothContours = flag != 0;
      result.smoothingFactor = factor;
    }
  }
  return result;
}

std::optional<TinSurfaceRecord> DecodeTinSurface(const unsigned char* unknownBits,
                                                 std::size_t unknownBitCount) {
  uint32_t signature = 0, revision = 0;
  if (unknownBits == nullptr || !ReadBits(unknownBits, unknownBitCount, 0, 16, &signature) ||
      signature != kTinSignature || !ReadBits(unknownBits, unknownBitCount, 16, 19, &revision) ||
      revision != kTinRevision)
    return std::nullopt;
  for (std::size_t start = 35; start + kTinMinVertices * kTinRecordBits <= unknownBitCount; ++start) {
    if (!PlausibleVertexAt(unknownBits, unknownBitCount, start)) continue;
    std::size_t count = 1;
    while (PlausibleVertexAt(unknownBits, unknownBitCount, start + count * kTinRecordBits)) ++count;
    if (count >= kTinMinVertices) {
      if (auto surface = ReadTinTables(unknownBits, unknownBitCount, start, count)) return surface;
      start += (count - 1) * kTinRecordBits;  // a vertex-shaped run that is not the surface's table
    }
  }
  return std::nullopt;
}

}  // namespace libredwgcivil
