#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "util/ucs.hpp"

/// How the model viewport draws (REQ-064 / ADR-026 (e)).
///
/// Depth testing was left OFF by ADR-025 ASSUMPTION-1 precisely until a visual-style requirement
/// existed, because enabling it silently would have changed how every existing drawing looks the
/// moment the view tilted. This enum is that requirement arriving: **Wireframe2D keeps depth
/// testing off and is bit-for-bit the pre-REQ-064 renderer**, and only the other styles turn it on.
enum class VisualStyle : std::uint8_t {
  Wireframe2D = 0,  ///< Default. Every edge visible; draw order decides. AutoCAD's "2D Wireframe".
  Hidden = 1,       ///< Wireframe plus depth testing: near geometry occludes far.
  Shaded = 2,       ///< Filled surfaces with diffuse headlight shading, edges still drawn.
};

/// Canonical name, for the command line, the ribbon and `.gs`.
[[nodiscard]] inline const char* VisualStyleName(VisualStyle s) {
  switch (s) {
  case VisualStyle::Wireframe2D: return "2D Wireframe";
  case VisualStyle::Hidden:      return "Hidden";
  case VisualStyle::Shaded:      return "Shaded";
  }
  return "2D Wireframe";
}

/// Parse a user-typed style name. Case-insensitive, tolerant of the spellings someone actually
/// types (`2D`, `wireframe`, `h`, `shaded`, and the raw enum ordinals `0`/`1`/`2`).
///
/// Lives here, in the dependency-free header, rather than beside the command: it is the one piece of
/// this feature that is pure logic with a real bug class — a mistyped alias silently accepting the
/// wrong style — and here it is reachable by the test target. Returns false and leaves \p out
/// untouched on anything unrecognised, so a bad command cannot change the view (REQ-201).
[[nodiscard]] inline bool VisualStyleFromName(const std::string& raw, VisualStyle* out) {
  if (!out)
    return false;
  std::string v;
  v.reserve(raw.size());
  for (char c : raw) {
    if (c == ' ' || c == '\t')
      continue;  // "2d wireframe" and "2dwireframe" are the same request
    v.push_back(static_cast<char>((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c));
  }
  if (v == "2d" || v == "2dwireframe" || v == "wireframe" || v == "w" || v == "0")
    *out = VisualStyle::Wireframe2D;
  else if (v == "hidden" || v == "h" || v == "1")
    *out = VisualStyle::Hidden;
  else if (v == "shaded" || v == "s" || v == "2")
    *out = VisualStyle::Shaded;
  else
    return false;
  return true;
}

// Pure CAD entity value types, dependency-free so both the model-space command layer
// (CadCommands.hpp) and the paper-space data model (PaperSpace.hpp) can reuse them
// without a circular include (ADR-009). Behaviour/free functions live in CadCommands.hpp.

/// Display / CAD metadata stored per entity (parallel index to line segments or circles).
struct EntityAttributes {
  /// Stable per-drawing identity (REQ-076 / ADR-027). Assigned from \ref AppCommandState::nextEntityId
  /// by \ref EnsureEntityIds, persisted in `.gs`, and **never reused within a drawing** — so a
  /// reference to a deleted entity resolves to nothing rather than to whatever took its array slot.
  ///
  /// **0 means unassigned**, which is a transient state between an entity's creation and the next
  /// sweep. Cross-object references store this id, never an array index (architecture §11.9): the
  /// entity arrays compact on erase, so an index is not a name.
  std::uint64_t id = 0;
  std::string layer = "0";
  std::string color = "ByLayer";
  /// "ByLayer", "ByBlock", or a linetype table name such as "Continuous" / "DASHED" / "CENTER".
  std::string linetype = "ByLayer";
  /// Millimetres on paper; \c -1.f means ByLayer (DXF 370 = -1).
  float lineweightMm = -1.f;
  /// 0 = opaque, 1 = fully transparent; \c -1.f means ByLayer.
  float transparency = -1.f;
};

/// A named text style (REQ-044 / ADR-020): reusable font + size + slant + weight applied to TEXT/MTEXT.
/// Color is intentionally NOT a style property — it stays a layer/object property (AutoCAD-faithful).
/// The drawing owns a \c std::vector<TextStyle>; each \ref CadAnnotation references one by \c styleName
/// with per-property override flags (bake-on-write resolution — see TextStyle.hpp).
struct TextStyle {
  std::string name;            ///< Unique within the drawing; "Standard" always exists and is not deletable.
  std::string fontFamily;  ///< "" = app default (`romans.shx`); TrueType family or SHX file name.
  float heightInches = 0.125f; ///< Plotted text height in inches (model height = this × drawing scale).
  float obliqueDeg = 0.f;      ///< Slant angle in degrees (>0 shears glyph tops to the right).
  bool bold = false;
  bool italic = false;
};

/// One drawable component of a surface style (REQ-070 / ADR-036 (d) (i)): the triangle edges, the
/// border, the major or minor contours, or the points.
///
/// The four properties are encoded exactly as \ref EntityAttributes encodes the same four, so the
/// resolvers the renderer already has (\c ResolveStoredColorForViewport, the lineweight helpers) work
/// on a component unchanged, and there is no second parsing path to keep in step with the first.
///
/// **Layer and plot style are deliberately absent** (ADR-036 (i)). A surface has exactly one layer —
/// its own \ref EntityAttributes — and GoSurvey has no plot-style table, so a per-component column for
/// either could be shown but never honoured, and a control the product will not obey is worse than an
/// absent one.
struct SurfaceComponentStyle {
  bool visible = true;
  /// "ByLayer", a `#RRGGBB` literal, or a named preset — \ref EntityAttributes::color's encoding.
  std::string color = "ByLayer";
  std::string linetype = "ByLayer";
  float lineweightMm = -1.f;  ///< Millimetres on paper; \c -1.f means ByLayer.

  bool operator==(const SurfaceComponentStyle& o) const {
    return visible == o.visible && color == o.color && linetype == o.linetype &&
           lineweightMm == o.lineweightMm;
  }
  bool operator!=(const SurfaceComponentStyle& o) const { return !(*this == o); }
};

/// One band of a surface analysis range table (REQ-072 / ADR-036 (g)).
///
/// Only the TOP of each band is stored. The bottom is the band below it, and the lowest band has no
/// bottom at all — which is not a saving but the rule: `util/surfaceanalysis`'s \c AssignBand reads
/// exactly this list, and storing both ends would let a table exist whose bands overlap or leave a
/// gap, i.e. a value with two colours or none. Bands must be in **strictly ascending** order.
struct SurfaceBand {
  /// The top of the band: **feet** when the mode is Elevation, **percent grade** when it is Slope.
  /// `double` for the same reason the contour intervals below are — a band edge is a parameter the
  /// legend prints, not a coordinate, so architecture §11.8's float-storage rule does not reach it.
  double upperBound = 0.0;
  /// "ByLayer", a `#RRGGBB` literal, or a named preset — \ref EntityAttributes::color's encoding,
  /// the same one \ref SurfaceComponentStyle uses, so a band colour is picked with the same control.
  std::string color = "ByLayer";

  bool operator==(const SurfaceBand& o) const {
    return upperBound == o.upperBound && color == o.color;
  }
  bool operator!=(const SurfaceBand& o) const { return !(*this == o); }
};

/// Which quantity a surface's triangles are coloured by (REQ-072).
///
/// \c None is the default and is what REQ-072's "turning banding off restores the style's plain
/// display unchanged" means: the plain display is the state a style STARTS in, not one it has to be
/// returned to, so a drawing that never opens the Analysis tab cannot be affected by it.
enum class SurfaceAnalysisMode { None = 0, Elevation = 1, Slope = 2, Direction = 3, SlopeAngle = 4 };

/// A named surface style (REQ-070 / ADR-036 (d)): how a surface is *drawn*, never what it is made of.
///
/// The drawing owns a \c std::vector<SurfaceStyle> and each \ref CadSurface references one by
/// \c styleName, so editing a style changes every surface using it — which is REQ-070's stated point,
/// and the reason a per-surface copy was declined (ADR-036 alternative (1)). "Standard" always exists
/// and is not deletable, exactly as \ref TextStyle's does; a name that no longer resolves falls back
/// to it rather than failing to draw.
///
/// **Resolution is on read, not bake-on-write** — the opposite of the choice ADR-020 made for text,
/// and deliberately (ADR-036 (d)): a text style is baked because ~12 render sites read the baked
/// value, whereas a surface style is read at exactly one place, the display-geometry generator.
/// Baking would put a stale copy on the surface and would defeat REQ-070's central constraint, that
/// changing a style property must not re-triangulate.
struct SurfaceStyle {
  std::string name;  ///< Unique within the drawing.

  SurfaceComponentStyle triangles;
  SurfaceComponentStyle border;
  SurfaceComponentStyle majorContour;
  SurfaceComponentStyle minorContour;
  SurfaceComponentStyle points;

  /// Contour intervals in feet. **double, not float**: these are multiplied by a step count to
  /// produce each contour's elevation, so a 0.1 ft interval stored as `float` would put every level
  /// slightly off the round number it is labelled with. They are a parameter, not a coordinate, so
  /// architecture §11.8's float-storage rule does not reach them.
  ///
  /// The major interval must be a whole multiple of the minor one — REQ-070 makes rejecting anything
  /// else an acceptance condition, because a major contour drawn at a level that is not also a minor
  /// level is a mis-labelled contour. \c SurfaceStyles::IntervalsCompatible is that rule.
  double minorIntervalFt = 2.0;
  double majorIntervalFt = 10.0;
  std::vector<double> userContourFt;
  int contourSmoothPasses = 0;       ///< Chaikin 0–5 (REQ-138).
  double contourLabelSpacingFt = 0.0;  ///< 0 = off.

  /// REQ-072 analysis. All four default to "off", so a style that never visits the Analysis tab —
  /// and every style in every drawing written before REQ-072 existed — displays exactly as it did.
  SurfaceAnalysisMode analysisMode = SurfaceAnalysisMode::None;

  /// The range table the triangles are coloured by. Its units follow \ref analysisMode: feet for
  /// Elevation, percent grade for Slope. Strictly ascending; \c AssignBand depends on it.
  ///
  /// **One table, whose meaning the mode sets** — rather than one table per mode. A surface is banded
  /// by elevation or by slope, never both at once (the triangle has one colour, ASSUMPTION-1), so a
  /// second table could only ever be the one not being displayed, and the legend would have to guess
  /// which of the two it was showing.
  std::vector<SurfaceBand> bands;

  /// REQ-072's slope arrows, independent of \ref analysisMode: a surveyor reads fall direction on an
  /// unbanded surface as often as on a banded one, and the requirement lists them as separate
  /// toggles.
  bool slopeArrowsOn = false;

  /// The arrows' own colour ramp, in **percent grade** — the same \ref SurfaceBand type and the same
  /// \c AssignBand rule, so an arrow's colour is decided exactly as a band's is. Separate from
  /// \ref bands because arrows are always graded by SLOPE, while \ref bands may be showing elevation.
  /// Empty means every arrow takes the surface's own colour.
  std::vector<SurfaceBand> arrowBands;

  bool operator==(const SurfaceStyle& o) const {
    return name == o.name && triangles == o.triangles && border == o.border &&
           majorContour == o.majorContour && minorContour == o.minorContour && points == o.points &&
           minorIntervalFt == o.minorIntervalFt && majorIntervalFt == o.majorIntervalFt &&
           userContourFt == o.userContourFt && contourSmoothPasses == o.contourSmoothPasses &&
           contourLabelSpacingFt == o.contourLabelSpacingFt &&
           analysisMode == o.analysisMode && bands == o.bands &&
           slopeArrowsOn == o.slopeArrowsOn && arrowBands == o.arrowBands;
  }
  bool operator!=(const SurfaceStyle& o) const { return !(*this == o); }
};

/// One drawable run of surface display geometry: a line buffer plus the appearance to draw it with
/// (REQ-070 / ADR-036 (h)).
///
/// **The vertices are BORROWED, never owned.** They point into
/// `AppCommandState::surfaceDisplayCache`, which is where the generated geometry actually lives, and
/// a 200,000-triangle surface's edge buffer is ~14 MB — copying it into a batch list would double
/// that on every rebuild for nothing. The batch list and the cache it points into are rebuilt
/// together in \c RefreshSurfaceDisplayGeometry and consumed in the same frame, so a batch never
/// outlives its buffer. **Nothing may hold one across a call to that function.**
struct SurfaceDisplayBatch {
  /// World-space x,y,z, six floats per segment — `GL_LINES` layout, the same one `surveyMarkers`
  /// and the old `surfaceEdges` parameter used. Never null in a batch that reaches the renderer.
  const std::vector<float>* verts = nullptr;
  float rgba[4] = {1.f, 1.f, 1.f, 1.f};
  /// Millimetres on paper, or \c -1.f for the renderer's default width. Millimetres and not pixels:
  /// the mm-to-device conversion depends on the framebuffer, which is the renderer's business and
  /// not the Domain's.
  float lineweightMm = -1.f;
};

/// One filled-triangle run of REQ-072 band geometry: a solid interior colour, no line width.
///
/// A sibling of \ref SurfaceDisplayBatch rather than a reuse of it: \c lineweightMm has no meaning
/// for a filled interior, and giving this struct its own shape keeps a caller from ever passing a
/// band buffer to the `GL_LINES` path or vice versa (TASK-086 §6 (3)).
struct SurfaceTriangleBatch {
  /// World-space x,y,z, nine floats per triangle — `GL_TRIANGLES` layout. Never null in a batch that
  /// reaches the renderer. **Borrowed**, exactly as \ref SurfaceDisplayBatch::verts is — see that
  /// struct's ownership note; the same rule and the same lifetime apply here.
  const std::vector<float>* verts = nullptr;
  float rgba[4] = {1.f, 1.f, 1.f, 1.f};
};

/// Everything drawn for the drawing's surfaces this frame (REQ-070 / ADR-036 (h)).
///
/// Replaces — rather than joins — the former flat `surfaceEdges` parameter on \c RenderScene. A
/// signature already 24 parameters long does not get four more for components that are always built
/// together and always consumed together.
struct CadSurfaceDisplayGeometry {
  /// REQ-072 band fills — drawn FIRST, before everything below, so the wireframe, contours, border
  /// and slope arrows all stay legible on top of the opaque interior rather than under it.
  std::vector<SurfaceTriangleBatch> bandTriangles;

  /// In draw order: triangles first, then contours, then the border, then REQ-072's slope arrows
  /// last — on top of the border, because an arrow that reads as buried under the outline it crosses
  /// is not a readable arrow.
  std::vector<SurfaceDisplayBatch> lines;

  [[nodiscard]] bool empty() const { return bandTriangles.empty() && lines.empty(); }
};

/// The Volume Dashboard's cut/fill map (REQ-073 amendment, TASK-095 §6 step 5) — a SEPARATE struct
/// from \ref CadSurfaceDisplayGeometry rather than a field added to it, because this is not a
/// per-surface style artefact: it is one dashboard's comparison between exactly two surfaces, and
/// bundling it into the per-surface cache would conflate two different staleness keys (a surface's
/// own style vs. a dashboard's own picked pair).
///
/// Two batches, both `GL_TRIANGLES`, both borrowed from `AppCommandState::VolumeDashboardState`
/// exactly as \ref SurfaceTriangleBatch borrows from the surface display cache — never held across a
/// frame boundary.
struct VolumeMapDisplayGeometry {
  SurfaceTriangleBatch cut;   ///< Empty `verts` when there is nothing to show (REQ-073's "nothing
                              ///< outside the common area" — the same rule, applied to "no cut here").
  SurfaceTriangleBatch fill;

  [[nodiscard]] bool empty() const {
    return (!cut.verts || cut.verts->empty()) && (!fill.verts || fill.verts->empty());
  }
};

/// Single-line TEXT, MTEXT box, or aligned linear dimension drawn over the viewport (world coordinates;
/// for paper-space entities the coordinates are paper inches — see ADR-009).
struct CadAnnotation {
  enum class Kind { Text = 0, Mtext = 1, DimAligned = 2, DimLinear = 3, DimAngular = 4, Table = 5 };
  Kind kind = Kind::Text;
  float insX = 0.f;
  float insY = 0.f;
  /// Elevation of the insertion point (REQ-057 / ADR-025). **Absolute**, not offset by
  /// \c worldDocumentOrigin — the local-storage rebase is X/Y-only (ADR-025 D2, see
  /// CadCoordinateFrame.hpp). Always 0 for paper-space text: a sheet is 2D (ADR-025 (g)).
  float insZ = 0.f;
  /// Text height in plotted inches (constant on sheet); model height = this × drawing scale.
  float plottedHeightInches = 0.125f;
  /// CCW from +X (math \c atan2); UI shows clockwise-from-north via \c BearingCwNorthDegFromMathAngleRad.
  float rotationRad = 0.f;
  std::string text;
  float boxMinX = 0.f, boxMinY = 0.f, boxMaxX = 0.f, boxMaxY = 0.f;
  /// MTEXT attachment point (DXF group 71): 1=top-left … 5=middle-center … 9=bottom-right.
  /// Drives in-box justification when rendering; default 1 keeps legacy top-left behavior.
  int mtextAttach = 1;
  /// Typeface for TEXT (and the base font for MTEXT runs without a [[font:…]] override): a TrueType
  /// family ("Arial") or an SHX name ("romans.shx"). Empty = the application default font.
  std::string fontFamily;
  /// TEXT character styling (MTEXT carries per-run styling in its [[b]]/[[i]]/[[u]] wire tags instead).
  bool bold = false;
  bool italic = false;
  bool underline = false;
  /// Named text style this annotation references (REQ-044 / ADR-020). Empty = no style: the font/height/
  /// oblique/bold/italic fields resolve from the annotation's own stored values (legacy / DXF-imported
  /// text, so older drawings render unchanged). Resolution is bake-on-write (see TextStyle.hpp): the
  /// fields above always hold the effective values, so renderers read them directly.
  std::string styleName;
  /// Oblique (slant) angle in degrees — effective value (from the style unless \c ovOblique). SHX strokes
  /// shear faithfully; TTF approximates (ImGui has no glyph shear).
  float obliqueDeg = 0.f;
  /// Per-property override flags: when set, this annotation's own field wins over its style and a style
  /// edit does not re-bake that property (ADR-020).
  bool ovFont = false, ovHeight = false, ovOblique = false, ovBold = false, ovItalic = false;
  /// \c Kind::DimAligned / \c DimLinear / \c DimAngular — extension or ray points (on measured geometry).
  float dimExt1X = 0.f, dimExt1Y = 0.f, dimExt2X = 0.f, dimExt2Y = 0.f;
  /// \c Kind::DimAngular — vertex (center) of the measured angle.
  float dimAngVertexX = 0.f;
  float dimAngVertexY = 0.f;
  /// Signed distance from chord midpoint to dimension line: aligned uses N=(-T.y,T.x) with T=normalize(e2-e1); linear uses N=(0,1) for horizontal span or N=(1,0) for vertical span; angular uses arc radius (world units).
  float dimSignedOffset = 0.f;
  /// \c Kind::DimLinear only — if true, measures |Y2−Y1| with a vertical dimension line; if false, |X2−X1| with horizontal dim line.
  bool dimLinearVertical = false;
  /// If >= 0, this MTEXT is the viewport label for the survey point whose \c SurveyPoint::id is this
  /// value (bidirectional link; the other half is \c SurveyPoint::labelMtextAnnId).
  ///
  /// **A point id, not an array index** (REQ-076 / architecture §11.9). Renamed from
  /// `surveyPointLabelFor` when it stopped being an index, deliberately: the rename turns every one
  /// of its ~35 read sites into a compile error rather than letting an index-shaped assumption
  /// survive as silently wrong data (the ADR-025 (a) lesson).
  int surveyPointLabelForId = -1;
  /// When true, the label was manually dragged and these world-unit offsets from the point override the global defaults.
  bool surveyLabelHasUserOffset = false;
  float surveyLabelUserOffsetEast = 0.f;
  float surveyLabelUserOffsetNorth = 0.f;
  /// REQ-148: Kind::Table — column count and row-major cell strings.
  int tableCols = 0;
  std::vector<std::string> tableCells;
};

[[nodiscard]] inline bool CadAnnotationHasTextBox(CadAnnotation::Kind k) {
  return k == CadAnnotation::Kind::Mtext || k == CadAnnotation::Kind::Table;
}

/// Committed 3-point arc (circumcircle + start/sweep in radians from +X).
/// Dependency-free so both the model store (CadCommands.hpp) and the paper-space store (PaperSpace.hpp,
/// ADR-013) can hold arcs without a circular include.
struct CadArc {
  float cx = 0.f;
  float cy = 0.f;
  float r = 0.f;
  float startRad = 0.f;
  float sweepRad = 0.f;
  /// Elevation of the arc plane (REQ-057 / ADR-025). Absolute (ADR-025 D2), always 0 in paper
  /// space (ADR-025 (g)). On a tilted arc this is the CENTRE point of the plane, not an
  /// elevation every point on the arc shares.
  float z = 0.f;
  /// Plane normal (REQ-312). World +Z is the flat case, which is every arc that existed before
  /// this field, and `ucs::FromNormal` maps a +Z normal onto the world X and Y axes exactly -- so
  /// `startRad` and `sweepRad` keep the meaning they have always had and a flat arc stays
  /// bit-identical through save and reload. A tilted arc measures those same two angles in the
  /// plane `ucs::FromNormal({cx, cy, z}, {nx, ny, nz})` gives -- the Arbitrary Axis Algorithm, so a
  /// DXF consumer that rebuilds the frame from group 210 lands on the same points. Paper space is
  /// 2D (ADR-025 (g)) and stays +Z there.
  float nx = 0.f;
  float ny = 0.f;
  float nz = 1.f;
};

// ---------------------------------------------------------------------------------------------
// Curve plane normals (REQ-312).
//
// An arc carries its normal in the struct above. A circle cannot: the circle store is a flat
// 4-float quad read directly at roughly 300 call sites, so the normal rides in a side-car
// std::vector<float> of 3 floats per circle, parallel to the quads the way the attribute array
// already is (D-2026-08-31-f). Roughly forty call sites append or erase a circle and every one of
// them has to keep the pairing, so the helpers below exist to make that one line rather than three.
// ---------------------------------------------------------------------------------------------

/// World +Z: the plane every arc and circle lay in before REQ-312, and the default for a new one.
inline constexpr float kFlatNormalX = 0.f;
inline constexpr float kFlatNormalY = 0.f;
inline constexpr float kFlatNormalZ = 1.f;

/// True when a normal is world +Z exactly.
///
/// Exact comparison, deliberately. This is the test that decides whether `.gs` writes a normal key
/// at all, and a legacy drawing has to re-save byte-identically -- a tolerance here would let a
/// normal that is 1e-9 off +Z re-save as flat, which is a silent edit to the user's file.
[[nodiscard]] inline bool IsFlatNormal(float nx, float ny, float nz) {
  return nx == kFlatNormalX && ny == kFlatNormalY && nz == kFlatNormalZ;
}

/// Append one circle's normal. Call wherever a quad is appended to the circle store.
inline void PushCircleNormal(std::vector<float>& normals, float nx = kFlatNormalX,
                             float ny = kFlatNormalY, float nz = kFlatNormalZ) {
  normals.push_back(nx);
  normals.push_back(ny);
  normals.push_back(nz);
}

/// Erase circle \p index's normal. Call wherever a quad is erased from the circle store.
inline void EraseCircleNormal(std::vector<float>& normals, size_t index) {
  if (index * 3 + 3 > normals.size())
    return;
  const auto begin = normals.begin() + static_cast<std::ptrdiff_t>(index * 3);
  normals.erase(begin, begin + 3);
}

/// Circle \p index's normal components, or world +Z when the side-car is short.
///
/// A short side-car is a defect, and `docinvariants` reports it loudly (REQ-204). Returning the
/// flat default here means a READ never faults on one, so the invariant check stays the thing that
/// tells you about it rather than a crash somewhere unrelated.
inline void CircleNormalAt(const std::vector<float>& normals, size_t index, float* nx, float* ny,
                           float* nz) {
  const bool have = index * 3 + 3 <= normals.size();
  if (nx)
    *nx = have ? normals[index * 3 + 0] : kFlatNormalX;
  if (ny)
    *ny = have ? normals[index * 3 + 1] : kFlatNormalY;
  if (nz)
    *nz = have ? normals[index * 3 + 2] : kFlatNormalZ;
}

/// True when circle \p index lies in a plane parallel to world XY.
[[nodiscard]] inline bool CircleIsFlat(const std::vector<float>& normals, size_t index) {
  float nx = 0.f, ny = 0.f, nz = 1.f;
  CircleNormalAt(normals, index, &nx, &ny, &nz);
  return IsFlatNormal(nx, ny, nz);
}

/// Grow or shrink the side-car to match \p circleCount circles, filling new entries with world +Z.
///
/// The migration path for a `.gs` written before REQ-312, and the repair a loader applies before
/// the document is handed to anything that reads a normal.
inline void EnsureCircleNormals(std::vector<float>& normals, size_t circleCount) {
  const size_t want = circleCount * 3;
  if (normals.size() > want) {
    normals.resize(want);
    return;
  }
  while (normals.size() < want)
    PushCircleNormal(normals);
}

/// A curve plane normal (REQ-312) put through the same planar transform its entity centre gets.
///
/// A normal is a direction, so it ignores the translation half of the transform: the rotation runs
/// about the origin, and the reflection is about a line through the origin at the mirror line's own
/// angle. The reflection uses the SAME 2*phi - theta rule the arc sweep already reflects by, written
/// as its matrix -- so a mirrored arc's plane and its swept range cannot end up disagreeing about
/// which way the mirror faced.
///
/// World +Z is a fixed point of both, which is why nothing needed either of these until arcs and
/// circles could tilt.
inline void RotateNormalAboutZ(float rad, float* nx, float* ny) {
  if (!nx || !ny)
    return;
  const float c = std::cos(rad);
  const float s = std::sin(rad);
  const float dx = *nx;
  const float dy = *ny;
  *nx = c * dx - s * dy;
  *ny = s * dx + c * dy;
}

inline void ReflectNormalAcrossLine(float x0, float y0, float x1, float y1, float* nx, float* ny) {
  if (!nx || !ny)
    return;
  const float phi = std::atan2(y1 - y0, x1 - x0);
  const float c = std::cos(2.f * phi);
  const float s = std::sin(2.f * phi);
  const float dx = *nx;
  const float dy = *ny;
  *nx = c * dx + s * dy;
  *ny = s * dx - c * dy;
}

/// The plane a curve carrying a normal lies in (REQ-311 / REQ-312): `ucs::FromNormal` at its centre.
///
/// **One function, deliberately.** The renderer, the hit test, the object snap, the DXF writer and
/// the tests all need to turn (centre, normal) into a frame they can measure an angle in, and any
/// two of them choosing a different zero direction for the same arc is a defect that looks like
/// nothing until a tilted arc is drawn. `ucs::FromNormal` is AutoCAD's Arbitrary Axis Algorithm —
/// the one DXF specifies for the group 210 extrusion vector — so this frame is also the frame a
/// DXF consumer reconstructs from the file.
///
/// For the flat case the algorithm returns the world X and Y axes exactly, so `startRad` and
/// `sweepRad` on every arc that predates REQ-312 keep the meaning they have always had.
///
/// A degenerate normal falls back to world XY rather than producing a garbage frame. The refusal
/// belongs where the curve is created (REQ-201); by the time anything draws one, the normal has
/// already been checked.
[[nodiscard]] inline ucs::Ucs CurvePlane(double cx, double cy, double cz, double nx, double ny, double nz) {
  ucs::Ucs p;
  if (ucs::FromNormal({cx, cy, cz}, {nx, ny, nz}, &p))
    return p;
  p = ucs::Ucs{};
  p.origin = {cx, cy, cz};
  return p;
}

/// The plane an arc lies in — \ref CurvePlane at its centre and elevation.
[[nodiscard]] inline ucs::Ucs CurvePlane(const CadArc& a) {
  return CurvePlane(static_cast<double>(a.cx), static_cast<double>(a.cy), static_cast<double>(a.z),
                    static_cast<double>(a.nx), static_cast<double>(a.ny), static_cast<double>(a.nz));
}

/// A point at \p angleRad around an arc or circle, in the same space its centre is stored in.
[[nodiscard]] inline ray3d::Vec3 CurvePointAt(const ucs::Ucs& plane, double radius, double angleRad) {
  return ucs::PointOnPlaneCircle(plane, radius, angleRad);
}

// ---------------------------------------------------------------------------------------------
// Walking a curve (REQ-312).
//
// A circle or arc is drawn, hit-tested, snapped to, measured for extents and exported by walking
// it. Every one of those walks goes through \ref CurvePointAt above, so a tilted curve cannot be
// drawn in one place and picked in another -- what differs between the call sites is only what
// each does with the point (world triplets here; view-relative pairs plus a per-vertex Z in the
// renderer, which needs the view anchor subtracted in double before the cast), never where the
// point is.
//
// Each call site keeps its pre-REQ-312 two-dimensional arithmetic for a flat (+Z) curve.
// `ucs::FromNormal` reproduces the world X and Y axes exactly for a +Z normal, so the two paths
// agree to the bit and the branch is not needed for correctness -- it is here because this is the
// per-vertex loop for every curve on screen, and a flat drawing should not pay for a frame
// transform whose answer it already knows. That is the same reasoning as step 3's
// `CadWorkPlaneIsWorldXy` guard, one level further out.
// ---------------------------------------------------------------------------------------------

/// The angle of sample \p i of \p segments across a sweep.
///
/// Trivial, and named anyway: the renderer, the snap walk and the extents walk each sampled a
/// curve with their own spelling of this, and three sequences that agree only approximately put
/// the snap point somewhere the drawn curve does not go.
[[nodiscard]] inline double CurveSampleAngle(double startRad, double sweepRad, int i, int segments) {
  const double u = static_cast<double>(i) / static_cast<double>(segments < 1 ? 1 : segments);
  return startRad + sweepRad * u;
}

/// \p segments + 1 world points along a curve, endpoints included.
///
/// A full circle is \p sweepRad = 2*pi, which repeats the start point as the last sample. That is
/// deliberate: the closed-chain consumers already expect the repeat, and an arc's true endpoint is
/// what REQ-312's acceptance is written about.
inline void SampleCurveWorld(std::vector<ray3d::Vec3>& out, const ucs::Ucs& plane, double radius,
                             double startRad, double sweepRad, int segments) {
  out.clear();
  if (!(radius > 1e-12) || segments < 1)
    return;
  out.reserve(static_cast<size_t>(segments) + 1u);
  for (int i = 0; i <= segments; ++i)
    out.push_back(CurvePointAt(plane, radius, CurveSampleAngle(startRad, sweepRad, i, segments)));
}

/// A curve as world-space GL_LINES triplets -- the shape the snap walk, the block flattener and
/// the rubber-band preview all want.
inline void AppendCurveWorldSegs(std::vector<float>& out, const ucs::Ucs& plane, double radius,
                                 double startRad, double sweepRad, int segments) {
  if (!(radius > 1e-12) || segments < 1)
    return;
  ray3d::Vec3 prev = CurvePointAt(plane, radius, startRad);
  for (int i = 1; i <= segments; ++i) {
    const ray3d::Vec3 p = CurvePointAt(plane, radius, CurveSampleAngle(startRad, sweepRad, i, segments));
    out.push_back(static_cast<float>(prev.x));
    out.push_back(static_cast<float>(prev.y));
    out.push_back(static_cast<float>(prev.z));
    out.push_back(static_cast<float>(p.x));
    out.push_back(static_cast<float>(p.y));
    out.push_back(static_cast<float>(p.z));
    prev = p;
  }
}

/// Re-measure a transformed arc's start angle in the frame its NEW normal gives it (REQ-312).
///
/// `startRad` and `sweepRad` are measured in the arc's own frame, `ucs::FromNormal(centre, normal)`,
/// and that frame is rebuilt from the normal -- so a transform that moves the normal generally turns
/// the frame by a further rotation about it. The world-XY angle rules (add the rotation; reflect the
/// angle across the mirror line) track the frame only while the frame IS world XY, which is to say
/// only while the arc is flat. Off that plane they leave an arc with the right centre, the right
/// radius and the right plane, swept from the wrong place.
///
/// That was ASSUMPTION-3 in TASK-159 -- that the sweep rule and the normal rule agree -- and it is
/// wrong. Mirroring a half circle standing on the wall y = 0 across the world line y = x should give
/// one standing on x = 0 with its ends at (0, 190, 0) and (0, 210, 0); with the angle reflected in
/// world XY its ends come out at (0, 200, -10) and (0, 200, 10) instead: the same circle, turned a
/// quarter turn inside its own plane.
///
/// So the angle is not transformed at all. \p anchorWorld is where a KNOWN point of the arc has
/// moved to, and the new start angle is measured from it: for a rotation the moved START point; for
/// a reflection the moved END point, because a mirror reverses the traversal and the flat rule
/// already names that point as the new start.
///
/// A flat arc is left untouched, so its caller's existing arithmetic stands bit for bit.
inline void CadReanchorArcStart(CadArc* a, const ray3d::Vec3& anchorWorld) {
  if (!a || IsFlatNormal(a->nx, a->ny, a->nz))
    return;
  const ucs::Point2D p = ucs::WorldToPlane(CurvePlane(*a), anchorWorld);
  a->startRad = static_cast<float>(std::atan2(p.y, p.x));
}

/// The world point at \p angleRad on \p a, through the arc's own plane.
///
/// The anchor \ref CadReanchorArcStart wants, taken BEFORE the transform: on a tilted arc this is
/// not the point the XY parametrisation names, so a caller that reaches for `cx + r*cos(t)` here
/// re-anchors the arc onto a point it never passed through.
[[nodiscard]] inline ray3d::Vec3 CurveWorldPointOnArc(const CadArc& a, double angleRad) {
  return CurvePointAt(CurvePlane(a), static_cast<double>(a.r), angleRad);
}

/// An arc's two endpoints, resolved through its own plane.
///
/// The pair REQ-312's acceptance conditions are written about, and what the endpoint snap offers.
inline void CurveEndpointsWorld(const CadArc& a, ray3d::Vec3* outStart, ray3d::Vec3* outEnd) {
  const ucs::Ucs plane = CurvePlane(a);
  const double r = static_cast<double>(a.r);
  if (outStart)
    *outStart = CurvePointAt(plane, r, static_cast<double>(a.startRad));
  if (outEnd)
    *outEnd = CurvePointAt(plane, r, static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad));
}

