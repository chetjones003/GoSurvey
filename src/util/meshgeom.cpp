#include "meshgeom.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace meshgeom {

Bounds ComputeBounds(const std::vector<float>& vertsXyz) {
  Bounds b;
  if (vertsXyz.empty() || vertsXyz.size() % 3 != 0)
    return b;  // stays invalid — an empty mesh is not a mesh at the origin
  b.valid = true;
  b.mnX = b.mxX = vertsXyz[0];
  b.mnY = b.mxY = vertsXyz[1];
  b.mnZ = b.mxZ = vertsXyz[2];
  for (size_t i = 3; i + 2 < vertsXyz.size(); i += 3) {
    b.mnX = std::min(b.mnX, vertsXyz[i]);
    b.mxX = std::max(b.mxX, vertsXyz[i]);
    b.mnY = std::min(b.mnY, vertsXyz[i + 1]);
    b.mxY = std::max(b.mxY, vertsXyz[i + 1]);
    b.mnZ = std::min(b.mnZ, vertsXyz[i + 2]);
    b.mxZ = std::max(b.mxZ, vertsXyz[i + 2]);
  }
  return b;
}

void ExpandBounds(Bounds* acc, const Bounds& add) {
  if (!acc || !add.valid)
    return;
  if (!acc->valid) {
    *acc = add;
    return;
  }
  acc->mnX = std::min(acc->mnX, add.mnX);
  acc->mxX = std::max(acc->mxX, add.mxX);
  acc->mnY = std::min(acc->mnY, add.mnY);
  acc->mxY = std::max(acc->mxY, add.mxY);
  acc->mnZ = std::min(acc->mnZ, add.mnZ);
  acc->mxZ = std::max(acc->mxZ, add.mxZ);
}

const char* MeshProblemText(MeshProblem p) {
  switch (p) {
  case MeshProblem::Ok:                    return "ok";
  case MeshProblem::VertsNotTriples:       return "vertex array length is not a multiple of 3";
  case MeshProblem::NormalsMismatch:       return "normal count does not match vertex count";
  case MeshProblem::IndicesNotTriangles:   return "index count is not a multiple of 3";
  case MeshProblem::IndexOutOfRange:       return "an index refers to a vertex that does not exist";
  case MeshProblem::PartRangeOutOfBounds:  return "a part's index range runs past the end of the mesh";
  case MeshProblem::PartCountNotTriangles: return "a part covers a partial triangle";
  }
  return "unknown problem";
}

MeshProblem ValidateMesh(const std::vector<float>& vertsXyz, const std::vector<float>& normalsXyz,
                         const std::vector<std::uint32_t>& indices,
                         const std::vector<std::pair<int, int>>& partRanges) {
  if (vertsXyz.size() % 3 != 0)
    return MeshProblem::VertsNotTriples;
  // Normals are all-or-nothing: an empty array means "compute them", but a partial one means the
  // source disagreed with itself and guessing which vertices it meant would be inventing geometry.
  if (!normalsXyz.empty() && normalsXyz.size() != vertsXyz.size())
    return MeshProblem::NormalsMismatch;
  if (indices.size() % 3 != 0)
    return MeshProblem::IndicesNotTriangles;

  const std::uint32_t vertexCount = static_cast<std::uint32_t>(vertsXyz.size() / 3);
  for (std::uint32_t idx : indices) {
    if (idx >= vertexCount)
      return MeshProblem::IndexOutOfRange;
  }

  const long long n = static_cast<long long>(indices.size());
  for (const auto& pr : partRanges) {
    const long long begin = pr.first;
    const long long count = pr.second;
    if (begin < 0 || count < 0 || begin + count > n)
      return MeshProblem::PartRangeOutOfBounds;
    if (count % 3 != 0)
      return MeshProblem::PartCountNotTriangles;
  }
  return MeshProblem::Ok;
}

void ComputeVertexNormals(const std::vector<float>& vertsXyz, const std::vector<std::uint32_t>& indices,
                          std::vector<float>* outNormals) {
  if (!outNormals)
    return;
  outNormals->assign(vertsXyz.size(), 0.f);
  if (vertsXyz.size() % 3 != 0 || indices.size() % 3 != 0)
    return;
  const std::uint32_t vertexCount = static_cast<std::uint32_t>(vertsXyz.size() / 3);

  for (size_t t = 0; t + 2 < indices.size(); t += 3) {
    const std::uint32_t ia = indices[t], ib = indices[t + 1], ic = indices[t + 2];
    if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount)
      continue;  // ValidateMesh rejects these; tolerate here so a bad call cannot read out of bounds
    const float* a = &vertsXyz[static_cast<size_t>(ia) * 3];
    const float* b = &vertsXyz[static_cast<size_t>(ib) * 3];
    const float* c = &vertsXyz[static_cast<size_t>(ic) * 3];
    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    // Un-normalised cross product: its LENGTH is twice the triangle's area, which is exactly the
    // area weighting we want. Normalising here would give a sliver the same say as a large face.
    const float nx = e1[1] * e2[2] - e1[2] * e2[1];
    const float ny = e1[2] * e2[0] - e1[0] * e2[2];
    const float nz = e1[0] * e2[1] - e1[1] * e2[0];
    for (std::uint32_t v : {ia, ib, ic}) {
      (*outNormals)[static_cast<size_t>(v) * 3 + 0] += nx;
      (*outNormals)[static_cast<size_t>(v) * 3 + 1] += ny;
      (*outNormals)[static_cast<size_t>(v) * 3 + 2] += nz;
    }
  }

  for (size_t i = 0; i + 2 < outNormals->size(); i += 3) {
    const float len = std::sqrt((*outNormals)[i] * (*outNormals)[i] + (*outNormals)[i + 1] * (*outNormals)[i + 1] +
                                (*outNormals)[i + 2] * (*outNormals)[i + 2]);
    if (len > 1.e-20f) {
      (*outNormals)[i] /= len;
      (*outNormals)[i + 1] /= len;
      (*outNormals)[i + 2] /= len;
    } else {
      // An unreferenced or fully degenerate vertex. +Z rather than zero: a zero normal reaches the
      // shader as a black fragment, which reads as a hole in the model.
      (*outNormals)[i] = 0.f;
      (*outNormals)[i + 1] = 0.f;
      (*outNormals)[i + 2] = 1.f;
    }
  }
}

} // namespace meshgeom
