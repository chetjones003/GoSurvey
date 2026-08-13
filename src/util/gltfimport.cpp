#include "gltfimport.hpp"

#include "meshgeom.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace gltf {
namespace {

using json = nlohmann::json;

// glTF accessor component types (spec §3.6.2.2).
constexpr int kByte = 5120, kUByte = 5121, kShort = 5122, kUShort = 5123, kUInt = 5125, kFloat = 5126;
constexpr int kModeTriangles = 4;

/// A 4×4 affine transform, row-major, as glTF composes node hierarchies.
struct Mat4 {
  double m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

Mat4 Multiply(const Mat4& a, const Mat4& b) {
  Mat4 r;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      double s = 0.0;
      for (int k = 0; k < 4; ++k)
        s += a.m[i * 4 + k] * b.m[k * 4 + j];
      r.m[i * 4 + j] = s;
    }
  return r;
}

void TransformPoint(const Mat4& t, double x, double y, double z, double* ox, double* oy, double* oz) {
  *ox = t.m[0] * x + t.m[1] * y + t.m[2] * z + t.m[3];
  *oy = t.m[4] * x + t.m[5] * y + t.m[6] * z + t.m[7];
  *oz = t.m[8] * x + t.m[9] * y + t.m[10] * z + t.m[11];
}

/// Directions ignore translation. Non-uniform node scale would strictly need the inverse transpose;
/// this applies the 3×3 and renormalises, which is exact for rotation and uniform scale and very
/// slightly off for non-uniform scale — a trade recorded rather than hidden, because carrying an
/// inverse-transpose through the node walk buys nothing for the models REQ-065 targets.
void TransformDirection(const Mat4& t, double x, double y, double z, double* ox, double* oy, double* oz) {
  *ox = t.m[0] * x + t.m[1] * y + t.m[2] * z;
  *oy = t.m[4] * x + t.m[5] * y + t.m[6] * z;
  *oz = t.m[8] * x + t.m[9] * y + t.m[10] * z;
}

/// glTF stores `matrix` in COLUMN-major order (spec §3.5.2); this reads it into our row-major form.
Mat4 MatrixFromColumnMajor(const json& a) {
  Mat4 t;
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      t.m[r * 4 + c] = a[static_cast<size_t>(c * 4 + r)].get<double>();
  return t;
}

Mat4 MatrixFromTrs(const json& node) {
  double tx = 0, ty = 0, tz = 0;
  double qx = 0, qy = 0, qz = 0, qw = 1;
  double sx = 1, sy = 1, sz = 1;
  if (node.contains("translation") && node["translation"].is_array() && node["translation"].size() == 3) {
    tx = node["translation"][0].get<double>();
    ty = node["translation"][1].get<double>();
    tz = node["translation"][2].get<double>();
  }
  if (node.contains("rotation") && node["rotation"].is_array() && node["rotation"].size() == 4) {
    qx = node["rotation"][0].get<double>();
    qy = node["rotation"][1].get<double>();
    qz = node["rotation"][2].get<double>();
    qw = node["rotation"][3].get<double>();
  }
  if (node.contains("scale") && node["scale"].is_array() && node["scale"].size() == 3) {
    sx = node["scale"][0].get<double>();
    sy = node["scale"][1].get<double>();
    sz = node["scale"][2].get<double>();
  }
  // R from the quaternion, then columns scaled — the T·R·S order the spec mandates.
  const double xx = qx * qx, yy = qy * qy, zz = qz * qz;
  const double xy = qx * qy, xz = qx * qz, yz = qy * qz;
  const double wx = qw * qx, wy = qw * qy, wz = qw * qz;
  Mat4 t;
  t.m[0] = (1 - 2 * (yy + zz)) * sx;  t.m[1] = (2 * (xy - wz)) * sy;      t.m[2] = (2 * (xz + wy)) * sz;      t.m[3] = tx;
  t.m[4] = (2 * (xy + wz)) * sx;      t.m[5] = (1 - 2 * (xx + zz)) * sy;  t.m[6] = (2 * (yz - wx)) * sz;      t.m[7] = ty;
  t.m[8] = (2 * (xz - wy)) * sx;      t.m[9] = (2 * (yz + wx)) * sy;      t.m[10] = (1 - 2 * (xx + yy)) * sz; t.m[11] = tz;
  return t;
}

int Base64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

bool DecodeBase64(const std::string& in, std::vector<std::uint8_t>* out) {
  out->clear();
  out->reserve(in.size() * 3 / 4);
  int acc = 0;
  int bits = 0;
  for (char c : in) {
    if (c == '=' || c == '\n' || c == '\r' || c == ' ')
      continue;
    const int v = Base64Value(c);
    if (v < 0)
      return false;
    acc = (acc << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out->push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
    }
  }
  return true;
}

/// Number of components for a glTF accessor `type`.
int ComponentsFor(const std::string& type) {
  if (type == "SCALAR") return 1;
  if (type == "VEC2") return 2;
  if (type == "VEC3") return 3;
  if (type == "VEC4") return 4;
  if (type == "MAT4") return 16;
  return 0;
}

int ComponentSize(int componentType) {
  switch (componentType) {
  case kByte:
  case kUByte: return 1;
  case kShort:
  case kUShort: return 2;
  case kUInt:
  case kFloat: return 4;
  default: return 0;
  }
}

/// Everything needed to walk one accessor's data out of its buffer view.
struct Reader {
  const std::uint8_t* base = nullptr;
  size_t size = 0;
  size_t stride = 0;
  int componentType = 0;
  int components = 0;
  size_t count = 0;
};

double ReadComponent(const std::uint8_t* p, int componentType) {
  switch (componentType) {
  case kFloat: { float v; std::memcpy(&v, p, 4); return static_cast<double>(v); }
  case kUInt:  { std::uint32_t v; std::memcpy(&v, p, 4); return static_cast<double>(v); }
  case kUShort:{ std::uint16_t v; std::memcpy(&v, p, 2); return static_cast<double>(v); }
  case kShort: { std::int16_t v;  std::memcpy(&v, p, 2); return static_cast<double>(v); }
  case kUByte: return static_cast<double>(*p);
  case kByte:  return static_cast<double>(*reinterpret_cast<const std::int8_t*>(p));
  default: return 0.0;
  }
}

} // namespace