/// Axis-aligned ellipse: center + major-axis vector (semi-major length = |majV|) + minor/major ratio (0,1].
struct CadEllipse {
  float cx = 0.f;
  float cy = 0.f;
  float majVx = 1.f;
  float majVy = 0.f;
  float ratio = 0.5f;
  /// Elevation of the ellipse's plane (REQ-057 / ADR-025) — parallel to XY, absolute
  /// (ADR-025 D2), always 0 in paper space (ADR-025 (g)). Same rationale as \ref CadArc::z.
  float z = 0.f;
};

/// One named sub-range of a mesh — a single object from the imported model (REQ-063).
///
/// Parts exist so an imported model keeps its structure: a pipe run stays distinguishable from a
/// valve, and each carries the base colour its source material declared. They are a *drawing*
/// division, not a selection one — see \ref CadMesh.
struct CadMeshPart {
  std::string name;
  int indexBegin = 0;  ///< First index into CadMesh::indices.
  int indexCount = 0;  ///< Number of indices (a multiple of 3).
  float r = 0.78f;
  float g = 0.78f;
  float b = 0.78f;
};

/// An imported triangle mesh (REQ-063 / ADR-026 (c)) — **reference geometry, never authored here**.
///
/// No command creates one, no grip moves a vertex, and it is excluded from DXF/DWG export, which
/// has no lossless representation for it. It participates in layers, selection, erase and extents.
///
/// **Held as `shared_ptr<const CadMesh>`** by both the live state and every undo snapshot
/// (architecture §11.5 as amended 2026-08-12). That is not an optimisation detail — it is what
/// makes the entity affordable at all: snapshots deep-copy every other geometry store, 50 frames
/// deep, so a 2M-triangle mesh would otherwise cost ~2.6 GB of undo stack and be re-copied by every
/// unrelated edit. Immutability is the precondition for that sharing, so a mesh is *replaced*,
/// never modified in place.
struct CadMesh {
  /// Interleaved x,y,z — architecture §11.8, the same convention as every other geometry store.
  std::vector<float> vertsXyz;
  /// One unit normal per vertex, parallel to \ref vertsXyz (stride 3, same vertex count).
  std::vector<float> normalsXyz;
  /// Triangle list. `uint32_t` because REQ-063's 2M-triangle ceiling is 6M indices — well past
  /// what 16-bit indices could address, and the acceptance says so explicitly.
  std::vector<std::uint32_t> indices;
  std::vector<CadMeshPart> parts;
  /// Source file the model came from, for the Properties panel and the log.
  std::string sourceName;

