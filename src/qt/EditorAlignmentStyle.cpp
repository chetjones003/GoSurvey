#include "EditorAlignmentStyle.hpp"

#include "AlignmentStyle.hpp"
#include "CadCommands.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cstdio>

namespace {

QString Q(const std::string& s) { return QString::fromUtf8(s.c_str()); }

/// Runs one typed ALIGNMENTSTYLE command and returns what it reported.
QString Run(AppCommandState& state, std::vector<std::string>& log, const QString& command) {
  std::vector<std::string> out;
  const std::string text = command.toUtf8().toStdString();
  char buf[512];
  std::snprintf(buf, sizeof buf, "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof buf, state, out);
  QString message;
  for (const std::string& line : out) {
    log.push_back(line);
    message += Q(line) + "\n";
  }
  return message.trimmed();
}

}  // namespace

void ShowAlignmentStyleDialog(QWidget* parent, AppCommandState& state, std::vector<std::string>& log,
                              const std::string& initialName) {
  QDialog dialog(parent);
  dialog.setObjectName("alignmentStyleDialog");
  dialog.setWindowTitle("Alignment Style");
  dialog.resize(640, 520);
  auto* root = new QVBoxLayout(&dialog);

  // --- style picker and the New / Delete actions ---
  auto* top = new QHBoxLayout;
  top->addWidget(new QLabel("Style:", &dialog));
  auto* chooser = new QComboBox(&dialog);
  chooser->setObjectName("alignmentStyleChooser");
  top->addWidget(chooser, 1);
  auto* create = new QPushButton("New", &dialog);
  create->setObjectName("alignmentStyleNew");
  auto* remove = new QPushButton("Delete", &dialog);
  remove->setObjectName("alignmentStyleDelete");
  top->addWidget(create);
  top->addWidget(remove);
  root->addLayout(top);

  QTabWidget* tabs = new QTabWidget(&dialog);
  tabs->setObjectName("alignmentStyleTabs");
  root->addWidget(tabs, 1);

  // Information: what the style is and what uses it.
  auto* info = new QWidget(&dialog);
  auto* infoForm = new QFormLayout(info);
  auto* infoName = new QLabel(info);
  infoName->setObjectName("alignmentStyleInfoName");
  auto* infoUsers = new QLabel(info);
  infoUsers->setObjectName("alignmentStyleInfoUsers");
  infoForm->addRow("Name", infoName);
  infoForm->addRow("Used by", infoUsers);
  infoForm->addRow(new QLabel("Changes apply to every alignment that uses this style. Per-alignment overrides stay local.", info));
  tabs->addTab(info, "Information");

  // Design: placeholder until REQ-502.
  auto* design = new QWidget(&dialog);
  auto* designLayout = new QVBoxLayout(design);
  auto* designNote = new QLabel("Design checks are not configured yet. They arrive with design criteria (REQ-502).", design);
  designNote->setWordWrap(true);
  designLayout->addWidget(designNote);
  designLayout->addStretch(1);
  tabs->addTab(design, "Design");

  // Markers: geometry point markers, station ticks.
  auto* markers = new QWidget(&dialog);
  auto* markersForm = new QFormLayout(markers);
  auto* markerShow = new QCheckBox("Show geometry point markers (TS, SC, PC, PT, PI)", markers);
  markerShow->setObjectName("alignmentStyleMarkers");
  auto* markerSize = new QDoubleSpinBox(markers);
  markerSize->setObjectName("alignmentStyleMarkerSize");
  markerSize->setRange(0.0, 1000.0);
  markerSize->setDecimals(2);
  auto* tickShow = new QCheckBox("Show station ticks", markers);
  tickShow->setObjectName("alignmentStyleTicks");
  auto* tickSide = new QComboBox(markers);
  tickSide->setObjectName("alignmentStyleTickSide");
  tickSide->addItems({"Both sides", "Left", "Right"});
  auto* majorTick = new QDoubleSpinBox(markers);
  majorTick->setObjectName("alignmentStyleMajorTick");
  majorTick->setRange(0.0, 1000.0);
  majorTick->setDecimals(2);
  auto* minorTick = new QDoubleSpinBox(markers);
  minorTick->setObjectName("alignmentStyleMinorTick");
  minorTick->setRange(0.0, 1000.0);
  minorTick->setDecimals(2);
  markersForm->addRow(markerShow);
  markersForm->addRow("Marker half-size", markerSize);
  markersForm->addRow(tickShow);
  markersForm->addRow("Tick side", tickSide);
  markersForm->addRow("Major tick length", majorTick);
  markersForm->addRow("Minor tick length", minorTick);
  tabs->addTab(markers, "Markers");

  // Display: component visibility and colour, arrows, label visibility.
  auto* display = new QWidget(&dialog);
  auto* displayLayout = new QVBoxLayout(display);
  auto* componentForm = new QFormLayout;
  struct ComponentRow {
    const char* word;   // command word: tangent, curve, spiral
    QCheckBox* show;
    QLineEdit* color;
  };
  std::vector<ComponentRow> components;
  for (const char* word : {"tangent", "curve", "spiral"}) {
    auto* row = new QWidget(display);
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    auto* show = new QCheckBox(QString::fromUtf8(word).left(1).toUpper() + QString::fromUtf8(word).mid(1), row);
    show->setObjectName(QString("alignmentStyle%1Show").arg(QString::fromUtf8(word)));
    auto* color = new QLineEdit(row);
    color->setObjectName(QString("alignmentStyle%1Color").arg(QString::fromUtf8(word)));
    color->setPlaceholderText("ByLayer or #RRGGBB");
    auto* apply = new QPushButton("Apply colour", row);
    apply->setObjectName(QString("alignmentStyle%1ApplyColor").arg(QString::fromUtf8(word)));
    hl->addWidget(show);
    hl->addWidget(color, 1);
    hl->addWidget(apply);
    componentForm->addRow(row);
    components.push_back({word, show, color});
    QObject::connect(apply, &QPushButton::clicked, &dialog, [&state, &log, &dialog, word, color, &chooser]() {
      const std::string name = chooser->currentText().toUtf8().toStdString();
      (void)dialog;
      const QString text = color->text().trimmed();
      if (text.isEmpty()) return;
      std::vector<std::string> out;
      const QString command = QString("ALIGNMENTSTYLE COLOR, %1, %2, %3").arg(Q(name)).arg(QString::fromUtf8(word)).arg(text);
      Run(state, out, command);
      for (const std::string& line : out) log.push_back(line);
    });
  }
  displayLayout->addLayout(componentForm);
  auto* arrowShow = new QCheckBox("Show direction arrow at the middle of the alignment", display);
  arrowShow->setObjectName("alignmentStyleArrows");
  auto* arrowLength = new QDoubleSpinBox(display);
  arrowLength->setObjectName("alignmentStyleArrowLength");
  arrowLength->setRange(0.0, 1000.0);
  arrowLength->setDecimals(2);
  auto* labelShow = new QCheckBox("Show station and equation labels", display);
  labelShow->setObjectName("alignmentStyleLabels");
  displayLayout->addWidget(arrowShow);
  auto* arrowForm = new QFormLayout;
  arrowForm->addRow("Arrow length", arrowLength);
  displayLayout->addLayout(arrowForm);
  displayLayout->addWidget(labelShow);
  displayLayout->addStretch(1);
  tabs->addTab(display, "Display");

  // Summary: everything the style holds, read-only.
  auto* summary = new QWidget(&dialog);
  auto* summaryLayout = new QVBoxLayout(summary);
  auto* summaryText = new QLabel(summary);
  summaryText->setObjectName("alignmentStyleSummary");
  summaryText->setWordWrap(true);
  summaryText->setTextInteractionFlags(Qt::TextSelectableByMouse);
  summaryLayout->addWidget(summaryText);
  summaryLayout->addStretch(1);
  tabs->addTab(summary, "Summary");

  auto* status = new QLabel(&dialog);
  status->setObjectName("alignmentStyleStatus");
  status->setWordWrap(true);
  root->addWidget(status);
  auto* buttons = new QHBoxLayout;
  buttons->addStretch(1);
  auto* close = new QPushButton("Close", &dialog);
  buttons->addWidget(close);
  root->addLayout(buttons);
  QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);

