#include "EditorAlignmentCreate.hpp"

#include "CadCommands.hpp"
#include "EditorViewport.hpp"
#include "EditorWindow.hpp"
#include "alignment/Alignment.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>
#include <cassert>
#include <cmath>
#include <string>
#include <utility>

namespace {

constexpr std::array<const char*, 5> kTypeNames = {"Centerline", "Offset", "Curb Return", "Miscellaneous", "Rail"};

QString Q(const std::string& s) { return QString::fromUtf8(s.c_str()); }
std::string S(const QString& s) { return s.toUtf8().toStdString(); }
QString Number(double v) { return QString::number(v, 'f', 2); }

/// A Civil 3D-style field: the caption on its own line above the control.
QWidget* Captioned(const QString& caption, QWidget* field, QWidget* parent) {
  assert(field != nullptr && parent != nullptr);
  auto* box = new QWidget(parent);
  auto* column = new QVBoxLayout(box);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(3);
  column->addWidget(new QLabel(caption, box));
  column->addWidget(field);
  assert(box->layout() == column);
  return box;
}

QComboBox* NameCombo(QWidget* parent, const QStringList& names, const QString& current) {
  assert(parent != nullptr);
  auto* combo = new QComboBox(parent);
  combo->addItems(names);
  const int at = combo->findText(current, Qt::MatchFixedString);
  combo->setCurrentIndex(at >= 0 ? at : 0);
  assert(combo->count() == names.size());
  return combo;
}

struct DialogFields {
  QLineEdit* name = nullptr;
  QComboBox* type = nullptr;
  QPlainTextEdit* description = nullptr;
  QLineEdit* station = nullptr;
  QComboBox* style = nullptr;
  QComboBox* layer = nullptr;
  QComboBox* labelSet = nullptr;
  QLineEdit* startX = nullptr;     // layout only
  QLineEdit* startY = nullptr;
  QLineEdit* direction = nullptr;
  QCheckBox* addCurves = nullptr;  // objects only
  QLineEdit* radius = nullptr;
  QCheckBox* erase = nullptr;
  QLineEdit* speed = nullptr;
  QCheckBox* useCheckSet = nullptr;
  QComboBox* checkSet = nullptr;
  QLabel* error = nullptr;
};

QWidget* BuildLayoutStart(DialogFields* f, QWidget* parent) {
  assert(f != nullptr && parent != nullptr);
  auto* group = new QGroupBox("Starting point and direction (optional)", parent);
  auto* grid = new QHBoxLayout(group);
  f->startX = new QLineEdit(group);
  f->startY = new QLineEdit(group);
  f->direction = new QLineEdit(group);
  f->startX->setObjectName("createAlignmentStartX");
  f->startY->setObjectName("createAlignmentStartY");
  f->direction->setObjectName("createAlignmentDirection");
  f->startX->setPlaceholderText("pick");
  f->startY->setPlaceholderText("pick");
  f->direction->setPlaceholderText("free");
  grid->addWidget(Captioned("X:", f->startX, group));
  grid->addWidget(Captioned("Y:", f->startY, group));
  grid->addWidget(Captioned("Azimuth (deg):", f->direction, group));
  assert(f->direction != nullptr);
  return group;
}

QWidget* BuildConversionOptions(const AppCommandState& st, DialogFields* f, QWidget* parent) {
  assert(f != nullptr && parent != nullptr);
  auto* group = new QGroupBox("Conversion options", parent);
  auto* column = new QVBoxLayout(group);
  f->addCurves = new QCheckBox("Add curves between tangents", group);
  f->addCurves->setObjectName("createAlignmentAddCurves");
  f->addCurves->setChecked(true);
  f->radius = new QLineEdit(Number(EffectiveAlignmentCurveRadius(st)), group);
  f->radius->setObjectName("createAlignmentRadius");
  f->erase = new QCheckBox("Erase existing entities", group);
  f->erase->setObjectName("createAlignmentErase");
  f->erase->setChecked(true);
  QObject::connect(f->addCurves, &QCheckBox::toggled, f->radius, &QWidget::setEnabled);
  column->addWidget(f->addCurves);
  auto* indented = Captioned("Default radius:", f->radius, group);
  indented->setContentsMargins(22, 0, 0, 0);
  column->addWidget(indented);
  column->addWidget(f->erase);
  assert(f->erase != nullptr);
  return group;
}

QWidget* BuildGeneralTab(const AppCommandState& st, bool fromObjects, DialogFields* f, QWidget* parent) {
  assert(f != nullptr && parent != nullptr);
  auto* tab = new QWidget(parent);
  auto* column = new QVBoxLayout(tab);
  QStringList styles, layers, sets;
  for (const auto& s : st.alignmentStyles) styles << Q(s.name);
  for (const auto& l : st.drawingLayerTable) layers << Q(l.name);
  for (const auto& s : st.alignmentLabelSets) sets << Q(s.name);
  if (layers.isEmpty()) layers << "0";
  f->style = NameCombo(tab, styles, "Standard");
  f->layer = NameCombo(tab, layers, Q(st.currentLayer));
  f->layer->setEditable(true);
  f->labelSet = NameCombo(tab, sets, "Standard");
  f->style->setObjectName("createAlignmentStyle");
  f->layer->setObjectName("createAlignmentLayer");
  f->labelSet->setObjectName("createAlignmentLabelSet");
  column->addWidget(Captioned("Alignment style:", f->style, tab));
  column->addWidget(Captioned("Alignment layer:", f->layer, tab));
  column->addWidget(Captioned("Alignment label set:", f->labelSet, tab));
  column->addWidget(fromObjects ? BuildConversionOptions(st, f, tab) : BuildLayoutStart(f, tab));
  column->addStretch(1);
  assert(f->labelSet != nullptr);
  return tab;
}

QWidget* BuildCriteriaTab(const AppCommandState& st, DialogFields* f, QWidget* parent) {
  assert(f != nullptr && parent != nullptr);
  auto* tab = new QWidget(parent);
  auto* column = new QVBoxLayout(tab);
  f->speed = new QLineEdit(tab);
  f->speed->setObjectName("createAlignmentSpeed");
  f->speed->setPlaceholderText("none");
  column->addWidget(Captioned("Starting design speed:", f->speed, tab));
  QStringList sets;
  for (const auto& s : st.alignmentCheckSets) sets << Q(s.name);
  f->useCheckSet = new QCheckBox("Use design check set", tab);
  f->useCheckSet->setObjectName("createAlignmentUseCheckSet");
  f->checkSet = NameCombo(tab, sets, {});
  f->checkSet->setObjectName("createAlignmentCheckSet");
  f->useCheckSet->setEnabled(!sets.isEmpty());
  f->checkSet->setEnabled(false);
  QObject::connect(f->useCheckSet, &QCheckBox::toggled, f->checkSet, &QWidget::setEnabled);
  column->addWidget(f->useCheckSet);
  column->addWidget(f->checkSet);
  column->addStretch(1);
  assert(f->checkSet != nullptr);
  return tab;
}

QString DescriptionText(const DialogFields& f) {
  assert(f.description != nullptr);
  QString text = f.description->toPlainText();
  text.replace('\n', ' ');
  assert(!text.contains('\n'));
  return text.trimmed();
}

/// The first problem with what was typed, or an empty string. Commas would split the command's fields.
QString Validate(const AppCommandState& st, const DialogFields& f, bool fromObjects) {
  assert(f.name != nullptr && f.station != nullptr);
  const QString name = f.name->text().trimmed();
  if (name.isEmpty()) return "Enter a name.";
  for (const auto& a : st.alignments)
    if (Q(a.name).compare(name, Qt::CaseInsensitive) == 0) return "An alignment named \"" + name + "\" already exists.";
  if (name.contains(',') || DescriptionText(f).contains(',') || f.layer->currentText().contains(','))
    return "Names and descriptions cannot contain commas.";
  double v = 0.0;
  if (!alignment::ParseStation(S(f.station->text().trimmed()), &v)) return "The starting station is not a station (try 5+00).";
  bool ok = true;
  if (!f.speed->text().trimmed().isEmpty() && (f.speed->text().toDouble(&ok) <= 0.0 || !ok))
    return "The design speed must be a number above zero.";
  if (fromObjects) {
    if (f.addCurves->isChecked() && (f.radius->text().toDouble(&ok) <= 0.0 || !ok)) return "The radius must be above zero.";
    return {};
  }
  const bool hasX = !f.startX->text().trimmed().isEmpty(), hasY = !f.startY->text().trimmed().isEmpty();
  if (hasX != hasY) return "Give both X and Y for the starting point, or neither.";
  bool okX = true, okY = true, okDirection = true;
  if (hasX) {
    (void)f.startX->text().toDouble(&okX);
    (void)f.startY->text().toDouble(&okY);
  }
  if (!okX || !okY) return "The starting point must be two numbers.";
  const bool hasDirection = !f.direction->text().trimmed().isEmpty();
  if (hasDirection) (void)f.direction->text().toDouble(&okDirection);
  if (hasDirection && (!hasX || !okDirection))
    return "A starting direction needs a starting point and is an azimuth in degrees.";
  assert(!fromObjects);
  return {};
}

QString CriteriaFields(const DialogFields& f) {
  assert(f.speed != nullptr && f.useCheckSet != nullptr);
  QString out;
  if (f.useCheckSet->isChecked() && !f.checkSet->currentText().isEmpty()) out += ", CRITERIA=" + f.checkSet->currentText();
  if (!f.speed->text().trimmed().isEmpty()) out += ", SPEED=" + f.speed->text().trimmed();
  assert(!out.contains('\n'));
  return out;
}

CreateAlignmentCommands BuildCommands(const AppCommandState& st, const DialogFields& f, bool fromObjects) {
  assert(f.name != nullptr && f.type != nullptr);
  const QString name = f.name->text().trimmed();
  const QString station = f.station->text().trimmed();
  CreateAlignmentCommands c;
  c.name = name;
  if (fromObjects) {
    c.create = "CREATEALIGNMENTFROMOBJECTS " + name + ", START=" + station + ", TYPE=" + f.type->currentText() +
               ", DESCRIPTION=" + DescriptionText(f) + ", LAYER=" + f.layer->currentText() + ", STYLE=" +
               f.style->currentText() + ", LABELSET=" + f.labelSet->currentText() + CriteriaFields(f);
    if (f.addCurves->isChecked()) c.create += ", FILLET=" + f.radius->text().trimmed();
    if (f.erase->isChecked()) c.create += ", ERASE";
    c.create += Q(AlignmentFromObjectsPickArgs(st));
    return c;
  }
  c.create = "CREATEALIGNMENT " + name + ", " + DescriptionText(f) + ", " + station + ", " + f.layer->currentText() + ", " +
             f.style->currentText() + ", TYPE=" + f.type->currentText() + ", LABELSET=" + f.labelSet->currentText() +
             CriteriaFields(f);
  if (f.startX->text().trimmed().isEmpty()) return c;  // the toolbar opens idle; a Tangent-Tangent tool starts the layout
  c.layout = "ALIGNMENTLAYOUT " + name + ", FROM=" + f.startX->text().trimmed() + " " + f.startY->text().trimmed();
  if (!f.direction->text().trimmed().isEmpty()) c.layout += ", DIRECTION=" + f.direction->text().trimmed();
  assert(!c.layout.isEmpty());
  return c;
}

/// The top of both dialogs: Name, Type, Description and Starting station, as Civil 3D lays them out.
void BuildHeader(QDialog* dialog, QVBoxLayout* column, DialogFields* f, const AppCommandState& st) {
  assert(dialog != nullptr && column != nullptr && f != nullptr);
  f->name = new QLineEdit(Q(NextAlignmentName(st)), dialog);
  f->name->setObjectName("createAlignmentName");
  f->type = new QComboBox(dialog);
  for (const char* t : kTypeNames) f->type->addItem(t);
  f->type->setObjectName("createAlignmentType");
  f->description = new QPlainTextEdit(dialog);
  f->description->setObjectName("createAlignmentDescription");
  f->description->setFixedHeight(48);
  f->station = new QLineEdit(Q(alignment::FormatStation(0.0)), dialog);
  f->station->setObjectName("createAlignmentStation");
  f->station->setMaximumWidth(110);
  column->addWidget(Captioned("Name:", f->name, dialog));
  column->addWidget(Captioned("Type:", f->type, dialog));
  column->addWidget(Captioned("Description:", f->description, dialog));
  auto* stationRow = new QHBoxLayout;
  stationRow->addStretch(1);
  stationRow->addWidget(new QLabel("Starting station:", dialog));
  stationRow->addWidget(f->station);
  column->addLayout(stationRow);
  assert(f->station != nullptr);
}

}  // namespace