  [[nodiscard]] int vertexCount() const { return static_cast<int>(vertsXyz.size() / 3); }
  [[nodiscard]] int triangleCount() const { return static_cast<int>(indices.size() / 3); }
};

/// The triangulation of a TIN surface (REQ-068 / ADR-028 (a)) — the large, **immutable** half.
///
/// Held as `shared_ptr<const CadTin>` by both the live state and every undo snapshot, exactly as
/// \ref CadMesh is and for the same reason (architecture §11.5, amended 2026-08-12).
/// `DrawingGeometrySnapshot` deep-copies every geometry array across 50 frames, so a by-value TIN at
/// REQ-100's surface density (~200k triangles) would cost roughly 7 MB per frame — ~350 MB of undo
/// stack, **re-paid every time an unrelated edit happens**, because drawing a line copies the whole
/// model with it. That is the trap TASK-041 found for meshes; here it was designed out in advance.
///
/// "Editing" a surface means **replacing this pointer** with a freshly built triangulation, never
/// writing through it — which is the condition the §11.5 exemption rests on.
struct CadTin {
  /// Interleaved x,y,z (architecture §11.8). X/Y are local storage coordinates (world = local +
  /// worldDocumentOrigin); Z is absolute, per ADR-025 D2 — the same convention as every other store.
  std::vector<float> vertsXyz;
  /// Triangle list, 3 indices per triangle, counter-clockwise. `uint32` to match \ref CadMesh and to
  /// leave headroom well past REQ-100's ~200k-triangle surface profile.
  std::vector<std::uint32_t> indices;

