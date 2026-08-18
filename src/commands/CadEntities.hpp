#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
  std::string fontFamily;      ///< "" = app default; a TrueType family ("Arial") or an SHX name ("romans.shx").
  float heightInches = 0.125f; ///< Plotted text height in inches (model height = this × drawing scale).
  float obliqueDeg = 0.f;      ///< Slant angle in degrees (>0 shears glyph tops to the right).
  bool bold = false;
  bool italic = false;
};

/// Single-line TEXT, MTEXT box, or aligned linear dimension drawn over the viewport (world coordinates;
/// for paper-space entities the coordinates are paper inches — see ADR-009).
struct CadAnnotation {
  enum class Kind { Text = 0, Mtext = 1, DimAligned = 2, DimLinear = 3, DimAngular = 4 };
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
};

/// Committed 3-point arc (circumcircle + start/sweep in radians from +X).
/// Dependency-free so both the model store (CadCommands.hpp) and the paper-space store (PaperSpace.hpp,
/// ADR-013) can hold arcs without a circular include.
struct CadArc {
  float cx = 0.f;
  float cy = 0.f;
  float r = 0.f;
  float startRad = 0.f;
  float sweepRad = 0.f;
  /// Elevation of the arc's plane (REQ-057 / ADR-025). The arc stays parallel to XY — a
  /// tilted arc would need a plane normal, which no accepted requirement asks for. Absolute
  /// (ADR-025 D2). Always 0 in paper space (ADR-025 (g)).
  float z = 0.f;
};

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
enum class CadBoundaryKind : std::uint8_t { Outer, Hide, Show };

/// A boundary ring referenced by stable entity id (REQ-076) — must resolve to a **closed** polyline.
struct CadSurfaceBoundary {
  std::uint64_t entityId = 0;
  CadBoundaryKind kind = CadBoundaryKind::Outer;
};

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

  /// Names of the point groups supplying the surface's points (REQ-067).
  ///
  /// **By name, not by index** — a name is a stable identifier, so architecture §11.9 is satisfied,
  /// and it mirrors how a CadAnnotation references a text style (ADR-020). A name that no longer
  /// resolves is reported at build time rather than quietly producing an empty surface.
  std::vector<std::string> sourcePointGroups;

  /// Breaklines forcing a triangulation edge along them (REQ-069) — each a Line or a Polyline in the
  /// drawing, referenced by stable entity id (REQ-076, architecture §11.9), never by index. An id
  /// that no longer resolves at rebuild time is dropped from this list, not left dangling
  /// (`BuildSurfaceFromSources`) — REQ-069's "deleting a polyline used as a breakline removes it from
  /// the definition."
  std::vector<std::uint64_t> breaklineIds;

  /// Boundary rings, applied in this exact order (REQ-069: "boundaries apply in definition order").
  /// Same dangling-id handling as \ref breaklineIds.
  std::vector<CadSurfaceBoundary> boundaries;

  /// The built triangulation, or null when the surface has never been built.
  std::shared_ptr<const CadTin> tin;

  /// What the last build did, kept for the UI and the log (REQ-201). Not persisted — a reload
  /// re-reports on the next build.
  std::string lastBuildMessage;

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
