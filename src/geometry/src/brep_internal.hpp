#pragma once

#include <GoSurvey/Geometry/brep.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <unordered_map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace brep {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kHalfPi = 0.5 * kPi;

/// The z-bounds of a plane-cut cylinder face, each `c0 + c1 cos u + c2 sin u` in the surface frame.
struct CylinderCut {
  double lo[3] = {0.0, 0.0, 0.0};
  double hi[3] = {0.0, 0.0, 0.0};
};

/// The axial extent of a cylinder face bounded by a procedural `Intersection` edge, as a function of
/// longitude `u`: at each `u` the face spans `[zLo(u), zHi(u)]` on its own surface, where the surface
/// crosses the *other* surface the edge carries. Set up once per face, then queried per `u`.
struct IsectStrip {
  const Surface* sf = nullptr;
  const Surface* other = nullptr;
  double zSearchLo = 0.0;
  double zSearchHi = 0.0;
  bool oneSided = false;  ///< the loop has ONE Intersection edge: the strip runs curve <-> `rimZ`.
  double rimZ = 0.0;      ///< the constant axial bound (a flat rim) opposite the curve, if `oneSided`.

  /// REQ-339 shoulder-crossing extension (GitHub issue #504). Set only when the loop's Intersection
  /// edges reference TWO distinct "other" surfaces rather than one — a coaxial-stack wall or a
  /// cutter face whose mouth curve is clipped by a shoulder plane partway along. `other` is
  /// whichever of the two governs the side with the smaller `clipZ`-frame coordinate, `other2` the
  /// side with the larger. `hasClip`/`clipKeepBelow` additionally clip a computed `[zLo, zHi]` strip
  /// to one side of `clipZ` — used by a coaxial-stack WALL face's own bite (its mouth curve search
  /// against the cutter alone would otherwise overshoot past the shoulder into the neighbour
  /// segment's territory, per the root-cause note on REQ-339's 2026-09-14 revision).
  const Surface* other2 = nullptr;
  double clipZ = 0.0;
  bool hasClip = false;
  bool clipKeepBelow = true;
  /// Set alongside `hasClip`: the face's own full (untouched-by-the-notch) axial span, in `sf`'s own
  /// frame — a coaxial-stack wall band is FULL height at most longitudes, dipping to the notch only
  /// near the cutter; where `IsectStripAt` finds no crossing at all (outside the notch), that full
  /// span is the face's real material there, not nothing.
  double fullLo = 0.0;
  double fullHi = 0.0;

  [[nodiscard]] bool valid() const { return sf && other && zSearchHi > zSearchLo; }
};

/// A **cone** face bounded by one oblique-plane cut and one flat rim — a `SliceConeOblique` piece
/// (REQ-314 B2b-2 tail, GitHub issue #283, TASK-204). The cut is either a `CurveKind::Ellipse` (the
/// ellipse regime, slice (a)) or a `CurveKind::Intersection` against a `SurfaceKind::Plane`
/// co-surface (the parabola/hyperbola single-notch regime, slice (b)) — either way the plane's
/// params are recovered from the edge's own stored geometry, so this face costs nothing beyond
/// whichever edge kind is already there.
struct ConeCutStrip {
  const Surface* sf = nullptr;
  double rimZ = 0.0;
  bool haveRim = false;
  int cutCount = 0;
  static constexpr int kMaxCuts = 2;
  double nx[kMaxCuts]{};
  double ny[kMaxCuts]{};
  double nz[kMaxCuts]{};
  double planeC[kMaxCuts]{};
  [[nodiscard]] bool valid() const { return sf != nullptr; }
};

/// The latitude extent of a **sphere** face bounded by a procedural `Intersection` edge, as a
/// function of longitude `u`: at each `u` the meridian spans `[vLo(u), vHi(u)]` between the two
/// crossings of the *other* surface the edge carries (REQ-314 B2b-2, GitHub issue #242 — the
/// sphere ∩ cylinder offset quartic). The `v`-analogue of \ref IsectStrip.
struct SphereIsectStrip {
  const Surface* sf = nullptr;     ///< the sphere
  const Surface* other = nullptr;  ///< the surface it crosses (a cylinder)
  double vSearchLo = 0.0;
  double vSearchHi = 0.0;
  [[nodiscard]] bool valid() const { return sf && other && vSearchHi > vSearchLo; }
};