  [[nodiscard]] int vertexCount() const { return static_cast<int>(vertsXyz.size() / 3); }
  [[nodiscard]] int triangleCount() const { return static_cast<int>(indices.size() / 3); }
};

/// How a boundary ring affects the surface it is applied to (REQ-069). Mirrors
/// \c TinBoundaryKind in `util/tinbuild.hpp` field-for-field — kept as a separate local type rather
/// than an include of it, the same reason \ref CadTin mirrors `TinBuildResult`'s layout instead of
/// including `tinbuild.hpp`: this header stays dependency-free (§11.4), and the conversion is one
/// `switch` at the one call site that needs both types (`BuildSurfaceFromSources`).
enum class CadBoundaryKind : std::uint8_t { Outer, Hide, Show, Clip, Mask };

/// A boundary ring referenced by stable entity id (REQ-076) — must resolve to a **closed** polyline.
struct CadSurfaceBoundary {
  std::uint64_t entityId = 0;
  CadBoundaryKind kind = CadBoundaryKind::Outer;
  /// What the user called it, for the Surface Manager's definition tree (REQ-075). Optional and
  /// purely descriptive — the entity id is the identity, never this. Empty is normal.
  std::string name;
};

/// A breakline referenced by stable entity id (REQ-076) — a Line or a Polyline in the drawing.
///
/// A struct rather than a bare id so it can carry the description the Add Breaklines dialog collects
/// (REQ-075), mirroring \ref CadSurfaceBoundary. `.gs` writes these as objects and still reads the
/// original `breaklineIds` array of bare numbers, so a drawing saved before REQ-075 loads unchanged.
struct CadSurfaceBreakline {
  std::uint64_t entityId = 0;
  /// Descriptive only, like \ref CadSurfaceBoundary::name. Empty is normal.
  std::string description;
};

