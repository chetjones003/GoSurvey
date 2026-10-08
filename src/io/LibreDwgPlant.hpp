#pragma once

#include "util/brep.hpp"

#include <string>
#include <vector>

struct _dwg_struct;
struct _dwg_object;

/// Plant 3D part placement (REQ-320 increment 2, D-2026-10-08-b, GitHub issue #786).
///
/// A Plant 3D / PI piping drawing stores each fitting's B-rep once, as the 3DSOLID of a
/// `Plant3DCatalogItem_*` block definition, and places it with a Plant custom entity — no INSERT
/// stands in for it. A straight pipe stores no solid at all, only its centreline and radius. This
/// module reads just those entities' placement fields from the bits LibreDWG leaves undecoded
/// (`Dwg_Object::unknown_bits`) — never Plant's parametric engine data (ADR-026) — and every decode
/// must end exactly where the entity's data ends, so a layout this reader does not understand is
/// refused by name rather than misplaced.
namespace libredwgplant {

/// One catalog block a part places: a block-local point `p` lands at `origin + R p`, with `R`
/// row-major and its columns the block's X, Y, Z axes in world coordinates.
struct BlockPlacement {
  unsigned long long blockHandle = 0;
  double rot[9] = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  brep::Vec3 origin{};
};

struct Part {
  enum class Kind { Fitting, Connector, Pipe, Beam };
  Kind kind = Kind::Fitting;
  /// Fitting: exactly one. Connector: one per block sub-part (a weld has none).
  std::vector<BlockPlacement> blocks;
  brep::Vec3 pipeStart{};      ///< Pipe: centreline start. Beam: member start.
  brep::Vec3 pipeEnd{};        ///< Pipe: centreline end. Beam: member end.
  double pipeRadius = 0.0;     ///< Pipe only.
  double pipeStartTrim = 0.0;  ///< Pipe only: the solid starts this far along from `pipeStart`.
  double pipeEndTrim = 0.0;    ///< Pipe only: the solid stops this far short of `pipeEnd`.
  brep::Vec3 beamX{};          ///< Beam only: flange-width direction, unit length.
  double beamDepth = 0.0;      ///< Beam only: overall depth, drawing units (inches in this fixture).
  double beamWidth = 0.0;      ///< Beam only: flange width.
  double beamFlange = 0.0;     ///< Beam only: flange thickness.
  double beamWeb = 0.0;        ///< Beam only: web thickness.
};

/// The Plant 3D class name of \p obj (`ACPPPIPEINLINEASSET`, `ACPPCONNECTOR`, `ACPPPIPE`,
/// `ACPPSTRUCTUREBEAM`, ...) read from the drawing's class table, or empty when \p obj is not a
/// Plant 3D entity.
[[nodiscard]] std::string PlantClassName(const _dwg_struct* dwg, const _dwg_object* obj);

/// Decodes \p obj's placement. Returns false and sets \p why to a short reason naming what could
/// not be read when the entity is not one of the three placed kinds or its data does not have the
/// layout below (REQ-201: refused by name, never guessed).
[[nodiscard]] bool DecodePart(const _dwg_struct* dwg, const _dwg_object* obj, Part* out, std::string* why);

/// \p local (block coordinates, block base point at the origin) moved rigidly to \p at, then
/// shifted by `-docOrigin` (REQ-101 local storage). False when \p at is not a proper rotation.
[[nodiscard]] bool PlaceSolid(const brep::Solid& local, const BlockPlacement& at, const brep::Vec3& docOrigin,
                              brep::Solid* out);

/// A straight pipe as a solid cylinder along its centreline, less its start and end cut-backs,
/// shifted by `-docOrigin`.
[[nodiscard]] bool MakePipe(const Part& pipe, const brep::Vec3& docOrigin, brep::Solid* out);

/// A straight wide-flange beam extruded along its centreline, shifted by `-docOrigin`. The
/// cross-section is the published outer dimensions of a standard W4–W27 shape (no fillet).
[[nodiscard]] bool MakeBeam(const Part& beam, const brep::Vec3& docOrigin, brep::Solid* out);

}  // namespace libredwgplant
