#pragma once
// Shared widgets for the Qt surface dialogs (issue #893): colour / linetype / lineweight pickers, the
// banner, and the dialog look. Internal to src/qt — the dialogs themselves live in EditorSurfaces.cpp,
// EditorSurfaceManager.cpp and EditorSurfaceStyles.cpp.

#include "CadUiStyleWidgets.hpp"
#include "QtAccent.hpp"

#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPalette>
#include <QString>
#include <QVBoxLayout>

#include <string>

namespace surfaceui {

inline QString Q(const std::string& s) { return QString::fromUtf8(s.c_str()); }
inline std::string S(const QString& s) { return s.toUtf8().toStdString(); }

/// The colour a stored value resolves to for a swatch. ByLayer has no colour of its own, so it is grey.
inline QColor StorageColor(const std::string& storage) {
  for (const auto& preset : kNamedColors)
    if (storage == preset.storage && storage != "ByLayer")
      return QColor::fromRgbF(preset.r, preset.g, preset.b);
  if (storage.size() == 7 && storage[0] == '#') {
    const QColor parsed(Q(storage));
    if (parsed.isValid()) return parsed;
  }
  return QColor(150, 150, 150);
}

inline QIcon Swatch(const std::string& storage) {
  QPixmap pixmap(16, 12);
  pixmap.fill(StorageColor(storage));
  return QIcon(pixmap);
}

/// Fills \p combo with the named colour presets (item data = stored value) and selects \p current. A
/// stored `#RRGGBB` that is not a preset gets its own item, so opening the dialog never loses it.
inline void FillColorCombo(QComboBox* combo, const std::string& current) {
  combo->clear();
  int selected = -1;
  for (const auto& preset : kNamedColors) {
    combo->addItem(Swatch(preset.storage), preset.label, Q(preset.storage));
    if (current == preset.storage) selected = combo->count() - 1;
  }
  if (selected < 0) {
    combo->addItem(Swatch(current), Q(current), Q(current));
    selected = combo->count() - 1;
  }
  combo->setCurrentIndex(selected);
}

inline void FillLinetypeCombo(QComboBox* combo, const std::string& current) {
  combo->clear();
  int selected = 0;
  for (int i = 0; i < kEntityLinetypeCount; ++i) {
    combo->addItem(kEntityLinetypeLabels[i], Q(kEntityLinetypeStorage[i]));
    if (current == kEntityLinetypeStorage[i]) selected = i;
  }
  combo->setCurrentIndex(selected);
}

inline void FillLineweightCombo(QComboBox* combo, float currentMm) {
  combo->clear();
  for (int i = 0; i < kUiLineweightPresetCount; ++i) {
    char label[64];
    SnprintLineweightPresetLabel(label, sizeof(label), kUiLineweightMmPresets[i], false);
    combo->addItem(label, static_cast<double>(kUiLineweightMmPresets[i]));
  }
  combo->setCurrentIndex(LineweightPresetIndexFromMm(currentMm));
}

/// The dialog title strip: a title and one line saying what the dialog edits.
inline QLabel* AddBanner(QVBoxLayout* root, QWidget* parent, const QString& title, const QString& subtitle) {
  auto* banner = new QLabel(QString("<div style='font-size:15pt;font-weight:600'>%1</div><div>%2</div>")
                                .arg(title.toHtmlEscaped(), subtitle.toHtmlEscaped()), parent);
  banner->setObjectName("surfaceBanner");
  banner->setTextFormat(Qt::RichText);
  banner->setWordWrap(true);
  root->addWidget(banner);
  return banner;
}

/// The look of every surface dialog: an accent bar beside the title, flat underlined tabs, ruled group
/// headings and a primary button, all in the topo-line amber (QtAccent.hpp, the same accent as the other Qt dialogs).
inline void StyleDialog(QDialog* dialog) {
  const bool dark = dialog->palette().color(QPalette::Window).lightness() < 128;
  const char* accent = QtAccentFor(dark).strong;
  const char* accentFill = QtAccentFor(dark).fill;
  const char* hover = QtAccentFor(dark).fillHover;
  dialog->setStyleSheet(QString(
      "QLabel#surfaceBanner { border-left:5px solid %1; background:palette(alternate-base); padding:10px 14px; }"
      "QTabWidget::pane { border-top:2px solid %1; }"
      "QTabBar::tab { padding:7px 14px; border:0; border-bottom:2px solid transparent; }"
      "QTabBar::tab:selected { border-bottom:2px solid %1; font-weight:600; }"
      "QGroupBox { border:0; border-top:1px solid palette(mid); margin-top:12px; padding-top:10px; }"
      "QGroupBox::title { subcontrol-origin:margin; left:0; font-weight:600; color:%1; }"
      "QPushButton[primary=\"true\"] { background:%2; color:white; font-weight:600; padding:6px 18px; "
      "border:0; border-radius:4px; } QPushButton[primary=\"true\"]:hover { background:%3; } "
      "QPushButton[primary=\"true\"]:disabled { background:palette(mid); }")
                          .arg(accent, accentFill, hover));
}

}  // namespace surfaceui
