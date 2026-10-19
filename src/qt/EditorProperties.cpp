// The Properties panel (issue #891; REQ-024 / REQ-057 / REQ-101).
//
// What each field means, what is refused and what one undo step covers live in the shared
// `propedit` services (and CadApplyLayer/Color/Linetype/Lineweight/TransparencyToSelection), which
// the ImGui panel also uses. This file is only the Qt widgets around them.
#include "EditorWindow.hpp"
#include "TopographicArtwork.hpp"
#include <QApplication>
#include <QPainter>

#include "CadColor.hpp"
#include "CadLinetype.hpp"
#include "CadPropertyEdit.hpp"
#include "CadUiStyleWidgets.hpp"
#include "util/AngleFormat.hpp"
#include "util/NumFormat.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QFocusEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

class TerrainPropertiesBody final : public QWidget {
 protected:
  void paintEvent(QPaintEvent* event) override {
    QWidget::paintEvent(event);
    if (qApp->property("cadLightTheme").toBool()) return;
    QPainter painter(this);
    const qreal artHeight = width() * 220.0 / 300.0;
    qtArtwork::DrawTerrainContours(painter, QRectF(0, height() - artHeight, width(), artHeight));
  }
};

QString Text(const std::string& text) { return QString::fromUtf8(text.c_str()); }

/// Commits when it loses focus; Ctrl+Enter also commits. No Q_OBJECT: a callback, not a signal.
class ContentEdit final : public QPlainTextEdit {
public:
  using QPlainTextEdit::QPlainTextEdit;
  std::function<void()> committed;
protected:
  void focusOutEvent(QFocusEvent* event) override {
    QPlainTextEdit::focusOutEvent(event);
    if (committed) committed();
  }
  void keyPressEvent(QKeyEvent* event) override {
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        (event->modifiers() & Qt::ControlModifier)) {
      if (committed) committed();
      return;
    }
    QPlainTextEdit::keyPressEvent(event);
  }
};

constexpr const char* kVaries = "*varies*";

QString Merge(const QStringList& values) {
  if (values.isEmpty()) return QString::fromUtf8("\xE2\x80\x94");
  for (const auto& value : values) if (value != values.front()) return kVaries;
  return values.front();
}

}  // namespace

void EditorWindow::BuildPropertiesPanel() {
  m_PropertiesScroll = new QScrollArea(this);
  m_PropertiesScroll->setObjectName("propertiesPanel");
  m_PropertiesScroll->setWidgetResizable(true);
  m_PropertiesScroll->setFrameShape(QFrame::NoFrame);
  m_PropertiesScroll->setMinimumWidth(250);
}

