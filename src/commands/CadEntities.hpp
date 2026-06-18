#pragma once

#include <string>
#include <vector>

// Pure CAD entity value types, dependency-free so both the model-space command layer
// (CadCommands.hpp) and the paper-space data model (PaperSpace.hpp) can reuse them
// without a circular include (ADR-009). Behaviour/free functions live in CadCommands.hpp.

/// Display / CAD metadata stored per entity (parallel index to line segments or circles).
struct EntityAttributes {
  std::string layer = "0";
  std::string color = "ByLayer";
  /// "ByLayer", "ByBlock", or a linetype table name such as "Continuous" / "DASHED" / "CENTER".
  std::string linetype = "ByLayer";
  /// Millimetres on paper; \c -1.f means ByLayer (DXF 370 = -1).
  float lineweightMm = -1.f;
  /// 0 = opaque, 1 = fully transparent; \c -1.f means ByLayer.
  float transparency = -1.f;
};

/// Single-line TEXT, MTEXT box, or aligned linear dimension drawn over the viewport (world coordinates;
/// for paper-space entities the coordinates are paper inches — see ADR-009).
struct CadAnnotation {
  enum class Kind { Text = 0, Mtext = 1, DimAligned = 2, DimLinear = 3, DimAngular = 4 };
  Kind kind = Kind::Text;
  float insX = 0.f;
  float insY = 0.f;
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
  /// \c Kind::DimAligned / \c DimLinear / \c DimAngular — extension or ray points (on measured geometry).
  float dimExt1X = 0.f, dimExt1Y = 0.f, dimExt2X = 0.f, dimExt2Y = 0.f;
  /// \c Kind::DimAngular — vertex (center) of the measured angle.
  float dimAngVertexX = 0.f;
  float dimAngVertexY = 0.f;
  /// Signed distance from chord midpoint to dimension line: aligned uses N=(-T.y,T.x) with T=normalize(e2-e1); linear uses N=(0,1) for horizontal span or N=(1,0) for vertical span; angular uses arc radius (world units).
  float dimSignedOffset = 0.f;
  /// \c Kind::DimLinear only — if true, measures |Y2−Y1| with a vertical dimension line; if false, |X2−X1| with horizontal dim line.
  bool dimLinearVertical = false;
  /// If >= 0, this MTEXT is the viewport label for \c surveyPoints[this index] (bidirectional link).
  int surveyPointLabelFor = -1;
  /// When true, the label was manually dragged and these world-unit offsets from the point override the global defaults.
  bool surveyLabelHasUserOffset = false;
  float surveyLabelUserOffsetEast = 0.f;
  float surveyLabelUserOffsetNorth = 0.f;
};

/// A solid-filled region (ADR-011), imported from a SOLID-fill HATCH. Holds one or more closed boundary
/// loops in the same local coordinate frame as line geometry: loop 0 is the outer boundary, any further
/// loops are holes (islands). Rendered filled with even-odd rule in the GL pass and re-exported as a HATCH.
struct CadFilledRegion {
  std::vector<float> verts;      ///< Flat x,y pairs for all loops, concatenated (local storage coordinates).
  std::vector<int>   loopStart;  ///< Pair-index where each loop begins; loopStart[0]==0. Loop k spans
                                 ///< [loopStart[k], loopStart[k+1]) (last loop runs to verts.size()/2).
  /// Vertex (pair) count of loop \p k.
  int loopCount(size_t k) const {
    const int begin = loopStart[k];
    const int end = (k + 1 < loopStart.size()) ? loopStart[k + 1] : static_cast<int>(verts.size() / 2);
    return end - begin;
  }
};