/// Per-feature-line descriptive data (REQ-087), one entry per line, parallel to
/// \c featureLineAttrs.
///
/// Kept out of \c EntityAttributes deliberately (ADR-035 (c)): that struct is clipboard-copied and
/// DXF-exported, and a feature line's name has no meaning in either place.
struct CadFeatureLineInfo {
  std::string name;         ///< What the user called it. Not required to be unique.
  std::string description;
};

/// One row of a feature line's elevation table (REQ-088).
///
/// **Derived, never stored** — ADR-035 (e): "Storing a grade would create a second source of truth
/// for the same elevation, and the two would disagree the moment geometry moved." Every field below
/// is recomputed from the vertex chain each time the table is asked for, so moving the line in plan
/// changes the stations and grades with no explicit invalidation anywhere.
struct FeatureLineElevRow {
  int    vertexIndex = 0;             ///< 0-based index within this feature line
  bool   isElevationPoint = false;    ///< a flagged vertex rather than a PI (ADR-035 (a))
  double station = 0.0;               ///< cumulative PLAN distance from the first point
  float  elevation = 0.f;
  double lengthAhead = 0.0;           ///< plan distance to the next point; 0 at an open line's end

  /// Grade over the segment behind / ahead, as a percentage: rise / plan-run x 100, matching what
  /// SURFELEV (REQ-074) already reports.
  ///
  /// **NaN, not 0, where there is no such segment.** The first point of an open line has no grade
  /// back at all, and reporting that as "0.00%" would read as "level" — a statement about the
  /// ground that happens to be false. Callers test with std::isnan and print a dash.
  double gradeBackPct = 0.0;
  double gradeAheadPct = 0.0;
};