void EditorWindow::RefreshProperties() {
  if (!m_PropertiesScroll) return;
  const auto& st = m_State;
  // Rebuild only when something the panel shows has changed, so a field being typed into is never
  // pulled out from under the user by an unrelated refresh.
  std::uint64_t fp = 1469598103934665603ull;
  auto mix = [&fp](std::uint64_t v) { fp = (fp ^ v) * 1099511628211ull; };
  for (const auto& e : st.selection) { mix(static_cast<std::uint64_t>(e.type)); mix(static_cast<std::uint64_t>(e.index)); }
  for (int i : st.selectedSurveyPointIndices) mix(static_cast<std::uint64_t>(i) + 977);
  mix(st.cadGpuRevision); mix(static_cast<std::uint64_t>(st.displayLinearPrecision));
  mix(static_cast<std::uint64_t>(std::llround(st.worldDocumentOriginX * 1000))); mix(static_cast<std::uint64_t>(std::llround(st.worldDocumentOriginY * 1000)));
  mix(st.drawingLayerTable.size()); mix(static_cast<std::uint64_t>(st.activeDrawingIdx));
  mix(qHash(m_PropertiesNote));
  if (fp == m_PropertiesFingerprint && m_PropertiesScroll->widget()) return;
  m_PropertiesFingerprint = fp;

  auto* body = new TerrainPropertiesBody;
  body->setAutoFillBackground(false);
  body->setObjectName("propertiesBody");
  auto* root = new QVBoxLayout(body);
  root->setContentsMargins(6, 6, 6, 6);
  root->setSpacing(4);
  const int precision = st.displayLinearPrecision;
  auto fmt = [precision](double v) { return QString::fromStdString(FormatLinear(v, precision)); };

  // --- selection summary ------------------------------------------------------------------------
  int nLine = 0, nCirc = 0, nText = 0, nOther = 0, nSurface = 0;
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) ++nLine;
    else if (e.type == SelectedEntity::Type::Circle) ++nCirc;
    else if (e.type == SelectedEntity::Type::Annotation) ++nText;
    else { ++nOther; if (e.type == SelectedEntity::Type::Surface) ++nSurface; }
  }
  const int total = static_cast<int>(st.selection.size());
  QString summary;
  if (total == 0) summary = "No selection";
  else {
    const int kinds = (nLine > 0) + (nCirc > 0) + (nText > 0) + (nOther > 0);
    QString what;
    if (kinds > 1) {
      QStringList parts;
      if (nLine) parts << QString("Line %1").arg(nLine);
      if (nCirc) parts << QString("Circle %1").arg(nCirc);
      if (nText) parts << QString("Text %1").arg(nText);
      if (nOther) parts << QString("Other %1").arg(nOther);
      what = "Mixed: " + parts.join(", ");
    }
    else if (nLine) what = nLine == 1 ? "Line" : QString("%1 lines").arg(nLine);
    else if (nCirc) what = nCirc == 1 ? "Circle" : QString("%1 circles").arg(nCirc);
    else if (nText) what = nText == 1 ? "Text" : QString("%1 annotations").arg(nText);
    else what = nSurface == nOther ? (nSurface == 1 ? QString("TIN Surface") : QString("%1 surfaces").arg(nSurface)) : QString("Other objects");
    summary = QString("Selected: %1 object%2 — %3").arg(total).arg(total == 1 ? "" : "s").arg(what);
  }
  auto* header = new QLabel(summary, body);
  header->setObjectName("propertiesSummary");
  header->setWordWrap(true);
  QFont headerFont = header->font(); headerFont.setBold(true); header->setFont(headerFont);
  root->addWidget(header);
  auto* note = new QLabel(m_PropertiesNote, body);
  note->setObjectName("propertiesNote");
  note->setWordWrap(true);
  note->setStyleSheet("color:#c54646;");
  note->setVisible(!m_PropertiesNote.isEmpty());
  root->addWidget(note);

  // --- section / row builders -------------------------------------------------------------------
  auto section = [&](const QString& title, const char* name) {
    auto* box = new QFrame(body);
    box->setObjectName(name);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    auto* heading = new QLabel(title, box);
    heading->setObjectName("propertiesSectionHeading");
    QFont f = heading->font(); f.setBold(true); heading->setFont(f);
    heading->setStyleSheet(qApp->property("cadLightTheme").toBool()
        ? "background:#1f3a5f; color:#ffffff; padding:3px 8px;"
        : "background:#30373c; color:#f4f7f8; border-left:3px solid #55d8d0; padding:3px 8px;");
    layout->addWidget(heading);
    auto* rows = new QWidget(box);
    auto* grid = new QVBoxLayout(rows);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(3);
    layout->addWidget(rows);
    root->addWidget(box);
    return grid;
  };
  auto addRow = [&](QVBoxLayout* grid, const QString& label, QWidget* field, char axis = 0) {
    auto* row = new QWidget(grid->parentWidget());
    auto* outer = new QHBoxLayout(row);
    outer->setContentsMargins(6, 0, 6, 0);
    outer->setSpacing(6);
    auto* caption = new QLabel(label, row);
    caption->setObjectName("propertiesRowLabel");
    caption->setFixedWidth(124);
    outer->addWidget(caption);
    auto* fieldRow = new QHBoxLayout();
    fieldRow->setContentsMargins(0, 0, 0, 0); fieldRow->setSpacing(4);
    if (axis) {  // X/Y/Z badge on coordinate fields, as in the 0.7.4 panel (REQ-081)
      auto* badge = new QLabel(QString(QChar(axis)), row);
      badge->setAlignment(Qt::AlignCenter); badge->setFixedWidth(18);
      const char* color = axis == 'X' ? "#c0392b" : axis == 'Y' ? "#2e8b57" : "#2f6fc0";
      badge->setStyleSheet(QString("background:%1; color:white; border-radius:2px; font-weight:bold;").arg(color));
      fieldRow->addWidget(badge);
    }
    fieldRow->addWidget(field, 1);
    outer->addLayout(fieldRow, 1);
    grid->addWidget(row);
    return row;
  };
  auto readOnly = [&](QVBoxLayout* grid, const QString& label, const QString& value, const char* name) {
    auto* text = new QLabel(value, body);
    text->setObjectName(name);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addRow(grid, label, text);
  };

  // Apply an outcome: say what was refused or corrected, then redraw from the drawing.
  auto finish = [this](const propedit::Outcome& outcome) {
    m_PropertiesNote = QString::fromStdString(outcome.message);
    if (!outcome.message.empty()) m_Log.push_back("PROPERTIES — " + outcome.message);
    m_PropertiesFingerprint = ~0ull;
    Refresh();
  };
  // A numeric field: commits on Enter/focus-out when its text really changed; an unparsable entry
  // is refused with a message and the field goes back to the drawing's value.
  auto numberField = [&](const char* name, const QString& shown, std::function<propedit::Outcome(double)> apply,
                         bool enabled = true) {
    auto* edit = new QLineEdit(shown, body);
    edit->setObjectName(name);
    edit->setEnabled(enabled);
    edit->setAlignment(Qt::AlignRight);
    connect(edit, &QLineEdit::editingFinished, edit, [edit, shown, apply, finish] {
      const QString typed = edit->text().trimmed();
      if (typed == shown) return;
      bool ok = false;
      const double value = typed.toDouble(&ok);
      if (!ok) { finish({propedit::Result::Rejected, "Enter a number."}); return; }
      finish(apply(value));
    });
    return edit;
  };

  // --- General ----------------------------------------------------------------------------------
  const bool none = total == 0;
  const auto general = propedit::ReadGeneralAttrs(st);
  auto* generalRows = section("General", "propertiesGeneral");
  auto pickCombo = [&](const char* name, const QStringList& items, int current, bool mixed, std::function<void(int)> chosen) {
    auto* combo = new QComboBox(body);
    combo->setObjectName(name);
    if (mixed) combo->addItem("(varies)");
    combo->addItems(items);
    combo->setCurrentIndex(mixed ? 0 : std::max(0, current));
    combo->setEnabled(!none && general.count > 0);
    if (none) { combo->clear(); combo->addItem(QString::fromUtf8("\xE2\x80\x94")); }
    const int offset = mixed ? 1 : 0;
    connect(combo, &QComboBox::activated, combo, [chosen, offset](int index) { if (index >= offset) chosen(index - offset); });
    return combo;
  };
  {
    QStringList layers;
    for (const auto& layer : st.drawingLayerTable) layers.append(Text(layer.name));
    auto* layer = new QComboBox(body);
    layer->setObjectName("propLayer");
    layer->setEditable(true);
    if (general.layerMixed) layer->addItem("(varies)");
    layer->addItems(layers);
    layer->setCurrentText(none ? QString::fromUtf8("\xE2\x80\x94") : general.layerMixed ? "(varies)" : Text(general.layer));
    layer->setEnabled(!none && general.count > 0);
    connect(layer, &QComboBox::textActivated, layer, [this, finish](const QString& value) {
      const QString name = value.trimmed();
      if (name.isEmpty() || name == "(varies)") return;
      const int n = CadApplyLayerToSelection(m_State, name.toUtf8().toStdString());
      if (n > 0) m_Log.push_back("LAYER — " + std::to_string(n) + " object(s) moved to layer \"" + name.toStdString() + "\".");
      finish({});
    });
    addRow(generalRows, "Layer", layer);

    QStringList colors; int colorIndex = -1;
    for (int i = 0; i < kNamedColorCount; ++i) {
      colors.append(kNamedColors[i].storage);
      if (!general.colorMixed && CadColorStorageMatches(general.color, kNamedColors[i].storage)) colorIndex = i;
    }
    if (!general.colorMixed && colorIndex < 0 && !general.color.empty()) { colors.append(Text(general.color)); colorIndex = colors.size() - 1; }
    colors.append("More colors...");
    auto* color = pickCombo("propColor", colors, colorIndex, general.colorMixed, [this, finish, colors](int index) {
      std::string storage = colors.at(index).toStdString();
      if (colors.at(index) == "More colors...") {
        const QColor picked = QColorDialog::getColor(Qt::white, this, "Object color");
        if (!picked.isValid()) { finish({}); return; }
        storage = picked.name().toStdString();
      }
      if (CadApplyColorToSelection(m_State, storage) > 0) m_Log.push_back("COLOR — set to " + storage + ".");
      finish({});
    });
    for (int i = 0; i < color->count(); ++i) {
      const int named = i - (general.colorMixed ? 1 : 0);
      if (named >= 0 && named < kNamedColorCount && !none) {
        QPixmap swatch(14, 14);
        swatch.fill(i - (general.colorMixed ? 1 : 0) == 0 ? QColor(Qt::gray) : QColor::fromRgbF(kNamedColors[named].r, kNamedColors[named].g, kNamedColors[named].b));
        color->setItemIcon(i, QIcon(swatch));
      }
    }
    addRow(generalRows, "Color", color);

    QStringList linetypes; int linetypeIndex = -1;
    for (int i = 0; i < kEntityLinetypeCount; ++i) {
      linetypes.append(kEntityLinetypeLabels[i]);
      if (!general.linetypeMixed && CadCanonicalLinetypeNameForDxf(general.linetype) == CadCanonicalLinetypeNameForDxf(kEntityLinetypeStorage[i])) linetypeIndex = i;
    }
    if (!general.linetypeMixed && linetypeIndex < 0 && !general.linetype.empty()) { linetypes.append(Text(general.linetype)); linetypeIndex = linetypes.size() - 1; }
    addRow(generalRows, "Linetype", pickCombo("propLinetype", linetypes, linetypeIndex, general.linetypeMixed, [this, finish](int index) {
      if (index < kEntityLinetypeCount) (void)CadApplyLinetypeToSelection(m_State, kEntityLinetypeStorage[index]);
      finish({});
    }));

    QStringList weights;
    for (int i = 0; i < kUiLineweightPresetCount; ++i) {
      char label[96]; SnprintLineweightPresetLabel(label, sizeof(label), kUiLineweightMmPresets[i], false); weights.append(label);
    }
    addRow(generalRows, "Lineweight", pickCombo("propLineweight", weights, LineweightPresetIndexFromMm(general.lineweightMm), general.lineweightMixed, [this, finish](int index) {
      (void)CadApplyLineweightToSelection(m_State, kUiLineweightMmPresets[index]);
      finish({});
    }));

    static const char* const kTransparencyLabels[] = {"By Layer", "0 %", "25 %", "50 %", "75 %", "90 %", "100 %"};
    QStringList transparencies;
    for (int i = 0; i < kUiTransparencyPresetCount; ++i) transparencies.append(kTransparencyLabels[i]);
    int transparencyIndex = 0;
    if (general.transparency >= 0.f) {
      float best = 1e9f;
      for (int i = 1; i < kUiTransparencyPresetCount; ++i) {
        const float d = std::fabs(general.transparency - kUiTransparencyPresets[i]);
        if (d < best) { best = d; transparencyIndex = i; }
      }
    }
    addRow(generalRows, "Transparency", pickCombo("propTransparency", transparencies, transparencyIndex, general.transparencyMixed, [this, finish](int index) {
      (void)CadApplyTransparencyToSelection(m_State, kUiTransparencyPresets[index]);
      finish({});
    }));
  }

  // --- Geometry ---------------------------------------------------------------------------------
  auto* geometryRows = section("Geometry", "propertiesGeometry");
  const double ox = st.worldDocumentOriginX, oy = st.worldDocumentOriginY;
  const auto angle = CadAngleDisplaySettings(st);
  auto bearing = [&angle](double dx, double dy) {  // clockwise from north, [0, 360)
    double degrees = std::atan2(dx, dy) * (180.0 / 3.14159265358979323846);
    if (degrees < 0.0) degrees += 360.0;
    return QString::fromStdString(FormatBearing(degrees, angle));
  };
  std::vector<SelectedEntity> lines, circles, texts;
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) lines.push_back(e);
    else if (e.type == SelectedEntity::Type::Circle) circles.push_back(e);
    else if (e.type == SelectedEntity::Type::Annotation) texts.push_back(e);
  }
  const bool pure = nOther == 0 && ((lines.size() == st.selection.size()) || (circles.size() == st.selection.size()) || (texts.size() == st.selection.size()));
  const size_t lineCount = st.userLinesFlat.size() / 6, circleCount = st.userCirclesCxCyZR.size() / 4;
  if (none) {
    for (const char* label : {"Start X", "Start Y", "End X", "End Y"}) {
      auto* dash = new QLabel(QString::fromUtf8("\xE2\x80\x94"), body);
      dash->setEnabled(false);
      addRow(geometryRows, label, dash);
    }
  } else if (!pure) {
    auto* mixed = new QLabel("Mixed object types — geometry is read-only here. Edit General above, or select only lines, circles, or text.", body);
    mixed->setObjectName("propertiesMixedNote");
    mixed->setWordWrap(true);
    geometryRows->addWidget(mixed);
  } else if (lines.size() == 1 && static_cast<size_t>(lines[0].index) < lineCount) {
    const int i = lines[0].index;
    const double* v = &st.userLinesFlat[static_cast<size_t>(i) * 6];
    using F = propedit::LineField;
    struct Item { const char* label; const char* name; F field; double shown; char axis; };
    const Item items[] = {
        {"Start X", "propLineStartX", F::StartX, v[0] + ox, 'X'}, {"Start Y", "propLineStartY", F::StartY, v[1] + oy, 'Y'},
        {"Start Z", "propLineStartZ", F::StartZ, v[2], 'Z'},     {"End X", "propLineEndX", F::EndX, v[3] + ox, 'X'},
        {"End Y", "propLineEndY", F::EndY, v[4] + oy, 'Y'},       {"End Z", "propLineEndZ", F::EndZ, v[5], 'Z'}};
    for (const auto& item : items) {
      addRow(geometryRows, item.label, numberField(item.name, fmt(item.shown), [this, i, f = item.field](double value) {
        return propedit::SetLineField(m_State, i, f, value);
      }), item.axis);
    }
    const double dx = v[3] - v[0], dy = v[4] - v[1], dz = v[5] - v[2];
    const double horizontal = std::hypot(dx, dy);
    auto* derived = section("Derived", "propertiesDerived");
    readOnly(derived, "Length", fmt(horizontal), "propDerivedLength");
    if (dz != 0.0) {
      readOnly(derived, "Length (slope)", fmt(std::sqrt(dx * dx + dy * dy + dz * dz)), "propDerivedSlope");
      readOnly(derived, "Rise", fmt(dz), "propDerivedRise");
      if (horizontal > 1e-6) readOnly(derived, "Grade", QString::asprintf("%.2f%%", dz / horizontal * 100.0), "propDerivedGrade");
    }
    readOnly(derived, "Rotation rel. north", bearing(dx, dy), "propDerivedBearing");
  } else if (circles.size() == 1 && static_cast<size_t>(circles[0].index) < circleCount) {
    const int i = circles[0].index;
    const double* v = &st.userCirclesCxCyZR[static_cast<size_t>(i) * 4];
    using F = propedit::CircleField;
    struct Item { const char* label; const char* name; F field; double shown; char axis; };
    const Item items[] = {
        {"Center X", "propCircleCenterX", F::CenterX, v[0] + ox, 'X'}, {"Center Y", "propCircleCenterY", F::CenterY, v[1] + oy, 'Y'},
        {"Center Z", "propCircleCenterZ", F::CenterZ, v[2], 'Z'},     {"Radius", "propCircleRadius", F::Radius, v[3], 0}};
    for (const auto& item : items) {
      addRow(geometryRows, item.label, numberField(item.name, fmt(item.shown), [this, i, f = item.field](double value) {
        return propedit::SetCircleField(m_State, i, f, value);
      }), item.axis);
    }
    constexpr double kPi = 3.14159265358979323846;
    auto* derived = section("Derived", "propertiesDerived");
    readOnly(derived, "Diameter", fmt(2 * v[3]), "propDerivedDiameter");
    readOnly(derived, "Circumference", fmt(2 * kPi * v[3]), "propDerivedCircumference");
    readOnly(derived, "Area", fmt(kPi * v[3] * v[3]), "propDerivedArea");
  } else if (texts.size() == 1 && static_cast<size_t>(texts[0].index) < st.cadAnnotations.size()) {
    const int i = texts[0].index;
    const CadAnnotation& ann = st.cadAnnotations[static_cast<size_t>(i)];
    using F = propedit::TextField;
    const char* kind = ann.kind == CadAnnotation::Kind::Text ? "TEXT" : ann.kind == CadAnnotation::Kind::Mtext ? "MTEXT"
        : ann.kind == CadAnnotation::Kind::DimAligned ? "DIMALIGNED" : ann.kind == CadAnnotation::Kind::DimLinear ? "DIMLINEAR"
        : ann.kind == CadAnnotation::Kind::DimAngular ? "DIMANGULAR" : "TABLE";
    readOnly(geometryRows, "Kind", kind, "propTextKind");
    auto number = [&](const char* label, const char* name, F field, const QString& shown, char axis = 0) {
      addRow(geometryRows, label, numberField(name, shown, [this, i, field](double value) {
        return propedit::SetTextNumber(m_State, i, field, value);
      }), axis);
    };
    number("Insertion X", "propTextInsertionX", F::InsertionX, fmt(ann.insX + ox), 'X');
    number("Insertion Y", "propTextInsertionY", F::InsertionY, fmt(ann.insY + oy), 'Y');
    number("Insertion Z", "propTextInsertionZ", F::InsertionZ, fmt(ann.insZ), 'Z');
    number("Plotted height (in)", "propTextHeight", F::PlottedHeightInches, QString::asprintf("%.4f", static_cast<double>(ann.plottedHeightInches)));
    if (ann.kind == CadAnnotation::Kind::Text)
      number("Rotation (CW from N)", "propTextRotation", F::RotationBearingDeg,
             QString::asprintf("%.2f", static_cast<double>(BearingCwNorthDegFromMathAngleRad(ann.rotationRad))));
    if (ann.kind == CadAnnotation::Kind::Mtext) {
      number("Box min X", "propTextBoxMinX", F::BoxMinX, fmt(ann.boxMinX));
      number("Box min Y", "propTextBoxMinY", F::BoxMinY, fmt(ann.boxMinY));
      number("Box max X", "propTextBoxMaxX", F::BoxMaxX, fmt(ann.boxMaxX));
      number("Box max Y", "propTextBoxMaxY", F::BoxMaxY, fmt(ann.boxMaxY));
    }
    if (ann.kind != CadAnnotation::Kind::Table) {
      auto* content = new ContentEdit(body);
      content->setObjectName("propTextContent");
      content->setPlainText(Text(ann.text));
      content->setFixedHeight(84);
      const QString original = Text(ann.text);
      content->committed = [this, content, i, original, finish] {
        const QString typed = content->toPlainText();
        if (typed == original) return;
        finish(propedit::SetTextContent(m_State, i, typed.toUtf8().toStdString()));
      };
      addRow(geometryRows, "Content", content);
    }
    auto* derived = section("Derived", "propertiesDerived");
    readOnly(derived, "Model text height", fmt(CadAnnotationHeightWorld(ann, m_State.modelUnitsPerPlottedInch)) + " model units", "propDerivedTextHeight");
    if (ann.kind == CadAnnotation::Kind::Mtext) {
      readOnly(derived, "Box width", fmt(std::fabs(ann.boxMaxX - ann.boxMinX)), "propDerivedBoxWidth");
      readOnly(derived, "Box height", fmt(std::fabs(ann.boxMaxY - ann.boxMinY)), "propDerivedBoxHeight");
    }
  } else if (!lines.empty() && lines.size() == st.selection.size()) {
    QStringList start, end, length, bear;
    for (const auto& e : lines) {
      if (static_cast<size_t>(e.index) >= lineCount) continue;
      const double* v = &st.userLinesFlat[static_cast<size_t>(e.index) * 6];
      start.append(fmt(v[0] + ox) + ", " + fmt(v[1] + oy)); end.append(fmt(v[3] + ox) + ", " + fmt(v[4] + oy));
      length.append(fmt(std::hypot(v[3] - v[0], v[4] - v[1]))); bear.append(bearing(v[3] - v[0], v[4] - v[1]));
    }
    readOnly(geometryRows, "Start point", Merge(start), "propMultiStart");
    readOnly(geometryRows, "End point", Merge(end), "propMultiEnd");
    readOnly(geometryRows, "Length", Merge(length), "propMultiLength");
    readOnly(geometryRows, "Rotation rel. north", Merge(bear), "propMultiBearing");
  } else if (!circles.empty() && circles.size() == st.selection.size()) {
    QStringList center, radius, diameter, circumference, area;
    constexpr double kPi = 3.14159265358979323846;
    for (const auto& e : circles) {
      if (static_cast<size_t>(e.index) >= circleCount) continue;
      const double* v = &st.userCirclesCxCyZR[static_cast<size_t>(e.index) * 4];
      center.append(fmt(v[0] + ox) + ", " + fmt(v[1] + oy)); radius.append(fmt(v[3])); diameter.append(fmt(2 * v[3]));
      circumference.append(fmt(2 * kPi * v[3])); area.append(fmt(kPi * v[3] * v[3]));
    }
    readOnly(geometryRows, "Center", Merge(center), "propMultiCenter");
    readOnly(geometryRows, "Radius", Merge(radius), "propMultiRadius");
    readOnly(geometryRows, "Diameter", Merge(diameter), "propMultiDiameter");
    readOnly(geometryRows, "Circumference", Merge(circumference), "propMultiCircumference");
    readOnly(geometryRows, "Area", Merge(area), "propMultiArea");
  } else if (!texts.empty()) {
    QStringList x, y, height, rotation;
    for (const auto& e : texts) {
      if (static_cast<size_t>(e.index) >= st.cadAnnotations.size()) continue;
      const CadAnnotation& a = st.cadAnnotations[static_cast<size_t>(e.index)];
      x.append(fmt(a.insX + ox)); y.append(fmt(a.insY + oy));
      height.append(QString::asprintf("%.4f", static_cast<double>(a.plottedHeightInches)));
      rotation.append(QString::fromStdString(FormatBearing(BearingCwNorthDegFromMathAngleRad(a.rotationRad), angle)));
    }
    readOnly(geometryRows, "Insertion X", Merge(x), "propMultiInsertionX");
    readOnly(geometryRows, "Insertion Y", Merge(y), "propMultiInsertionY");
    readOnly(geometryRows, "Plotted height (in)", Merge(height), "propMultiTextHeight");
    readOnly(geometryRows, "Rotation rel. north", Merge(rotation), "propMultiRotation");
    auto* hint = new QLabel("Select a single TEXT or MTEXT to edit content and box here.", body);
    hint->setWordWrap(true);
    geometryRows->addWidget(hint);
  }
  // A single TIN surface: read-only, as in 0.7.4 -- every figure is derived from the triangulation or the
  // definition, so an editable copy here would be a second source of truth the next rebuild overwrites.
  int surfaceCount = 0;
  size_t surfaceIndex = 0;
  for (const auto& e : st.selection)
    if (e.type == SelectedEntity::Type::Surface && ++surfaceCount == 1) surfaceIndex = static_cast<size_t>(e.index);
  if (surfaceCount == 1 && surfaceIndex < st.cadSurfaces.size()) {
    const CadSurface& surface = st.cadSurfaces[surfaceIndex];
    auto* rows = section("Surface", "propertiesSurface");
    readOnly(rows, "Name", Text(surface.name), "propSurfaceName");
    readOnly(rows, "Points", QString::number(surface.vertexCount()), "propSurfacePoints");
    readOnly(rows, "Triangles", QString::number(surface.triangleCount()), "propSurfaceTriangles");
    if (surface.tin && surface.tin->vertsXyz.size() >= 3) {
      double lo = surface.tin->vertsXyz[2], hi = lo;
      for (size_t i = 2; i < surface.tin->vertsXyz.size(); i += 3) {
        lo = std::min(lo, surface.tin->vertsXyz[i]);
        hi = std::max(hi, surface.tin->vertsXyz[i]);
      }
      readOnly(rows, "Elevation range", fmt(lo) + " to " + fmt(hi), "propSurfaceElevation");
    } else {
      readOnly(rows, "Elevation range", QString::fromUtf8("\xE2\x80\x94"), "propSurfaceElevation");
    }
    readOnly(rows, "Definition", QString("%1 group(s), %2 file(s), %3 breakline(s), %4 boundary(ies)")
                                     .arg(surface.sourcePointGroups.size()).arg(surface.sourcePointFiles.size())
                                     .arg(surface.breaklines.size()).arg(surface.boundaries.size()), "propSurfaceDefinition");
    // REQ-086: a surface showing an older triangulation than its definition describes must SAY so.
    if (surface.lastBuildIncomplete)
      readOnly(rows, "Status", QString::fromUtf8("out of date \xE2\x80\x94 ") + Text(surface.lastBuildMessage), "propSurfaceStatus");
    auto* hint = new QLabel("Read-only. Edit the definition in the Surfaces manager.", body);
    hint->setObjectName("propSurfaceNote");
    hint->setWordWrap(true);
    rows->addWidget(hint);
  }
  if (!st.selectedSurveyPointIndices.empty()) {
    auto* points = new QLabel(QString("%1 survey point%2 selected. Bulk editing: VIEWPOINTS.").arg(st.selectedSurveyPointIndices.size()).arg(st.selectedSurveyPointIndices.size() == 1 ? "" : "s"), body);
    points->setObjectName("propertiesSurveyNote");
    points->setWordWrap(true);
    root->addWidget(points);
  }
  root->addStretch(1);

  if (auto* old = m_PropertiesScroll->takeWidget()) old->deleteLater();
  m_PropertiesScroll->setWidget(body);
}