CreateAlignmentCommands RunCreateAlignmentDialog(QWidget* parent, const AppCommandState& state, bool fromObjects,
                                                 std::function<void()> help) {
  assert(state.alignments.size() < state.alignments.max_size());
  QDialog dialog(parent);
  dialog.setObjectName(fromObjects ? "createAlignmentFromObjectsDialog" : "createAlignmentLayoutDialog");
  dialog.setWindowTitle(fromObjects ? "Create Alignment from Objects" : "Create Alignment - Layout");
  dialog.setMinimumWidth(390);
  auto* column = new QVBoxLayout(&dialog);
  DialogFields f;
  BuildHeader(&dialog, column, &f, state);
  auto* tabs = new QTabWidget(&dialog);
  tabs->setObjectName("createAlignmentTabs");
  tabs->addTab(BuildGeneralTab(state, fromObjects, &f, tabs), "General");
  tabs->addTab(BuildCriteriaTab(state, &f, tabs), "Design Criteria");
  column->addWidget(tabs, 1);
  f.error = new QLabel(&dialog);
  f.error->setObjectName("createAlignmentError");
  f.error->setStyleSheet("color: #d9534f;");
  f.error->setWordWrap(true);
  column->addWidget(f.error);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                           (help ? QDialogButtonBox::Help : QDialogButtonBox::NoButton),
                                       &dialog);
  column->addWidget(buttons);
  if (help) QObject::connect(buttons, &QDialogButtonBox::helpRequested, &dialog, help);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
    const QString problem = Validate(state, f, fromObjects);
    f.error->setText(problem);
    if (problem.isEmpty()) dialog.accept();
  });
  if (dialog.exec() != QDialog::Accepted) return {};
  assert(f.error->text().isEmpty());
  return BuildCommands(state, f, fromObjects);
}

