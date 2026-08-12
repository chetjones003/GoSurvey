#pragma once

#include <string>
#include <utility>
#include <vector>

// Pure DXF record composition — no AppCommandState, no GUI stack, so the test target can link it
// (the `PlotFont.hpp` / `HatchGeom.hpp` / `OrthoConstrain.hpp` precedent).
//
// This exists because a DXF record's *group-code order and subclass structure* is where
// conformance bugs live, and that part is pure data. A TEXT record whose group 73 sits outside the
// second AcDbText subclass makes AutoCAD discard the ENTIRE drawing — GoSurvey shipped exactly
// that defect, undetected, because the writer could not be tested (REQ-052 / TASK-030 §5).
//
// Numeric fields are passed PRE-FORMATTED as strings. That is deliberate: the exporter formats
// with std::to_string under its own stream settings, so taking strings keeps this a faithful
// composition step that cannot change a single output byte.

/// One DXF group-code / value pair, in emission order.
struct DxfOutPair {
  int code = 0;
  std::string value;
};

/// A single-line TEXT entity (AcDbEntity + AcDbText).
struct DxfTextRecord {
  std::string handleHex;      ///< group 5
  std::string ownerHandleHex; ///< group 330
  std::string layer;          ///< group 8
  std::string linetype;       ///< group 6
  std::string colorAci;       ///< group 62
  std::string lineweight370;  ///< group 370
  /// Group 440 is written only when the entity actually carries transparency.
  bool hasTransparency = false;
  std::string transparency440;
  std::string x, y, z;                 ///< groups 10 / 20 / 30 (insertion, baseline)
  std::string height;                  ///< group 40
  std::string text;                    ///< group 1
  std::string rotationDeg;             ///< group 50 (DEGREES, not radians)
  std::string style = "Standard";      ///< group 7 — an AcDbText property, not AcDbEntity
  std::string genFlags71 = "0";        ///< group 71
  std::string hJust72 = "0";           ///< group 72
  std::string vJust73 = "0";           ///< group 73 — belongs to the SECOND AcDbText subclass
  std::string extrusionX = "0.0";      ///< group 210
  std::string extrusionY = "0.0";      ///< group 220
  std::string extrusionZ = "1.0";      ///< group 230
};

/// Appends a conformant TEXT record to \p out.
///
/// The subclass layout follows the ObjectARX DXF reference exactly:
///   0 TEXT · 5 · 330 · 100 AcDbEntity · 8 · 6 · 62 · 370 · [440]
///          · 100 AcDbText · 10 20 30 · 40 · 1 · 50 · 7 · 71 · 72 · 210 220 230
///          · 100 AcDbText · 73
/// AcDbText is declared **twice**; group 73 is a member of the second declaration. Emitting 73
/// under a single marker is what AutoCAD rejects with "Unexpected DXF group code: 73".
inline void DxfAppendTextRecord(const DxfTextRecord& r, std::vector<DxfOutPair>* out) {
  if (!out)
    return;
  auto add = [out](int code, const std::string& value) { out->push_back(DxfOutPair{code, value}); };

  add(0, "TEXT");
  add(5, r.handleHex);
  add(330, r.ownerHandleHex);

  add(100, "AcDbEntity");
  add(8, r.layer);
  add(6, r.linetype);
  add(62, r.colorAci);
  add(370, r.lineweight370);
  if (r.hasTransparency)
    add(440, r.transparency440);

  add(100, "AcDbText");
  add(10, r.x);
  add(20, r.y);
  add(30, r.z);
  add(40, r.height);
  add(1, r.text);
  add(50, r.rotationDeg);
  add(7, r.style);
  add(71, r.genFlags71);
  add(72, r.hJust72);
  add(210, r.extrusionX);
  add(220, r.extrusionY);
  add(230, r.extrusionZ);

  // Second subclass marker — see the note above; group 73 is invalid without it.
  add(100, "AcDbText");
  add(73, r.vJust73);
}

/// Packs a 0..1 transparency into a DXF group-440 value, AutoCAD's 0x02000000 | alpha encoding.
/// Returns false when the entity is opaque, in which case group 440 must be omitted entirely
/// (an explicit "fully opaque" 440 is not the same as ByLayer).
inline bool DxfTransparency440(float transparency01, int* packedOut) {
  if (transparency01 <= 1.e-5f)
    return false;
  float t = transparency01;
  if (t < 0.f) t = 0.f;
  if (t > 1.f) t = 1.f;
  const unsigned char b = static_cast<unsigned char>(static_cast<int>(t * 255.f + 0.5f));
  if (packedOut)
    *packedOut = static_cast<int>(0x02000000u | static_cast<unsigned int>(b));
  return true;
}

/// A lightweight polyline (AcDbEntity + AcDbPolyline) — what AutoCAD's RECTANG produces and what
/// GoSurvey stores every polyline, and every RECT, as (REQ-052 / REQ-053).
struct DxfLwPolylineRecord {
  std::string handleHex;      ///< group 5
  std::string ownerHandleHex; ///< group 330
  std::string layer;          ///< group 8
  std::string linetype;       ///< group 6
  std::string colorAci;       ///< group 62
  std::string lineweight370;  ///< group 370
  /// Group 440 is written only when the entity actually carries transparency.
  bool hasTransparency = false;
  std::string transparency440;
  /// Vertices in order, each an already-formatted (x, y) pair. Groups 10 / 20, repeated per vertex.
  std::vector<std::pair<std::string, std::string>> vertices;
  /// Group 70 bit 1: the polyline closes back to its first vertex. A rectangle is always closed.
  bool closed = false;
  std::string elevation38 = "0.0";  ///< group 38
  std::string extrusionX = "0.0";   ///< group 210
  std::string extrusionY = "0.0";   ///< group 220
  std::string extrusionZ = "1.0";   ///< group 230
};

/// Appends a conformant LWPOLYLINE record to \p out.
///
/// Group-code order per the ObjectARX DXF reference:
///   0 LWPOLYLINE · 5 · 330 · 100 AcDbEntity · 8 · 6 · 62 · 370 · [440]
///                · 100 AcDbPolyline · 90 (vertex count) · 70 (flags) · 38
///                · {10 · 20} per vertex · 210 220 230
///
/// Group 90 must carry the true vertex count and must precede 70 — AutoCAD reads 90 to size the vertex
/// array before it consumes the 10/20 pairs. A record with no vertices is skipped rather than written,
/// because a 90-of-zero LWPOLYLINE is what makes AutoCAD reject the enclosing section.
inline void DxfAppendLwPolylineRecord(const DxfLwPolylineRecord& r, std::vector<DxfOutPair>* out) {
  if (!out || r.vertices.empty())
    return;
  auto add = [out](int code, const std::string& value) { out->push_back(DxfOutPair{code, value}); };

  add(0, "LWPOLYLINE");
  add(5, r.handleHex);
  add(330, r.ownerHandleHex);
  add(100, "AcDbEntity");
  add(8, r.layer);
  add(6, r.linetype);
  add(62, r.colorAci);
  add(370, r.lineweight370);
  if (r.hasTransparency)
    add(440, r.transparency440);

  add(100, "AcDbPolyline");
  add(90, std::to_string(r.vertices.size()));
  add(70, r.closed ? "1" : "0");
  add(38, r.elevation38);
  for (const auto& v : r.vertices) {
    add(10, v.first);
    add(20, v.second);
  }
  add(210, r.extrusionX);
  add(220, r.extrusionY);
  add(230, r.extrusionZ);
}