/// A point file a surface reads its points from directly (REQ-086) — a LINK, not an import: the file
/// is re-read on every rebuild and its points never become drawing survey points.
///
/// The layout travels with the path because a point file does not describe its own column order, and
/// a link that guessed would silently swap northing for easting on reload. Same reason
/// \c skipFirstRow is stored rather than re-detected.
struct CadSurfacePointFile {
  std::string path;             ///< As the user gave it. Resolved at rebuild time, never cached.
  int  layoutIndex = 0;         ///< Index into \c SurveyCsvLayoutFromUiIndex — the REQ-083 layouts.
  bool skipFirstRow = false;    ///< The file has a header row.
};

enum class SurfaceKind : std::uint8_t { Tin, Grid, TinVolume, GridVolume, Corridor };

/// A named TIN surface (REQ-068).
///
/// Small and copyable: the heavy triangulation hangs off a shared pointer, so copying a surface —
/// which every undo snapshot does — is a couple of strings and a refcount bump.
///
/// Surfaces are **not written to DXF or DWG**: there is no representation GoSurvey can write
/// losslessly, and the exclusion is stated in the export log rather than left to be discovered
/// (ADR-028 (f), REQ-201) — the same treatment \ref CadMesh gets.
struct CadSurface {
  std::string name;  ///< Unique within the drawing.
  SurfaceKind kind = SurfaceKind::Tin;
  std::string description;
  double gridOriginX = 0.0, gridOriginY = 0.0, gridSpacingX = 1.0, gridSpacingY = 1.0;
  int gridCols = 0, gridRows = 0;
  std::vector<float> gridZ;
  std::vector<std::pair<double, double>> swappedEdgePicks;
  std::vector<std::pair<double, double>> deletedEdgePicks;
  /// REQ-144: extra vertices in the local frame (world = local + origin), stored as x,y,z triples.
  std::vector<float> addedPointXyz;
  /// REQ-144: each pick removes the nearest remaining assembled input point at rebuild (local XY).
  std::vector<std::pair<double, double>> deletedPointPicks;
  /// REQ-150: replace nearest assembled point (from local XY) with to-XYZ (local).
  struct MovedPoint {
    double fromX = 0.0, fromY = 0.0;
    float toX = 0.f, toY = 0.f, toZ = 0.f;
  };
  std::vector<MovedPoint> movedPoints;
  std::vector<CadSurfaceBreakline> corridorFeatureLines;