namespace {

struct SpiralRow {
  QCheckBox* check = nullptr;
  QLineEdit* length = nullptr;
};

/// One "Spiral in" / "Spiral out" row of the grid: the checkbox, the length and the A value √(R·Ls) it gives.
SpiralRow AddSpiralRow(QDialog* dialog, QGridLayout* grid, int row, QLineEdit* radiusEdit, const QString& label,
                       double length, double radius, const char* objectName) {
  assert(dialog != nullptr && grid != nullptr && radiusEdit != nullptr);
  assert(radius > 0.0);
  SpiralRow r;
  r.check = new QCheckBox(label, dialog);
  r.check->setChecked(length > 0.0);
  r.length = new QLineEdit(Number(length > 0.0 ? length : radius), dialog);
  r.length->setObjectName(objectName);
  r.length->setEnabled(length > 0.0);
  auto* aValue = new QLineEdit(dialog);  // derived, so read-only, as Civil 3D shows it
  aValue->setReadOnly(true);
  aValue->setEnabled(false);
  aValue->setObjectName(QString(objectName) + "A");
  auto refresh = [aValue, edit = r.length, radiusEdit]() {
    const double a = std::sqrt(std::max(0.0, edit->text().toDouble() * radiusEdit->text().toDouble()));
    aValue->setText(QString::number(a, 'f', 3));
  };
  QObject::connect(r.check, &QCheckBox::toggled, r.length, &QWidget::setEnabled);
  QObject::connect(r.length, &QLineEdit::textChanged, aValue, refresh);
  QObject::connect(radiusEdit, &QLineEdit::textChanged, aValue, refresh);
  refresh();
  grid->addWidget(r.check, row, 0, Qt::AlignBottom);
  grid->addWidget(Captioned("Length:", r.length, dialog), row, 1);
  grid->addWidget(Captioned("A value:", aValue, dialog), row, 2);
  return r;
}

}  // namespace