// ------------------------------------------------------------------------------------------------

ImportResult ImportGltfBytes(const std::vector<std::uint8_t>& bytes, bool isGlb, const std::string& baseDir,
                             const ImportOptions& opt) {
  ImportResult out;

  std::string jsonText;
  std::vector<std::uint8_t> glbBin;

  if (isGlb) {
    // GLB container (spec §4.4): 12-byte header then length-prefixed chunks. Every bound is checked
    // because a truncated GLB is the most common broken file, and reading past the end here is a
    // crash rather than a diagnosis.
    if (bytes.size() < 12) {
      out.error = "GLB is shorter than its 12-byte header — file is truncated.";
      return out;
    }
    std::uint32_t magic = 0, version = 0, total = 0;
    std::memcpy(&magic, &bytes[0], 4);
    std::memcpy(&version, &bytes[4], 4);
    std::memcpy(&total, &bytes[8], 4);
    if (magic != 0x46546C67u) {  // 'glTF'
      out.error = "not a GLB file (bad magic).";
      return out;
    }
    if (version != 2) {
      out.error = "GLB version " + std::to_string(version) + " is not supported (only glTF 2.0).";
      return out;
    }
    if (total > bytes.size()) {
      out.error = "GLB header declares " + std::to_string(total) + " bytes but the file has " +
                  std::to_string(bytes.size()) + " — file is truncated.";
      return out;
    }
    size_t off = 12;
    while (off + 8 <= bytes.size()) {
      std::uint32_t chunkLen = 0, chunkType = 0;
      std::memcpy(&chunkLen, &bytes[off], 4);
      std::memcpy(&chunkType, &bytes[off + 4], 4);
      off += 8;
      if (off + chunkLen > bytes.size()) {
        out.error = "GLB chunk runs past the end of the file — file is truncated.";
        return out;
      }
      if (chunkType == 0x4E4F534Au)  // 'JSON'
        jsonText.assign(reinterpret_cast<const char*>(&bytes[off]), chunkLen);
      else if (chunkType == 0x004E4942u)  // 'BIN'
        glbBin.assign(bytes.begin() + static_cast<std::ptrdiff_t>(off),
                      bytes.begin() + static_cast<std::ptrdiff_t>(off + chunkLen));
      off += chunkLen;
      off = (off + 3) & ~size_t(3);  // chunks are 4-byte aligned
    }
    if (jsonText.empty()) {
      out.error = "GLB contains no JSON chunk.";
      return out;
    }
  } else {
    jsonText.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }

  json root;
  try {
    root = json::parse(jsonText);
  } catch (const std::exception& e) {
    out.error = std::string("glTF JSON is malformed: ") + e.what();
    return out;
  }
  if (!root.is_object()) {
    out.error = "glTF root is not a JSON object.";
    return out;
  }
  if (root.contains("asset") && root["asset"].contains("version")) {
    const std::string v = root["asset"]["version"].get<std::string>();
    if (v.rfind("2.", 0) != 0) {
      out.error = "glTF asset version " + v + " is not supported (only 2.x).";
      return out;
    }
  }

  // Features present but not imported. REQ-065 requires these be reported, not dropped in silence.
  auto noteSkipped = [&](const char* key, const char* what) {
    if (root.contains(key) && root[key].is_array() && !root[key].empty())
      out.skipped.push_back(std::string(what) + " (" + std::to_string(root[key].size()) + ")");
  };
  noteSkipped("animations", "animations");
  noteSkipped("skins", "skins");
  noteSkipped("cameras", "cameras");
  noteSkipped("textures", "textures");
  noteSkipped("images", "images");

  // --- buffers ---------------------------------------------------------------------------------
  std::vector<std::vector<std::uint8_t>> buffers;
  if (root.contains("buffers") && root["buffers"].is_array()) {
    for (const auto& b : root["buffers"]) {
      std::vector<std::uint8_t> data;
      if (!b.contains("uri")) {
        data = glbBin;  // the GLB BIN chunk
      } else {
        const std::string uri = b["uri"].get<std::string>();
        const std::string kB64 = ";base64,";
        const size_t at = uri.find(kB64);
        if (uri.rfind("data:", 0) == 0 && at != std::string::npos) {
          if (!DecodeBase64(uri.substr(at + kB64.size()), &data)) {
            out.error = "a data: buffer URI is not valid base64.";
            return out;
          }
        } else {
          std::filesystem::path p = std::filesystem::path(baseDir) / std::filesystem::u8path(uri);
          std::ifstream f(p, std::ios::binary);
          if (!f) {
            out.error = "external buffer not found: " + uri;
            return out;
          }
          data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
      }
      if (b.contains("byteLength")) {
        const size_t declared = b["byteLength"].get<size_t>();
        if (data.size() < declared) {
          out.error = "buffer is shorter than its declared byteLength (" + std::to_string(data.size()) + " < " +
                      std::to_string(declared) + ") — file is truncated.";
          return out;
        }
      }
      buffers.push_back(std::move(data));
    }
  }

  // --- accessor reader -------------------------------------------------------------------------
  const json emptyArray = json::array();
  const json& bufferViews = root.contains("bufferViews") ? root["bufferViews"] : emptyArray;
  const json& accessors = root.contains("accessors") ? root["accessors"] : emptyArray;

  auto makeReader = [&](int accessorIndex, Reader* rd, std::string* err) -> bool {
    if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= accessors.size()) {
      *err = "accessor index " + std::to_string(accessorIndex) + " is out of range.";
      return false;
    }
    const json& a = accessors[static_cast<size_t>(accessorIndex)];
    if (a.contains("sparse")) {
      *err = "sparse accessors are not supported.";
      return false;
    }
    rd->componentType = a.value("componentType", 0);
    rd->components = ComponentsFor(a.value("type", std::string()));
    rd->count = a.value("count", size_t{0});
    const int csize = ComponentSize(rd->componentType);
    if (csize == 0 || rd->components == 0) {
      *err = "accessor has an unsupported componentType/type combination.";
      return false;
    }
    if (!a.contains("bufferView")) {
      *err = "accessor without a bufferView is not supported.";
      return false;
    }
    const int bvIndex = a["bufferView"].get<int>();
    if (bvIndex < 0 || static_cast<size_t>(bvIndex) >= bufferViews.size()) {
      *err = "bufferView index is out of range.";
      return false;
    }
    const json& bv = bufferViews[static_cast<size_t>(bvIndex)];
    const int bufIndex = bv.value("buffer", -1);
    if (bufIndex < 0 || static_cast<size_t>(bufIndex) >= buffers.size()) {
      *err = "buffer index is out of range.";
      return false;
    }
    const std::vector<std::uint8_t>& buf = buffers[static_cast<size_t>(bufIndex)];
    const size_t bvOffset = bv.value("byteOffset", size_t{0});
    const size_t bvLength = bv.value("byteLength", size_t{0});
    const size_t accOffset = a.value("byteOffset", size_t{0});
    rd->stride = bv.value("byteStride", size_t{0});
    if (rd->stride == 0)
      rd->stride = static_cast<size_t>(csize) * static_cast<size_t>(rd->components);  // tightly packed
    if (bvOffset > buf.size() || bvOffset + bvLength > buf.size()) {
      *err = "bufferView runs past the end of its buffer — file is truncated.";
      return false;
    }
    // The last element must fit: offset + (count-1)*stride + elementSize.
    const size_t elemSize = static_cast<size_t>(csize) * static_cast<size_t>(rd->components);
    if (rd->count > 0) {
      const size_t need = accOffset + (rd->count - 1) * rd->stride + elemSize;
      if (need > bvLength) {
        *err = "accessor runs past the end of its bufferView — file is truncated.";
        return false;
      }
    }
    rd->base = buf.data() + bvOffset + accOffset;
    rd->size = bvLength - accOffset;
    return true;
  };

  // --- node walk -------------------------------------------------------------------------------
  const json& nodes = root.contains("nodes") ? root["nodes"] : emptyArray;
  const json& meshes = root.contains("meshes") ? root["meshes"] : emptyArray;
  const json& materials = root.contains("materials") ? root["materials"] : emptyArray;

  bool skippedNonTriangles = false;
  bool skippedMorphTargets = false;

  // Emits one primitive, with \p world already composed from the whole node chain.
  auto emitPrimitive = [&](const json& prim, const Mat4& world, const std::string& nodeName,
                           std::string* err) -> bool {
    if (prim.value("mode", kModeTriangles) != kModeTriangles) {
      skippedNonTriangles = true;  // points and lines are not surfaces; REQ-063 stores triangles
      return true;
    }
    if (prim.contains("targets"))
      skippedMorphTargets = true;
    if (!prim.contains("attributes") || !prim["attributes"].contains("POSITION")) {
      *err = "a primitive has no POSITION attribute.";
      return false;
    }
    Reader pos;
    if (!makeReader(prim["attributes"]["POSITION"].get<int>(), &pos, err))
      return false;
    if (pos.components != 3) {
      *err = "POSITION accessor is not VEC3.";
      return false;
    }
    Reader nrm;
    bool haveNormals = false;
    if (prim["attributes"].contains("NORMAL")) {
      if (!makeReader(prim["attributes"]["NORMAL"].get<int>(), &nrm, err))
        return false;
      haveNormals = nrm.components == 3 && nrm.count == pos.count;
    }

    const std::uint32_t baseVertex = static_cast<std::uint32_t>(out.vertsXyz.size() / 3);
    const int pcs = ComponentSize(pos.componentType);
    for (size_t v = 0; v < pos.count; ++v) {
      const std::uint8_t* p = pos.base + v * pos.stride;
      const double gx = ReadComponent(p, pos.componentType);
      const double gy = ReadComponent(p + pcs, pos.componentType);
      const double gz = ReadComponent(p + 2 * pcs, pos.componentType);
      double lx = 0, ly = 0, lz = 0;
      TransformPoint(world, gx, gy, gz, &lx, &ly, &lz);
      double cx = 0, cy = 0, cz = 0;
      GltfYUpToCadZUp(lx, ly, lz, &cx, &cy, &cz);
      // Scale and insertion applied in DOUBLE, before narrowing — the local-storage invariant. A
      // model placed at state-plane coordinates keeps sub-hundredth precision only because this
      // arithmetic happens here rather than after the cast.
      out.vertsXyz.push_back(static_cast<float>(cx * opt.unitScale + opt.insertX));
      out.vertsXyz.push_back(static_cast<float>(cy * opt.unitScale + opt.insertY));
      out.vertsXyz.push_back(static_cast<float>(cz * opt.unitScale + opt.insertZ));

      if (haveNormals) {
        const std::uint8_t* n = nrm.base + v * nrm.stride;
        const int ncs = ComponentSize(nrm.componentType);
        const double nxg = ReadComponent(n, nrm.componentType);
        const double nyg = ReadComponent(n + ncs, nrm.componentType);
        const double nzg = ReadComponent(n + 2 * ncs, nrm.componentType);
        double tx = 0, ty = 0, tz = 0;
        TransformDirection(world, nxg, nyg, nzg, &tx, &ty, &tz);
        double ncx = 0, ncy = 0, ncz = 0;
        GltfYUpToCadZUp(tx, ty, tz, &ncx, &ncy, &ncz);
        const double len = std::sqrt(ncx * ncx + ncy * ncy + ncz * ncz);
        if (len > 1e-20) { ncx /= len; ncy /= len; ncz /= len; } else { ncx = 0; ncy = 0; ncz = 1; }
        out.normalsXyz.push_back(static_cast<float>(ncx));
        out.normalsXyz.push_back(static_cast<float>(ncy));
        out.normalsXyz.push_back(static_cast<float>(ncz));
      } else {
        out.normalsXyz.push_back(0.f);
        out.normalsXyz.push_back(0.f);
        out.normalsXyz.push_back(0.f);  // filled in by ComputeVertexNormals at the end
      }
    }

    ImportedPart part;
    part.name = nodeName;
    part.indexBegin = static_cast<int>(out.indices.size());

    if (prim.contains("indices")) {
      Reader idx;
      if (!makeReader(prim["indices"].get<int>(), &idx, err))
        return false;
      const int ics = ComponentSize(idx.componentType);
      for (size_t k = 0; k < idx.count; ++k) {
        const double vi = ReadComponent(idx.base + k * idx.stride, idx.componentType);
        const std::uint32_t rel = static_cast<std::uint32_t>(vi);
        if (rel >= pos.count) {
          *err = "an index refers to a vertex the primitive does not have — file is corrupt.";
          return false;
        }
        out.indices.push_back(baseVertex + rel);
        (void)ics;
      }
    } else {
      // Non-indexed primitive: vertices are consumed in order.
      for (size_t k = 0; k < pos.count; ++k)
        out.indices.push_back(baseVertex + static_cast<std::uint32_t>(k));
    }
    part.indexCount = static_cast<int>(out.indices.size()) - part.indexBegin;
    if (part.indexCount % 3 != 0) {
      *err = "a triangle primitive has an index count that is not a multiple of 3.";
      return false;
    }

    // Base colour from the PBR material. Only the factor: textures are out of scope (REQ-065), and
    // a textured material still has a factor worth honouring.
    const int matIndex = prim.value("material", -1);
    if (matIndex >= 0 && static_cast<size_t>(matIndex) < materials.size()) {
      const json& mat = materials[static_cast<size_t>(matIndex)];
      if (mat.contains("pbrMetallicRoughness") && mat["pbrMetallicRoughness"].contains("baseColorFactor")) {
        const json& bc = mat["pbrMetallicRoughness"]["baseColorFactor"];
        if (bc.is_array() && bc.size() >= 3) {
          part.r = bc[0].get<float>();
          part.g = bc[1].get<float>();
          part.b = bc[2].get<float>();
        }
      }
      if (part.name.empty() && mat.contains("name"))
        part.name = mat["name"].get<std::string>();
    }
    if (part.indexCount > 0)
      out.parts.push_back(std::move(part));
    return true;
  };

  // Depth-first over the node hierarchy, composing transforms parent → child. Iterative with an
  // explicit stack and a visit set: a malformed file can contain a cycle, and recursion on one is a
  // stack overflow rather than an error message.
  struct Pending {
    int node;
    Mat4 parent;
    int depth;
  };
  std::vector<Pending> stack;
  std::vector<char> visiting(nodes.size(), 0);

  std::vector<int> roots;
  if (root.contains("scenes") && root["scenes"].is_array() && !root["scenes"].empty()) {
    const size_t sceneIdx = root.value("scene", size_t{0});
    const json& scene = root["scenes"][std::min(sceneIdx, root["scenes"].size() - 1)];
    if (scene.contains("nodes"))
      for (const auto& n : scene["nodes"])
        roots.push_back(n.get<int>());
  }
  if (roots.empty())  // no scene: import every node, which is what a bare mesh export produces
    for (size_t i = 0; i < nodes.size(); ++i)
      roots.push_back(static_cast<int>(i));

  for (auto it = roots.rbegin(); it != roots.rend(); ++it)
    stack.push_back(Pending{*it, Mat4{}, 0});

  std::string err;
  while (!stack.empty()) {
    const Pending cur = stack.back();
    stack.pop_back();
    if (cur.node < 0 || static_cast<size_t>(cur.node) >= nodes.size())
      continue;
    if (cur.depth > 512) {
      out.error = "node hierarchy is deeper than 512 levels — the file is probably cyclic.";
      return out;
    }
    if (visiting[static_cast<size_t>(cur.node)]) {
      out.error = "node hierarchy contains a cycle.";
      return out;
    }
    visiting[static_cast<size_t>(cur.node)] = 1;

    const json& node = nodes[static_cast<size_t>(cur.node)];
    Mat4 local;
    if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() == 16)
      local = MatrixFromColumnMajor(node["matrix"]);
    else
      local = MatrixFromTrs(node);
    const Mat4 world = Multiply(cur.parent, local);

    const std::string nodeName = node.contains("name") ? node["name"].get<std::string>()
                                                       : ("node " + std::to_string(cur.node));
    if (node.contains("mesh")) {
      const int mi = node["mesh"].get<int>();
      if (mi >= 0 && static_cast<size_t>(mi) < meshes.size()) {
        const json& m = meshes[static_cast<size_t>(mi)];
        std::string meshName = nodeName;
        if (node.contains("name") == false && m.contains("name"))
          meshName = m["name"].get<std::string>();
        if (m.contains("primitives") && m["primitives"].is_array()) {
          for (const auto& prim : m["primitives"]) {
            if (!emitPrimitive(prim, world, meshName, &err)) {
              out.error = err;
              return out;  // nothing is committed — `out` is discarded by the caller on !ok
            }
          }
        }
      }
    }
    if (node.contains("children"))
      for (const auto& c : node["children"])
        stack.push_back(Pending{c.get<int>(), world, cur.depth + 1});
  }

  if (skippedNonTriangles)
    out.skipped.push_back("point/line primitives");
  if (skippedMorphTargets)
    out.skipped.push_back("morph targets");

  if (out.indices.empty()) {
    out.error = "the file contains no triangle geometry.";
    return out;
  }

  // Any vertex whose normal is still zero came from a primitive with no NORMAL attribute; compute
  // them so shading is not black there (REQ-064's shader treats a zero normal as facing away).
  bool anyZeroNormal = false;
  for (size_t i = 0; i + 2 < out.normalsXyz.size(); i += 3) {
    if (out.normalsXyz[i] == 0.f && out.normalsXyz[i + 1] == 0.f && out.normalsXyz[i + 2] == 0.f) {
      anyZeroNormal = true;
      break;
    }
  }
  if (anyZeroNormal) {
    std::vector<float> computed;
    meshgeom::ComputeVertexNormals(out.vertsXyz, out.indices, &computed);
    if (computed.size() == out.normalsXyz.size()) {
      for (size_t i = 0; i + 2 < out.normalsXyz.size(); i += 3) {
        if (out.normalsXyz[i] == 0.f && out.normalsXyz[i + 1] == 0.f && out.normalsXyz[i + 2] == 0.f) {
          out.normalsXyz[i] = computed[i];
          out.normalsXyz[i + 1] = computed[i + 1];
          out.normalsXyz[i + 2] = computed[i + 2];
        }
      }
    }
  }

  // Final structural check with the same validator the `.gs` loader uses, so an importer bug cannot
  // put geometry on the GPU that a loaded file would have been refused for.
  std::vector<std::pair<int, int>> ranges;
  ranges.reserve(out.parts.size());
  for (const ImportedPart& p : out.parts)
    ranges.emplace_back(p.indexBegin, p.indexCount);
  const meshgeom::MeshProblem problem =
      meshgeom::ValidateMesh(out.vertsXyz, out.normalsXyz, out.indices, ranges);
  if (problem != meshgeom::MeshProblem::Ok) {
    out.error = std::string("imported geometry failed validation — ") + meshgeom::MeshProblemText(problem);
    return out;
  }

  out.ok = true;
  return out;
}

ImportResult ImportGltfFile(const std::string& pathUtf8, const ImportOptions& opt) {
  ImportResult out;
  std::filesystem::path p = std::filesystem::u8path(pathUtf8);
  std::ifstream f(p, std::ios::binary);
  if (!f) {
    out.error = "could not open " + pathUtf8;
    return out;
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (bytes.empty()) {
    out.error = "file is empty.";
    return out;
  }
  // Detect by content, not by extension: a .gltf that is really a GLB (or the reverse) is common
  // enough, and the magic is definitive.
  const bool isGlb = bytes.size() >= 4 && bytes[0] == 'g' && bytes[1] == 'l' && bytes[2] == 'T' && bytes[3] == 'F';
  const std::string baseDir = p.parent_path().u8string();
  return ImportGltfBytes(bytes, isGlb, baseDir, opt);
}

} // namespace gltf