  /// Names of the point groups supplying the surface's points (REQ-067).
  ///
  /// **By name, not by index** — a name is a stable identifier, so architecture §11.9 is satisfied,
  /// and it mirrors how a CadAnnotation references a text style (ADR-020). A name that no longer
  /// resolves is reported at build time rather than quietly producing an empty surface.
  std::vector<std::string> sourcePointGroups;

  /// Point files feeding the surface directly (REQ-086). Read on the UI thread during
  /// `ResolveSurfaceInputs`, never on the rebuild worker — the worker stays pure and touches neither
  /// \c AppCommandState nor the filesystem (architecture §8 rule 1).
  std::vector<CadSurfacePointFile> sourcePointFiles;

  /// Breaklines forcing a triangulation edge along them (REQ-069) — each a Line or a Polyline in the
  /// drawing, referenced by stable entity id (REQ-076, architecture §11.9), never by index. An id
  /// that no longer resolves at rebuild time is dropped from this list, not left dangling
  /// (`BuildSurfaceFromSources`) — REQ-069's "deleting a polyline used as a breakline removes it from
  /// the definition."
  std::vector<CadSurfaceBreakline> breaklines;

  /// Contour polylines as data sources (REQ-129) — same id/constraint path as breaklines, listed
  /// separately so the Surface Manager can show a Contours node.
  std::vector<CadSurfaceBreakline> contourSources;