QString RunCurveSpiralSettingsDialog(QWidget* parent, const AppCommandState& state, std::function<void()> help) {
  const alignment::CurveSettings& cs = state.alignmentCurveSettings;
  const double radius = EffectiveAlignmentCurveRadius(state);
  assert(radius > 0.0);
  QDialog dialog(parent);
  dialog.setObjectName("curveSpiralSettingsDialog");
  dialog.setWindowTitle("Curve and Spiral Settings");
  auto* column = new QVBoxLayout(&dialog);
  auto* type = new QComboBox(&dialog);
  type->addItem("Clothoid");
  type->setEnabled(false);  // the only spiral GoSurvey builds (REQ-481.3)
  column->addWidget(Captioned("Type:", type, &dialog));
  // One grid, so the spiral lengths and A values line up in columns as Civil 3D's do.
  auto* grid = new QGridLayout;
  grid->setColumnStretch(1, 1);
  grid->setColumnStretch(2, 1);
  column->addLayout(grid);
  auto* radiusEdit = new QLineEdit(Number(radius), &dialog);
  radiusEdit->setObjectName("curveSettingsRadius");
  const SpiralRow in = AddSpiralRow(&dialog, grid, 0, radiusEdit, "Spiral in", cs.spiralInLength, radius,
                                    "curveSettingsSpiralIn");
  auto* curve = new QCheckBox("Curve", &dialog);
  curve->setChecked(true);
  curve->setEnabled(false);  // a spiral-spiral without a curve is not built
  grid->addWidget(curve, 1, 0, Qt::AlignBottom);
  grid->addWidget(Captioned("Default radius (applies to curve and spirals)", radiusEdit, &dialog), 1, 1, 1, 2);
  const SpiralRow out = AddSpiralRow(&dialog, grid, 2, radiusEdit, "Spiral out", cs.spiralOutLength, radius,
                                     "curveSettingsSpiralOut");
  auto* rule = new QFrame(&dialog);
  rule->setFrameShape(QFrame::HLine);
  rule->setFrameShadow(QFrame::Sunken);
  column->addWidget(rule);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                           (help ? QDialogButtonBox::Help : QDialogButtonBox::NoButton),
                                       &dialog);
  if (help) QObject::connect(buttons, &QDialogButtonBox::helpRequested, &dialog, help);
  column->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return {};
  assert(in.check != nullptr && out.check != nullptr);
  return "ALIGNMENTCURVESETTINGS RADIUS=" + radiusEdit->text().trimmed() +
         ", SPIRALIN=" + (in.check->isChecked() ? in.length->text().trimmed() : QString("OFF")) +
         ", SPIRALOUT=" + (out.check->isChecked() ? out.length->text().trimmed() : QString("OFF"));
}