  const auto current = [&]() -> const AlignmentStyle* {
    return AlignmentStyles::Find(state.alignmentStyles, chooser->currentText().toUtf8().toStdString());
  };

  // Reads the style back into every control. Called after each command, so the controls always show the real values.
  const auto populate = [&]() {
    const AlignmentStyle* s = current();
    const bool any = s != nullptr;
    for (QWidget* w : {static_cast<QWidget*>(markers), static_cast<QWidget*>(display)}) w->setEnabled(any);
    if (!any) {
      infoName->setText("(no style)");
      infoUsers->setText("0");
      summaryText->setText("No alignment styles.");
      return;
    }
    QSignalBlocker b1(markerShow), b2(markerSize), b3(tickShow), b4(tickSide), b5(majorTick), b6(minorTick),
        b7(arrowShow), b8(arrowLength), b9(labelShow);
    infoName->setText(Q(s->name));
    infoUsers->setText(QString::number(AlignmentStyles::CountAlignmentsResolvingTo(state.alignments, state.alignmentStyles, s->name)));
    markerShow->setChecked(s->markers.visible);
    markerSize->setValue(s->markerHalfSize);
    tickShow->setChecked(s->ticks.visible);
    tickSide->setCurrentIndex(s->tickSide);
    majorTick->setValue(s->majorTickLength);
    minorTick->setValue(s->minorTickLength);
    arrowShow->setChecked(s->arrows.visible);
    arrowLength->setValue(s->arrowLength);
    labelShow->setChecked(s->labelsVisible);
    for (const ComponentRow& row : components) {
      QSignalBlocker block(row.show);
      const SurfaceComponentStyle* comp = std::string(row.word) == "tangent" ? &s->tangent
                                          : std::string(row.word) == "curve" ? &s->curve : &s->spiral;
      row.show->setChecked(comp->visible);
      row.color->setText(Q(comp->color));
    }
    const QString summaryLines =
        QString("Style: %1\nTicks: %2 (major %3, minor %4, side %5)\nMarkers: %6 (half-size %7)\n"
                "Arrows: %8 (length %9)\nLabels: %10\nTangent: %11, colour %12\nCurve: %13, colour %14\n"
                "Spiral: %15, colour %16\nUsed by %17 alignment(s)")
            .arg(Q(s->name)).arg(s->ticks.visible ? "on" : "off").arg(s->majorTickLength).arg(s->minorTickLength)
            .arg(s->tickSide == 0 ? "both" : s->tickSide == 1 ? "left" : "right")
            .arg(s->markers.visible ? "on" : "off").arg(s->markerHalfSize)
            .arg(s->arrows.visible ? "on" : "off").arg(s->arrowLength)
            .arg(s->labelsVisible ? "shown" : "hidden")
            .arg(s->tangent.visible ? "shown" : "hidden").arg(Q(s->tangent.color))
            .arg(s->curve.visible ? "shown" : "hidden").arg(Q(s->curve.color))
            .arg(s->spiral.visible ? "shown" : "hidden").arg(Q(s->spiral.color))
            .arg(AlignmentStyles::CountAlignmentsResolvingTo(state.alignments, state.alignmentStyles, s->name));
    summaryText->setText(summaryLines);
    infoUsers->setText(QString::number(AlignmentStyles::CountAlignmentsResolvingTo(state.alignments, state.alignmentStyles, s->name)));
  };