  /// Boundary rings, applied in this exact order (REQ-069: "boundaries apply in definition order").
  /// Same dangling-id handling as \ref breaklineIds.
  std::vector<CadSurfaceBoundary> boundaries;

  /// Named \ref SurfaceStyle this surface is drawn with (REQ-070 / ADR-036 (d)).
  ///
  /// **By name, not by index**, like \ref sourcePointGroups above and for the same reason
  /// (architecture §11.9). Empty, or a name no longer in the table, resolves to "Standard" —
  /// REQ-070's "a surface whose style was deleted falls back to a default style rather than failing
  /// to draw", and also what every surface in every `.gs` written before this field existed reads as.
  std::string styleName;

  /// REQ-136: when both names are set this surface is a TIN volume surface (comparison minus
  /// base). Empty on an ordinary definition-driven TIN. Names, not indices (architecture §11.9).
  std::string volumeBaseName;
  std::string volumeComparisonName;

  [[nodiscard]] bool isVolumeSurface() const {
    return kind == SurfaceKind::TinVolume || kind == SurfaceKind::GridVolume || !volumeBaseName.empty() ||
           !volumeComparisonName.empty();
  }

  /// The built triangulation, or null when the surface has never been built.
  std::shared_ptr<const CadTin> tin;

  /// What the last build did, kept for the UI and the log (REQ-201). Not persisted — a reload
  /// re-reports on the next build.
  std::string lastBuildMessage;

  /// The last rebuild was abandoned because a source could not be read (REQ-086) — the surface is
  /// showing an older triangulation than its definition describes.
  ///
  /// Separate from \c builtAtRevision on purpose. That field is advanced even on this failure, so the
  /// per-frame tick does NOT re-open a missing file sixty times a second; the retry happens on the
  /// next drawing change or an explicit rebuild. Without this flag the surface would then read as
  /// "current", which is exactly the silence REQ-086 is trying to prevent. Not persisted, like
  /// \ref lastBuildMessage.
  bool lastBuildIncomplete = false;

  /// `cadGpuRevision` this surface was last built (or last attempted) against — REQ-069's dynamic
  /// rebuild. `cadGpuRevision` already increments on every drawing mutation (it is what drives the
  /// unsaved-changes indicator), so comparing against it needs no separate per-surface dirty flag and
  /// no new mark-dirty call site at every point/line/polyline mutation: `builtAtRevision !=
  /// cadGpuRevision` means "something changed since this surface was last built," whatever that
  /// something was. Not persisted — a reload always looks current until the next real edit.
  std::uint32_t builtAtRevision = 0xFFFFFFFFu;

  [[nodiscard]] int vertexCount() const { return tin ? tin->vertexCount() : 0; }
  [[nodiscard]] int triangleCount() const { return tin ? tin->triangleCount() : 0; }
};

/// A solid-filled region (ADR-011), imported from a SOLID-fill HATCH. Holds one or more closed boundary
/// loops in the same local coordinate frame as line geometry: loop 0 is the outer boundary, any further
/// loops are holes (islands). Rendered filled with even-odd rule in the GL pass and re-exported as a HATCH.
struct CadFilledRegion {
  /// Flat **x,y,z triplets** for all loops, concatenated (local storage coordinates for X/Y; Z is
  /// absolute — ADR-025 D2). Renamed from `verts` when Z was interleaved (REQ-057 / ADR-025 (a)): the
  /// rename is deliberate, so every site that assumed the old stride-2 layout fails to compile rather
  /// than silently reading a Y as an X. Z is always 0 for paper-space regions — a sheet is 2D (ADR-025 (g)).
  std::vector<float> vertsXyz;
  std::vector<int>   loopStart;  ///< Vertex index where each loop begins; loopStart[0]==0. Loop k spans
                                 ///< [loopStart[k], loopStart[k+1]) (last loop runs to vertsXyz.size()/3).
  /// Hatch pattern (REQ-043, ADR-018). Empty or "SOLID" → a solid fill (the ADR-011 behaviour). Otherwise a
  /// line pattern name ("ANSI31", "ANSI37", "NET") rendered as a clipped line family driven by angle + scale.
  /// Legacy/imported solid fills deserialize with an empty pattern, so they read back as SOLID.
  std::string patternName;
  float patternAngleDeg = 0.f;   ///< Extra rotation added to the pattern's base direction(s).
  float patternScale = 1.f;      ///< Multiplies the line spacing (larger = sparser).
  /// True when this region is a solid fill (no line pattern).
  bool isSolid() const { return patternName.empty() || patternName == "SOLID"; }
  /// Vertex count of loop \p k.
  int loopCount(size_t k) const {
    const int begin = loopStart[k];
    const int end = (k + 1 < loopStart.size()) ? loopStart[k + 1] : static_cast<int>(vertsXyz.size() / 3);
    return end - begin;
  }
};