namespace {

/// Line-art icons for the layout tools, drawn in the palette's text colour like Civil 3D's toolbar glyphs.
QIcon ToolIcon(int which) {
  assert(which >= 0 && which <= 4);
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter p(&pixmap);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(qApp->palette().color(QPalette::WindowText), 2.0));
  QPainterPath path;
  if (which == 0) {  // Tangent-Tangent (No curves): two tangents meeting at a PI
    path.moveTo(4, 27);
    path.lineTo(16, 6);
    path.lineTo(28, 27);
  } else if (which == 1) {  // With curves: the PI rounded off
    path.moveTo(4, 27);
    path.lineTo(11, 14);
    path.quadTo(16, 5, 21, 14);
    path.lineTo(28, 27);
  } else if (which == 2) {  // Curve and Spiral Settings
    path.moveTo(5, 27);
    path.cubicTo(8, 8, 18, 5, 27, 5);
  } else if (which == 3) {  // Delete Sub-entity: a dashed line with a red cross
    p.setPen(QPen(qApp->palette().color(QPalette::WindowText), 2.0, Qt::DashLine));
    p.drawLine(4, 8, 20, 22);
    p.setPen(QPen(QColor(220, 50, 50), 2.5));
    p.drawLine(18, 18, 28, 28);
    p.drawLine(28, 18, 18, 28);
  } else {  // Alignment Grid View: a table
    p.drawRect(5, 6, 22, 20);
    p.drawLine(5, 13, 27, 13);
    p.drawLine(5, 20, 27, 20);
    p.drawLine(13, 6, 13, 26);
  }
  p.drawPath(path);
  p.end();
  assert(!pixmap.isNull());
  return QIcon(pixmap);
}

QToolButton* ToolButton(QWidget* parent, const QString& objectName, const QIcon& icon, const QString& tip) {
  assert(parent != nullptr);
  auto* b = new QToolButton(parent);
  b->setObjectName(objectName);
  b->setIcon(icon);
  b->setIconSize(QSize(22, 22));
  b->setToolTip(tip);
  b->setAutoRaise(true);
  assert(b->objectName() == objectName);
  return b;
}

QFrame* Separator(QWidget* parent) {
  assert(parent != nullptr);
  auto* line = new QFrame(parent);
  line->setFrameShape(QFrame::VLine);
  line->setFrameShadow(QFrame::Sunken);
  assert(line->frameShape() == QFrame::VLine);
  return line;
}

}  // namespace

