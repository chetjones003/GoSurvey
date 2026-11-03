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
    auto* line = prior->findChild<QComboBox*>("gradingFeatureLine");
    auto* surface = prior->findChild<QComboBox*>("gradingSurface");
    if (line) RefreshFeatureLines(*this, line, CurrentFeatureId(line));
    if (surface) RefreshSurfaces(*this, surface, surface->currentData().toULongLong());
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
      const int si = FindSurfaceIndexById(m_State, def.surfaceId);
      if (si >= 0) surface->setCurrentIndex(si);
    }
  };
  QObject::connect(line, qOverload<int>(&QComboBox::currentIndexChanged), dialog, populateInputs);
  populateInputs(line->currentIndex());
  auto paletteIsCurrent = [this, dialog, line, surface, status]() {
    const auto generation = dialog->property("documentGeneration").toULongLong();
    if (generation == m_DocumentGeneration) return true;
    RefreshFeatureLines(*this, line, 0);
    RefreshSurfaces(*this, surface);
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
    if (target->currentIndex() == 0 && surface->count() == 0) {
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
      Submit("V"); Submit(QString::number(value->value()));
      if (target->currentIndex() == 1) { Submit("P"); Submit(QString::number(grade->value())); }
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