  const auto fillChooser = [&](const std::string& select) {
    QSignalBlocker block(chooser);
    chooser->clear();
    for (const AlignmentStyle& s : state.alignmentStyles) chooser->addItem(Q(s.name));
    const int ix = chooser->findText(Q(select), Qt::MatchFixedString);
    if (ix >= 0) chooser->setCurrentIndex(ix);
    else if (chooser->count() > 0) chooser->setCurrentIndex(0);
  };

  // Each control sends the matching SET / SHOW / HIDE command, then the dialog reads the result back.
  const auto setItem = [&](const char* item, const QString& value) {
    const QString name = chooser->currentText();
    status->setText(Run(state, log, QString("ALIGNMENTSTYLE SET, %1, %2, %3").arg(name).arg(QString::fromUtf8(item)).arg(value)));
    populate();
  };
  const auto setFlag = [&](const char* item, bool on) { setItem(item, on ? "ON" : "OFF"); };
  QObject::connect(markerShow, &QCheckBox::toggled, &dialog, [=](bool on) { setFlag("MARKERS", on); });
  QObject::connect(tickShow, &QCheckBox::toggled, &dialog, [=](bool on) { setFlag("TICKS", on); });
  QObject::connect(arrowShow, &QCheckBox::toggled, &dialog, [=](bool on) { setFlag("ARROWS", on); });
  QObject::connect(labelShow, &QCheckBox::toggled, &dialog, [=](bool on) { setFlag("LABELS", on); });
  QObject::connect(markerSize, &QDoubleSpinBox::editingFinished, &dialog, [=]() { setItem("MARKERSIZE", QString::number(markerSize->value(), 'f', 4)); });
  QObject::connect(majorTick, &QDoubleSpinBox::editingFinished, &dialog, [=]() { setItem("MAJORTICK", QString::number(majorTick->value(), 'f', 4)); });
  QObject::connect(minorTick, &QDoubleSpinBox::editingFinished, &dialog, [=]() { setItem("MINORTICK", QString::number(minorTick->value(), 'f', 4)); });
  QObject::connect(arrowLength, &QDoubleSpinBox::editingFinished, &dialog, [=]() { setItem("ARROWLENGTH", QString::number(arrowLength->value(), 'f', 4)); });
  QObject::connect(tickSide, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [=](int ix) {
    setItem("TICKSIDE", ix == 0 ? "BOTH" : ix == 1 ? "LEFT" : "RIGHT");
  });
  for (const ComponentRow& row : components) {
    QObject::connect(row.show, &QCheckBox::toggled, &dialog, [&, row](bool on) {
      const QString name = chooser->currentText();
      status->setText(Run(state, log, QString("ALIGNMENTSTYLE %1, %2, %3").arg(on ? "SHOW" : "HIDE").arg(name).arg(QString::fromUtf8(row.word))));
      populate();
    });
  }
  QObject::connect(chooser, &QComboBox::currentIndexChanged, &dialog, [=]() { status->clear(); populate(); });

  QObject::connect(create, &QPushButton::clicked, &dialog, [&]() {
    bool ok = false;
    const QString name = QInputDialog::getText(&dialog, "New Alignment Style", "Style name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    status->setText(Run(state, log, QString("ALIGNMENTSTYLE NEW, %1").arg(name.trimmed())));
    fillChooser(name.trimmed().toUtf8().toStdString());
    populate();
  });
  QObject::connect(remove, &QPushButton::clicked, &dialog, [&]() {
    const QString name = chooser->currentText();
    status->setText(Run(state, log, QString("ALIGNMENTSTYLE DELETE, %1").arg(name)));
    fillChooser(AlignmentStyles::kStandardName);
    populate();
  });

  fillChooser(initialName);
  populate();
  dialog.exec();
}