AlignmentLayoutTools::AlignmentLayoutTools(AppCommandState& state, std::vector<std::string>& log,
                                           std::function<void(const QString&)> submit, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::WindowTitleHint | Qt::WindowCloseButtonHint), state_(state), log_(log),
      submit_(std::move(submit)) {
  assert(submit_);
  setObjectName("alignmentLayoutTools");
  auto* column = new QVBoxLayout(this);
  column->setContentsMargins(6, 4, 6, 4);
  auto* row = new QHBoxLayout;
  tangents_ = ToolButton(this, "layoutToolsTangents", ToolIcon(0), "Tangent-Tangent (No curves)");
  tangents_->setPopupMode(QToolButton::MenuButtonPopup);
  tangents_->setCheckable(true);
  auto* menu = new QMenu(tangents_);
  QAction* noCurves = menu->addAction(ToolIcon(0), "Tangent-Tangent (No curves)");
  QAction* withCurves = menu->addAction(ToolIcon(1), "Tangent-Tangent (With curves)");
  menu->addSeparator();
  QAction* settings = menu->addAction(ToolIcon(2), "Curve and Spiral Settings...");
  noCurves->setObjectName("layoutToolsNoCurves");
  withCurves->setObjectName("layoutToolsWithCurves");
  settings->setObjectName("layoutToolsSettings");
  tangents_->setMenu(menu);
  connect(noCurves, &QAction::triggered, this, [this] { StartLayout(false); });
  connect(withCurves, &QAction::triggered, this, [this] { StartLayout(true); });
  connect(settings, &QAction::triggered, this, [this] { EditSettings(); });
  connect(tangents_, &QToolButton::clicked, this, [this] { StartLayout(state_.alignmentCurveSettings.addCurves); });
  auto* remove = ToolButton(this, "layoutToolsDeleteSubEntity", ToolIcon(3), "Delete Sub-entity");
  auto* grid = ToolButton(this, "layoutToolsGridView", ToolIcon(4), "Alignment Grid View");
  auto* undo = ToolButton(this, "layoutToolsUndo", CadCommandIcon("undo.png"), "Undo");
  redo_ = ToolButton(this, "layoutToolsRedo", CadCommandIcon("redo.png"), "Redo");
  connect(remove, &QToolButton::clicked, this, [this] { DeleteSubEntity(); });
  connect(grid, &QToolButton::clicked, this, [this] { Run("EDITALIGNMENT " + Name()); });
  connect(undo, &QToolButton::clicked, this, [this] {
    if (state_.active == AppCommandState::Kind::AlignmentLayout) submit_("U");  // takes back one PI
    else Run("UNDO");
  });
  connect(redo_, &QToolButton::clicked, this, [this] { Run("REDO"); });
  for (QWidget* w : std::array<QWidget*, 8>{tangents_, Separator(this), remove, Separator(this), grid, Separator(this),
                                             undo, redo_})
    row->addWidget(w);
  row->addStretch(1);
  column->addLayout(row);
  auto* statusRow = new QHBoxLayout;
  status_ = new QLabel(this);
  status_->setObjectName("layoutToolsStatus");
  statusRow->addWidget(status_, 1);
  statusRow->addWidget(Separator(this));
  statusRow->addWidget(new QLabel("Spiral Type: Clothoid", this), 1);
  column->addLayout(statusRow);
  assert(status_ != nullptr);
}

QString AlignmentLayoutTools::Name() const {
  assert(submit_);
  const QString name = Q(state_.alignmentLayoutToolsName);
  assert(!name.contains('\n'));
  return name;
}

void AlignmentLayoutTools::Run(const QString& command) {
  assert(!command.isEmpty());
  if (state_.active != AppCommandState::Kind::None) CancelActiveCommand(state_, log_);
  assert(state_.active == AppCommandState::Kind::None);
  submit_(command);
}

void AlignmentLayoutTools::StartLayout(bool curves) {
  const QString name = Name();
  assert(!name.isEmpty());
  if (state_.active == AppCommandState::Kind::AlignmentLayout) submit_("");  // Enter finishes the current layout
  Run("ALIGNMENTLAYOUT " + name + (curves ? ", CURVES" : ", NOCURVES"));
  assert(name == Name());
}

