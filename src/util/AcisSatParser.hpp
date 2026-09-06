#pragma once

#include "brep.hpp"

#include <string>

/// ACIS SAT ("Standard ACIS Text") record parser (REQ-320 / ADR-051, GitHub issue #299).
///
/// Some vendor block-library `.dwg`/`.dxf` files (Plant 3D piping symbols, mechanical fittings) carry
/// their geometry as a single ACIS `3DSOLID` entity rather than lines/circles/polylines. LibreDWG
/// decrypts the container but hands back only the raw ACIS record stream — it never tessellates or
/// otherwise interprets it. This module turns that stream into a \ref brep::Solid, entirely in-tree
/// (REQ-300 forbids vendoring ACIS/OpenCASCADE/Parasolid): it reads the ACIS topology graph directly
/// (body -> lump -> shell -> face -> loop -> coedge -> edge -> vertex -> point) and maps its analytic
/// surfaces onto the existing \ref brep::SurfaceKind values.
///
/// **Scope (ADR-051), all by deliberate, recorded decision — not an oversight:**
/// - **SAT (text) only.** SAB (binary ACIS) is a different token framing of the same record model and
///   is deferred to GitHub issue #301. The caller (`LibreDwgCad.cpp`) is expected to check the DWG
///   `3DSOLID`'s `version` field and refuse SAB before ever calling this parser.
/// - **Analytic primitive surfaces: plane/cylinder/cone**, plus **free-form and derived surfaces**
///   (`spline-surface`, `blend-surface`, `sweep-surface` — GitHub issue #300) that map onto
///   `SurfaceKind::Nurbs` (REQ-315/ADR-048) when representable. `sphere-surface` and `torus-surface`
///   are recognized but refused — their loops can pinch at a pole or wrap a periodic tube seam, a
///   genuinely different shape than cylinder/cone's two recognizable loop patterns below, and are a
///   tracked fast-follow of this same feature rather than a hastily-generalized recognizer
///   (ADR-051 (b-1)).
/// - **A `spline-surface` face's loop must bound the whole, untrimmed patch rectangle** (four edges at
///   the patch's four corners) — ADR-048 (b) never allows a proper trim, so a spline surface with a
///   genuinely trimmed boundary is refused by name rather than approximated. Degree is limited to
///   `nurbs::kMaxDegree` (3) per direction, the same limit every other NURBS patch in this kernel has.
///   A `blend-surface` or `sweep-surface` is accepted only when it names, as its representable
///   reduction, a surface this importer can otherwise build (plane/cone/spline) — a genuinely
///   variable-radius blend or a swept/lofted surface with no such reduction is refused by name.
/// - **A cylindrical or conical face's loop must be a full revolve** (two full-circle rim edges,
///   `v0 == v1` on each, no seam) — its u-span is simply `[0, 2*pi)`. A **partial** revolve (a seam
///   line, an arc, a seam line, an arc) is recognizable but deliberately NOT accepted this increment:
///   its u-span has to be derived from the seam edges' actual angular position, a materially
///   different and separately-tested derivation, so it is refused by name and left as a fast-follow
///   of this same feature rather than landed without a fixture that exercises it. A general trimmed
///   loop is refused too; that is issue #302's kernel extension, not this parser's to solve. A planar
///   face's boundary has no such restriction (the kernel already tessellates an arbitrary simple
///   polygon of line/arc edges for a plane, per `Problem::PlaneFaceNotSimple`'s existing "no holes"
///   limit).
/// - **One lump, one shell, solid bodies only.** A `wire` body (curves, no faces) or a `sheet` body
///   (an open shell) is refused; a body with more than one lump or a shell with more than one sub-shell
///   is refused. These are all real ACIS possibilities this increment does not need to reach: the
///   flange/fitting symbols issue #299 was filed against are each a single solid lump.
///
/// Anything outside this scope is **refused with a specific message naming the record/face involved,
/// never approximated and never silently dropped** (ADR-051 (d), REQ-201) — the whole point of this
/// feature is that today's silent skip produces an empty block with no explanation.
namespace acissat {

/// The outcome of importing one ACIS body from a SAT stream.
struct ImportResult {
  bool ok = false;
  brep::Solid solid;
  /// Set only when \ref ok is false: a short, specific, user-facing sentence naming what could not be
  /// represented (e.g. "edge 12: curve kind 'intcurve' is not supported (ACIS import, GitHub #300)").
  std::string error;
};

/// Parses \p sat (the decrypted SAT text LibreDWG exposes as `Dwg_Entity__3DSOLID::acis_data`) and
/// builds a single `brep::Solid` from its one supported body. \p entityLabel is used only to make the
/// error message specific (e.g. a DWG entity handle) and may be empty.
[[nodiscard]] ImportResult ImportSatSolid(const std::string& sat, const std::string& entityLabel);

}  // namespace acissat
