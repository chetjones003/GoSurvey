#pragma once
// REQ-436 / D-2026-10-21-f: the Qt chrome's primary accent is the topo-line amber (hue ~30 degrees, the colour of
// the amber contours in TopographicArtwork.hpp). Dark and Light share the hue and differ in lightness so text and
// controls stay readable. Secondary emphasis is a calm neutral and warnings keep their own gold; neither is the
// primary accent. Every Qt stylesheet that needs an accent takes it from here (or from an `@token@`, see
// QtApplyAccent) so the two appearances cannot drift apart.
#include <QCoreApplication>
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
                               "#9d5407", "#7a4204", "#4f5963", "#7a5800"};
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

inline QString QtInputSubcontrolStyle(bool dark) {
  const QString icons = QCoreApplication::applicationDirPath() + "/resources/icons/";
  const QString divider = dark ? "#424b51" : "#aeb4bc";
  const QString appearance = dark ? "dark" : "light";
  return QString(R"(
    QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin:border; subcontrol-position:top right; width:24px; height:16px; border-left:1px solid %1; border-bottom:1px solid %1; background:transparent; }
    QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin:border; subcontrol-position:bottom right; width:24px; height:16px; border-left:1px solid %1; background:transparent; }
    QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { width:12px; height:12px; image:url(%2spin_up_%3.svg); }
    QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { width:12px; height:12px; image:url(%2spin_down_%3.svg); }
    QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:top right; width:22px; border-left:1px solid %1; background:transparent; }
    QComboBox::down-arrow { width:10px; height:10px; image:url(%2spin_down_%3.svg); }
  )").arg(divider, icons, appearance);
}

inline QString QtCheckBoxIndicatorStyle(bool dark) {
  const QString checkmark = QCoreApplication::applicationDirPath() + "/resources/icons/checkmark_white.svg";
  const QString edge = dark ? "#71808a" : "#87939f";
  const QString surface = dark ? "#15191c" : "#ffffff";
  return QString("QCheckBox::indicator { width:16px; height:16px; border:1px solid %1; border-radius:3px; background:%2; }\n"
                 "QCheckBox::indicator:checked { background:%3; border-color:%4; image:url(%5); }")
      .arg(edge, surface, QtAccentFor(dark).fill, QtAccentFor(dark).strong, checkmark);
}

inline QString QtTabCloseButtonStyle(bool dark) {
  const QString icons = QCoreApplication::applicationDirPath() + "/resources/icons/";
  return QString("#drawingTabs::close-button { image:url(%1tab_close_%2.svg); width:14px; height:14px; }\n"
                 "#drawingTabs::close-button:hover { image:url(%1tab_close_hover.svg); }")
      .arg(icons, dark ? "dark" : "light");
}

inline QString QtPropertiesSectionHeadingStyle(bool dark) {
  return QtApplyAccent(dark
      ? "background:#30373c; color:#f4f7f8; border-left:3px solid @strong@; padding:3px 8px;"
      : "background:#fbebd7; color:#30353b; border-left:3px solid @strong@; padding:3px 8px;", dark);
}
