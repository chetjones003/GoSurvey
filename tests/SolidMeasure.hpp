#pragma once

#include "util/brep.hpp"
#include "util/ray3d.hpp"

#include <cmath>
#include <cstdint>

/// Volume (signed tetrahedra) and axis-aligned extents of \p s's tessellated mesh — what the
/// viewport draws — shifted by \p offset. Used to compare imported solids with AutoCAD's MASSPROP
/// (issue #786) independently of brep's own integrals.
inline bool MeshVolumeAndExtents(const brep::Solid& s, const brep::Vec3& offset, double* volume, brep::Vec3* mn,
                                 brep::Vec3* mx) {
  brep::Tessellation t;
  if (!brep::Tessellate(s, 1e-3, &t, nullptr) || t.vertsXyz.size() < 9)
    return false;
  const auto vert = [&](std::uint32_t i) {
    return brep::Vec3{t.vertsXyz[3 * i], t.vertsXyz[3 * i + 1], t.vertsXyz[3 * i + 2]};
  };
  // Tetrahedra about the mesh's own centre: per-face tessellations meet with hairline seams, and a
  // seam's contribution grows with its distance from the apex, so a far-away apex (the origin, for
  // a part placed thousands of units out) would turn those seams into a visible volume error.
  brep::Vec3 c0{};
  const std::uint32_t n = static_cast<std::uint32_t>(t.vertsXyz.size() / 3);
  for (std::uint32_t i = 0; i < n; ++i)
    c0 = ray3d::Add(c0, vert(i));
  c0 = ray3d::Scale(c0, 1.0 / n);
  double v6 = 0.0;
  for (size_t k = 0; k + 2 < t.indices.size(); k += 3) {
    const brep::Vec3 a = ray3d::Sub(vert(t.indices[k]), c0);
    const brep::Vec3 b = ray3d::Sub(vert(t.indices[k + 1]), c0);
    const brep::Vec3 c = ray3d::Sub(vert(t.indices[k + 2]), c0);
    v6 += ray3d::Dot(a, ray3d::Cross(b, c));
  }
  *volume = std::fabs(v6) / 6.0;
  *mn = *mx = vert(0);
  for (std::uint32_t i = 0; 3 * i + 2 < t.vertsXyz.size(); ++i) {
    const brep::Vec3 p = vert(i);
    mn->x = p.x < mn->x ? p.x : mn->x;
    mn->y = p.y < mn->y ? p.y : mn->y;
    mn->z = p.z < mn->z ? p.z : mn->z;
    mx->x = p.x > mx->x ? p.x : mx->x;
    mx->y = p.y > mx->y ? p.y : mx->y;
    mx->z = p.z > mx->z ? p.z : mx->z;
  }
  *mn = ray3d::Add(*mn, offset);
  *mx = ray3d::Add(*mx, offset);
  return true;
}