/// Which two faces use each edge, in one sweep.
struct EdgeUseTable {
  std::vector<std::array<int, 2>> face;
  std::vector<std::array<int, 2>> loop;
  std::vector<std::array<int, 2>> slot;
  std::vector<std::array<bool, 2>> rev;
  std::vector<int> count;
};

/// One requested edge of a blend — a fillet or a chamfer — reduced to what the crowding pre-check
/// below needs. Both operations fill this identically; only the SETBACK is derived differently (the
/// fillet converts a radius through `r / tan(theta/2)`, the chamfer is handed it).
struct BlendSpan {
  Vec3 p[2]{};                       ///< the edge's two endpoint positions
  Vec3 t{};                          ///< unit, `p[0]` -> `p[1]`
  double length = 0.0;
  int v[2] = {-1, -1};               ///< the original endpoint indices
  int face[2] = {-1, -1};
  Vec3 u[2]{};                       ///< in-face unit perpendicular to the edge, INTO the material
  double setback = 0.0;              ///< how far the blend cuts into each adjacent face
  double consumed[2] = {0.0, 0.0};   ///< taken off this edge's own LENGTH at each end, by a corner
};

enum class BlendFit { Ok, EdgeTooShort, Overlaps };

/// The right circular solid with a circle of \p rA at \p centreA and one of \p rB at \p centreB,
/// stated the way `MakeCylinder` / `MakeCone` state it: the LARGER circle is the base (which is what
/// `MakeCone` requires), the frame's Z runs from it to the other. False when degenerate.
struct Conical {
  ucs::Ucs frame;
  double rBase = 0.0;
  double rTop = 0.0;
  double height = 0.0;
  bool cylinder = false;
};

// ---------------------------------------------------------------------------------------------
// Feature operations — Slice (REQ-314 / ADR-046 increment 3, GitHub issue #147).
//
// The first operation that operates on an existing solid's topology rather than building from a
// profile, and the machinery the analytic Booleans reuse: classify each vertex against a plane,
// clip the faces the plane crosses, and stitch the pieces back into closed shells with a new planar
// cap. Increment 3a handles planar-faced solids; a curved face is refused, because an oblique plane
// through a cylinder cuts an ellipse the kernel's `{Line, Arc}` curves cannot hold.
// ---------------------------------------------------------------------------------------------


/// A polygon plus the outward normal of the face it will become — the slice's working form before
/// shared vertices and edges are welded back together.
struct PolyFace {
  std::vector<Vec3> ring;
  Vec3 normal;
};

/// The ellipse a plane cuts from a **cone** — unlike a cylinder, the cone's radius grows along its
/// axis, so the axis-plane intersection point is NOT the ellipse's centre (REQ-314 B2b-2 tail,
/// GitHub issue #283, TASK-204). Derived by expressing the cone as the quadric `X^2+Y^2 = k^2 Z^2` in
/// apex-centred local coordinates (`k = (r1-r0)/h`, the cone's slope; apex at local `z = -r0/k`),
/// substituting a general in-plane parametrisation `X = Q0 + p*e1 + q*e2` (`Q0` = axis ∩ plane, `e1,e2`
/// an arbitrary in-plane orthonormal basis with `(e1, e2, normal)` right-handed), and diagonalising the
/// resulting 2-D conic `A p^2 + 2 D p q + B q^2 + E p + F q + G = 0` by the standard symmetric-2x2
/// eigen-decomposition. Verified numerically (worst residual ~1e-9 of the ellipse equation over 20000
/// random cone/plane configurations, including survey-scale frames) before being coded here.
struct ConeObliqueEllipse {
  Vec3 centre{};    ///< world
  Vec3 normal{};    ///< world, the +axis-ish orientation (matches `dotNZ > 0 ? pn : -pn`)
  Vec3 majorDir{};  ///< world, unit, the larger semi-axis direction
  double majorSemi = 0.0;
  double minorSemi = 0.0;
  double zLo = 0.0;  ///< the ellipse's own extreme axial (local Z) extent, for the cap-clip check
  double zHi = 0.0;
};

enum class BoolOp { Union, Subtract, Intersect };