void AlignmentLayoutTools::EditSettings() {
  const QString command = RunCurveSpiralSettingsDialog(this, state_, help_);
  assert(command.isEmpty() || command.startsWith("ALIGNMENTCURVESETTINGS"));
  if (command.isEmpty()) return;
  submit_(command);  // a settings change does not interrupt the layout; the next PI uses it
}

void AlignmentLayoutTools::DeleteSubEntity() {
  const alignment::Alignment* a = nullptr;
  for (const auto& candidate : state_.alignments)
    if (Q(candidate.name).compare(Name(), Qt::CaseInsensitive) == 0) a = &candidate;
  if (a == nullptr || a->Empty()) return;
  bool ok = false;
  const int count = static_cast<int>(a->Entities().size());
  assert(count > 0);
  const int n = QInputDialog::getInt(this, "Delete Sub-entity", "Entity number to delete (1 to " + QString::number(count) + "):",
                                     count, 1, count, 1, &ok);
  if (ok) Run("ALIGNMENTREMOVE " + Name() + ", " + QString::number(n));
}

void AlignmentLayoutTools::Sync() {
  const QString name = Name();
  bool exists = false;
  for (const auto& a : state_.alignments) exists = exists || Q(a.name).compare(name, Qt::CaseInsensitive) == 0;
  if (name.isEmpty() || !exists) {
    hide();
    return;
  }
  const bool laying = state_.active == AppCommandState::Kind::AlignmentLayout;
  used_ = used_ || laying;
  setWindowTitle("Alignment Layout Tools - " + name);
  const bool curves = state_.alignmentCurveSettings.addCurves;
  tangents_->setIcon(ToolIcon(curves ? 1 : 0));
  tangents_->setToolTip(curves ? "Tangent-Tangent (With curves)" : "Tangent-Tangent (No curves)");
  tangents_->setChecked(laying);  // the current tool shows pressed, as in Civil 3D
  status_->setText(!used_ ? QString("Select a command from the layout tools")
                          : curves ? QString("Tan-Tan (With curves)") : QString("Tan-Tan (No curves)"));
  redo_->setEnabled(!laying);
  if (isHidden()) show();
  assert(!windowTitle().isEmpty());
}

void AlignmentLayoutTools::closeEvent(QCloseEvent* event) {
  assert(event != nullptr);
  state_.alignmentLayoutToolsName.clear();
  QWidget::closeEvent(event);
  assert(state_.alignmentLayoutToolsName.empty());
}

void EditorWindow::ShowCreateAlignmentDialog(bool fromObjects) {
  m_ModalOpen = true;
  const CreateAlignmentCommands commands = RunCreateAlignmentDialog(this, m_State, fromObjects, [this, fromObjects] {
    ShowContextualHelp(fromObjects ? "createalignmentfromobjects" : "alignment");
  });
  m_ModalOpen = false;
  assert(commands.create.isEmpty() || !commands.create.contains('\n'));
  if (commands.create.isEmpty()) {  // cancelled: the picked objects are let go, as Civil 3D does
    if (fromObjects) m_State.selection.clear();
    Refresh();
    return;
  }
  const std::size_t count = m_State.alignments.size();
  Submit(commands.create);
  if (fromObjects || m_State.alignments.size() == count) return;  // refused: the command said why
  if (!commands.layout.isEmpty()) {
    Submit(commands.layout);
  } else {
    m_State.alignmentLayoutToolsName = commands.name.toUtf8().toStdString();
    Refresh();
  }
  assert(m_State.alignments.size() > count);
}

void EditorWindow::SyncAlignmentLayoutTools() {
  auto* tools = dynamic_cast<AlignmentLayoutTools*>(findChild<QWidget*>("alignmentLayoutTools"));
  if (tools == nullptr) {
    if (m_State.alignmentLayoutToolsName.empty()) return;
    tools = new AlignmentLayoutTools(m_State, m_Log, [this](const QString& command) { Submit(command); }, this);
    tools->SetHelp([this] { ShowContextualHelp("alignmentcurvesettings"); });
    tools->adjustSize();
    // Civil 3D opens the toolbar at the top left of the drawing, clear of the view label.
    QWidget* drawing = m_Viewport != nullptr ? static_cast<QWidget*>(m_Viewport) : static_cast<QWidget*>(this);
    tools->move(drawing->mapToGlobal(QPoint(16, 40)));
  }
  tools->Sync();
}
