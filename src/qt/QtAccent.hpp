#pragma once
// REQ-436 / D-2026-10-21-f: the Qt chrome's primary accent is the topo-line amber (hue ~30 degrees, the colour of
// the amber contours in TopographicArtwork.hpp). Dark and Light share the hue and differ in lightness so text and
// controls stay readable. Secondary emphasis is a calm neutral and warnings keep their own gold; neither is the
// primary accent. Every Qt stylesheet that needs an accent takes it from here (or from an `@token@`, see
// QtApplyAccent) so the two appearances cannot drift apart.
#include <QString>

struct QtAccent {
  const char* strong;     // focus lines, indicators, selected-tab bars, accent text on the surface
  const char* fill;       // filled primary controls (white text)
  const char* fillHover;  // the same control under the pointer (white text)
  const char* tint;       // pressed / checked / selected-row background
  const char* hoverTint;  // hover background (Light only; Dark hover stays graphite)
  const char* edge;       // hover border
  const char* link;       // link / accent text
  const char* linkHover;
  const char* quiet;      // secondary emphasis: calm neutral
  const char* warn;       // warning text, distinct from the primary accent
};

inline const QtAccent& QtAccentFor(bool dark) {
  static const QtAccent kDark{"#f29b44", "#a95a08", "#b5600a", "#4a3213", "#30383d", "#8a6238",
                              "#f5b06a", "#ffd0a0", "#a2b3be", "#f2d04c"};
  static const QtAccent kLight{"#9d5407", "#a95a08", "#8f4c06", "#f5d5ae", "#fbebd7", "#d9a468",
                               "#9d5407", "#7a4204", "#5f6770", "#7a5800"};
  return dark ? kDark : kLight;
}

/// Replaces `@strong@`, `@fill@`, `@fillHover@`, `@tint@`, `@hoverTint@`, `@edge@`, `@link@`, `@linkHover@`, `@quiet@`
/// and `@warn@` in a stylesheet so literal CSS blocks stay readable.
inline QString QtApplyAccent(QString qss, bool dark) {
  const QtAccent& a = QtAccentFor(dark);
  qss.replace("@strong@", a.strong).replace("@fillHover@", a.fillHover).replace("@fill@", a.fill)
      .replace("@tint@", a.tint).replace("@hoverTint@", a.hoverTint).replace("@edge@", a.edge)
      .replace("@linkHover@", a.linkHover).replace("@link@", a.link).replace("@quiet@", a.quiet)
      .replace("@warn@", a.warn);
  return qss;
}