// ---------------------------------------------------------------------------------------------
// Curved Boolean operands — the B1 subset, refined by D-2026-09-02-b: a curved operand is handled
// for UNION and INTERSECT only, and only when it is a right circular cylinder that meets the other
// solid along full circles. A curved SUBTRACT bores a hole whose wall faces inward, which
// `Surface` cannot express — deferred to B2, refused `BooleanCurvedFace` here. An oblique cylinder
// (an ellipse) is refused `BooleanObliqueCylinder`. Two configurations are recognised:
//   A. the cylinder's axis is perpendicular to two planar faces of the other solid, its circular
//      footprint clear inside both  — INTERSECT is the plug, UNION is a boss with the two faces bored;
//   B. two coaxial cylinders — INTERSECT is the shared segment, UNION is a merged or stepped stack.
// Anything else falls through and is refused by the planar path.
// ---------------------------------------------------------------------------------------------

struct CylinderShape {
  ucs::Ucs axis;        ///< origin = base-cap centre, zAxis = axis direction.
  double radius = 0.0;
  double length = 0.0;
};

struct SphereShape {
  Vec3 centre;
  double radius = 0.0;
};

/// One physical band of a coaxial stack (issue #495 / REQ-338 338c/338d-1), position along the
/// stack's own shared axis line (not a length). \p r0 is the radius at \p z0, \p r1 the radius at
/// \p z1 — equal for a cylindrical band, different for a conical (tapered) one (338d-1).
struct CoaxialSeg {
  double z0 = 0.0;
  double z1 = 0.0;
  double r0 = 0.0;
  double r1 = 0.0;
};

