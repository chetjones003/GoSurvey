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
#include "brep_internal.hpp"

namespace brep {

namespace {

/// The Steinmetz bicylinder: the INTERSECT of two right circular cylinders of **equal radius** whose
/// axes **cross at right angles** (REQ-314 B2b-1 coda, D-2026-09-02-i). Their surfaces meet along two
/// full ellipses (the quartic intersection `x⁴ = …` factors into the planes `z = ±x`), so the result
/// is bounded by four cylinder half-patches and four half-ellipse edges — every edge a closed-form
/// `CurveKind::Ellipse`, no procedural curve. \p fr: origin at the axis crossing, `zAxis` along
/// cylinder A, `xAxis` along cylinder B. Volume is the textbook `16 r³ / 3`, area `16 r²`.
[[nodiscard]] bool BuildSteinmetzIntersection(const ucs::Ucs& fr, double r, Solid* out,
                                              Problem* outWhy) {
  Solid s;
  const Vec3 X = fr.xAxis;
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  const Vec3 c = fr.origin;
  const int v0 = AddVertex(&s, ray3d::Add(c, ray3d::Scale(Y, r)));   // cyl-A angle u = +pi/2
  const int v1 = AddVertex(&s, ray3d::Add(c, ray3d::Scale(Y, -r)));  // u = -pi/2
  const double a = r * std::sqrt(2.0);
  const double b = r;
  const Vec3 nP = ray3d::Normalize(ray3d::Sub(Z, X));  // plane z = x
  const Vec3 nM = ray3d::Normalize(ray3d::Add(Z, X));  // plane z = -x
  const Vec3 mP = ray3d::Normalize(ray3d::Add(Z, X));  // its major direction
  const Vec3 mM = ray3d::Normalize(ray3d::Sub(X, Z));
  // Four half-ellipse arcs, each from v0 to v1; +/- picks the x>0 or x<0 half.
  const int ePp = AddEllipse(&s, v0, v1, c, nP, mP, a, b, -kPi);  // z = x,  x > 0
  const int ePn = AddEllipse(&s, v0, v1, c, nP, mP, a, b, kPi);   // z = x,  x < 0
  const int eMp = AddEllipse(&s, v0, v1, c, nM, mM, a, b, -kPi);  // z = -x, x > 0
  const int eMn = AddEllipse(&s, v0, v1, c, nM, mM, a, b, kPi);   // z = -x, x < 0

  auto halfCyl = [&](const ucs::Ucs& sfFrame, double u0, double u1, int loEdge, bool loRev, int hiEdge,
                     bool hiRev) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = sfFrame;
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = 2.0 * r;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = {{loEdge, loRev}, {hiEdge, hiRev}};
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  ucs::Ucs frA = fr;
  frA.origin = ray3d::Sub(c, ray3d::Scale(Z, r));
  ucs::Ucs frB;
  frB.origin = ray3d::Sub(c, ray3d::Scale(X, r));
  frB.zAxis = X;
  frB.xAxis = Z;
  frB.yAxis = ray3d::Normalize(ray3d::Cross(X, Z));
  // Each face: one arc forward, one reversed, so every edge is used once each way overall.
  halfCyl(frA, -kHalfPi, kHalfPi, eMp, false, ePp, true);        // cyl A, x > 0
  halfCyl(frA, kHalfPi, kHalfPi + kPi, eMn, false, ePn, true);   // cyl A, x < 0
  halfCyl(frB, -kHalfPi, kHalfPi, ePp, false, eMn, true);        // cyl B, z > 0
  halfCyl(frB, kHalfPi, kHalfPi + kPi, ePn, false, eMp, true);   // cyl B, z < 0
  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A − B` where `A` and `B` are the Steinmetz pair: cylinder `A` (kept) with a full perpendicular
/// channel of cylinder `B` bored clean through it (REQ-314 B2b-1 coda, D-2026-09-02-i). \p fr: origin
/// at the axis crossing, `zAxis` along `A`, `xAxis` along `B`. \p zA0 / \p zA1 are `A`'s cap heights
/// along its axis, measured from the crossing (`zA0 < -r < 0 < r < zA1`). The two intersection
/// ellipses (planes `z = ±x`) split `A`'s wall into a top band and a bottom band; the channel wall is
/// two inward cylinder patches. Volume is `vol(A) − 16 r³ / 3`.
[[nodiscard]] bool BuildSteinmetzSubtract(const ucs::Ucs& fr, double r, double zA0, double zA1,
                                          Solid* out, Problem* outWhy) {
  Solid s;
  const Vec3 X = fr.xAxis;
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  const Vec3 c = fr.origin;
  auto P = [&](const Vec3& base, double z) { return ray3d::Add(base, ray3d::Scale(Z, z)); };
  const int v0 = AddVertex(&s, ray3d::Add(c, ray3d::Scale(Y, r)));   // ellipse crossing, +Y
  const int v1 = AddVertex(&s, ray3d::Add(c, ray3d::Scale(Y, -r)));  // ellipse crossing, -Y
  const int rTa = AddVertex(&s, P(ray3d::Add(c, ray3d::Scale(Y, r)), zA1));
  const int rTb = AddVertex(&s, P(ray3d::Add(c, ray3d::Scale(Y, -r)), zA1));
  const int rBa = AddVertex(&s, P(ray3d::Add(c, ray3d::Scale(Y, r)), zA0));
  const int rBb = AddVertex(&s, P(ray3d::Add(c, ray3d::Scale(Y, -r)), zA0));

  const double a = r * std::sqrt(2.0);
  const double b = r;
  const Vec3 nP = ray3d::Normalize(ray3d::Sub(Z, X));  // plane z = x
  const Vec3 nM = ray3d::Normalize(ray3d::Add(Z, X));  // plane z = -x
  const Vec3 mP = ray3d::Normalize(ray3d::Add(Z, X));
  const Vec3 mM = ray3d::Normalize(ray3d::Sub(X, Z));
  // The four half-ellipses. aP+/aM+ run v1 -> v0 (through the x>0 side); aP-/aM- run v0 -> v1 (x<0).
  const int aPp = AddEllipse(&s, v1, v0, c, nP, mP, a, b, kPi);
  const int aPn = AddEllipse(&s, v0, v1, c, nP, mP, a, b, kPi);
  const int aMp = AddEllipse(&s, v1, v0, c, nM, mM, a, b, kPi);
  const int aMn = AddEllipse(&s, v0, v1, c, nM, mM, a, b, kPi);

  const Vec3 topC = P(c, zA1);
  const Vec3 botC = P(c, zA0);
  const int rtP = AddArc(&s, rTb, rTa, topC, Z, kPi);   // top rim, x>0
  const int rtN = AddArc(&s, rTa, rTb, topC, Z, kPi);   // top rim, x<0
  const int rbP = AddArc(&s, rBb, rBa, botC, Z, kPi);   // bottom rim, x>0
  const int rbN = AddArc(&s, rBa, rBb, botC, Z, kPi);   // bottom rim, x<0
  const int sTa = AddLine(&s, v0, rTa);
  const int sTb = AddLine(&s, v1, rTb);
  const int sBa = AddLine(&s, v0, rBa);
  const int sBb = AddLine(&s, v1, rBb);

  auto cylBand = [&](const ucs::Ucs& sf, double h, double u0, double u1, bool inward,
                     std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = sf;
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = h;
    f.surface.inward = inward;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  ucs::Ucs frTop = fr;  // A's wall, crossing at z = 0, cap at z = zA1
  ucs::Ucs frBot = fr;
  frBot.origin = botC;
  ucs::Ucs frCh;  // cylinder B's channel wall
  frCh.origin = c;
  frCh.zAxis = X;
  frCh.xAxis = Z;
  frCh.yAxis = ray3d::Normalize(ray3d::Cross(X, Z));

  // A's top band (z >= |x|): two half-faces split through the crossing points.
  cylBand(frTop, zA1, -kHalfPi, kHalfPi, false, {{aPp, false}, {sTa, false}, {rtP, true}, {sTb, true}});
  cylBand(frTop, zA1, kHalfPi, kHalfPi + kPi, false,
          {{aMn, false}, {sTb, false}, {rtN, true}, {sTa, true}});
  // A's bottom band (z <= -|x|).
  cylBand(frBot, -zA0, -kHalfPi, kHalfPi, false,
          {{aMp, true}, {sBb, false}, {rbP, false}, {sBa, true}});
  cylBand(frBot, -zA0, kHalfPi, kHalfPi + kPi, false,
          {{aPn, true}, {sBa, false}, {rbN, false}, {sBb, true}});
  // A's two flat caps.
  s.faces.push_back(MakePlaneFace(topC, Z, {{rtP, false}, {rtN, false}}));
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{rbP, true}, {rbN, true}}));
  // B's channel wall, inward: two patches bounded only by ellipse arcs.
  cylBand(frCh, 2.0 * r, -kHalfPi, kHalfPi, true, {{aPp, true}, {aMn, true}});
  cylBand(frCh, 2.0 * r, kHalfPi, kHalfPi + kPi, true, {{aPn, false}, {aMp, false}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∪ B` of the Steinmetz pair — a T-pipe (or cross-pipe): both cylinders' walls outside the other,
/// plus all four flat caps (REQ-314 B2b-1 coda, D-2026-09-02-i). \p fr: origin at the crossing,
/// `zAxis` along `A`, `xAxis` along `B`. \p zA0 / \p zA1 and \p xB0 / \p xB1 are the two cylinders'
/// cap positions from the crossing (`… < -r < 0 < r < …`). The two intersection ellipses (planes
/// `z = ±x`) are the internal seam; each cylinder's wall becomes two bands (a top/bottom for `A`, a
/// `±x` pair for `B`), every band split into two half-faces through the crossing points. 10 vertices,
/// 20 edges, 12 faces. Volume `vol(A) + vol(B) − 16 r³ / 3`.
[[nodiscard]] bool BuildSteinmetzUnion(const ucs::Ucs& fr, double r, double zA0, double zA1,
                                       double xB0, double xB1, Solid* out, Problem* outWhy) {
  Solid s;
  const Vec3 X = fr.xAxis;
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  const Vec3 c = fr.origin;
  auto Wc = [&](double sx, double sz) {
    return ray3d::Add(c, ray3d::Add(ray3d::Scale(X, sx), ray3d::Scale(Z, sz)));
  };
  const int v0 = AddVertex(&s, ray3d::Add(c, ray3d::Scale(Y, r)));   // crossing, +Y
  const int v1 = AddVertex(&s, ray3d::Add(c, ray3d::Scale(Y, -r)));  // crossing, -Y
  const int rTa = AddVertex(&s, ray3d::Add(Wc(0.0, zA1), ray3d::Scale(Y, r)));
  const int rTb = AddVertex(&s, ray3d::Add(Wc(0.0, zA1), ray3d::Scale(Y, -r)));
  const int rBa = AddVertex(&s, ray3d::Add(Wc(0.0, zA0), ray3d::Scale(Y, r)));
  const int rBb = AddVertex(&s, ray3d::Add(Wc(0.0, zA0), ray3d::Scale(Y, -r)));
  const int qPa = AddVertex(&s, ray3d::Add(Wc(xB1, 0.0), ray3d::Scale(Y, r)));
  const int qPb = AddVertex(&s, ray3d::Add(Wc(xB1, 0.0), ray3d::Scale(Y, -r)));
  const int qNa = AddVertex(&s, ray3d::Add(Wc(xB0, 0.0), ray3d::Scale(Y, r)));
  const int qNb = AddVertex(&s, ray3d::Add(Wc(xB0, 0.0), ray3d::Scale(Y, -r)));

  const double a = r * std::sqrt(2.0);
  const double b = r;
  const Vec3 nP = ray3d::Normalize(ray3d::Sub(Z, X));  // plane z = x
  const Vec3 nM = ray3d::Normalize(ray3d::Add(Z, X));  // plane z = -x
  const Vec3 mP = ray3d::Normalize(ray3d::Add(Z, X));
  const Vec3 mM = ray3d::Normalize(ray3d::Sub(X, Z));
  const int ePp = AddEllipse(&s, v1, v0, c, nP, mP, a, b, kPi);  // z = x,  x > 0
  const int ePn = AddEllipse(&s, v0, v1, c, nP, mP, a, b, kPi);  // z = x,  x < 0
  const int eMp = AddEllipse(&s, v1, v0, c, nM, mM, a, b, kPi);  // z = -x, x > 0
  const int eMn = AddEllipse(&s, v0, v1, c, nM, mM, a, b, kPi);  // z = -x, x < 0

  const Vec3 topC = Wc(0.0, zA1);
  const Vec3 botC = Wc(0.0, zA0);
  const Vec3 pxC = Wc(xB1, 0.0);
  const Vec3 nxC = Wc(xB0, 0.0);
  const int rtP = AddArc(&s, rTb, rTa, topC, Z, kPi);   // A top rim, x > 0
  const int rtN = AddArc(&s, rTa, rTb, topC, Z, kPi);   // A top rim, x < 0
  const int rbP = AddArc(&s, rBb, rBa, botC, Z, kPi);   // A bottom rim, x > 0
  const int rbN = AddArc(&s, rBa, rBb, botC, Z, kPi);   // A bottom rim, x < 0
  const int rpA = AddArc(&s, qPb, qPa, pxC, X, kPi);    // B +x rim, z < 0
  const int rpB = AddArc(&s, qPa, qPb, pxC, X, kPi);    // B +x rim, z > 0
  const int rnA = AddArc(&s, qNb, qNa, nxC, X, kPi);    // B -x rim, z < 0
  const int rnB = AddArc(&s, qNa, qNb, nxC, X, kPi);    // B -x rim, z > 0
  const int sTa = AddLine(&s, v0, rTa);
  const int sTb = AddLine(&s, v1, rTb);
  const int sBa = AddLine(&s, v0, rBa);
  const int sBb = AddLine(&s, v1, rBb);
  const int sPa = AddLine(&s, v0, qPa);
  const int sPb = AddLine(&s, v1, qPb);
  const int sNa = AddLine(&s, v0, qNa);
  const int sNb = AddLine(&s, v1, qNb);

  auto band = [&](const ucs::Ucs& sf, double h, double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = sf;
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = h;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  ucs::Ucs frTop = fr;
  ucs::Ucs frBot = fr;
  frBot.origin = botC;
  ucs::Ucs frPx;
  frPx.origin = c;
  frPx.zAxis = X;
  frPx.xAxis = Z;
  frPx.yAxis = ray3d::Normalize(ray3d::Cross(X, Z));
  ucs::Ucs frNx = frPx;
  frNx.origin = nxC;

  // A's wall outside B: a top band (z >= |x|) and a bottom band, each two half-faces.
  band(frTop, zA1, -kHalfPi, kHalfPi, {{ePp, false}, {sTa, false}, {rtP, true}, {sTb, true}});
  band(frTop, zA1, kHalfPi, kHalfPi + kPi, {{eMn, false}, {sTb, false}, {rtN, true}, {sTa, true}});
  band(frBot, -zA0, -kHalfPi, kHalfPi, {{eMp, true}, {sBb, false}, {rbP, false}, {sBa, true}});
  band(frBot, -zA0, kHalfPi, kHalfPi + kPi, {{ePn, true}, {sBa, false}, {rbN, false}, {sBb, true}});
  s.faces.push_back(MakePlaneFace(topC, Z, {{rtP, false}, {rtN, false}}));
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{rbP, true}, {rbN, true}}));
  // B's wall outside A: a +x band and a -x band, each two half-faces (split at world z = 0).
  band(frPx, xB1, -kHalfPi, kHalfPi, {{ePp, true}, {sPb, false}, {rpB, true}, {sPa, true}});
  band(frPx, xB1, kHalfPi, kHalfPi + kPi, {{eMp, false}, {sPa, false}, {rpA, true}, {sPb, true}});
  band(frNx, -xB0, -kHalfPi, kHalfPi, {{eMn, true}, {sNa, false}, {rnB, false}, {sNb, true}});
  band(frNx, -xB0, kHalfPi, kHalfPi + kPi, {{ePn, false}, {sNb, false}, {rnA, false}, {sNa, true}});
  s.faces.push_back(MakePlaneFace(pxC, X, {{rpA, false}, {rpB, false}}));
  s.faces.push_back(MakePlaneFace(nxC, ray3d::Scale(X, -1.0), {{rnA, true}, {rnB, true}}));

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∩ B` of a thin cylinder `A` (radius \p r, axis `fr.xAxis`) fully crossing a thicker cylinder
/// `B` (radius \p R, axis `fr.zAxis`) at right angles through `fr.origin` — the branch-pipe lens
/// (REQ-314 B2b-2, D-2026-09-03-a). The two operands meet along two **quartic** loops (`fr` local:
/// `x = ±√(R² − r² sin² φ)`, `y = −r sin φ`, `z = r cos φ`), stored as eight `CurveKind::Intersection`
/// half-edges. 8 vertices, 10 edges, 4 faces — two half-bands of `A`'s wall inside `B`, two lens-end
/// patches of `B`'s wall inside `A`. Every face integrates numerically.
[[nodiscard]] bool BuildBranchPipeIntersection(const ucs::Ucs& fr, double r, double R, Solid* out,
                                               Problem* outWhy) {
  if (!(R > r) || !(r > 0.0))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 X = fr.xAxis;
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const double psi0 = std::asin(std::clamp(r / R, -1.0, 1.0));

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = fr.origin;
  aSurf.frame.zAxis = X;
  aSurf.frame.xAxis = Z;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 4.0 * R;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame.origin = fr.origin;
  bSurf.frame.zAxis = Z;
  bSurf.frame.xAxis = X;
  bSurf.frame.yAxis = Y;
  bSurf.radius = R;
  bSurf.height = 4.0 * R;

  Solid s;
  auto cplus = [&](double phi, int sign) {
    const double x = std::sqrt(std::max(0.0, R * R - r * r * std::sin(phi) * std::sin(phi))) * sign;
    return W(Vec3{x, -r * std::sin(phi), r * std::cos(phi)});
  };
  const int p0 = AddVertex(&s, cplus(0.0, 1));
  const int qb = AddVertex(&s, cplus(kHalfPi, 1));
  const int p1 = AddVertex(&s, cplus(kPi, 1));
  const int qa = AddVertex(&s, cplus(1.5 * kPi, 1));
  const int n0 = AddVertex(&s, cplus(0.0, -1));
  const int nb = AddVertex(&s, cplus(kHalfPi, -1));
  const int n1 = AddVertex(&s, cplus(kPi, -1));
  const int na = AddVertex(&s, cplus(1.5 * kPi, -1));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cplus(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int e1 = isect(p0, qb, 0.25 * kPi, 1);
  const int e2 = isect(qb, p1, 0.75 * kPi, 1);
  const int e3 = isect(p1, qa, 1.25 * kPi, 1);
  const int e4 = isect(qa, p0, 1.75 * kPi, 1);
  const int e5 = isect(n0, nb, 0.25 * kPi, -1);
  const int e6 = isect(nb, n1, 0.75 * kPi, -1);
  const int e7 = isect(n1, na, 1.25 * kPi, -1);
  const int e8 = isect(na, n0, 1.75 * kPi, -1);
  const int sTop = AddLine(&s, n0, p0);  // z = r seam of A
  const int sBot = AddLine(&s, n1, p1);  // z = -r seam of A

  auto cylFace = [&](const Surface& surf, double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  cylFace(aSurf, 0.0, kPi,
          {{e1, false}, {e2, false}, {sBot, true}, {e6, true}, {e5, true}, {sTop, false}});
  cylFace(aSurf, kPi, kTwoPi,
          {{e3, false}, {e4, false}, {sTop, true}, {e8, true}, {e7, true}, {sBot, false}});
  cylFace(bSurf, -psi0, psi0, {{e4, true}, {e3, true}, {e2, true}, {e1, true}});
  cylFace(bSurf, kPi - psi0, kPi + psi0, {{e5, false}, {e6, false}, {e7, false}, {e8, false}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}
} // namespace

/// `B − A`: the thick cylinder `B` (radius \p R, axis `fr.zAxis`, caps at \p zB0 / \p zB1) with the
/// thin branch cylinder `A` (radius \p r, axis `fr.xAxis`) bored clean through it — a genus-1 solid
/// (REQ-314 B2b-2). 8 vertices, 12 edges, 6 faces: `B`'s wall split in two by the `ψ = ±π/2` seams,
/// each half carrying the branch mouth as an inner loop; `B`'s two flat caps; and `A`'s wall inside
/// `B`, inward, in two halves. Volume `vol(B) − 16 r³/3`-analogue (the lens).
[[nodiscard]] bool BuildBranchPipeSubtract(const ucs::Ucs& fr, double r, double R, double zB0,
                                           double zB1, Solid* out, Problem* outWhy) {
  if (!(R > r) || !(r > 0.0) || !(zB1 - zB0 > 2.0 * r))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 X = fr.xAxis;
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = fr.origin;
  aSurf.frame.zAxis = X;
  aSurf.frame.xAxis = Z;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 4.0 * R;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame.origin = fr.origin;
  bSurf.frame.zAxis = Z;
  bSurf.frame.xAxis = X;
  bSurf.frame.yAxis = Y;
  bSurf.radius = R;
  bSurf.height = zB1 - zB0;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double x = std::sqrt(std::max(0.0, R * R - r * r * std::sin(phi) * std::sin(phi))) * sign;
    return W(Vec3{x, -r * std::sin(phi), r * std::cos(phi)});
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int bp = AddVertex(&s, W(Vec3{0.0, R, zB0}));
  const int bm = AddVertex(&s, W(Vec3{0.0, -R, zB0}));
  const int tp = AddVertex(&s, W(Vec3{0.0, R, zB1}));
  const int tm = AddVertex(&s, W(Vec3{0.0, -R, zB1}));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);   // C+ through Qa (psi > 0)
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);   // C+ through Qb (psi < 0)
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);  // C- through Na (psi < pi)
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);  // C- through Nb (psi > pi)
  const int sA0 = AddLine(&s, n0, p0);            // A seam z = r
  const int sAp = AddLine(&s, n1, p1);            // A seam z = -r
  const int seamP = AddLine(&s, bp, tp);          // B seam psi = +pi/2
  const int seamM = AddLine(&s, bm, tm);          // B seam psi = -pi/2
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int brF = AddArc(&s, bm, bp, botC, Z, kPi);  // bottom rim, front (psi -pi/2 -> pi/2)
  const int brB = AddArc(&s, bp, bm, botC, Z, kPi);  // bottom rim, back
  const int trF = AddArc(&s, tm, tp, topC, Z, kPi);  // top rim, front
  const int trB = AddArc(&s, tp, tm, topC, Z, kPi);  // top rim, back

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  face(bSurf, -kHalfPi, kHalfPi,
       {Loop{{{brF, false}, {seamP, false}, {trF, true}, {seamM, true}}},
        Loop{{{cpU, false}, {cpL, true}}}});
  face(bSurf, kHalfPi, kHalfPi + kPi,
       {Loop{{{brB, false}, {seamM, false}, {trB, true}, {seamP, true}}},
        Loop{{{cnU, true}, {cnL, false}}}});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brF, true}, {brB, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trF, false}, {trB, false}}));
  Surface aIn = aSurf;
  aIn.inward = true;
  face(aIn, 0.0, kPi, {Loop{{{cpL, false}, {sAp, true}, {cnL, true}, {sA0, false}}}});
  face(aIn, kPi, kTwoPi, {Loop{{{cpU, true}, {sA0, true}, {cnU, false}, {sAp, false}}}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `Blo ∪ Bhi − A`: a radial cross-hole (thin cylinder `A`, radius \p r, axis `fr.xAxis`) drilled
/// through a two-segment coaxial stack that steps from radius \p Ra (below \p Zs) to \p Rb (above),
/// where the hole crosses that one shoulder (REQ-339, GitHub issue #504 — the shape the 2026-09-14
/// revision withdrew, re-enabled once \ref IsectStripAt could take a second clipping bound).
/// `fr.zAxis` is the stack axis, `fr.origin` the point on it level with the hole's own centre
/// (so `-r < Zs < r`, and `zB0 < Zs < zB1` bound the two segments' own caps).
///
/// 20 vertices, 30 edges, 10 faces (verified against the withdrawn attempt's own topology count,
/// which was already correct — only the numeric integrator was not): the mouth curve on each of the
/// two wall radii is split into a Hi arc (crossing `Bhi`) and a Lo arc (crossing `Blo`), joined by a
/// straight generatrix on `A` itself where the mouth crosses the shoulder plane; the shoulder's own
/// annulus is left as two disconnected remnants (front/back of the hole) rather than a full ring.
[[nodiscard]] bool BuildCoaxialStepRadialSubtract(const ucs::Ucs& fr, double r, double Ra, double Rb,
                                                  double zB0, double Zs, double zB1, Solid* out,
                                                  Problem* outWhy) {
  if (!(r > 0.0) || !(Ra > r) || !(Rb > r) || std::fabs(Ra - Rb) < 1e-9 * std::max(Ra, Rb))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  if (!(zB0 < Zs) || !(Zs < zB1) || !(-r < Zs) || !(Zs < r))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = fr.origin;
  aSurf.frame.zAxis = fr.xAxis;
  aSurf.frame.xAxis = fr.zAxis;
  aSurf.frame.yAxis = ray3d::Scale(fr.yAxis, -1.0);
  aSurf.radius = r;
  aSurf.height = 4.0 * std::max(Ra, Rb);
  Surface raSurf;
  raSurf.kind = SurfaceKind::Cylinder;
  raSurf.frame.origin = fr.origin;
  raSurf.frame.zAxis = fr.zAxis;
  raSurf.frame.xAxis = fr.xAxis;
  raSurf.frame.yAxis = fr.yAxis;
  raSurf.radius = Ra;
  raSurf.height = Zs - zB0;
  Surface rbSurf = raSurf;
  rbSurf.radius = Rb;
  rbSurf.height = zB1 - Zs;

  auto cpt = [&](double phi, int sign, double R) {
    const double x = std::sqrt(std::max(0.0, R * R - r * r * std::sin(phi) * std::sin(phi))) * sign;
    return W(Vec3{x, -r * std::sin(phi), r * std::cos(phi)});
  };
  const double phiS = std::acos(std::clamp(Zs / r, -1.0, 1.0));
  const double Yh = r * std::sin(phiS);

  Solid s;
  const int P0 = AddVertex(&s, cpt(0.0, 1, Rb));
  const int Vb = AddVertex(&s, cpt(phiS, 1, Rb));
  const int Va = AddVertex(&s, cpt(phiS, 1, Ra));
  const int P1 = AddVertex(&s, cpt(kPi, 1, Ra));
  const int Va2 = AddVertex(&s, cpt(kTwoPi - phiS, 1, Ra));
  const int Vb2 = AddVertex(&s, cpt(kTwoPi - phiS, 1, Rb));
  const int N0 = AddVertex(&s, cpt(0.0, -1, Rb));
  const int Nb = AddVertex(&s, cpt(phiS, -1, Rb));
  const int Na = AddVertex(&s, cpt(phiS, -1, Ra));
  const int N1 = AddVertex(&s, cpt(kPi, -1, Ra));
  const int Na2 = AddVertex(&s, cpt(kTwoPi - phiS, -1, Ra));
  const int Nb2 = AddVertex(&s, cpt(kTwoPi - phiS, -1, Rb));
  const int Bp = AddVertex(&s, W(Vec3{0.0, Ra, zB0}));
  const int Bm = AddVertex(&s, W(Vec3{0.0, -Ra, zB0}));
  const int Tp = AddVertex(&s, W(Vec3{0.0, Rb, zB1}));
  const int Tm = AddVertex(&s, W(Vec3{0.0, -Rb, zB1}));
  const int RaTopP = AddVertex(&s, W(Vec3{0.0, Ra, Zs}));
  const int RaTopM = AddVertex(&s, W(Vec3{0.0, -Ra, Zs}));
  const int RbBotP = AddVertex(&s, W(Vec3{0.0, Rb, Zs}));
  const int RbBotM = AddVertex(&s, W(Vec3{0.0, -Rb, Zs}));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign, const Surface& wall) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign, wall.radius);
    e.isectSurfaces = {aSurf, wall};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int hiF_R = isect(P0, Vb, 0.5 * phiS, 1, rbSurf);
  const int loF_R = isect(Va, P1, 0.5 * (phiS + kPi), 1, raSurf);
  const int loF_L = isect(P1, Va2, 0.5 * (kPi + kTwoPi - phiS), 1, raSurf);
  const int hiF_L = isect(Vb2, P0, 0.5 * (kTwoPi - phiS + kTwoPi), 1, rbSurf);
  const int hiB_R = isect(N0, Nb, 0.5 * phiS, -1, rbSurf);
  const int loB_R = isect(Na, N1, 0.5 * (phiS + kPi), -1, raSurf);
  const int loB_L = isect(N1, Na2, 0.5 * (kPi + kTwoPi - phiS), -1, raSurf);
  const int hiB_L = isect(Nb2, N0, 0.5 * (kTwoPi - phiS + kTwoPi), -1, rbSurf);

  const int genF1 = AddLine(&s, Va, Vb);
  const int genF2 = AddLine(&s, Va2, Vb2);
  const int genB1 = AddLine(&s, Na, Nb);
  const int genB2 = AddLine(&s, Na2, Nb2);
  const int seamTop = AddLine(&s, N0, P0);
  const int seamBot = AddLine(&s, N1, P1);

  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const Vec3 shoulderC = W(Vec3{0.0, 0.0, Zs});
  const Vec3 Zax = fr.zAxis;
  const int botRimF = AddArc(&s, Bm, Bp, botC, Zax, kPi);
  const int botRimB = AddArc(&s, Bp, Bm, botC, Zax, kPi);
  const int topRimF = AddArc(&s, Tm, Tp, topC, Zax, kPi);
  const int topRimB = AddArc(&s, Tp, Tm, topC, Zax, kPi);
  const int seamRaP = AddLine(&s, Bp, RaTopP);
  const int seamRaM = AddLine(&s, Bm, RaTopM);
  const int seamRbP = AddLine(&s, RbBotP, Tp);
  const int seamRbM = AddLine(&s, RbBotM, Tm);

  const double flankRa = kHalfPi - std::asin(std::clamp(Yh / Ra, -1.0, 1.0));
  const double flankRb = kHalfPi - std::asin(std::clamp(Yh / Rb, -1.0, 1.0));
  const int topFlatRaRight = AddArc(&s, RaTopM, Va, shoulderC, Zax, flankRa);
  const int topFlatRaLeft = AddArc(&s, Va2, RaTopP, shoulderC, Zax, flankRa);
  const int topFlatRaBackRight = AddArc(&s, RaTopP, Na2, shoulderC, Zax, flankRa);
  const int topFlatRaBackLeft = AddArc(&s, Na, RaTopM, shoulderC, Zax, flankRa);
  const int botFlatRbRight = AddArc(&s, RbBotM, Vb, shoulderC, Zax, flankRb);
  const int botFlatRbLeft = AddArc(&s, Vb2, RbBotP, shoulderC, Zax, flankRb);
  const int botFlatRbBackRight = AddArc(&s, RbBotP, Nb2, shoulderC, Zax, flankRb);
  const int botFlatRbBackLeft = AddArc(&s, Nb, RbBotM, shoulderC, Zax, flankRb);

  auto cylFace = [&](const Surface& surf, double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  cylFace(raSurf, -kHalfPi, kHalfPi,
          {{botRimF, false}, {seamRaP, false}, {topFlatRaLeft, true}, {loF_L, true}, {loF_R, true},
           {topFlatRaRight, true}, {seamRaM, true}});
  cylFace(raSurf, kHalfPi, kHalfPi + kPi,
          {{botRimB, false}, {seamRaM, false}, {topFlatRaBackLeft, true}, {loB_R, false},
           {loB_L, false}, {topFlatRaBackRight, true}, {seamRaP, true}});
  cylFace(rbSurf, -kHalfPi, kHalfPi,
          {{botFlatRbRight, false}, {hiF_R, true}, {hiF_L, true}, {botFlatRbLeft, false},
           {seamRbP, false}, {topRimF, true}, {seamRbM, true}});
  cylFace(rbSurf, kHalfPi, kHalfPi + kPi,
          {{botFlatRbBackRight, false}, {hiB_L, false}, {hiB_R, false}, {botFlatRbBackLeft, false},
           {seamRbM, false}, {topRimB, true}, {seamRbP, true}});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Zax, -1.0), {{botRimF, true}, {botRimB, true}}));
  s.faces.push_back(MakePlaneFace(topC, Zax, {{topRimF, false}, {topRimB, false}}));

  Surface aIn = aSurf;
  aIn.inward = true;
  cylFace(aIn, 0.0, kPi,
          {{hiF_R, false}, {genF1, true}, {loF_R, false}, {seamBot, true}, {loB_R, true},
           {genB1, false}, {hiB_R, true}, {seamTop, false}});
  cylFace(aIn, kPi, kTwoPi,
          {{loF_L, false}, {genF2, false}, {hiF_L, false}, {seamTop, true}, {hiB_L, true},
           {genB2, true}, {loB_L, true}, {seamBot, false}});

  // The shoulder faces away from whichever segment is narrower here: if Rb is the wider one, the
  // ring is the underside of its own extra material (normal -Z); if Ra is wider, it is the topside
  // of Ra's own material (normal +Z) — both remnants share the same shoulder plane, so both share
  // this same outward normal.
  const Vec3 shoulderN = Rb > Ra ? ray3d::Scale(Zax, -1.0) : Zax;
  s.faces.push_back(MakePlaneFace(
      shoulderC, shoulderN,
      {{topFlatRaLeft, false}, {topFlatRaBackRight, false}, {genB2, false}, {botFlatRbBackRight, true},
       {botFlatRbLeft, true}, {genF2, true}}));
  s.faces.push_back(MakePlaneFace(
      shoulderC, shoulderN,
      {{genF1, false}, {botFlatRbRight, true}, {botFlatRbBackLeft, true}, {genB1, true},
       {topFlatRaBackLeft, false}, {topFlatRaRight, false}}));

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

namespace {

/// `A ∩ B` for a **non-perpendicular** branch pipe (REQ-314 B2b-2, GitHub issue #242): the thin
/// cylinder `A` (radius \p r) crosses the thick `B` (radius \p R) with their axes coplanar and
/// crossing at `fr.origin`, but tilted by \p alpha off perpendicular (`|alpha| < π/2`; `alpha = 0`
/// is \ref BuildBranchPipeIntersection). `fr.zAxis` is `B`'s axis and `fr.xAxis` the in-plane
/// component of `A`'s axis, so `A`'s axis is `cos α·x̂ + sin α·ẑ`. Same lens topology as the
/// perpendicular case — 8 vertices, 10 edges (8 procedural `CurveKind::Intersection` quarter-arcs),
/// 4 faces — with the curve `ζ(φ) = (r sinα cosφ ± √(R² − r² sin²φ)) / cosα` along `A`'s axis. Every
/// face integrates numerically.
[[nodiscard]] bool BuildAngledBranchPipeIntersection(const ucs::Ucs& fr, double r, double R,
                                                     double alpha, Solid* out, Problem* outWhy) {
  if (!(R > r) || !(r > 0.0) || !(std::fabs(alpha) < kHalfPi - 1e-6))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  const Vec3 Y = fr.yAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  // A's axis (tHat) and the seam direction the wall's u = 0 follows (xaHat), in fr coords.
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = fr.origin;
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 8.0 * R;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame.origin = fr.origin;
  bSurf.frame.zAxis = fr.zAxis;
  bSurf.frame.xAxis = fr.xAxis;
  bSurf.frame.yAxis = Y;
  bSurf.radius = R;
  bSurf.height = 8.0 * R;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double root = std::sqrt(std::max(0.0, R * R - r * r * std::sin(phi) * std::sin(phi)));
    const double zeta = (r * sa * std::cos(phi) + sign * root) / ca;
    return W(Vec3{zeta * ca - r * std::cos(phi) * sa, -r * std::sin(phi),
                  zeta * sa + r * std::cos(phi) * ca});
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int qb = AddVertex(&s, cpt(kHalfPi, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int qa = AddVertex(&s, cpt(1.5 * kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int nb = AddVertex(&s, cpt(kHalfPi, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int na = AddVertex(&s, cpt(1.5 * kPi, -1));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int e1 = isect(p0, qb, 0.25 * kPi, 1);
  const int e2 = isect(qb, p1, 0.75 * kPi, 1);
  const int e3 = isect(p1, qa, 1.25 * kPi, 1);
  const int e4 = isect(qa, p0, 1.75 * kPi, 1);
  const int e5 = isect(n0, nb, 0.25 * kPi, -1);
  const int e6 = isect(nb, n1, 0.75 * kPi, -1);
  const int e7 = isect(n1, na, 1.25 * kPi, -1);
  const int e8 = isect(na, n0, 1.75 * kPi, -1);
  const int sTop = AddLine(&s, n0, p0);  // φ = 0 seam of A
  const int sBot = AddLine(&s, n1, p1);  // φ = π seam of A

  auto cylFace = [&](const Surface& surf, double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  const double psi0 = std::asin(std::clamp(r / R, -1.0, 1.0));
  cylFace(aSurf, 0.0, kPi,
          {{e1, false}, {e2, false}, {sBot, true}, {e6, true}, {e5, true}, {sTop, false}});
  cylFace(aSurf, kPi, kTwoPi,
          {{e3, false}, {e4, false}, {sTop, true}, {e8, true}, {e7, true}, {sBot, false}});
  cylFace(bSurf, -psi0, psi0, {{e4, true}, {e3, true}, {e2, true}, {e1, true}});
  cylFace(bSurf, kPi - psi0, kPi + psi0, {{e5, false}, {e6, false}, {e7, false}, {e8, false}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∩ B` for a **skew** (offset, non-coplanar) perpendicular branch pipe (REQ-314 B2b-2, GitHub
/// issue #242): the thin cylinder `A` (radius \p r, axis `fr.yAxis`, through `(g, 0, 0)`) crosses the
/// thick `B` (radius \p R, axis `fr.zAxis`) at right angles but with their axes missing each other by
/// \p g (`0 ≤ g`, `g + r < R`). Same lens topology as the coplanar cases — 8 vertices, 10 edges
/// (8 procedural), 4 faces — with the curve `s(φ) = ±√(R² − (g + r sinφ)²)` along `A`'s axis and the
/// two thick-wall patches now off-centre (u ∈ [uLo, uHi] and its mirror). Every face integrates
/// numerically.
[[nodiscard]] bool BuildSkewBranchPipeIntersection(const ucs::Ucs& fr, double r, double R, double g,
                                                   Solid* out, Problem* outWhy) {
  if (!(R > r) || !(r > 0.0) || !(g >= 0.0) || !(g + r < R))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Y = fr.yAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = W(Vec3{g, 0.0, 0.0});
  aSurf.frame.zAxis = Y;             // A's axis
  aSurf.frame.xAxis = fr.zAxis;      // φ = 0 points toward +z
  aSurf.frame.yAxis = fr.xAxis;
  aSurf.radius = r;
  aSurf.height = 8.0 * R;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame = fr;
  bSurf.radius = R;
  bSurf.height = 8.0 * R;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double px = g + r * std::sin(phi);
    const double sc = std::sqrt(std::max(0.0, R * R - px * px)) * sign;
    return W(Vec3{px, sc, r * std::cos(phi)});
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int qb = AddVertex(&s, cpt(kHalfPi, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int qa = AddVertex(&s, cpt(1.5 * kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int nb = AddVertex(&s, cpt(kHalfPi, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int na = AddVertex(&s, cpt(1.5 * kPi, -1));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int e1 = isect(p0, qb, 0.25 * kPi, 1);
  const int e2 = isect(qb, p1, 0.75 * kPi, 1);
  const int e3 = isect(p1, qa, 1.25 * kPi, 1);
  const int e4 = isect(qa, p0, 1.75 * kPi, 1);
  const int e5 = isect(n0, nb, 0.25 * kPi, -1);
  const int e6 = isect(nb, n1, 0.75 * kPi, -1);
  const int e7 = isect(n1, na, 1.25 * kPi, -1);
  const int e8 = isect(na, n0, 1.75 * kPi, -1);
  const int sTop = AddLine(&s, n0, p0);  // φ = 0 seam of A
  const int sBot = AddLine(&s, n1, p1);  // φ = π seam of A

  auto cylFace = [&](const Surface& surf, double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  // The +s mouth on B runs from φ = π/2 (px = g + r, nearest B's axis) to φ = 3π/2 (px = g − r).
  const double uLo = std::atan2(std::sqrt(std::max(0.0, R * R - (g + r) * (g + r))), g + r);
  const double uHi = std::atan2(std::sqrt(std::max(0.0, R * R - (g - r) * (g - r))), g - r);
  cylFace(aSurf, 0.0, kPi,
          {{e1, false}, {e2, false}, {sBot, true}, {e6, true}, {e5, true}, {sTop, false}});
  cylFace(aSurf, kPi, kTwoPi,
          {{e3, false}, {e4, false}, {sTop, true}, {e8, true}, {e7, true}, {sBot, false}});
  cylFace(bSurf, uLo, uHi, {{e4, true}, {e3, true}, {e2, true}, {e1, true}});
  cylFace(bSurf, -uHi, -uLo, {{e5, false}, {e6, false}, {e7, false}, {e8, false}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// Shared frame + curve helpers for the skew (offset) perpendicular branch pipe. `A`'s axis is
/// `fr.yAxis` through `(g, 0, 0)`; `B`'s is `fr.zAxis`. Both mouths sit on `B`'s `+x` half, so the
/// thick wall is split at `ψ = 0` and `ψ = π` (not `±π/2`) — one mouth per half.
struct SkewBranch {
  ucs::Ucs fr;
  double r = 0.0;
  double R = 0.0;
  double g = 0.0;
  Surface aSurf;
  Surface bSurf;
  [[nodiscard]] Vec3 W(const Vec3& l) const { return ucs::UcsToWorld(fr, l); }
  [[nodiscard]] Vec3 cpt(double phi, int sign) const {
    const double px = g + r * std::sin(phi);
    return W(Vec3{px, std::sqrt(std::max(0.0, R * R - px * px)) * sign, r * std::cos(phi)});
  }
  [[nodiscard]] Vec3 thinPt(double phi, double y) const {
    return W(Vec3{g + r * std::sin(phi), y, r * std::cos(phi)});
  }
};

[[nodiscard]] bool MakeSkewBranch(const ucs::Ucs& fr, double r, double R, double g, SkewBranch* sb) {
  if (!(R > r) || !(r > 0.0) || !(g >= 0.0) || !(g + r < R))
    return false;
  sb->fr = fr;
  sb->r = r;
  sb->R = R;
  sb->g = g;
  sb->aSurf.kind = SurfaceKind::Cylinder;
  sb->aSurf.frame.origin = sb->W(Vec3{g, 0.0, 0.0});
  sb->aSurf.frame.zAxis = fr.yAxis;
  sb->aSurf.frame.xAxis = fr.zAxis;
  sb->aSurf.frame.yAxis = fr.xAxis;
  sb->aSurf.radius = r;
  sb->aSurf.height = 8.0 * R;
  sb->bSurf.kind = SurfaceKind::Cylinder;
  sb->bSurf.frame = fr;
  sb->bSurf.radius = R;
  sb->bSurf.height = 8.0 * R;
  return true;
}

/// `B − A` for a **skew** (offset) perpendicular branch pipe (REQ-314 B2b-2, GitHub issue #242): the
/// thin `A` (axis `fr.yAxis`, through `(g, 0, 0)`) bored clean through the thick `B` (axis
/// `fr.zAxis`, caps \p zB0 / \p zB1). Both mouths lie on `B`'s `+x` half, so the thick wall splits at
/// `ψ = 0` / `ψ = π` — each half carries one mouth as an inner loop. 8v / 12e / 6f, genus 1.
[[nodiscard]] bool BuildSkewBranchPipeSubtract(const ucs::Ucs& fr, double r, double R, double g,
                                               double zB0, double zB1, Solid* out, Problem* outWhy) {
  SkewBranch sb;
  if (!MakeSkewBranch(fr, r, R, g, &sb) || !(zB1 - zB0 > 2.0 * r))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return sb.W(l); };
  Solid s;
  const int p0 = AddVertex(&s, sb.cpt(0.0, 1));
  const int p1 = AddVertex(&s, sb.cpt(kPi, 1));
  const int n0 = AddVertex(&s, sb.cpt(0.0, -1));
  const int n1 = AddVertex(&s, sb.cpt(kPi, -1));
  const int bp = AddVertex(&s, W(Vec3{R, 0.0, zB0}));    // ψ = 0
  const int bm = AddVertex(&s, W(Vec3{-R, 0.0, zB0}));   // ψ = π
  const int tp = AddVertex(&s, W(Vec3{R, 0.0, zB1}));
  const int tm = AddVertex(&s, W(Vec3{-R, 0.0, zB1}));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = sb.cpt(witnessPhi, sign);
    e.isectSurfaces = {sb.aSurf, sb.bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);
  const int sA0 = AddLine(&s, n0, p0);
  const int sAp = AddLine(&s, n1, p1);
  const int seam0 = AddLine(&s, bp, tp);   // B seam ψ = 0
  const int seamPi = AddLine(&s, bm, tm);  // B seam ψ = π
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int brU = AddArc(&s, bp, bm, botC, Z, kPi);  // bottom rim ψ 0 -> π (through +y)
  const int brD = AddArc(&s, bm, bp, botC, Z, kPi);  // bottom rim ψ π -> 2π (through -y)
  const int trU = AddArc(&s, tp, tm, topC, Z, kPi);
  const int trD = AddArc(&s, tm, tp, topC, Z, kPi);

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  face(sb.bSurf, 0.0, kPi,
       {Loop{{{brU, false}, {seamPi, false}, {trU, true}, {seam0, true}}},
        Loop{{{cpU, false}, {cpL, true}}}});
  face(sb.bSurf, kPi, kTwoPi,
       {Loop{{{brD, false}, {seam0, false}, {trD, true}, {seamPi, true}}},
        Loop{{{cnU, true}, {cnL, false}}}});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brU, true}, {brD, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trU, false}, {trD, false}}));
  Surface aIn = sb.aSurf;
  aIn.inward = true;
  face(aIn, 0.0, kPi, {Loop{{{cpL, false}, {sAp, true}, {cnL, true}, {sA0, false}}}});
  face(aIn, kPi, kTwoPi, {Loop{{{cpU, true}, {sA0, true}, {cnU, false}, {sAp, false}}}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∪ B` for a **skew** (offset) perpendicular branch pipe (REQ-314 B2b-2, GitHub issue #242). Same
/// thick-wall split as \ref BuildSkewBranchPipeSubtract; the two thin stubs run out along `±fr.yAxis`
/// to flat caps at \p yA0 / \p yA1 (measured from `fr.origin`). 12v / 18e / 10f.
[[nodiscard]] bool BuildSkewBranchPipeUnion(const ucs::Ucs& fr, double r, double R, double g,
                                            double zB0, double zB1, double yA0, double yA1, Solid* out,
                                            Problem* outWhy) {
  SkewBranch sb;
  const double sMax = std::sqrt(std::max(0.0, R * R - (g - r) * (g - r)));
  if (!MakeSkewBranch(fr, r, R, g, &sb) || !(zB1 - zB0 > 2.0 * r) || !(yA0 < -sMax) || !(yA1 > sMax))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return sb.W(l); };
  Solid s;
  const int p0 = AddVertex(&s, sb.cpt(0.0, 1));
  const int p1 = AddVertex(&s, sb.cpt(kPi, 1));
  const int n0 = AddVertex(&s, sb.cpt(0.0, -1));
  const int n1 = AddVertex(&s, sb.cpt(kPi, -1));
  const int bp = AddVertex(&s, W(Vec3{R, 0.0, zB0}));
  const int bm = AddVertex(&s, W(Vec3{-R, 0.0, zB0}));
  const int tp = AddVertex(&s, W(Vec3{R, 0.0, zB1}));
  const int tm = AddVertex(&s, W(Vec3{-R, 0.0, zB1}));
  const int k0 = AddVertex(&s, sb.thinPt(0.0, yA1));  // +y stub rim
  const int k1 = AddVertex(&s, sb.thinPt(kPi, yA1));
  const int m0 = AddVertex(&s, sb.thinPt(0.0, yA0));  // -y stub rim
  const int m1 = AddVertex(&s, sb.thinPt(kPi, yA0));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = sb.cpt(witnessPhi, sign);
    e.isectSurfaces = {sb.aSurf, sb.bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);
  const int seam0 = AddLine(&s, bp, tp);
  const int seamPi = AddLine(&s, bm, tm);
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int brU = AddArc(&s, bp, bm, botC, Z, kPi);
  const int brD = AddArc(&s, bm, bp, botC, Z, kPi);
  const int trU = AddArc(&s, tp, tm, topC, Z, kPi);
  const int trD = AddArc(&s, tm, tp, topC, Z, kPi);
  const int ks0 = AddLine(&s, p0, k0);
  const int ksP = AddLine(&s, p1, k1);
  const int ms0 = AddLine(&s, n0, m0);
  const int msP = AddLine(&s, n1, m1);
  const Vec3 kC = W(Vec3{g, yA1, 0.0});
  const Vec3 mC = W(Vec3{g, yA0, 0.0});
  const Vec3 negY = ray3d::Scale(Y, -1.0);
  const int krF = AddArc(&s, k0, k1, kC, Y, kPi);
  const int krB = AddArc(&s, k1, k0, kC, Y, kPi);
  const int mrF = AddArc(&s, m0, m1, mC, negY, kPi);
  const int mrB = AddArc(&s, m1, m0, mC, negY, kPi);

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  face(sb.bSurf, 0.0, kPi,
       {Loop{{{brU, false}, {seamPi, false}, {trU, true}, {seam0, true}}},
        Loop{{{cpU, false}, {cpL, true}}}});
  face(sb.bSurf, kPi, kTwoPi,
       {Loop{{{brD, false}, {seam0, false}, {trD, true}, {seamPi, true}}},
        Loop{{{cnU, false}, {cnL, true}}}});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brU, true}, {brD, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trU, false}, {trD, false}}));
  face(sb.aSurf, 0.0, kPi, {Loop{{{cpL, false}, {ksP, false}, {krF, true}, {ks0, true}}}});
  face(sb.aSurf, kPi, kTwoPi, {Loop{{{cpU, true}, {ks0, false}, {krB, true}, {ksP, true}}}});
  face(sb.aSurf, 0.0, kPi, {Loop{{{cnL, false}, {msP, false}, {mrF, true}, {ms0, true}}}});
  face(sb.aSurf, kPi, kTwoPi, {Loop{{{cnU, true}, {ms0, false}, {mrB, true}, {msP, true}}}});
  s.faces.push_back(MakePlaneFace(kC, Y, {{krF, false}, {krB, false}}));
  s.faces.push_back(MakePlaneFace(mC, negY, {{mrF, false}, {mrB, false}}));

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// One stub of `A − B` (thin − thick) for a **skew** (offset) perpendicular branch pipe (REQ-314
/// B2b-2, GitHub issue #242): the thick `B` bites the offset thin `A` in two. The \p sideSign stub —
/// `A`'s wall in two u halves from the flat cap at `fr.yAxis`-parameter \p yFlat to the quartic mouth,
/// plus an **inward** off-centre patch of `B`'s wall as the concave end. 4v / 6e / 4f, χ = 2.
[[nodiscard]] bool BuildSkewBranchPipeThinStub(const ucs::Ucs& fr, double r, double R, double g,
                                               double yFlat, int sideSign, Solid* out,
                                               Problem* outWhy) {
  SkewBranch sb;
  const double sMax = std::sqrt(std::max(0.0, R * R - (g - r) * (g - r)));
  if (!MakeSkewBranch(fr, r, R, g, &sb) || !(std::fabs(yFlat) > sMax + 1e-9 * (R + r)))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const int msign = sideSign > 0 ? 1 : -1;
  const Vec3 Y = fr.yAxis;
  auto W = [&](const Vec3& l) { return sb.W(l); };
  auto cpt = [&](double phi) { return sb.cpt(phi, msign); };

  Solid s;
  const int p0 = AddVertex(&s, cpt(0.0));                 // mouth φ = 0
  const int pP = AddVertex(&s, cpt(kPi));                 // mouth φ = π
  const int q0 = AddVertex(&s, sb.thinPt(0.0, yFlat));    // rim φ = 0
  const int qP = AddVertex(&s, sb.thinPt(kPi, yFlat));    // rim φ = π

  auto isect = [&](int a, int b, double witnessPhi) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = a;
    e.v1 = b;
    e.frame.origin = cpt(witnessPhi);
    e.isectSurfaces = {sb.aSurf, sb.bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int eP = isect(p0, pP, 0.5 * kPi);
  const int eN = isect(pP, p0, 1.5 * kPi);
  const int sp0 = AddLine(&s, p0, q0);
  const int spP = AddLine(&s, pP, qP);
  const Vec3 fc = W(Vec3{g, yFlat, 0.0});
  const int rc0 = AddArc(&s, q0, qP, fc, Y, kPi);
  const int rcP = AddArc(&s, qP, q0, fc, Y, kPi);

  auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = sb.aSurf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  const double uLo = std::atan2(std::sqrt(std::max(0.0, R * R - (g + r) * (g + r))), g + r);
  const double uHi = std::atan2(std::sqrt(std::max(0.0, R * R - (g - r) * (g - r))), g - r);
  auto dimple = [&](std::vector<EdgeUse> uses) {
    Face f;
    f.surface = sb.bSurf;
    f.surface.inward = true;
    f.uStart = msign > 0 ? uLo : -uHi;
    f.uEnd = msign > 0 ? uHi : -uLo;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };

  if (msign > 0) {
    wall(0.0, kPi, {{eP, false}, {spP, false}, {rc0, true}, {sp0, true}});
    wall(kPi, kTwoPi, {{eN, false}, {sp0, false}, {rcP, true}, {spP, true}});
    s.faces.push_back(MakePlaneFace(fc, Y, {{rc0, false}, {rcP, false}}));
    dimple({{eN, true}, {eP, true}});
  } else {
    wall(0.0, kPi, {{rc0, false}, {spP, true}, {eP, true}, {sp0, false}});
    wall(kPi, kTwoPi, {{rcP, false}, {sp0, true}, {eN, true}, {spP, false}});
    s.faces.push_back(MakePlaneFace(fc, ray3d::Scale(Y, -1.0), {{rc0, true}, {rcP, true}}));
    dimple({{eP, false}, {eN, false}});
  }

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∩ B` for the **fully general** branch pipe (REQ-314 B2b-2, GitHub issue #242): the thin
/// cylinder `A` (radius \p r) crosses the thick `B` (radius \p R) with axes that are neither
/// coplanar nor perpendicular — tilted **and** offset at once. `fr.zAxis` is `B`'s axis; `fr.xAxis`
/// is `A`'s own in-plane (⊥ `fr.zAxis`) direction, so `A`'s axis is `cos α·x̂ + sin α·ẑ` (as in
/// \ref BuildAngledBranchPipeIntersection); it passes through `(0, g, 0)` in that frame, `g` the
/// perpendicular offset (as in \ref BuildSkewBranchPipeIntersection). Both prior builders are the
/// `g = 0` and `α = 0` special cases of the one quadratic here:
/// `s(φ) = (r sinα cosφ ± √(R² − (g − r sinφ)²)) / cosα`. Same 8v / 10e (8 procedural) / 4f, χ = 2
/// lens topology; the thick-wall mouth patches no longer have a closed-form angular extent, so their
/// `u` bounds are found by a one-time sample scan with margin (the numeric strip search at
/// integration time finds the exact band regardless). Every face integrates numerically.
[[nodiscard]] bool BuildGeneralBranchPipeIntersection(const ucs::Ucs& fr, double r, double R,
                                                       double alpha, double g, Solid* out,
                                                       Problem* outWhy) {
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  if (!(R > r) || !(r > 0.0) || !(ca > 1e-6))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Y = fr.yAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = W(Vec3{0.0, g, 0.0});
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 8.0 * (R + std::fabs(g));
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame = fr;
  bSurf.radius = R;
  bSurf.height = 8.0 * (R + std::fabs(g));

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double K = r * std::cos(phi);
    const double py = g - r * std::sin(phi);
    const double root = std::sqrt(std::max(0.0, R * R - py * py));
    const double zeta = (r * sa * std::cos(phi) + sign * root) / ca;
    return W(Vec3{zeta * ca - K * sa, py, zeta * sa + K * ca});
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int qb = AddVertex(&s, cpt(kHalfPi, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int qa = AddVertex(&s, cpt(1.5 * kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int nb = AddVertex(&s, cpt(kHalfPi, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int na = AddVertex(&s, cpt(1.5 * kPi, -1));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int e1 = isect(p0, qb, 0.25 * kPi, 1);
  const int e2 = isect(qb, p1, 0.75 * kPi, 1);
  const int e3 = isect(p1, qa, 1.25 * kPi, 1);
  const int e4 = isect(qa, p0, 1.75 * kPi, 1);
  const int e5 = isect(n0, nb, 0.25 * kPi, -1);
  const int e6 = isect(nb, n1, 0.75 * kPi, -1);
  const int e7 = isect(n1, na, 1.25 * kPi, -1);
  const int e8 = isect(na, n0, 1.75 * kPi, -1);
  const int sTop = AddLine(&s, n0, p0);  // φ = 0 seam of A
  const int sBot = AddLine(&s, n1, p1);  // φ = π seam of A

  auto cylFace = [&](const Surface& surf, double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  // The thick-wall mouth has no closed-form angular extent once both alpha and g are nonzero, so
  // sample it: track each side's u-range (unwrapped about its phi = pi/2 / 3pi/2 sample so a mouth
  // near the +/-pi seam doesn't spuriously wrap) and pad with a margin — the strip search at
  // integration/tessellation time finds the exact band regardless of how generous this is.
  auto thickU = [&](const Vec3& p) {
    const Vec3 d = ray3d::Sub(p, fr.origin);
    return std::atan2(ray3d::Dot(d, fr.yAxis), ray3d::Dot(d, fr.xAxis));
  };
  auto scanRange = [&](int sign, double refPhi) {
    const double uRef = thickU(cpt(refPhi, sign));
    double lo = uRef;
    double hi = uRef;
    constexpr int kSamples = 360;
    for (int i = 0; i <= kSamples; ++i) {
      const double phi = kTwoPi * static_cast<double>(i) / kSamples;
      double u = thickU(cpt(phi, sign));
      u -= kTwoPi * std::round((u - uRef) / kTwoPi);  // unwrap near uRef
      lo = std::min(lo, u);
      hi = std::max(hi, u);
    }
    const double margin = 0.15 * (hi - lo) + 1e-6;
    return std::pair<double, double>{lo - margin, hi + margin};
  };
  const auto [uLoP, uHiP] = scanRange(1, kHalfPi);
  const auto [uLoN, uHiN] = scanRange(-1, -kHalfPi);
  cylFace(aSurf, 0.0, kPi,
          {{e1, false}, {e2, false}, {sBot, true}, {e6, true}, {e5, true}, {sTop, false}});
  cylFace(aSurf, kPi, kTwoPi,
          {{e3, false}, {e4, false}, {sTop, true}, {e8, true}, {e7, true}, {sBot, false}});
  cylFace(bSurf, uLoP, uHiP, {{e4, true}, {e3, true}, {e2, true}, {e1, true}});
  cylFace(bSurf, uLoN, uHiN, {{e5, false}, {e6, false}, {e7, false}, {e8, false}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// The two mouths a general (tilted-and-skew) branch pipe cuts into `B`'s wall do not sit at fixed,
/// predictable longitudes the way every narrower special case's do, so the wall's two seams have to
/// be placed dynamically: at the midpoints of the two angular gaps between the mouths. Scans both
/// mouths' `u`-extent the same way \ref BuildGeneralBranchPipeIntersection bounds them, orders the
/// two by longitude, and returns the two seam angles (`seamA < seamB < seamA + 2π`) plus which
/// mouth (`pFirst`) falls in `[seamA, seamB]`.
struct GeneralBranchSeams {
  double seamA = 0.0;
  double seamB = 0.0;
  bool pFirst = true;  ///< true: the `+` mouth (sign 1) is the one spanning [seamA, seamB].
};

[[nodiscard]] GeneralBranchSeams FindGeneralBranchSeams(const ucs::Ucs& fr,
                                                        const std::function<Vec3(double, int)>& cpt) {
  auto thickU = [&](const Vec3& p) {
    const Vec3 d = ray3d::Sub(p, fr.origin);
    return std::atan2(ray3d::Dot(d, fr.yAxis), ray3d::Dot(d, fr.xAxis));
  };
  auto scan = [&](int sign, double refPhi, double* lo, double* hi) {
    const double uRef = thickU(cpt(refPhi, sign));
    *lo = uRef;
    *hi = uRef;
    constexpr int kSamples = 360;
    for (int i = 0; i <= kSamples; ++i) {
      const double phi = kTwoPi * static_cast<double>(i) / kSamples;
      double u = thickU(cpt(phi, sign));
      u -= kTwoPi * std::round((u - uRef) / kTwoPi);
      *lo = std::min(*lo, u);
      *hi = std::max(*hi, u);
    }
  };
  double pLo = 0.0, pHi = 0.0, nLo = 0.0, nHi = 0.0;
  scan(1, kHalfPi, &pLo, &pHi);
  scan(-1, -kHalfPi, &nLo, &nHi);
  const double pC = 0.5 * (pLo + pHi);
  double nC = 0.5 * (nLo + nHi);
  const double shift = -kTwoPi * std::round((nC - pC) / kTwoPi);
  nLo += shift;
  nHi += shift;
  nC += shift;
  GeneralBranchSeams gs;
  gs.pFirst = pC < nC;
  const double lo1 = gs.pFirst ? pLo : nLo;
  const double hi1 = gs.pFirst ? pHi : nHi;
  const double lo2 = gs.pFirst ? nLo : pLo;
  const double hi2 = gs.pFirst ? nHi : pHi;
  gs.seamA = 0.5 * (hi1 + lo2);          // the gap between mouth 1 and mouth 2
  gs.seamB = 0.5 * (hi2 + lo1 + kTwoPi);  // the gap between mouth 2 and mouth 1, wrapped
  return gs;
}

/// `B − A` for the **fully general** branch pipe (REQ-314 B2b-2, GitHub issue #242): the thin `A`
/// (tilted by \p alpha, offset by \p g — see \ref BuildGeneralBranchPipeIntersection) bored clean
/// through the thick `B` (axis `fr.zAxis`, caps \p zB0 / \p zB1). `B`'s wall splits at the two
/// dynamic seams \ref FindGeneralBranchSeams finds; everything else — the bore, the caps, the mouth
/// loops' windings (fixed per edge, independent of which seam-bounded face ends up hosting them) —
/// mirrors \ref BuildBranchPipeSubtract. 8 vertices, 12 edges, 6 faces; genus 1.
[[nodiscard]] bool BuildGeneralBranchPipeSubtract(const ucs::Ucs& fr, double r, double R, double alpha,
                                                  double g, double zB0, double zB1, Solid* out,
                                                  Problem* outWhy) {
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  if (!(R > r) || !(r > 0.0) || !(ca > 1e-6) || !(zB1 - zB0 > 2.0 * r))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = W(Vec3{0.0, g, 0.0});
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 8.0 * (R + std::fabs(g));
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame = fr;
  bSurf.radius = R;
  bSurf.height = zB1 - zB0;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double K = r * std::cos(phi);
    const double py = g - r * std::sin(phi);
    const double root = std::sqrt(std::max(0.0, R * R - py * py));
    const double zeta = (r * sa * std::cos(phi) + sign * root) / ca;
    return W(Vec3{zeta * ca - K * sa, py, zeta * sa + K * ca});
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);
  const int sA0 = AddLine(&s, n0, p0);
  const int sAp = AddLine(&s, n1, p1);

  const GeneralBranchSeams gs = FindGeneralBranchSeams(fr, cpt);
  auto rimPt = [&](double th, double z) { return W(Vec3{R * std::cos(th), R * std::sin(th), z}); };
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int bA = AddVertex(&s, rimPt(gs.seamA, zB0));
  const int bB = AddVertex(&s, rimPt(gs.seamB, zB0));
  const int tA = AddVertex(&s, rimPt(gs.seamA, zB1));
  const int tB = AddVertex(&s, rimPt(gs.seamB, zB1));
  const int seamLineA = AddLine(&s, bA, tA);
  const int seamLineB = AddLine(&s, bB, tB);
  const int brAB = AddArc(&s, bA, bB, botC, Z, gs.seamB - gs.seamA);
  const int brBA = AddArc(&s, bB, bA, botC, Z, kTwoPi - (gs.seamB - gs.seamA));
  const int trAB = AddArc(&s, tA, tB, topC, Z, gs.seamB - gs.seamA);
  const int trBA = AddArc(&s, tB, tA, topC, Z, kTwoPi - (gs.seamB - gs.seamA));

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  // [seamA, seamB] hosts mouth 2 (whichever sign comes second in the unwrapped order); the other
  // face hosts mouth 1. Each mouth loop's own winding is fixed by the edge (see FindGeneralBranchSeams
  // doc) — independent of which face slot it lands in.
  const Loop pMouth{{{cpU, false}, {cpL, true}}};
  const Loop nMouth{{{cnU, true}, {cnL, false}}};
  const Loop& mouth2 = gs.pFirst ? nMouth : pMouth;
  const Loop& mouth1 = gs.pFirst ? pMouth : nMouth;
  face(bSurf, gs.seamA, gs.seamB,
       {Loop{{{brAB, false}, {seamLineB, false}, {trAB, true}, {seamLineA, true}}}, mouth2});
  face(bSurf, gs.seamB, gs.seamA + kTwoPi,
       {Loop{{{brBA, false}, {seamLineA, false}, {trBA, true}, {seamLineB, true}}}, mouth1});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brAB, true}, {brBA, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trAB, false}, {trBA, false}}));
  Surface aIn = aSurf;
  aIn.inward = true;
  face(aIn, 0.0, kPi, {Loop{{{cpL, false}, {sAp, true}, {cnL, true}, {sA0, false}}}});
  face(aIn, kPi, kTwoPi, {Loop{{{cpU, true}, {sA0, true}, {cnU, false}, {sAp, false}}}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∪ B` for the **fully general** branch pipe (REQ-314 B2b-2, GitHub issue #242): same dynamic
/// seam placement as \ref BuildGeneralBranchPipeSubtract; the two thin stubs run out along `±tHat`
/// (`A`'s tilted axis) to flat caps at the axis parameters \p zetaA0 / \p zetaA1, exactly as
/// \ref BuildAngledBranchPipeUnion's stubs do. 12 vertices, 18 edges, 10 faces.
[[nodiscard]] bool BuildGeneralBranchPipeUnion(const ucs::Ucs& fr, double r, double R, double alpha,
                                               double g, double zB0, double zB1, double zetaA0,
                                               double zetaA1, Solid* out, Problem* outWhy) {
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  if (!(R > r) || !(r > 0.0) || !(ca > 1e-6) || !(zB1 - zB0 > 2.0 * r) || !(zetaA1 > zetaA0))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));
  auto thinPt = [&](double phi, double zeta) {
    return W(Vec3{zeta * ca - r * std::cos(phi) * sa, g - r * std::sin(phi),
                  zeta * sa + r * std::cos(phi) * ca});
  };

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = W(Vec3{0.0, g, 0.0});
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = zetaA1 - zetaA0;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame = fr;
  bSurf.radius = R;
  bSurf.height = zB1 - zB0;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double root = std::sqrt(std::max(0.0, R * R - (g - r * std::sin(phi)) * (g - r * std::sin(phi))));
    const double zeta = (r * sa * std::cos(phi) + sign * root) / ca;
    return thinPt(phi, zeta);
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int k0 = AddVertex(&s, thinPt(0.0, zetaA1));
  const int k1 = AddVertex(&s, thinPt(kPi, zetaA1));
  const int m0 = AddVertex(&s, thinPt(0.0, zetaA0));
  const int m1 = AddVertex(&s, thinPt(kPi, zetaA0));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);
  const int ks0 = AddLine(&s, p0, k0);
  const int ksP = AddLine(&s, p1, k1);
  const int ms0 = AddLine(&s, n0, m0);
  const int msP = AddLine(&s, n1, m1);
  const Vec3 kC = W(Vec3{zetaA1 * ca, g, zetaA1 * sa});
  const Vec3 mC = W(Vec3{zetaA0 * ca, g, zetaA0 * sa});
  const Vec3 negT = ray3d::Scale(tHat, -1.0);
  const int krF = AddArc(&s, k0, k1, kC, tHat, kPi);
  const int krB = AddArc(&s, k1, k0, kC, tHat, kPi);
  const int mrF = AddArc(&s, m0, m1, mC, negT, kPi);
  const int mrB = AddArc(&s, m1, m0, mC, negT, kPi);

  const GeneralBranchSeams gs = FindGeneralBranchSeams(fr, cpt);
  auto rimPt = [&](double th, double z) { return W(Vec3{R * std::cos(th), R * std::sin(th), z}); };
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int bA = AddVertex(&s, rimPt(gs.seamA, zB0));
  const int bB = AddVertex(&s, rimPt(gs.seamB, zB0));
  const int tA = AddVertex(&s, rimPt(gs.seamA, zB1));
  const int tB = AddVertex(&s, rimPt(gs.seamB, zB1));
  const int seamLineA = AddLine(&s, bA, tA);
  const int seamLineB = AddLine(&s, bB, tB);
  const int brAB = AddArc(&s, bA, bB, botC, Z, gs.seamB - gs.seamA);
  const int brBA = AddArc(&s, bB, bA, botC, Z, kTwoPi - (gs.seamB - gs.seamA));
  const int trAB = AddArc(&s, tA, tB, topC, Z, gs.seamB - gs.seamA);
  const int trBA = AddArc(&s, tB, tA, topC, Z, kTwoPi - (gs.seamB - gs.seamA));

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  const Loop pMouth{{{cpU, false}, {cpL, true}}};
  const Loop nMouth{{{cnU, false}, {cnL, true}}};
  const Loop& mouth2 = gs.pFirst ? nMouth : pMouth;
  const Loop& mouth1 = gs.pFirst ? pMouth : nMouth;
  face(bSurf, gs.seamA, gs.seamB,
       {Loop{{{brAB, false}, {seamLineB, false}, {trAB, true}, {seamLineA, true}}}, mouth2});
  face(bSurf, gs.seamB, gs.seamA + kTwoPi,
       {Loop{{{brBA, false}, {seamLineA, false}, {trBA, true}, {seamLineB, true}}}, mouth1});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brAB, true}, {brBA, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trAB, false}, {trBA, false}}));
  face(aSurf, 0.0, kPi, {Loop{{{cpL, false}, {ksP, false}, {krF, true}, {ks0, true}}}});
  face(aSurf, kPi, kTwoPi, {Loop{{{cpU, true}, {ks0, false}, {krB, true}, {ksP, true}}}});
  face(aSurf, 0.0, kPi, {Loop{{{cnL, false}, {msP, false}, {mrF, true}, {ms0, true}}}});
  face(aSurf, kPi, kTwoPi, {Loop{{{cnU, true}, {ms0, false}, {mrB, true}, {msP, true}}}});
  s.faces.push_back(MakePlaneFace(kC, tHat, {{krF, false}, {krB, false}}));
  s.faces.push_back(MakePlaneFace(mC, negT, {{mrF, false}, {mrB, false}}));

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// One stub of `A − B` (thin − thick) for the **fully general** (tilted-and-skew) branch pipe
/// (REQ-314 B2b-2, GitHub issue #283): the thick `B` bites the offset-and-tilted thin `A` in two.
/// Generalises \ref BuildBranchPipeThinStub (tilt only) and \ref BuildSkewBranchPipeThinStub (offset
/// only) exactly the way \ref BuildGeneralBranchPipeIntersection generalises their non-stub
/// siblings: identical 4v / 6e (2 procedural) / 4f, χ = 2 topology, the mouth curve is the general
/// `ζ(φ)`, and the thick-wall dimple's angular extent has no closed form once both `alpha` and `g`
/// are nonzero, so it is found the same sample-scan way \ref BuildGeneralBranchPipeIntersection
/// bounds its mouth patches.
[[nodiscard]] bool BuildGeneralBranchPipeThinStub(const ucs::Ucs& fr, double r, double R, double alpha,
                                                  double g, double zetaFlat, int sideSign, Solid* out,
                                                  Problem* outWhy) {
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  const double zetaMax = (r * std::fabs(sa) + R) / ca;
  if (!(R > r) || !(r > 0.0) || !(ca > 1e-6) || !(std::fabs(g) + r < R) ||
      !(std::fabs(zetaFlat) > zetaMax + 1e-9 * (R + r)))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const int msign = sideSign > 0 ? 1 : -1;
  const Vec3 Y = fr.yAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));
  auto thinPt = [&](double phi, double zeta) {
    return W(Vec3{zeta * ca - r * std::cos(phi) * sa, g - r * std::sin(phi),
                  zeta * sa + r * std::cos(phi) * ca});
  };
  auto cpt = [&](double phi) {
    const double py = g - r * std::sin(phi);
    const double root = std::sqrt(std::max(0.0, R * R - py * py));
    return thinPt(phi, (r * sa * std::cos(phi) + msign * root) / ca);
  };

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = W(Vec3{0.0, g, 0.0});
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 4.0 * (std::fabs(zetaFlat) + R);
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame = fr;
  bSurf.radius = R;
  bSurf.height = 8.0 * (R + std::fabs(g));

  Solid s;
  const int p0 = AddVertex(&s, cpt(0.0));               // mouth phi = 0
  const int pP = AddVertex(&s, cpt(kPi));               // mouth phi = pi
  const int q0 = AddVertex(&s, thinPt(0.0, zetaFlat));  // rim phi = 0
  const int qP = AddVertex(&s, thinPt(kPi, zetaFlat));  // rim phi = pi

  auto isect = [&](int a, int b, double witnessPhi) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = a;
    e.v1 = b;
    e.frame.origin = cpt(witnessPhi);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int eP = isect(p0, pP, 0.5 * kPi);  // mouth, y > 0 side
  const int eN = isect(pP, p0, 1.5 * kPi);  // mouth, y < 0 side
  const int sp0 = AddLine(&s, p0, q0);      // wall seam phi = 0
  const int spP = AddLine(&s, pP, qP);      // wall seam phi = pi
  const Vec3 fc = W(Vec3{zetaFlat * ca, g, zetaFlat * sa});
  const int rc0 = AddArc(&s, q0, qP, fc, tHat, kPi);  // rim, y > 0
  const int rcP = AddArc(&s, qP, q0, fc, tHat, kPi);  // rim, y < 0

  auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = aSurf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  // The mouth's u-extent on B's wall has no closed form once both alpha and g are nonzero (see
  // BuildGeneralBranchPipeIntersection::scanRange); sample it the same way, about this one mouth.
  auto thickU = [&](const Vec3& p) {
    const Vec3 d = ray3d::Sub(p, fr.origin);
    return std::atan2(ray3d::Dot(d, fr.yAxis), ray3d::Dot(d, fr.xAxis));
  };
  const double refPhi = msign > 0 ? kHalfPi : -kHalfPi;
  const double uRef = thickU(cpt(refPhi));
  double uLo = uRef;
  double uHi = uRef;
  constexpr int kSamples = 360;
  for (int i = 0; i <= kSamples; ++i) {
    const double phi = kTwoPi * static_cast<double>(i) / kSamples;
    double u = thickU(cpt(phi));
    u -= kTwoPi * std::round((u - uRef) / kTwoPi);
    uLo = std::min(uLo, u);
    uHi = std::max(uHi, u);
  }
  const double margin = 0.15 * (uHi - uLo) + 1e-6;
  uLo -= margin;
  uHi += margin;
  auto dimple = [&](std::vector<EdgeUse> uses) {
    Face f;
    f.surface = bSurf;
    f.surface.inward = true;
    f.uStart = uLo;
    f.uEnd = uHi;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };

  if (msign > 0) {
    wall(0.0, kPi, {{eP, false}, {spP, false}, {rc0, true}, {sp0, true}});
    wall(kPi, kTwoPi, {{eN, false}, {sp0, false}, {rcP, true}, {spP, true}});
    s.faces.push_back(MakePlaneFace(fc, tHat, {{rc0, false}, {rcP, false}}));
    dimple({{eN, true}, {eP, true}});
  } else {
    wall(0.0, kPi, {{rc0, false}, {spP, true}, {eP, true}, {sp0, false}});
    wall(kPi, kTwoPi, {{rcP, false}, {sp0, true}, {eN, true}, {spP, false}});
    s.faces.push_back(MakePlaneFace(fc, ray3d::Scale(tHat, -1.0), {{rc0, true}, {rcP, true}}));
    dimple({{eP, false}, {eN, false}});
  }

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `B − A` for a **non-perpendicular** branch pipe (REQ-314 B2b-2, GitHub issue #242): the thin
/// branch `A` (radius \p r, tilted by \p alpha) bored clean through the thick main `B` (radius \p R,
/// axis `fr.zAxis`, caps \p zB0 / \p zB1). `BuildBranchPipeSubtract` generalised the same way
/// \ref BuildAngledBranchPipeIntersection generalises the perpendicular lens — only `A`'s surface
/// frame and the procedural curve `ζ(φ)` change; `B`'s wall halves, caps and the two inward bore
/// halves keep their topology. 8 vertices, 12 edges, 6 faces; genus 1.
[[nodiscard]] bool BuildAngledBranchPipeSubtract(const ucs::Ucs& fr, double r, double R, double alpha,
                                                 double zB0, double zB1, Solid* out, Problem* outWhy) {
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  const double zSpan = (R + r * std::fabs(sa)) / ca * std::fabs(sa) + r;  // mouth half-height on B's axis
  if (!(R > r) || !(r > 0.0) || !(std::fabs(alpha) < kHalfPi - 1e-6) || !(zB1 - zB0 > 2.0 * zSpan))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = fr.origin;
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 8.0 * R;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame.origin = fr.origin;
  bSurf.frame.zAxis = Z;
  bSurf.frame.xAxis = fr.xAxis;
  bSurf.frame.yAxis = Y;
  bSurf.radius = R;
  bSurf.height = zB1 - zB0;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double root = std::sqrt(std::max(0.0, R * R - r * r * std::sin(phi) * std::sin(phi)));
    const double zeta = (r * sa * std::cos(phi) + sign * root) / ca;
    return W(Vec3{zeta * ca - r * std::cos(phi) * sa, -r * std::sin(phi),
                  zeta * sa + r * std::cos(phi) * ca});
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int bp = AddVertex(&s, W(Vec3{0.0, R, zB0}));
  const int bm = AddVertex(&s, W(Vec3{0.0, -R, zB0}));
  const int tp = AddVertex(&s, W(Vec3{0.0, R, zB1}));
  const int tm = AddVertex(&s, W(Vec3{0.0, -R, zB1}));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);
  const int sA0 = AddLine(&s, n0, p0);
  const int sAp = AddLine(&s, n1, p1);
  const int seamP = AddLine(&s, bp, tp);
  const int seamM = AddLine(&s, bm, tm);
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int brF = AddArc(&s, bm, bp, botC, Z, kPi);
  const int brB = AddArc(&s, bp, bm, botC, Z, kPi);
  const int trF = AddArc(&s, tm, tp, topC, Z, kPi);
  const int trB = AddArc(&s, tp, tm, topC, Z, kPi);

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  face(bSurf, -kHalfPi, kHalfPi,
       {Loop{{{brF, false}, {seamP, false}, {trF, true}, {seamM, true}}},
        Loop{{{cpU, false}, {cpL, true}}}});
  face(bSurf, kHalfPi, kHalfPi + kPi,
       {Loop{{{brB, false}, {seamM, false}, {trB, true}, {seamP, true}}},
        Loop{{{cnU, true}, {cnL, false}}}});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brF, true}, {brB, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trF, false}, {trB, false}}));
  Surface aIn = aSurf;
  aIn.inward = true;
  face(aIn, 0.0, kPi, {Loop{{{cpL, false}, {sAp, true}, {cnL, true}, {sA0, false}}}});
  face(aIn, kPi, kTwoPi, {Loop{{{cpU, true}, {sA0, true}, {cnU, false}, {sAp, false}}}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∪ B` for a **non-perpendicular** branch pipe (REQ-314 B2b-2, GitHub issue #242): the thin
/// branch `A` (radius \p r, tilted by \p alpha, axis-parameter caps \p zetaA0 / \p zetaA1 measured
/// from `fr.origin` along `A`'s axis) fused onto the thick main `B`. Generalised from
/// \ref BuildBranchPipeUnion exactly as the other two — only `A`'s frame, the curve `ζ(φ)`, and the
/// stub caps (now perpendicular to `A`'s tilted axis) change. 12 vertices, 18 edges, 10 faces.
[[nodiscard]] bool BuildAngledBranchPipeUnion(const ucs::Ucs& fr, double r, double R, double alpha,
                                              double zB0, double zB1, double zetaA0, double zetaA1,
                                              Solid* out, Problem* outWhy) {
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  const double zetaMax = (R + r * std::fabs(sa)) / ca;
  const double zSpan = zetaMax * std::fabs(sa) + r;
  if (!(R > r) || !(r > 0.0) || !(std::fabs(alpha) < kHalfPi - 1e-6) || !(zB1 - zB0 > 2.0 * zSpan) ||
      !(zetaA0 < -zetaMax) || !(zetaA1 > zetaMax))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));
  const Vec3 tHatW = tHat;  // fr axes are already world vectors
  // A point on A's wall at angle phi, axis-parameter zeta.
  auto thinPt = [&](double phi, double zeta) {
    return W(Vec3{zeta * ca - r * std::cos(phi) * sa, -r * std::sin(phi),
                  zeta * sa + r * std::cos(phi) * ca});
  };

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = fr.origin;
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = zetaA1 - zetaA0;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame.origin = fr.origin;
  bSurf.frame.zAxis = Z;
  bSurf.frame.xAxis = fr.xAxis;
  bSurf.frame.yAxis = Y;
  bSurf.radius = R;
  bSurf.height = zB1 - zB0;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double root = std::sqrt(std::max(0.0, R * R - r * r * std::sin(phi) * std::sin(phi)));
    const double zeta = (r * sa * std::cos(phi) + sign * root) / ca;
    return thinPt(phi, zeta);
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int bp = AddVertex(&s, W(Vec3{0.0, R, zB0}));
  const int bm = AddVertex(&s, W(Vec3{0.0, -R, zB0}));
  const int tp = AddVertex(&s, W(Vec3{0.0, R, zB1}));
  const int tm = AddVertex(&s, W(Vec3{0.0, -R, zB1}));
  const int k0 = AddVertex(&s, thinPt(0.0, zetaA1));
  const int k1 = AddVertex(&s, thinPt(kPi, zetaA1));
  const int m0 = AddVertex(&s, thinPt(0.0, zetaA0));
  const int m1 = AddVertex(&s, thinPt(kPi, zetaA0));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);
  const int seamP = AddLine(&s, bp, tp);
  const int seamM = AddLine(&s, bm, tm);
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int brF = AddArc(&s, bm, bp, botC, Z, kPi);
  const int brB = AddArc(&s, bp, bm, botC, Z, kPi);
  const int trF = AddArc(&s, tm, tp, topC, Z, kPi);
  const int trB = AddArc(&s, tp, tm, topC, Z, kPi);
  const int ks0 = AddLine(&s, p0, k0);
  const int ksP = AddLine(&s, p1, k1);
  const int ms0 = AddLine(&s, n0, m0);
  const int msP = AddLine(&s, n1, m1);
  const Vec3 kCc = W(Vec3{zetaA1 * ca, 0.0, zetaA1 * sa});  // stub cap centres, on A's axis
  const Vec3 mCc = W(Vec3{zetaA0 * ca, 0.0, zetaA0 * sa});
  const Vec3 negT = ray3d::Scale(tHatW, -1.0);
  const int krF = AddArc(&s, k0, k1, kCc, tHatW, kPi);
  const int krB = AddArc(&s, k1, k0, kCc, tHatW, kPi);
  const int mrF = AddArc(&s, m0, m1, mCc, negT, kPi);
  const int mrB = AddArc(&s, m1, m0, mCc, negT, kPi);

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  face(bSurf, -kHalfPi, kHalfPi,
       {Loop{{{brF, false}, {seamP, false}, {trF, true}, {seamM, true}}},
        Loop{{{cpU, false}, {cpL, true}}}});
  face(bSurf, kHalfPi, kHalfPi + kPi,
       {Loop{{{brB, false}, {seamM, false}, {trB, true}, {seamP, true}}},
        Loop{{{cnU, false}, {cnL, true}}}});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brF, true}, {brB, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trF, false}, {trB, false}}));
  face(aSurf, 0.0, kPi, {Loop{{{cpL, false}, {ksP, false}, {krF, true}, {ks0, true}}}});
  face(aSurf, kPi, kTwoPi, {Loop{{{cpU, true}, {ks0, false}, {krB, true}, {ksP, true}}}});
  face(aSurf, 0.0, kPi, {Loop{{{cnL, false}, {msP, false}, {mrF, true}, {ms0, true}}}});
  face(aSurf, kPi, kTwoPi, {Loop{{{cnU, true}, {ms0, false}, {mrB, true}, {msP, true}}}});
  s.faces.push_back(MakePlaneFace(kCc, tHatW, {{krF, false}, {krB, false}}));
  s.faces.push_back(MakePlaneFace(mCc, negT, {{mrF, false}, {mrB, false}}));

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// One stub of `A − B` (thin − thick) for a branch pipe (REQ-314 B2b-2, GitHub issue #242): the
/// thick main `B` (radius \p R, axis `fr.zAxis`) bites the thin branch `A` (radius \p r, tilted by
/// \p alpha, `fr.xAxis` = its in-plane component) clean in two. This is the \p sideSign stub — the
/// piece of `A` from its flat cap at axis-parameter \p zetaFlat to the quartic mouth `B` cut: `A`'s
/// wall in two u halves, the flat cap, and an **inward** patch of `B`'s wall as the concave end.
/// 4 vertices, 6 edges (2 procedural), 4 faces, χ = 2. Built in \p fr and left there. `alpha = 0` is
/// the perpendicular tee. Every curved face integrates numerically.
[[nodiscard]] bool BuildBranchPipeThinStub(const ucs::Ucs& fr, double r, double R, double alpha,
                                           double zetaFlat, int sideSign, Solid* out,
                                           Problem* outWhy) {
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  const double zetaMax = (R + r * std::fabs(sa)) / ca;
  if (!(R > r) || !(r > 0.0) || !(std::fabs(alpha) < kHalfPi - 1e-6) ||
      !(std::fabs(zetaFlat) > zetaMax + 1e-9 * (R + r)))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const int msign = sideSign > 0 ? 1 : -1;
  const Vec3 Y = fr.yAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));
  auto thinPt = [&](double phi, double zeta) {
    return W(Vec3{zeta * ca - r * std::cos(phi) * sa, -r * std::sin(phi),
                  zeta * sa + r * std::cos(phi) * ca});
  };
  auto cpt = [&](double phi) {
    const double root = std::sqrt(std::max(0.0, R * R - r * r * std::sin(phi) * std::sin(phi)));
    return thinPt(phi, (r * sa * std::cos(phi) + msign * root) / ca);
  };

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = fr.origin;
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 4.0 * (std::fabs(zetaFlat) + R);
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame = fr;
  bSurf.radius = R;
  bSurf.height = 8.0 * R;

  Solid s;
  const int p0 = AddVertex(&s, cpt(0.0));               // mouth φ = 0
  const int pP = AddVertex(&s, cpt(kPi));               // mouth φ = π
  const int q0 = AddVertex(&s, thinPt(0.0, zetaFlat));  // rim φ = 0
  const int qP = AddVertex(&s, thinPt(kPi, zetaFlat));  // rim φ = π

  auto isect = [&](int a, int b, double witnessPhi) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = a;
    e.v1 = b;
    e.frame.origin = cpt(witnessPhi);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int eP = isect(p0, pP, 0.5 * kPi);  // mouth, y > 0
  const int eN = isect(pP, p0, 1.5 * kPi);  // mouth, y < 0
  const int sp0 = AddLine(&s, p0, q0);      // wall seam φ = 0
  const int spP = AddLine(&s, pP, qP);      // wall seam φ = π
  const Vec3 fc = W(Vec3{zetaFlat * ca, 0.0, zetaFlat * sa});
  const int rc0 = AddArc(&s, q0, qP, fc, tHat, kPi);  // rim, y > 0
  const int rcP = AddArc(&s, qP, q0, fc, tHat, kPi);  // rim, y < 0

  auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = aSurf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  const double psi0 = std::asin(std::clamp(r / R, -1.0, 1.0));
  auto dimple = [&](std::vector<EdgeUse> uses) {
    Face f;
    f.surface = bSurf;
    f.surface.inward = true;
    f.uStart = msign > 0 ? -psi0 : kPi - psi0;
    f.uEnd = msign > 0 ? psi0 : kPi + psi0;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };

  if (msign > 0) {
    // Loop below the rim: mouth is the bottom edge of each wall half, rim the top.
    wall(0.0, kPi, {{eP, false}, {spP, false}, {rc0, true}, {sp0, true}});
    wall(kPi, kTwoPi, {{eN, false}, {sp0, false}, {rcP, true}, {spP, true}});
    s.faces.push_back(MakePlaneFace(fc, tHat, {{rc0, false}, {rcP, false}}));
    dimple({{eN, true}, {eP, true}});
  } else {
    wall(0.0, kPi, {{rc0, false}, {spP, true}, {eP, true}, {sp0, false}});
    wall(kPi, kTwoPi, {{rcP, false}, {sp0, true}, {eN, true}, {spP, false}});
    s.faces.push_back(MakePlaneFace(fc, ray3d::Scale(tHat, -1.0), {{rc0, true}, {rcP, true}}));
    dimple({{eP, false}, {eN, false}});
  }

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∪ B`: the solid pipe-tee — the thick main `B` (radius \p R, axis `fr.zAxis`, caps \p zB0 /
/// \p zB1) fused with the thin branch `A` (radius \p r, axis `fr.xAxis`, caps \p xA0 / \p xA1)
/// (REQ-314 B2b-2). 12 vertices, 18 edges, 10 faces: `B`'s wall outside `A` (two halves, each with
/// the branch mouth as an inner loop) + `B`'s two caps + `A`'s wall outside `B` (four stub halves)
/// + `A`'s two caps. Volume `vol(A) + vol(B) − lens`.
[[nodiscard]] bool BuildBranchPipeUnion(const ucs::Ucs& fr, double r, double R, double zB0, double zB1,
                                        double xA0, double xA1, Solid* out, Problem* outWhy) {
  if (!(R > r) || !(r > 0.0) || !(zB1 - zB0 > 2.0 * r) || !(xA0 < -R) || !(xA1 > R))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 X = fr.xAxis;
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = fr.origin;
  aSurf.frame.zAxis = X;
  aSurf.frame.xAxis = Z;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = xA1 - xA0;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame.origin = fr.origin;
  bSurf.frame.zAxis = Z;
  bSurf.frame.xAxis = X;
  bSurf.frame.yAxis = Y;
  bSurf.radius = R;
  bSurf.height = zB1 - zB0;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double x = std::sqrt(std::max(0.0, R * R - r * r * std::sin(phi) * std::sin(phi))) * sign;
    return W(Vec3{x, -r * std::sin(phi), r * std::cos(phi)});
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int bp = AddVertex(&s, W(Vec3{0.0, R, zB0}));
  const int bm = AddVertex(&s, W(Vec3{0.0, -R, zB0}));
  const int tp = AddVertex(&s, W(Vec3{0.0, R, zB1}));
  const int tm = AddVertex(&s, W(Vec3{0.0, -R, zB1}));
  const int k0 = AddVertex(&s, W(Vec3{xA1, 0.0, r}));
  const int k1 = AddVertex(&s, W(Vec3{xA1, 0.0, -r}));
  const int m0 = AddVertex(&s, W(Vec3{xA0, 0.0, r}));
  const int m1 = AddVertex(&s, W(Vec3{xA0, 0.0, -r}));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);
  const int seamP = AddLine(&s, bp, tp);
  const int seamM = AddLine(&s, bm, tm);
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int brF = AddArc(&s, bm, bp, botC, Z, kPi);
  const int brB = AddArc(&s, bp, bm, botC, Z, kPi);
  const int trF = AddArc(&s, tm, tp, topC, Z, kPi);
  const int trB = AddArc(&s, tp, tm, topC, Z, kPi);
  const int ks0 = AddLine(&s, p0, k0);  // +X stub, A seam phi = 0
  const int ksP = AddLine(&s, p1, k1);  // +X stub, A seam phi = pi
  const int ms0 = AddLine(&s, n0, m0);  // -X stub, A seam phi = 0
  const int msP = AddLine(&s, n1, m1);
  const Vec3 kC = W(Vec3{xA1, 0.0, 0.0});
  const Vec3 mC = W(Vec3{xA0, 0.0, 0.0});
  const Vec3 negX = ray3d::Scale(X, -1.0);
  const int krF = AddArc(&s, k0, k1, kC, X, kPi);
  const int krB = AddArc(&s, k1, k0, kC, X, kPi);
  // The -X cap's rim arcs run about −X, so with the same loop pattern its face still winds outward.
  const int mrF = AddArc(&s, m0, m1, mC, negX, kPi);
  const int mrB = AddArc(&s, m1, m0, mC, negX, kPi);

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  face(bSurf, -kHalfPi, kHalfPi,
       {Loop{{{brF, false}, {seamP, false}, {trF, true}, {seamM, true}}},
        Loop{{{cpU, false}, {cpL, true}}}});
  face(bSurf, kHalfPi, kHalfPi + kPi,
       {Loop{{{brB, false}, {seamM, false}, {trB, true}, {seamP, true}}},
        Loop{{{cnU, false}, {cnL, true}}}});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brF, true}, {brB, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trF, false}, {trB, false}}));
  face(aSurf, 0.0, kPi,
       {Loop{{{cpL, false}, {ksP, false}, {krF, true}, {ks0, true}}}});
  face(aSurf, kPi, kTwoPi,
       {Loop{{{cpU, true}, {ks0, false}, {krB, true}, {ksP, true}}}});
  face(aSurf, 0.0, kPi,
       {Loop{{{cnL, false}, {msP, false}, {mrF, true}, {ms0, true}}}});
  face(aSurf, kPi, kTwoPi,
       {Loop{{{cnU, true}, {ms0, false}, {mrB, true}, {msP, true}}}});
  s.faces.push_back(MakePlaneFace(kC, X, {{krF, false}, {krB, false}}));
  s.faces.push_back(MakePlaneFace(mC, negX, {{mrF, false}, {mrB, false}}));

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}
} // namespace

/// Recognise a thin cylinder crossing a thicker one through an interior point, radii differ — the
/// pipe-tee (B2b-2). INTERSECT the lens, SUBTRACT bores the branch clean through the main (or, for a
/// thin minuend, splits the branch into two stubs), UNION fuses them — at **right angles**, at any
/// **non-perpendicular** coplanar angle (issue #242), and, offset **and** tilted at once, fully
/// general (issue #242 / #283). `*handled` stays false when the pair is not this configuration.
[[nodiscard]] bool TryBooleanBranchPipe(const CylinderShape& A, const CylinderShape& B, BoolOp op,
                                        std::vector<Solid>* out, bool* handled, Problem* outWhy) {
  const double sc = A.radius + B.radius + A.length + B.length;
  const double eps = 1e-7 * sc;
  if (std::fabs(A.radius - B.radius) <= eps)
    return false;  // equal radius — plane ellipses, not this recogniser
  const Vec3 az = A.axis.zAxis;
  const Vec3 bz = B.axis.zAxis;
  const double axDot = ray3d::Dot(az, bz);
  if (std::fabs(std::fabs(axDot) - 1.0) < 1e-7)
    return false;  // parallel / coaxial — a different recogniser
  const Vec3 wBA = ray3d::Sub(B.axis.origin, A.axis.origin);
  const Vec3 axCross = ray3d::Cross(az, bz);
  const double axCross2 = ray3d::Dot(axCross, axCross);
  if (axCross2 < 1e-18)
    return false;
  const double gap = std::fabs(ray3d::Dot(wBA, axCross)) / std::sqrt(axCross2);

  // thin / thick roles
  const CylinderShape& thin = A.radius < B.radius ? A : B;
  const CylinderShape& thick = A.radius < B.radius ? B : A;
  const double r = thin.radius;
  const double R = thick.radius;
  const bool perp = std::fabs(axDot) <= 1e-7;

  if (gap > eps) {
    // Skew axes — the offset branch pipe (issue #242). The closest-approach points of the two axis
    // lines, needed by both the perpendicular-offset and the fully-general cases below.
    const Vec3 td = thin.axis.zAxis;
    const Vec3 kd = thick.axis.zAxis;
    const Vec3 rv = ray3d::Sub(thin.axis.origin, thick.axis.origin);
    const double den = 1.0 - axDot * axDot;
    const double tPar = (axDot * ray3d::Dot(kd, rv) - ray3d::Dot(td, rv)) / den;
    const double kPar = (ray3d::Dot(kd, rv) - axDot * ray3d::Dot(td, rv)) / den;
    const Vec3 cThin = ray3d::Add(thin.axis.origin, ray3d::Scale(td, tPar));
    const Vec3 cThick = ray3d::Add(thick.axis.origin, ray3d::Scale(kd, kPar));
    const bool minuendThick = A.radius > B.radius;

    if (perp) {
      // Right angles, missing by `gap`, the thin fully crossing, caps clear.
      if (!(gap + r < R - eps))
        return false;
      ucs::Ucs sfr;
      if (!ucs::FromNormal(cThick, kd, &sfr))
        return Fail(Problem::BooleanResultInvalid, outWhy);
      const Vec3 gx = ray3d::Sub(cThin, cThick);  // thick axis -> thin axis
      if (!(ray3d::Length(gx) > 1e-9 * sc))
        return false;
      sfr.xAxis = ray3d::Normalize(gx);
      sfr.yAxis = ray3d::Normalize(ray3d::Cross(sfr.zAxis, sfr.xAxis));
      if (std::fabs(ray3d::Dot(td, sfr.yAxis)) < 1.0 - 1e-6)
        return false;  // the thin axis is also tilted — the general case below
      const double sMax = std::sqrt(std::max(0.0, R * R - (gap - r) * (gap - r)));
      const double t0 = ray3d::Dot(ray3d::Sub(thin.axis.origin, cThick), sfr.yAxis);
      const double t1 = t0 + thin.length * ray3d::Dot(td, sfr.yAxis);
      if (std::min(t0, t1) > -sMax - eps || std::max(t0, t1) < sMax + eps)
        return false;  // the thin does not fully cross
      const double kAlong = ray3d::Dot(ray3d::Sub(cThick, thick.axis.origin), kd);
      if (kAlong - r < eps || thick.length - kAlong - r < eps)
        return false;  // a thick cap sits inside the lens
      *handled = true;
      const double zB0 = -kAlong;
      const double zB1 = thick.length - kAlong;
      // thin cap parameters along fr.yAxis, measured from cThick (= sfr.origin).
      const double yLo = std::min(t0, t1);
      const double yHi = std::max(t0, t1);
      if (op == BoolOp::Subtract && !minuendThick) {
        Solid up;
        Solid down;
        if (!BuildSkewBranchPipeThinStub(sfr, r, R, gap, yHi, 1, &up, outWhy) ||
            !BuildSkewBranchPipeThinStub(sfr, r, R, gap, yLo, -1, &down, outWhy))
          return false;
        out->push_back(std::move(up));
        out->push_back(std::move(down));
        return Succeed(outWhy);
      }
      Solid result;
      bool ok = false;
      if (op == BoolOp::Intersect)
        ok = BuildSkewBranchPipeIntersection(sfr, r, R, gap, &result, outWhy);
      else if (op == BoolOp::Subtract)
        ok = BuildSkewBranchPipeSubtract(sfr, r, R, gap, zB0, zB1, &result, outWhy);
      else
        ok = BuildSkewBranchPipeUnion(sfr, r, R, gap, zB0, zB1, yLo, yHi, &result, outWhy);
      if (!ok)
        return false;
      out->push_back(std::move(result));
      return Succeed(outWhy);
    }

    // Fully general: tilted AND skew (issue #242, thin-thick stub added for #283). `gfr.xAxis` is
    // the thin axis's own in-plane component (as in the coplanar-tilted case), `gfr.origin` sits on
    // the thick axis at the closest approach, and the offset `g` — the thin axis's constant
    // coordinate along `gfr.yAxis` — is the true common-perpendicular gap (may be negative; the
    // builders don't care).
    ucs::Ucs gfr;
    if (!ucs::FromNormal(cThick, kd, &gfr))
      return Fail(Problem::BooleanResultInvalid, outWhy);
    const Vec3 gxperp = ray3d::Sub(td, ray3d::Scale(gfr.zAxis, ray3d::Dot(td, gfr.zAxis)));
    if (!(ray3d::Length(gxperp) > 1e-9 * sc))
      return Fail(Problem::BooleanResultInvalid, outWhy);
    gfr.xAxis = ray3d::Normalize(gxperp);
    gfr.yAxis = ray3d::Normalize(ray3d::Cross(gfr.zAxis, gfr.xAxis));
    const double galpha =
        std::atan2(ray3d::Dot(td, gfr.zAxis), ray3d::Dot(td, gfr.xAxis));
    const double gca = std::cos(galpha);
    const double gsa = std::sin(galpha);
    if (!(gca > 1e-6))
      return false;  // (near-)perpendicular — the branch above handles that exactly
    const double gOff = ray3d::Dot(ray3d::Sub(cThin, cThick), gfr.yAxis);
    if (!(std::fabs(gOff) + r < R - eps))
      return false;  // the thin does not clear the thick's equator at every longitude
    // Conservative (not tight) bounds on the curve's extent, enough to gate "fully crosses" /
    // "caps clear": |s(phi)| <= (r|sin a| + R)/cos a, and the thick-axis component of the curve is
    // bounded the same way the coplanar-tilted case bounds zetaMax / zThickMax.
    const double sBound = (r * std::fabs(gsa) + R) / gca;
    const double zBound = sBound * std::fabs(gsa) + r * gca;
    const double sThinAt = ray3d::Dot(ray3d::Sub(cThin, thin.axis.origin), td);
    const double sThickAt = ray3d::Dot(ray3d::Sub(cThick, thick.axis.origin), kd);
    if (sThinAt - sBound < eps || thin.length - sThinAt - sBound < eps ||
        sThickAt - zBound < eps || thick.length - sThickAt - zBound < eps)
      return false;
    *handled = true;
    const double zB0 = -sThickAt;
    const double zB1 = thick.length - sThickAt;
    const double zetaA0 = -sThinAt;
    const double zetaA1 = thin.length - sThinAt;
    if (op == BoolOp::Subtract && !minuendThick) {
      Solid up;
      Solid down;
      if (!BuildGeneralBranchPipeThinStub(gfr, r, R, galpha, gOff, zetaA1, 1, &up, outWhy) ||
          !BuildGeneralBranchPipeThinStub(gfr, r, R, galpha, gOff, zetaA0, -1, &down, outWhy))
        return false;
      out->push_back(std::move(up));
      out->push_back(std::move(down));
      return Succeed(outWhy);
    }
    Solid result;
    bool gok = false;
    if (op == BoolOp::Intersect)
      gok = BuildGeneralBranchPipeIntersection(gfr, r, R, galpha, gOff, &result, outWhy);
    else if (op == BoolOp::Subtract)
      gok = BuildGeneralBranchPipeSubtract(gfr, r, R, galpha, gOff, zB0, zB1, &result, outWhy);
    else
      gok = BuildGeneralBranchPipeUnion(gfr, r, R, galpha, gOff, zB0, zB1, zetaA0, zetaA1, &result,
                                        outWhy);
    if (!gok)
      return false;
    out->push_back(std::move(result));
    return Succeed(outWhy);
  }

  const double pAlong = ray3d::Dot(wBA, az);
  const double qAlong = ray3d::Dot(wBA, bz);
  const Vec3 meet =
      ray3d::Add(A.axis.origin, ray3d::Scale(az, (pAlong - axDot * qAlong) / (1.0 - axDot * axDot)));

  ucs::Ucs fr;
  if (!ucs::FromNormal(meet, thick.axis.zAxis, &fr))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 xperp = ray3d::Sub(
      thin.axis.zAxis, ray3d::Scale(fr.zAxis, ray3d::Dot(thin.axis.zAxis, fr.zAxis)));
  if (!(ray3d::Length(xperp) > 1e-9 * sc))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  fr.xAxis = ray3d::Normalize(xperp);
  fr.yAxis = ray3d::Normalize(ray3d::Cross(fr.zAxis, fr.xAxis));
  // `fr.xAxis` is `thin`'s own in-plane component, so `thin`'s axis is cos α·x̂ + sin α·ẑ with
  // α ∈ (−π/2, π/2); α ≈ 0 is the perpendicular tee.
  const double alpha =
      std::atan2(ray3d::Dot(thin.axis.zAxis, fr.zAxis), ray3d::Dot(thin.axis.zAxis, fr.xAxis));
  const double ca = std::cos(alpha);

  const double sThin = ray3d::Dot(ray3d::Sub(meet, thin.axis.origin), thin.axis.zAxis);
  const double sThick = ray3d::Dot(ray3d::Sub(meet, thick.axis.origin), thick.axis.zAxis);
  // The thin cylinder must fully cross the thick one and the lens must clear the thick one's caps —
  // widened for the tilt (at α = 0 these reduce to `sThin ≥ R`, `sThick ≥ r`).
  const double zetaMax = (R + r * std::fabs(std::sin(alpha))) / ca;
  const double zThickMax = zetaMax * std::fabs(std::sin(alpha)) + r;
  if (sThin - zetaMax < eps || thin.length - sThin - zetaMax < eps || sThick - zThickMax < eps ||
      thick.length - sThick - zThickMax < eps)
    return false;

  const bool minuendIsThick = A.radius > B.radius;

  if (op == BoolOp::Subtract && !minuendIsThick) {
    // thin − thick: the thick bites the thin in two — a pair of stubs, each a short branch with an
    // inward dimple where the main cut it (issue #242). Handles perpendicular and tilted alike.
    *handled = true;
    Solid up;
    Solid down;
    if (!BuildBranchPipeThinStub(fr, r, R, alpha, thin.length - sThin, 1, &up, outWhy) ||
        !BuildBranchPipeThinStub(fr, r, R, alpha, -sThin, -1, &down, outWhy))
      return false;
    out->push_back(std::move(up));
    out->push_back(std::move(down));
    return Succeed(outWhy);
  }

  if (!perp) {
    *handled = true;
    const double zB0n = -sThick;
    const double zB1n = thick.length - sThick;
    Solid tilted;
    bool ok = false;
    if (op == BoolOp::Intersect)
      ok = BuildAngledBranchPipeIntersection(fr, r, R, alpha, &tilted, outWhy);
    else if (op == BoolOp::Subtract)
      ok = BuildAngledBranchPipeSubtract(fr, r, R, alpha, zB0n, zB1n, &tilted, outWhy);
    else
      ok = BuildAngledBranchPipeUnion(fr, r, R, alpha, zB0n, zB1n, -sThin, thin.length - sThin,
                                      &tilted, outWhy);
    if (!ok)
      return false;
    out->push_back(std::move(tilted));
    return Succeed(outWhy);
  }

  *handled = true;

  const double zB0 = -sThick;
  const double zB1 = thick.length - sThick;
  const double sThinFr = ray3d::Dot(ray3d::Sub(meet, thin.axis.origin), fr.xAxis);
  const double xA0 = -sThinFr;
  const double xA1 = thin.length - sThinFr;
  Solid result;
  if (op == BoolOp::Intersect) {
    if (!BuildBranchPipeIntersection(fr, r, R, &result, outWhy))
      return false;
  } else if (op == BoolOp::Subtract) {
    if (!BuildBranchPipeSubtract(fr, r, R, zB0, zB1, &result, outWhy))
      return false;
  } else {
    if (!BuildBranchPipeUnion(fr, r, R, zB0, zB1, xA0, xA1, &result, outWhy))
      return false;
  }
  out->push_back(std::move(result));
  return Succeed(outWhy);
}

/// Recognise two equal-radius cylinders whose axes cross at right angles, both piercing clear of the
/// other's caps. INTERSECT is a Steinmetz bicylinder, `A − B` is `A` with a clean perpendicular
/// channel, and `A ∪ B` is a T-pipe — all closed form. `*handled` stays false when the pair is not
/// this configuration, so the caller falls through to the coaxial recogniser.
[[nodiscard]] bool TryBooleanSteinmetz(const CylinderShape& A, const CylinderShape& B, BoolOp op,
                                       std::vector<Solid>* out, bool* handled, Problem* outWhy) {
  const double sc = A.radius + A.length + B.length;
  const double eps = 1e-7 * sc;
  if (std::fabs(A.radius - B.radius) > eps)
    return false;
  const Vec3 az = A.axis.zAxis;
  const Vec3 bz = B.axis.zAxis;
  if (std::fabs(ray3d::Dot(az, bz)) > 1e-7)
    return false;  // axes not perpendicular
  // Closest points of the two axis lines; with az ⟂ bz the normal equations decouple.
  const Vec3 w0 = ray3d::Sub(A.axis.origin, B.axis.origin);
  const Vec3 pA = ray3d::Add(A.axis.origin, ray3d::Scale(az, -ray3d::Dot(w0, az)));
  const Vec3 pB = ray3d::Add(B.axis.origin, ray3d::Scale(bz, ray3d::Dot(w0, bz)));
  if (ray3d::Length(ray3d::Sub(pA, pB)) > eps)
    return false;  // axes are skew, not intersecting
  const Vec3 meet = ray3d::Scale(ray3d::Add(pA, pB), 0.5);
  const double r = A.radius;
  const double sa = ray3d::Dot(ray3d::Sub(meet, A.axis.origin), az);
  const double sb = ray3d::Dot(ray3d::Sub(meet, B.axis.origin), bz);
  if (sa < r - eps || sa > A.length - r + eps || sb < r - eps || sb > B.length - r + eps)
    return false;  // an intersection ellipse would run off a cap — not the clean bicylinder

  *handled = true;

  ucs::Ucs fr;
  if (!ucs::FromNormal(meet, az, &fr))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  Vec3 xa = ray3d::Sub(bz, ray3d::Scale(az, ray3d::Dot(bz, az)));
  if (!(ray3d::Length(xa) > 1e-9))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  fr.xAxis = ray3d::Normalize(xa);
  fr.yAxis = ray3d::Normalize(ray3d::Cross(fr.zAxis, fr.xAxis));
  const double zA0 = -sa;
  const double zA1 = A.length - sa;
  const double xB0 = -sb;
  const double xB1 = B.length - sb;
  Solid r2;
  bool ok = false;
  if (op == BoolOp::Intersect)
    ok = BuildSteinmetzIntersection(fr, r, &r2, outWhy);
  else if (op == BoolOp::Subtract)
    ok = BuildSteinmetzSubtract(fr, r, zA0, zA1, &r2, outWhy);
  else
    ok = BuildSteinmetzUnion(fr, r, zA0, zA1, xB0, xB1, &r2, outWhy);
  if (!ok)
    return false;
  out->push_back(std::move(r2));
  return Succeed(outWhy);
}

} // namespace brep
