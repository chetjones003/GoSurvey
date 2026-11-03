#include "EditorWindow.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPalette>
#include <QPushButton>
#include <QSize>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <cmath>

namespace {
enum class PaletteGlyph { Surface, Distance, Absolute, Relative, Apply, Add, Remove, Infill, List, Delete };

QIcon GradingIcon(QWidget* palette, PaletteGlyph glyph) {
  constexpr int size = 24;
  QPixmap pixmap(size, size);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  const QColor ink = palette->palette().color(QPalette::ButtonText);
  const QColor accent = palette->palette().color(QPalette::Highlight);
  QPen line(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(line);
  painter.setBrush(Qt::NoBrush);
  const auto path = [&painter](std::initializer_list<QPointF> points) {
    QPainterPath shape;
    auto it = points.begin();
    if (it == points.end()) return;
    shape.moveTo(*it++);
    for (; it != points.end(); ++it) shape.lineTo(*it);
    painter.drawPath(shape);
  };
  const auto arrow = [&painter](QPointF from, QPointF to) {
    painter.drawLine(from, to);
    const QLineF shaft(from, to);
    const double angle = std::atan2(shaft.dy(), shaft.dx());
    constexpr double head = 4.2;
    for (const double direction : {-0.65, 0.65}) {
      const QPointF wing(to.x() - head * std::cos(angle + direction),
                         to.y() - head * std::sin(angle + direction));
      painter.drawLine(to, wing);
    }
  };
  switch (glyph) {
    case PaletteGlyph::Surface:
      path({{2.5, 17.5}, {8, 13.5}, {13, 16}, {21.5, 6.5}});
      painter.setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      path({{3, 21}, {21, 21}});
      break;
    case PaletteGlyph::Distance:
      arrow({4, 12}, {20, 12}); arrow({20, 12}, {4, 12});
      painter.drawLine(4, 7, 4, 17); painter.drawLine(20, 7, 20, 17);
      break;
    case PaletteGlyph::Absolute:
      painter.setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawLine(4, 19, 20, 19);
      painter.setPen(line);
      arrow({12, 18}, {12, 4});
      break;
    case PaletteGlyph::Relative:
      arrow({8, 18}, {8, 6}); arrow({16, 6}, {16, 18});
      break;
    case PaletteGlyph::Apply:
      path({{4, 12}, {9, 17}, {20, 6}});
      break;
    case PaletteGlyph::Add:
      painter.drawEllipse(QRectF(3, 3, 18, 18));
      painter.drawLine(12, 7, 12, 17); painter.drawLine(7, 12, 17, 12);
      break;
    case PaletteGlyph::Remove:
      painter.drawEllipse(QRectF(3, 3, 18, 18));
      painter.drawLine(7, 12, 17, 12);
      break;
    case PaletteGlyph::Infill:
      painter.drawRect(QRectF(4, 4, 16, 16));
      painter.setClipRect(QRectF(5, 5, 14, 14));
      for (int offset = -8; offset < 20; offset += 5)
        painter.drawLine(offset, 20, offset + 16, 4);
      painter.setClipping(false);
      break;
    case PaletteGlyph::List:
      for (int y : {6, 12, 18}) {
        painter.drawEllipse(QPointF(4, y), 0.9, 0.9);
        painter.drawLine(8, y, 20, y);
      }
      break;
    case PaletteGlyph::Delete:
      painter.drawLine(5, 7, 19, 7); painter.drawLine(9, 4, 15, 4);
      painter.drawLine(7, 8, 8, 20); painter.drawLine(17, 8, 16, 20);
      painter.drawLine(8, 20, 16, 20);
      painter.drawLine(11, 10, 11, 17); painter.drawLine(13, 10, 13, 17);
      break;
  }
  return QIcon(pixmap);
}

std::uint64_t CurrentFeatureId(QComboBox* combo) {
  return combo->currentData().toULongLong();
}

int CurrentFeatureIndex(EditorWindow& window, QComboBox* combo) {
  return FindEntityIndexById(window.State().featureLineAttrs, CurrentFeatureId(combo));
}

void RefreshFeatureLines(EditorWindow& window, QComboBox* combo, std::uint64_t preferredId) {
  EnsureEntityIds(window.State());
  const QSignalBlocker blocker(combo);
  combo->clear();
  for (size_t i = 0; i + 1 < window.State().featureLineOffsets.size(); ++i) {
    const std::string name = i < window.State().featureLineInfo.size()
                                 ? window.State().featureLineInfo[i].name
                                 : std::string();
    const QString label = name.empty() ? QString("Feature line %1").arg(i + 1)
                                       : QString::fromUtf8(name.c_str());
    combo->addItem(label, QVariant::fromValue<qulonglong>(window.State().featureLineAttrs[i].id));
  }
  const int ix = combo->findData(QVariant::fromValue<qulonglong>(preferredId));
  combo->setCurrentIndex(ix);
}

void RefreshSurfaces(EditorWindow& window, QComboBox* combo, std::uint64_t preferredId = 0) {
  const QSignalBlocker blocker(combo);
  combo->clear();
  for (size_t i = 0; i < window.State().cadSurfaces.size(); ++i) {
    const std::string name = window.State().cadSurfaces[i].name;
    const std::uint64_t id = i < window.State().cadSurfaceAttrs.size()
                                 ? window.State().cadSurfaceAttrs[i].id : 0;
    combo->addItem(QString::fromUtf8(name.c_str()), QVariant::fromValue<qulonglong>(id));
  }
  const int ix = combo->findData(QVariant::fromValue<qulonglong>(preferredId));
  if (ix >= 0) combo->setCurrentIndex(ix);
}

bool SelectFeature(EditorWindow& window, QComboBox* combo) {
  const int ix = CurrentFeatureIndex(window, combo);
  window.State().selection.clear();
  if (ix < 0) return false;
  window.State().selection.push_back({SelectedEntity::Type::FeatureLine, ix});
  return true;
}
}

void EditorWindow::ShowGradingTools() {
  if (auto* prior = findChild<QDialog*>("gradingToolsPalette")) {
    const bool sameDocument = prior->property("documentGeneration").toULongLong() == m_DocumentGeneration;
    auto* line = prior->findChild<QComboBox*>("gradingFeatureLine");
    auto* surface = prior->findChild<QComboBox*>("gradingSurface");
    auto* copyBaseline = prior->findChild<QComboBox*>("gradingCopyBaseline");
    if (line) RefreshFeatureLines(*this, line, sameDocument ? CurrentFeatureId(line) : 0);
    if (copyBaseline) RefreshFeatureLines(*this, copyBaseline, sameDocument ? CurrentFeatureId(copyBaseline) : 0);
    if (surface) RefreshSurfaces(*this, surface, sameDocument ? surface->currentData().toULongLong() : 0);
    if (!sameDocument) {
      if (auto* presets = prior->findChild<QComboBox*>("gradingPreset")) {
        const QSignalBlocker blocker(presets);
        presets->clear();
        for (const auto& preset : m_State.gradingPresets)
          presets->addItem(QString::fromUtf8(preset.name.c_str()));
        presets->setCurrentIndex(-1);
      }
      if (auto* name = prior->findChild<QLineEdit*>("gradingPresetName")) name->clear();
      if (auto* target = prior->findChild<QComboBox*>("gradingTarget")) target->setCurrentIndex(0);
      if (auto* value = prior->findChild<QDoubleSpinBox*>("gradingTargetValue")) value->setValue(10.0);
      if (auto* grade = prior->findChild<QDoubleSpinBox*>("gradingDistanceGrade")) grade->setValue(-2.0);
      if (auto* cut = prior->findChild<QLineEdit*>("gradingCutSlope")) cut->setText(QString::number(m_State.gradingCutRun, 'g', 8) + ":1");
      if (auto* fill = prior->findChild<QLineEdit*>("gradingFillSlope")) fill->setText(QString::number(m_State.gradingFillRun, 'g', 8) + ":1");
      if (auto* group = prior->findChild<QLineEdit*>("gradingGroupName")) group->clear();
      if (auto* status = prior->findChild<QLabel*>("gradingStatus")) status->setText("Drawing changed. Choose criteria for this drawing.");
    }
    if (line) {
      const int selected = line->currentIndex();
      line->setCurrentIndex(-1);
      line->setCurrentIndex(selected);
    }
    prior->setProperty("documentGeneration", QVariant::fromValue<qulonglong>(m_DocumentGeneration));
    prior->show(); prior->raise(); prior->activateWindow(); return;
  }
  EnsureEntityIds(m_State);
  auto* dialog = new QDialog(this);
  dialog->setObjectName("gradingToolsPalette");
  dialog->setWindowTitle("Grading Tools");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setModal(false);
  dialog->setProperty("documentGeneration", QVariant::fromValue<qulonglong>(m_DocumentGeneration));
  auto* layout = new QVBoxLayout(dialog);
  layout->setAlignment(Qt::AlignTop);
  layout->setSpacing(8);
  auto* intro = new QHBoxLayout;
  auto* introIcon = new QLabel(dialog);
  introIcon->setPixmap(GradingIcon(dialog, PaletteGlyph::Surface).pixmap(QSize(32, 32)));
  introIcon->setAccessibleName("Grading tools");
  auto* introText = new QVBoxLayout;
  auto* heading = new QLabel("Create or update a grading", dialog);
  heading->setObjectName("gradingIntroHeading");
  QFont headingFont = heading->font();
  headingFont.setBold(true);
  heading->setFont(headingFont);
  auto* guidance = new QLabel("Select a feature line, then choose a surface or a numeric target.\n"
                              "Use a group to manage related grading results and infill.", dialog);
  guidance->setObjectName("gradingIntroGuidance");
  guidance->setWordWrap(true);
  introText->addWidget(heading);
  introText->addWidget(guidance);
  intro->addWidget(introIcon, 0, Qt::AlignTop);
  intro->addLayout(introText, 1);
  layout->addLayout(intro);
  auto* targetHelp = new QLabel(dialog);
  targetHelp->setObjectName("gradingTargetHelp");
  targetHelp->setWordWrap(true);
  layout->addWidget(targetHelp);
  auto* status = new QLabel(dialog);
  status->setObjectName("gradingStatus");
  status->setWordWrap(true);
  status->setText("Ready. Choose a baseline or grading result to begin.");
  auto* form = new QFormLayout;
  auto* line = new QComboBox(dialog); line->setObjectName("gradingFeatureLine");
  RefreshFeatureLines(*this, line, m_State.selection.size() == 1 &&
      m_State.selection.front().type == SelectedEntity::Type::FeatureLine &&
      m_State.selection.front().index >= 0 && static_cast<size_t>(m_State.selection.front().index) < m_State.featureLineAttrs.size()
          ? m_State.featureLineAttrs[static_cast<size_t>(m_State.selection.front().index)].id : 0);
  auto* target = new QComboBox(dialog); target->setObjectName("gradingTarget");
  target->addItem(GradingIcon(dialog, PaletteGlyph::Surface), "Surface");
  target->addItem(GradingIcon(dialog, PaletteGlyph::Distance), "Distance");
  target->addItem(GradingIcon(dialog, PaletteGlyph::Absolute), "Absolute elevation");
  target->addItem(GradingIcon(dialog, PaletteGlyph::Relative), "Relative elevation");
  target->setIconSize(QSize(16, 16));
  auto* surface = new QComboBox(dialog); surface->setObjectName("gradingSurface");
  RefreshSurfaces(*this, surface);
  auto* value = new QDoubleSpinBox(dialog); value->setObjectName("gradingTargetValue");
  value->setRange(-1.0e9, 1.0e9); value->setDecimals(4); value->setValue(10.0);
  auto* valueLabel = new QLabel("Target value", dialog); valueLabel->setObjectName("gradingValueLabel");
  auto* grade = new QDoubleSpinBox(dialog); grade->setObjectName("gradingDistanceGrade");
  grade->setRange(-10000.0, 10000.0); grade->setDecimals(4); grade->setValue(-2.0);
  auto* cut = new QLineEdit(QString::number(m_State.gradingCutRun, 'g', 8) + ":1", dialog); cut->setObjectName("gradingCutSlope");
  auto* cutLabel = new QLabel("Cut slope (H:V)", dialog);
  auto* fill = new QLineEdit(QString::number(m_State.gradingFillRun, 'g', 8) + ":1", dialog); fill->setObjectName("gradingFillSlope");
  auto* fillLabel = new QLabel("Fill slope (H:V)", dialog);
  auto* side = new QComboBox(dialog); side->setObjectName("gradingSide"); side->addItems({"Left", "Right"});
  auto* group = new QLineEdit(dialog); group->setObjectName("gradingGroupName"); group->setPlaceholderText("Group name");
  form->addRow("Baseline or result", line); form->addRow("Target", target);
  form->addRow("Target surface", surface); form->addRow(valueLabel, value);
  form->addRow("Distance grade (%)", grade); form->addRow(cutLabel, cut);
  form->addRow(fillLabel, fill); form->addRow("Open baseline side", side);
  form->addRow("Grading group", group);
  auto* inputsHeading = new QLabel("Grading inputs", dialog);
  QFont sectionFont = inputsHeading->font();
  sectionFont.setBold(true);
  inputsHeading->setFont(sectionFont);
  layout->addWidget(inputsHeading);
  layout->addLayout(form);
  auto updateFields = [surface, value, valueLabel, grade, targetHelp](int index) {
    surface->setEnabled(index == 0);
    value->setEnabled(index != 0);
    grade->setEnabled(index == 1);
    static const QStringList valueLabels{"Target value", "Horizontal width", "Target elevation", "Elevation change"};
    valueLabel->setText(valueLabels.value(index));
    static const QStringList help{
        "Daylight follows the selected target surface.",
        "Set a horizontal width and signed grade; no target surface is required.",
        "Enter the elevation where the grading should finish.",
        "Enter the elevation change from the baseline."};
    targetHelp->setText(help.value(index));
  };
  QObject::connect(target, qOverload<int>(&QComboBox::currentIndexChanged), dialog, updateFields);
  updateFields(target->currentIndex());
  auto populateInputs = [this, line, target, surface, value, grade, cut, fill, side, updateFields](int) {
    const int ix = CurrentFeatureIndex(*this, line);
    if (ix < 0 || static_cast<size_t>(ix) >= m_State.featureLineInfo.size() ||
        !m_State.featureLineInfo[static_cast<size_t>(ix)].grading) return;
    const auto& def = *m_State.featureLineInfo[static_cast<size_t>(ix)].grading;
    const QSignalBlocker block(target);
    target->setCurrentIndex(static_cast<int>(def.target));
    updateFields(target->currentIndex());
    value->setValue(def.targetValue);
    grade->setValue(def.distanceGradePercent);
    cut->setText(QString::number(def.cutRun, 'g', 8) + ":1");
    fill->setText(QString::number(def.fillRun, 'g', 8) + ":1");
    side->setCurrentIndex(def.sideLeft ? 0 : 1);
    if (def.target == CadGradingDefinition::Target::Surface) {
      const int comboIndex = surface->findData(QVariant::fromValue<qulonglong>(def.surfaceId));
      surface->setCurrentIndex(comboIndex);
    }
  };
  QObject::connect(line, qOverload<int>(&QComboBox::currentIndexChanged), dialog, populateInputs);
  populateInputs(line->currentIndex());
  auto paletteIsCurrent = [this, dialog, line, surface, status, target, value, grade, cut, fill, side, group]() {
    const auto generation = dialog->property("documentGeneration").toULongLong();
    if (generation == m_DocumentGeneration) return true;
    RefreshFeatureLines(*this, line, 0);
    RefreshSurfaces(*this, surface);
    if (auto* copyBaseline = dialog->findChild<QComboBox*>("gradingCopyBaseline"))
      RefreshFeatureLines(*this, copyBaseline, 0);
    if (auto* presets = dialog->findChild<QComboBox*>("gradingPreset")) {
      const QSignalBlocker blocker(presets);
      presets->clear();
      for (const auto& preset : m_State.gradingPresets)
        presets->addItem(QString::fromUtf8(preset.name.c_str()));
      presets->setCurrentIndex(-1);
    }
    if (auto* name = dialog->findChild<QLineEdit*>("gradingPresetName")) name->clear();
    target->setCurrentIndex(0); value->setValue(10.0); grade->setValue(-2.0);
    cut->setText(QString::number(m_State.gradingCutRun, 'g', 8) + ":1");
    fill->setText(QString::number(m_State.gradingFillRun, 'g', 8) + ":1");
    side->setCurrentIndex(0); group->clear();
    dialog->setProperty("documentGeneration", QVariant::fromValue<qulonglong>(m_DocumentGeneration));
    status->setText("Drawing changed. Select a feature line again.");
    return false;
  };

  auto* create = new QPushButton(GradingIcon(dialog, PaletteGlyph::Apply), "Create / Rebuild", dialog); create->setObjectName("gradingApply");
  create->setToolTip("Create a grading result or rebuild the selected result with these inputs.");
  create->setAccessibleName("Create or rebuild grading");
  layout->addWidget(create);
  QObject::connect(create, &QPushButton::clicked, dialog, [this, line, target, surface, value, grade, cut, fill, side, status, populateInputs, paletteIsCurrent] {
    if (!paletteIsCurrent()) return;
    const std::uint64_t selectedId = CurrentFeatureId(line);
    RefreshFeatureLines(*this, line, selectedId);
    const int lineIx = CurrentFeatureIndex(*this, line);
    if (lineIx < 0) { status->setText("Select a feature line to begin grading."); return; }
    if (target->currentIndex() == 0 && surface->currentIndex() < 0) {
      status->setText("Surface grading needs a built target surface. Choose another target or create a surface.");
      return;
    }
    if (!SelectFeature(*this, line)) { status->setText("Select a feature line to begin grading."); return; }
    const bool wasResult = static_cast<size_t>(lineIx) < m_State.featureLineInfo.size() &&
                           m_State.featureLineInfo[static_cast<size_t>(lineIx)].grading.has_value();
    Submit("GRADING");
    if (m_State.active != AppCommandState::Kind::Grading) { status->setText(m_Log.empty() ? "GRADING did not start." : QString::fromUtf8(m_Log.back().c_str())); return; }
    Submit("T");
    const QStringList kinds{"Surface", "Distance", "Absolute", "Relative"};
    Submit(kinds.at(target->currentIndex()));
    if (target->currentIndex() == 0) { Submit("S"); Submit(surface->currentText()); }
    else {
      Submit("V"); Submit(QString::number(value->value(), 'f', value->decimals()));
      if (target->currentIndex() == 1) { Submit("P"); Submit(QString::number(grade->value(), 'f', grade->decimals())); }
    }
    Submit("C"); Submit(cut->text()); Submit("F"); Submit(fill->text());
    if (static_cast<size_t>(lineIx) < m_State.featureLineClosed.size() &&
        !m_State.featureLineClosed[static_cast<size_t>(lineIx)]) {
      Submit("D"); Submit(side->currentIndex() == 0 ? "L" : "R");
    }
    Submit("");
    EnsureEntityIds(m_State);
    const std::uint64_t resultId = wasResult ? selectedId :
        (m_State.featureLineAttrs.empty() ? 0 : m_State.featureLineAttrs.back().id);
    RefreshFeatureLines(*this, line, resultId);
    populateInputs(line->currentIndex());
    status->setText(m_Log.empty() ? "No grading result." : QString::fromUtf8(m_Log.back().c_str()));
  });

  auto* copyRow = new QHBoxLayout;
  auto* copyBaseline = new QComboBox(dialog); copyBaseline->setObjectName("gradingCopyBaseline");
  RefreshFeatureLines(*this, copyBaseline, 0);
  auto* copyCreate = new QPushButton("Copy criteria to new baseline", dialog);
  copyCreate->setObjectName("gradingCopyCreate");
  copyCreate->setToolTip("Copy the selected grading's criteria and create a separate result on the chosen baseline.");
  copyRow->addWidget(copyBaseline, 1); copyRow->addWidget(copyCreate);
  layout->addLayout(copyRow);
  QObject::connect(copyCreate, &QPushButton::clicked, dialog,
      [this, line, copyBaseline, target, surface, value, grade, cut, fill, side, status, create, paletteIsCurrent] {
    if (!paletteIsCurrent()) return;
    const int sourceIndex = CurrentFeatureIndex(*this, line);
    if (sourceIndex < 0 || static_cast<size_t>(sourceIndex) >= m_State.featureLineInfo.size() ||
        !m_State.featureLineInfo[static_cast<size_t>(sourceIndex)].grading) {
      status->setText("Select an existing grading result to copy."); return;
    }
    const auto source = *m_State.featureLineInfo[static_cast<size_t>(sourceIndex)].grading;
    if (source.target == CadGradingDefinition::Target::Surface &&
        FindSurfaceIndexById(m_State, source.surfaceId) < 0) {
      status->setText("The source target surface no longer exists. Choose another grading criteria."); return;
    }
    const int baseline = CurrentFeatureIndex(*this, copyBaseline);
    if (baseline < 0 || baseline == sourceIndex) { status->setText("Choose a different feature line as the new baseline."); return; }
    if (static_cast<size_t>(baseline) < m_State.featureLineInfo.size() &&
        m_State.featureLineInfo[static_cast<size_t>(baseline)].grading) {
      status->setText("The new baseline must be an ordinary feature line."); return;
    }
    target->setCurrentIndex(static_cast<int>(source.target));
    value->setValue(source.targetValue); grade->setValue(source.distanceGradePercent);
    cut->setText(QString::number(source.cutRun, 'g', 8) + ":1");
    fill->setText(QString::number(source.fillRun, 'g', 8) + ":1");
    side->setCurrentIndex(source.sideLeft ? 0 : 1);
    if (source.target == CadGradingDefinition::Target::Surface) {
      const int surfaceIndex = surface->findData(QVariant::fromValue<qulonglong>(source.surfaceId));
      if (surfaceIndex < 0) { status->setText("The source target surface no longer exists."); return; }
      surface->setCurrentIndex(surfaceIndex);
    }
    { const QSignalBlocker blocker(line); line->setCurrentIndex(line->findData(QVariant::fromValue<qulonglong>(CurrentFeatureId(copyBaseline)))); }
    create->click();
  });

  auto* presetHeading = new QLabel("Named criteria", dialog);
  presetHeading->setFont(sectionFont);
  layout->addWidget(presetHeading);
  auto* presetHelp = new QLabel("Choose a preset to load its values. Create From Preset always makes a new result on an ordinary baseline; Create / Rebuild can update the selected result.", dialog);
  presetHelp->setWordWrap(true);
  layout->addWidget(presetHelp);
  auto* presetRow = new QHBoxLayout;
  auto* presetName = new QLineEdit(dialog); presetName->setObjectName("gradingPresetName");
  presetName->setPlaceholderText("Preset name");
  auto* presets = new QComboBox(dialog); presets->setObjectName("gradingPreset");
  auto* savePreset = new QPushButton("Save", dialog); savePreset->setObjectName("gradingPresetSave");
  auto* updatePreset = new QPushButton("Update", dialog); updatePreset->setObjectName("gradingPresetUpdate");
  auto* deletePreset = new QPushButton("Delete", dialog); deletePreset->setObjectName("gradingPresetDelete");
  auto refreshPresets = [this, presets, presetName]() {
    const QString selected = presets->currentText();
    const QSignalBlocker blocker(presets);
    presets->clear();
    for (const auto& preset : m_State.gradingPresets)
      presets->addItem(QString::fromUtf8(preset.name.c_str()));
    const int ix = presets->findText(selected, Qt::MatchFixedString);
    if (ix >= 0) presets->setCurrentIndex(ix);
    if (presetName->text().isEmpty() && ix >= 0) presetName->setText(presets->currentText());
  };
  refreshPresets();
  presetRow->addWidget(presetName, 1); presetRow->addWidget(presets, 1);
  presetRow->addWidget(savePreset); presetRow->addWidget(updatePreset); presetRow->addWidget(deletePreset);
  layout->addLayout(presetRow);
  auto* createFromPreset = new QPushButton(GradingIcon(dialog, PaletteGlyph::Apply), "Create new result from preset", dialog);
  createFromPreset->setObjectName("gradingPresetCreate");
  createFromPreset->setToolTip("Create a separate grading result on the selected ordinary baseline using the chosen preset.");
  layout->addWidget(createFromPreset);
  auto applyPresetToFields = [this, target, surface, value, grade, cut, fill, side, status](const CadGradingPreset& preset) {
    target->setCurrentIndex(static_cast<int>(preset.target));
    value->setValue(preset.targetValue); grade->setValue(preset.distanceGradePercent);
    cut->setText(QString::number(preset.cutRun, 'g', 8) + ":1");
    fill->setText(QString::number(preset.fillRun, 'g', 8) + ":1");
    side->setCurrentIndex(preset.sideLeft ? 0 : 1);
    if (preset.target != CadGradingDefinition::Target::Surface) return true;
    surface->setCurrentIndex(-1);
    const int surfaceIndex = FindSurfaceIndexById(m_State, preset.targetSurfaceId);
    if (surfaceIndex < 0) {
      status->setText("The preset's target surface no longer exists. It was not substituted.");
      return false;
    }
    const int comboIndex = surface->findData(QVariant::fromValue<qulonglong>(preset.targetSurfaceId));
    if (comboIndex < 0) {
      status->setText("The preset's target surface is unavailable in the palette. It was not substituted.");
      return false;
    }
    surface->setCurrentIndex(comboIndex);
    return true;
  };
  QObject::connect(createFromPreset, &QPushButton::clicked, dialog,
      [this, line, presets, status, create, paletteIsCurrent, applyPresetToFields] {
    if (!paletteIsCurrent()) return;
    if (presets->currentIndex() < 0 || static_cast<size_t>(presets->currentIndex()) >= m_State.gradingPresets.size()) {
      status->setText("Choose a grading preset first."); return;
    }
    const int baselineIndex = CurrentFeatureIndex(*this, line);
    if (baselineIndex < 0 || (static_cast<size_t>(baselineIndex) < m_State.featureLineInfo.size() &&
        m_State.featureLineInfo[static_cast<size_t>(baselineIndex)].grading)) {
      status->setText("Choose an ordinary feature line as the new baseline. Presets always create a new result."); return;
    }
    const auto& preset = m_State.gradingPresets[static_cast<size_t>(presets->currentIndex())];
    if (!applyPresetToFields(preset)) return;
    create->click();
  });
  QObject::connect(presets, qOverload<int>(&QComboBox::currentIndexChanged), dialog,
                   [this, presets, presetName, applyPresetToFields, paletteIsCurrent](int ix) {
    if (!paletteIsCurrent()) return;
    if (ix < 0 || static_cast<size_t>(ix) >= m_State.gradingPresets.size()) return;
    const auto& p = m_State.gradingPresets[static_cast<size_t>(ix)];
    presetName->setText(QString::fromUtf8(p.name.c_str()));
    applyPresetToFields(p);
  });
  auto savePresetValue = [this, presetName, target, surface, value, grade, cut, fill, side,
                          presets, refreshPresets, status, paletteIsCurrent](bool update) {
    if (!paletteIsCurrent()) return;
    const QString qname = presetName->text().trimmed();
    if (qname.isEmpty()) { status->setText("Enter a name for the grading preset."); return; }
    const std::string name = qname.toUtf8().toStdString();
    auto found = std::find_if(m_State.gradingPresets.begin(), m_State.gradingPresets.end(),
        [&name](const CadGradingPreset& p) { return QString::fromUtf8(p.name.c_str()).compare(
            QString::fromUtf8(name.c_str()), Qt::CaseInsensitive) == 0; });
    if ((update && found == m_State.gradingPresets.end()) || (!update && found != m_State.gradingPresets.end())) {
      status->setText(update ? "Select an existing preset to update." : "A preset with that name already exists."); return;
    }
    bool cutOk = false, fillOk = false;
    const double cutRun = cut->text().section(':', 0, 0).toDouble(&cutOk);
    const double fillRun = fill->text().section(':', 0, 0).toDouble(&fillOk);
    if (!cutOk || !fillOk || !std::isfinite(cutRun) || !std::isfinite(fillRun) || cutRun <= 0.0 || fillRun <= 0.0) { status->setText("Cut and fill slopes must be positive H:V values."); return; }
    CadGradingPreset preset;
    preset.name = name;
    preset.targetSurfaceId = target->currentIndex() == 0 ? surface->currentData().toULongLong() : 0;
    preset.cutRun = cutRun; preset.fillRun = fillRun; preset.sideLeft = side->currentIndex() == 0;
    preset.target = static_cast<CadGradingDefinition::Target>(target->currentIndex());
    preset.targetValue = value->value(); preset.distanceGradePercent = grade->value();
    if (!std::isfinite(preset.targetValue) || !std::isfinite(preset.distanceGradePercent) ||
        (preset.target == CadGradingDefinition::Target::Distance && preset.targetValue <= 0.0)) {
      status->setText("Enter a finite target value and a positive distance width."); return;
    }
    if (preset.target == CadGradingDefinition::Target::Surface && preset.targetSurfaceId == 0) {
      status->setText("Choose an existing target surface before saving this preset."); return;
    }
    PushUndoSnapshot(m_State, update ? "Update grading preset" : "Create grading preset");
    if (update) *found = std::move(preset); else m_State.gradingPresets.push_back(std::move(preset));
    refreshPresets();
    status->setText(update ? "Grading preset updated." : "Grading preset saved in this drawing.");
  };
  QObject::connect(savePreset, &QPushButton::clicked, dialog, [savePresetValue] { savePresetValue(false); });
  QObject::connect(updatePreset, &QPushButton::clicked, dialog, [savePresetValue] { savePresetValue(true); });
  QObject::connect(deletePreset, &QPushButton::clicked, dialog, [this, presets, status, refreshPresets, paletteIsCurrent] {
    if (!paletteIsCurrent()) return;
    const int ix = presets->currentIndex();
    if (ix < 0 || static_cast<size_t>(ix) >= m_State.gradingPresets.size()) { status->setText("Select a preset to delete."); return; }
    PushUndoSnapshot(m_State, "Delete grading preset");
    m_State.gradingPresets.erase(m_State.gradingPresets.begin() + ix);
    refreshPresets(); status->setText("Grading preset deleted from this drawing.");
  });

  auto* groupHeading = new QLabel("Group and boundary tools", dialog);
  groupHeading->setFont(sectionFont);
  layout->addWidget(groupHeading);
  auto* groupHelp = new QLabel("Enter a group name above. Infill adds the selected closed feature line as a bounded pad.", dialog);
  groupHelp->setWordWrap(true);
  groupHelp->setObjectName("gradingGroupHelp");
  layout->addWidget(groupHelp);
  auto* actions = new QGridLayout;
  auto add = new QPushButton(GradingIcon(dialog, PaletteGlyph::Add), "Add member", dialog); add->setObjectName("gradingAdd");
  auto remove = new QPushButton(GradingIcon(dialog, PaletteGlyph::Remove), "Remove member", dialog); remove->setObjectName("gradingRemove");
  auto infill = new QPushButton(GradingIcon(dialog, PaletteGlyph::Infill), "Add infill", dialog); infill->setObjectName("gradingInfill");
  auto list = new QPushButton(GradingIcon(dialog, PaletteGlyph::List), "List group", dialog); list->setObjectName("gradingList");
  auto erase = new QPushButton(GradingIcon(dialog, PaletteGlyph::Delete), "Delete line", dialog); erase->setObjectName("gradingDelete");
  add->setToolTip("Add the selected grading result to the named group.");
  remove->setToolTip("Remove the selected member from the named group.");
  infill->setToolTip("Add the selected closed feature line as infill in the named group.");
  list->setToolTip("Show members of the named grading group.");
  erase->setToolTip("Delete the selected feature line and update any groups that contain it.");
  for (auto* button : {add, remove, infill, list, erase}) button->setIconSize(QSize(16, 16));
  actions->addWidget(add, 0, 0);
  actions->addWidget(remove, 0, 1);
  actions->addWidget(infill, 0, 2);
  actions->addWidget(list, 1, 0, 1, 2);
  actions->addWidget(erase, 1, 2);
  layout->addLayout(actions);
  layout->addWidget(status);
  QObject::connect(add, &QPushButton::clicked, dialog, [this, line, group, status, paletteIsCurrent] {
    if (!paletteIsCurrent()) return;
    if (!SelectFeature(*this, line)) { status->setText("Select a grading result first."); return; }
    Submit(QString("GRADINGGROUP %1, Add").arg(group->text())); status->setText(m_Log.empty() ? "No group result." : QString::fromUtf8(m_Log.back().c_str()));
  });
  QObject::connect(remove, &QPushButton::clicked, dialog, [this, line, group, status, paletteIsCurrent] {
    if (!paletteIsCurrent()) return;
    if (!SelectFeature(*this, line)) { status->setText("Select a group member first."); return; }
    Submit(QString("GRADINGGROUP %1, Remove").arg(group->text())); status->setText(m_Log.empty() ? "No group result." : QString::fromUtf8(m_Log.back().c_str()));
  });
  QObject::connect(infill, &QPushButton::clicked, dialog, [this, line, group, status, paletteIsCurrent] {
    if (!paletteIsCurrent()) return;
    if (!SelectFeature(*this, line)) { status->setText("Select a closed feature line first."); return; }
    Submit(QString("GRADINGINFILL %1").arg(group->text())); status->setText(m_Log.empty() ? "No infill result." : QString::fromUtf8(m_Log.back().c_str()));
  });
  QObject::connect(list, &QPushButton::clicked, dialog, [this, group, status, paletteIsCurrent] {
    if (!paletteIsCurrent()) return;
    Submit(QString("GRADINGGROUP %1, List").arg(group->text())); status->setText(m_Log.empty() ? "No group result." : QString::fromUtf8(m_Log.back().c_str()));
  });
  QObject::connect(erase, &QPushButton::clicked, dialog, [this, line, status, paletteIsCurrent] {
    if (!paletteIsCurrent()) return;
    if (!SelectFeature(*this, line)) { status->setText("Select a feature line to delete."); return; }
    Submit("ERASE"); RefreshFeatureLines(*this, line, 0); status->setText(m_Log.empty() ? "No delete result." : QString::fromUtf8(m_Log.back().c_str()));
  });
  dialog->resize(520, 620);
  dialog->show(); dialog->raise();
}