bool AllFinite(std::initializer_list<double> vs);
bool FinitePoint(const Vec3& p);
int AddVertex(Solid* s, const Vec3& p);
int AddLine(Solid* s, int v0, int v1);
int AddArc(Solid* s, int v0, int v1, const Vec3& centre, const Vec3& normal, double sweep);
int AddEllipse(Solid* s, int v0, int v1, const Vec3& centre, const Vec3& normal, const Vec3& majorDir, double a, double b, double sweep);
Surface PlaneSurface(const Vec3& origin, const Vec3& outwardNormal);
Face MakePlaneFace(const Vec3& origin, const Vec3& outwardNormal, std::vector<EdgeUse> uses);
void PlaceInFrame(Solid* s, const ucs::Ucs& frame);
void AddSingleShell(Solid* s);
bool Fail(Problem why, Problem* outWhy);
bool Succeed(Problem* outWhy);
bool FrameOk(const ucs::Ucs& frame);
double PlaneFaceArea(const Solid& s, const Face& f);
bool CylinderCutZExtent(const Solid& s, const Face& f, CylinderCut* out);
double SignedDistToSurface(const Surface& sf, const Vec3& p);
std::vector<Vec3> MarchIntersectionCurve(const Solid& s, const Edge& e, int n);
bool LoopHasIntersectionEdge(const Solid& s, const Loop& lp);
bool FaceLoopHasIntersectionEdge(const Solid& s, const Face& f);
bool FaceLoopHasEllipseEdge(const Solid& s, const Face& f);
IsectStrip MakeIsectStrip(const Solid& s, const Face& f);
bool IsectStripAt(const IsectStrip& st, double u, double* zLo, double* zHi);
ConeCutStrip MakeConeCutStrip(const Solid& s, const Face& f);
bool RevolutionFaceNeedsBoundaryIntegral(const Solid& s, const Face& f);
bool ConeCutStripAt(const ConeCutStrip& st, double u, double* zLo, double* zHi);
SphereIsectStrip MakeSphereIsectStrip(const Solid& s, const Face& f);
void SphereStripsAt(const SphereIsectStrip& st, double u, std::vector<std::pair<double, double>>* out);
Vec3 ReferencePoint(const Solid& s);
double VolumeAbout(const Solid& s, const Vec3& q, bool* outFinite, bool honorGeneralLoops);
double ModelScale(const Solid& s);
Vec3 ConicalPoint(const Surface& sf, double r0, double r1, double t, double z);
Vec3 CylinderPoint(const Surface& sf, double u, double v);
Vec3 SphericalPoint(const Surface& sf, double t, double v);
Vec3 ToroidalPoint(const Surface& sf, double t, double v);
void LocalSurfaceDerivs(const Surface& sf, double u, double v, Vec3* p, Vec3* su, Vec3* sv);
std::vector<double> GeneralLoopScanline(const std::vector<std::vector<curveisect::Vec2>>& paramLoops, double v);
double EllipseParamOf(const Edge& e, const Vec3& p);
void CollectFaceVertices(const Solid& s, const Face& f, std::vector<int>* out);
bool SolveThreePlanes(const Vec3& n0, double d0, const Vec3& n1, double d1, const Vec3& n2, double d2, Vec3* outW);
std::vector<int> FaceVertexSet(const Solid& s, const Face& f);
void CompactUnused(Solid* s);
EdgeUseTable BuildEdgeUseTable(const Solid& s);
BlendFit BlendsFit(const std::vector<BlendSpan>& spans);
bool ConicalBetween(const Vec3& centreA, double rA, const Vec3& centreB, double rB, Conical* out);
void StampConicalRecipe(Solid* s, const Conical& c);
bool WeldPlanarSolid(const std::vector<PolyFace>& polys, double scale, Problem complexReason, Solid* out, Problem* outWhy);
bool ComputeConeObliqueEllipse(const ucs::Ucs& fr, double r0, double r1, double h, const Vec3& planePoint, const Vec3& pn, double eps, ConeObliqueEllipse* out);
std::vector<Vec3> FaceRing(const Solid& s, const Face& f);
bool PointInPolygon3D(const Vec3& hit, const std::vector<Vec3>& ring, const Vec3& n, double eps, bool* onEdge);
bool PointInPlanarSolid(const Vec3& p, const Solid& s, double scale);
bool AabbsOverlap(const Solid& a, const Solid& b, double eps);
bool ClassifyCylinder(const Solid& s, CylinderShape* out);
bool AllFacesPlanar(const Solid& s);
bool BuildCoaxialStack(const ucs::Ucs& frame, const std::vector<double>& z, const std::vector<double>& rOutLo, const std::vector<double>& rOutHi, const std::vector<double>& rIn, Solid* out, Problem* outWhy);
bool BuildBore(const Solid& planar, int entryFace, const Vec3& entryC, const Vec3& nEntry, double radius, bool through, int exitFace, const Vec3& exitC, double blindDepth, Solid* out, Problem* outWhy);
bool BuildSteppedBore(const Solid& planar, int entryFace, const Vec3& entryC, const Vec3& nEntry, const std::vector<double>& radii, const std::vector<Vec3>& internalShoulders, int exitFace, const Vec3& exitC, Solid* out, Problem* outWhy);
bool ClassifySphere(const Solid& s, SphereShape* out);
bool TryBooleanSpherePlanar(const Solid& planar, const Solid& sph, const SphereShape& S, BoolOp op, bool sphIsMinuend, std::vector<Solid>* out, bool* handled, Problem* outWhy);
bool BuildBranchPipeSubtract(const ucs::Ucs& fr, double r, double R, double zB0, double zB1, Solid* out, Problem* outWhy);
bool BuildCoaxialStepRadialSubtract(const ucs::Ucs& fr, double r, double Ra, double Rb, double zB0, double Zs, double zB1, Solid* out, Problem* outWhy);
bool TryBooleanBranchPipe(const CylinderShape& A, const CylinderShape& B, BoolOp op, std::vector<Solid>* out, bool* handled, Problem* outWhy);
bool TryBooleanSteinmetz(const CylinderShape& A, const CylinderShape& B, BoolOp op, std::vector<Solid>* out, bool* handled, Problem* outWhy);
bool ExtractCoaxialStack(const Solid& s, Vec3* axisPoint, Vec3* axisDir, std::vector<CoaxialSeg>* segs);
bool TryBooleanCoaxialStackUnion(const Solid& a, const Solid& b, const Vec3& axisPoint, const Vec3& axisDir, const std::vector<CoaxialSeg>& segsA, const std::vector<CoaxialSeg>& segsBraw, std::vector<Solid>* out, Problem* outWhy);
bool TryBooleanCoaxialCylinders(const Solid& a, const Solid& b, const CylinderShape& A, const CylinderShape& B, BoolOp op, std::vector<Solid>* out, bool* handled, Problem* outWhy);
bool WeldCoaxialCap(const Solid& a, int faceA, const Solid& b, int faceB, Solid* out, Problem* outWhy);
bool TryBooleanCylinderThroughPlanar(const Solid& planar, const Solid& cyl, const CylinderShape& C, BoolOp op, bool cylIsMinuend, std::vector<Solid>* out, bool* handled, Problem* outWhy);
bool TryBooleanSphereCylinder(const SphereShape& S, const CylinderShape& C, BoolOp op, bool sphereIsMinuend, std::vector<Solid>* out, bool* handled, Problem* outWhy);

}  // namespace brep
