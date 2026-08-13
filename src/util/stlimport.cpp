#include "stlimport.hpp"

#include "meshgeom.hpp"

#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace stl {
namespace {

float ReadF32(const std::uint8_t* p) {
  float v;
  std::memcpy(&v, p, 4);
  return v;
}

/// A binary STL is 84 bytes of header + exactly 50 per triangle. That equality is the only reliable
/// way to tell the two formats apart: an ASCII file starts with "solid", but so can a binary one's
/// 80-byte comment header, and trusting the prefix misreads real files written by real tools.
bool LooksBinary(const std::vector<std::uint8_t>& b) {
  if (b.size() < 84)
    return false;
  std::uint32_t tri = 0;
  std::memcpy(&tri, &b[80], 4);
  const std::uint64_t expect = 84ull + static_cast<std::uint64_t>(tri) * 50ull;
  return expect == b.size();
}

void PushVertex(modelimport::Result* out, double x, double y, double z, const modelimport::Options& opt) {
  // Scale and insertion in DOUBLE before narrowing — the local-storage invariant, same as glTF.
  out->vertsXyz.push_back(static_cast<float>(x * opt.unitScale + opt.insertX));
  out->vertsXyz.push_back(static_cast<float>(y * opt.unitScale + opt.insertY));
  out->vertsXyz.push_back(static_cast<float>(z * opt.unitScale + opt.insertZ));
}

} // namespace

modelimport::Result ImportStlBytes(const std::vector<std::uint8_t>& bytes, const modelimport::Options& opt,
                                   const std::string& partName) {
  modelimport::Result out;
  if (bytes.size() < 15) {
    out.error = "STL is too small to contain any geometry.";
    return out;
  }

  // STL is already Z-up (AutoCAD's convention), so unlike glTF there is no axis remap here. Saying
  // so explicitly because the ABSENCE of a conversion is exactly the kind of thing that looks like
  // an oversight later.
  if (LooksBinary(bytes)) {
    std::uint32_t tri = 0;
    std::memcpy(&tri, &bytes[80], 4);
    out.vertsXyz.reserve(static_cast<size_t>(tri) * 9);
    out.normalsXyz.reserve(static_cast<size_t>(tri) * 9);
    out.indices.reserve(static_cast<size_t>(tri) * 3);
    for (std::uint32_t t = 0; t < tri; ++t) {
      const std::uint8_t* rec = &bytes[84 + static_cast<size_t>(t) * 50];
      const float nx = ReadF32(rec);
      const float ny = ReadF32(rec + 4);
      const float nz = ReadF32(rec + 8);
      const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
      for (int v = 0; v < 3; ++v) {
        const std::uint8_t* p = rec + 12 + v * 12;
        PushVertex(&out, ReadF32(p), ReadF32(p + 4), ReadF32(p + 8), opt);
        if (len > 1.e-20f) {
          out.normalsXyz.push_back(nx / len);
          out.normalsXyz.push_back(ny / len);
          out.normalsXyz.push_back(nz / len);
        } else {
          out.normalsXyz.push_back(0.f);  // zero facet normal: recomputed below
          out.normalsXyz.push_back(0.f);
          out.normalsXyz.push_back(0.f);
        }
        out.indices.push_back(static_cast<std::uint32_t>(out.indices.size()));
      }
    }
  } else {
    // ASCII. Tolerant of whitespace and of the optional "endsolid" name, but NOT of a vertex count
    // that is not a multiple of three — a truncated ASCII STL ends mid-facet and must be refused.
    std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream in(text);
    std::string tok;
    float nx = 0.f, ny = 0.f, nz = 0.f;
    size_t vertsInFacet = 0;
    while (in >> tok) {
      if (tok == "facet") {
        if (in >> tok && tok == "normal")
          in >> nx >> ny >> nz;
        vertsInFacet = 0;
      } else if (tok == "vertex") {
        double x = 0, y = 0, z = 0;
        if (!(in >> x >> y >> z)) {
          out.error = "ASCII STL ends part-way through a vertex — file is truncated.";
          return out;
        }
        PushVertex(&out, x, y, z, opt);
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1.e-20f) {
          out.normalsXyz.push_back(nx / len);
          out.normalsXyz.push_back(ny / len);
          out.normalsXyz.push_back(nz / len);
        } else {
          out.normalsXyz.push_back(0.f);
          out.normalsXyz.push_back(0.f);
          out.normalsXyz.push_back(0.f);
        }
        out.indices.push_back(static_cast<std::uint32_t>(out.indices.size()));
        ++vertsInFacet;
      } else if (tok == "endfacet") {
        if (vertsInFacet != 3) {
          out.error = "ASCII STL contains a facet with " + std::to_string(vertsInFacet) +
                      " vertices — only triangles are valid.";
          return out;
        }
      }
    }
    if (out.indices.size() % 3 != 0) {
      out.error = "ASCII STL ends part-way through a facet — file is truncated.";
      return out;
    }
  }

  if (out.indices.empty()) {
    out.error = "the STL contains no triangles.";
    return out;
  }

  // Facet normals that were zero (or absent) get computed, so nothing shades black.
  bool anyZero = false;
  for (size_t i = 0; i + 2 < out.normalsXyz.size(); i += 3)
    if (out.normalsXyz[i] == 0.f && out.normalsXyz[i + 1] == 0.f && out.normalsXyz[i + 2] == 0.f) {
      anyZero = true;
      break;
    }
  if (anyZero) {
    std::vector<float> computed;
    meshgeom::ComputeVertexNormals(out.vertsXyz, out.indices, &computed);
    if (computed.size() == out.normalsXyz.size())
      for (size_t i = 0; i + 2 < out.normalsXyz.size(); i += 3)
        if (out.normalsXyz[i] == 0.f && out.normalsXyz[i + 1] == 0.f && out.normalsXyz[i + 2] == 0.f) {
          out.normalsXyz[i] = computed[i];
          out.normalsXyz[i + 1] = computed[i + 1];
          out.normalsXyz[i + 2] = computed[i + 2];
        }
  }

  modelimport::Part part;
  part.name = partName;
  part.indexBegin = 0;
  part.indexCount = static_cast<int>(out.indices.size());
  out.parts.push_back(std::move(part));
  // The honest consequence of the format, surfaced rather than left for the user to notice: STL has
  // no colours and no object names, so the model is one part (REQ-201 / ADR-026's reason for
  // preferring glTF).
  out.skipped.push_back("per-object colours and names (STL carries neither)");

  const meshgeom::MeshProblem problem =
      meshgeom::ValidateMesh(out.vertsXyz, out.normalsXyz, out.indices, {{0, part.indexCount}});
  if (problem != meshgeom::MeshProblem::Ok) {
    out.error = std::string("imported geometry failed validation — ") + meshgeom::MeshProblemText(problem);
    return out;
  }
  out.ok = true;
  return out;
}

modelimport::Result ImportStlFile(const std::string& pathUtf8, const modelimport::Options& opt) {
  modelimport::Result out;
  const std::filesystem::path p = std::filesystem::u8path(pathUtf8);
  std::ifstream f(p, std::ios::binary);
  if (!f) {
    out.error = "could not open " + pathUtf8;
    return out;
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return ImportStlBytes(bytes, opt, p.stem().u8string());
}

} // namespace stl
