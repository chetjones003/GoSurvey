#pragma once

/// Pipe run solid generation (GitHub issue #486 increment B1 / REQ-345). Header-only so Catch2 can
/// cover the NPS lookup and the swept solids without GL, the same reason cadblock.hpp is.
///
/// A `CadPipeRun` (CadEntities.hpp) stores only its path and a nominal-size LABEL — never a
/// solid. This file is the one place that label becomes a physical radius and the path becomes
/// cylinders, so nothing else in the codebase invents a second NPS table or a second sweep.

#include "CadEntities.hpp"
#include "brep.hpp"
#include "cadsolid.hpp"
#include "ray3d.hpp"
#include "ucs.hpp"

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

/// One entry of the standard NPS (nominal pipe size) → outer-diameter table. Outer diameter alone
/// is what a swept pipe solid needs — wall thickness (which pressure class actually governs) does
/// not change the modeled OD, so the table is keyed on size only, not size+class.
struct CadPipeNpsEntry {
  double nps;    ///< nominal size in inches, e.g. 4.0 for "4in"
  double odIn;   ///< outer diameter in inches
};

/// Standard-wall NPS → OD table for the sizes this codebase's fittings library targets (issue
/// #486). Deliberately small and exact-match only: a size the table does not carry is a SPEC GAP
/// for the catalog (increment B4), not something to interpolate or guess.
inline constexpr CadPipeNpsEntry kCadPipeNpsTable[] = {
    {0.5, 0.840},  {0.75, 1.050}, {1.0, 1.315},  {1.25, 1.660}, {1.5, 1.900},
    {2.0, 2.375},  {2.5, 2.875}, {3.0, 3.500},  {4.0, 4.500},  {6.0, 6.625},
    {8.0, 8.625},  {10.0, 10.750}, {12.0, 12.750},
};

/// Parses an NPS label like `"4in"` or `"1.5in"` into inches. Returns false for anything that
/// doesn't parse as `<number>in` (case-insensitive, optional whitespace before "in") — a label
/// like a raw fraction ("1/2in") is out of scope until the catalog work needs it (SPEC GAP, not
/// guessed here).
[[nodiscard]] inline bool CadParsePipeNominalSizeInches(std::string_view label, double* outInches) {
  if (!outInches)
    return false;
  size_t end = label.size();
  while (end > 0 && (label[end - 1] == ' ' || label[end - 1] == '\t'))
    --end;
  if (end < 3)
    return false;
  const char c0 = label[end - 2];
  const char c1 = label[end - 1];
  const bool hasIn = (c0 == 'i' || c0 == 'I') && (c1 == 'n' || c1 == 'N');
  if (!hasIn)
    return false;
  std::string numPart(label.substr(0, end - 2));
  while (!numPart.empty() && (numPart.back() == ' ' || numPart.back() == '\t'))
    numPart.pop_back();
  if (numPart.empty())
    return false;
  char* parseEnd = nullptr;
  const double v = std::strtod(numPart.c_str(), &parseEnd);
  if (parseEnd == numPart.c_str() || parseEnd != numPart.c_str() + numPart.size())
    return false;
  if (!(v > 0.0))
    return false;
  *outInches = v;
  return true;
}

/// Looks up a pipe run's `nominalSize` label in \ref kCadPipeNpsTable and returns its outer
/// diameter in FEET (drawing units, D-2026-09-12 decision 4 — NPS labels display in inches, run
/// geometry is in feet). Returns false (and leaves `*odFeet` untouched) for an unparsable label or
/// a size the table does not carry.
[[nodiscard]] inline bool CadPipeNominalOdFeet(std::string_view nominalSize, double* odFeet) {
  if (!odFeet)
    return false;
  double nps = 0.0;
  if (!CadParsePipeNominalSizeInches(nominalSize, &nps))
    return false;
  for (const CadPipeNpsEntry& e : kCadPipeNpsTable) {
    if (std::fabs(e.nps - nps) < 1e-9) {
      *odFeet = (e.odIn / 12.0);
      return true;
    }
  }
  return false;
}

/// Builds one right-circular-cylinder solid per straight segment of \p run's path, outer diameter
/// from \ref CadPipeNominalOdFeet. Appends to \p out (does not clear it first). A run with fewer
/// than 2 vertices, an unresolvable nominal size, or a degenerate (zero-length / ill-defined-axis)
/// segment simply contributes no solid for that segment — the caller sees fewer solids than
/// segments rather than a crash or a garbage cylinder, consistent with REQ-201 (nothing invalid is
/// ever stored). Returns true iff at least one solid was appended.
[[nodiscard]] inline bool CadBuildPipeRunSolids(const CadPipeRun& run, std::vector<CadSolidPtr>* out) {
  if (!out)
    return false;
  double odFeet = 0.0;
  if (!CadPipeNominalOdFeet(run.nominalSize, &odFeet))
    return false;
  const double radius = odFeet * 0.5;
  if (!(radius > 0.0))
    return false;
  const size_t nVerts = run.vertsXyz.size() / 3;
  if (nVerts < 2)
    return false;
  bool any = false;
  for (size_t i = 0; i + 1 < nVerts; ++i) {
    const ray3d::Vec3 p0{run.vertsXyz[i * 3 + 0], run.vertsXyz[i * 3 + 1], run.vertsXyz[i * 3 + 2]};
    const ray3d::Vec3 p1{run.vertsXyz[(i + 1) * 3 + 0], run.vertsXyz[(i + 1) * 3 + 1],
                          run.vertsXyz[(i + 1) * 3 + 2]};
    const ray3d::Vec3 dir = ray3d::Sub(p1, p0);
    const double height = ray3d::Length(dir);
    if (!(height > 1e-9))
      continue;  // coincident vertices — no segment to sweep
    ucs::Ucs frame;
    if (!ucs::FromNormal(p0, dir, &frame))
      continue;  // degenerate axis; ucs::FromNormal already refused it
    brep::Solid solid;
    brep::Problem why = brep::Problem::Ok;
    if (!brep::MakeCylinder(frame, radius, height, &solid, &why))
      continue;
    out->push_back(std::make_shared<const brep::Solid>(std::move(solid)));
    any = true;
  }
  return any;
}
