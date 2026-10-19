// Qt Surface Style editor (issue #893, REQ-070 / REQ-072 / ADR-036 (d)).
//
// A style says how a surface is DRAWN, never what it is made of: nothing here touches a surface's
// definition. Edits show in the drawing as you make them. **OK keeps them as one undo step; Cancel
// (or closing the window) puts every style, and every surface's style name, back as it was** -- the
// 0.7.4 window had no way back from a live edit, and a Cancel that does not cancel is the one place
// this port deliberately differs (recorded in docs/qt-074-parity-ledger.md).
//
// The contour interval pair is validated as you type (REQ-070's specific message beside the fields)
// and reaches the style only while it is valid; OK is disabled while it is not.
#include "EditorWindow.hpp"
#include "CadSurfaceEdit.hpp"
#include "EditorSurfaceUi.hpp"
#include "EditorViewport.hpp"
#include "SurfaceStyle.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>

using surfaceui::Q;
using surfaceui::S;

namespace {

using ComponentGetter = std::function<SurfaceComponentStyle*()>;

struct ComponentWidgets {
  QCheckBox* visible = nullptr;
  QComboBox* color = nullptr;
  QComboBox* linetype = nullptr;
  QComboBox* weight = nullptr;
  ComponentGetter get;
};

}  // namespace

void EditorWindow::ShowSurfaceStyles(const QString& focusName, bool surfacesTitle) {
  if (findChild<QDialog*>("surfaceStyleDialog")) return;
  SurfaceStyles::EnsureStandard(m_State.surfaceStyles);
  const bool outerModal = m_ModalOpen;
  m_ModalOpen = true;

  const std::vector<SurfaceStyle> baseline = m_State.surfaceStyles;
  std::vector<std::string> baselineRefs;
  baselineRefs.reserve(m_State.cadSurfaces.size());
  for (const CadSurface& surface : m_State.cadSurfaces) baselineRefs.push_back(surface.styleName);
  bool dirty = false;
  int selected = 0;
  const char* unit = CadDrawingInsUnitsAbbrev(m_State.drawingInsUnits);  // issue #832
  const QString unitText = QString::fromUtf8(unit);

  QDialog dialog(this);
  dialog.setObjectName("surfaceStyleDialog");
  dialog.setWindowTitle(surfacesTitle ? "Surfaces" : "Surface Style");
  dialog.resize(1000, 600);
  auto* root = new QVBoxLayout(&dialog);
  surfaceui::AddBanner(root, &dialog, "Surface Style", "How a surface is drawn: contours, borders, triangles, points and analysis.");
  auto* body = new QHBoxLayout;
  root->addLayout(body, 1);

  // Left: the style list.
  auto* left = new QVBoxLayout;
  left->addWidget(new QLabel("Styles:", &dialog));
  auto* list = new QListWidget(&dialog);
  list->setObjectName("surfaceStyleList");
  list->setMinimumWidth(190);
  left->addWidget(list, 1);
  auto* newStyle = new QPushButton("New...", &dialog);
  newStyle->setObjectName("surfaceStyleNew");
  auto* deleteStyle = new QPushButton("Delete", &dialog);
  deleteStyle->setObjectName("surfaceStyleDelete");
  left->addWidget(newStyle);
  left->addWidget(deleteStyle);
  body->addLayout(left);

  auto* tabs = new QTabWidget(&dialog);
  tabs->setObjectName("surfaceStyleTabs");
  body->addWidget(tabs, 1);
  auto* status = new QLabel(&dialog);
  status->setObjectName("surfaceStyleStatus");
  status->setWordWrap(true);
  status->setStyleSheet("color:#c54646;");
  root->addWidget(status);

  auto* buttons = new QHBoxLayout;
  buttons->addStretch();
  auto* ok = new QPushButton("OK", &dialog);
  ok->setObjectName("surfaceStyleOk");
  ok->setProperty("primary", true);
  ok->setDefault(true);
  auto* cancel = new QPushButton("Cancel", &dialog);
  cancel->setObjectName("surfaceStyleCancel");
  buttons->addWidget(ok);
  buttons->addWidget(cancel);
  root->addLayout(buttons);
  surfaceui::StyleDialog(&dialog);

  const auto current = [&]() -> SurfaceStyle& { return m_State.surfaceStyles[static_cast<size_t>(selected)]; };
  std::vector<std::function<void()>> reloaders;  // widgets that mirror the selected style
  const auto reloadAll = [&] {
    for (const auto& reload : reloaders) reload();
  };
  const auto touch = [&] {
    dirty = true;
    BumpCadGpuCache(m_State);
    m_Viewport->update();
    reloadAll();
  };
  const auto usage = [&](const std::string& name) {
    return SurfaceStyles::CountSurfacesResolvingTo(m_State.cadSurfaces, m_State.surfaceStyles, name);
  };

  // --- component rows ---------------------------------------------------------------------------
  struct ComponentSpec {
    QString label;
    QString help;
    ComponentGetter get;
    QString key;
  };
  const auto makeComponentTable = [&](QWidget* parent, const std::vector<ComponentSpec>& specs) {
    auto* box = new QWidget(parent);
    auto* grid = new QGridLayout(box);
    grid->setContentsMargins(0, 0, 0, 0);
    const QStringList headers{"Component", "Color", "Linetype", "Lineweight"};
    for (int column = 0; column < headers.size(); ++column) {
      auto* header = new QLabel(QString("<b>%1</b>").arg(headers[column]), box);
      grid->addWidget(header, 0, column);
    }
    int row = 1;
    for (const ComponentSpec& spec : specs) {
      auto widgets = std::make_shared<ComponentWidgets>();
      widgets->get = spec.get;
      widgets->visible = new QCheckBox(spec.label, box);
      widgets->visible->setObjectName("ssc_" + spec.key + "_visible");
      if (!spec.help.isEmpty()) widgets->visible->setToolTip(spec.help);
      widgets->color = new QComboBox(box);
      widgets->color->setObjectName("ssc_" + spec.key + "_color");
      widgets->linetype = new QComboBox(box);
      widgets->linetype->setObjectName("ssc_" + spec.key + "_linetype");
      widgets->weight = new QComboBox(box);
      widgets->weight->setObjectName("ssc_" + spec.key + "_weight");
      grid->addWidget(widgets->visible, row, 0);
      grid->addWidget(widgets->color, row, 1);
      grid->addWidget(widgets->linetype, row, 2);
      grid->addWidget(widgets->weight, row, 3);
      ++row;
      QObject::connect(widgets->visible, &QCheckBox::toggled, box, [widgets, &touch](bool on) {
        widgets->get()->visible = on;
        touch();
      });
      QObject::connect(widgets->color, &QComboBox::activated, box, [widgets, &touch](int index) {
        widgets->get()->color = S(widgets->color->itemData(index).toString());
        touch();
      });
      QObject::connect(widgets->linetype, &QComboBox::activated, box, [widgets, &touch](int index) {
        widgets->get()->linetype = S(widgets->linetype->itemData(index).toString());
        touch();
      });
      QObject::connect(widgets->weight, &QComboBox::activated, box, [widgets, &touch](int index) {
        widgets->get()->lineweightMm = static_cast<float>(widgets->weight->itemData(index).toDouble());
        touch();
      });
      reloaders.push_back([widgets] {
        const SurfaceComponentStyle* c = widgets->get();
        const QSignalBlocker a(widgets->visible), b(widgets->color), d(widgets->linetype), e(widgets->weight);
        widgets->visible->setChecked(c->visible);
        surfaceui::FillColorCombo(widgets->color, c->color);
        surfaceui::FillLinetypeCombo(widgets->linetype, c->linetype);
        surfaceui::FillLineweightCombo(widgets->weight, c->lineweightMm);
      });
    }
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(3, 1);
    return box;
  };
  const auto componentPage = [&](const std::vector<ComponentSpec>& specs, const QString& note = {}) {
    auto* page = new QWidget(tabs);
    auto* layout = new QVBoxLayout(page);
    if (!note.isEmpty()) {
      auto* label = new QLabel(note, page);
      label->setWordWrap(true);
      layout->addWidget(label);
    }
    layout->addWidget(makeComponentTable(page, specs));
    layout->addStretch();
    return page;
  };
  const auto triangles = [&current] { return &current().triangles; };
  const auto border = [&current] { return &current().border; };
  const auto minorContour = [&current] { return &current().minorContour; };
  const auto majorContour = [&current] { return &current().majorContour; };
  const auto points = [&current] { return &current().points; };

  // --- Information ------------------------------------------------------------------------------
  auto* info = new QWidget(tabs);
  auto* infoLayout = new QVBoxLayout(info);
  auto* nameEdit = new QLineEdit(info);
  nameEdit->setObjectName("surfaceStyleName");
  auto* standardNote = new QLabel("Standard cannot be renamed.", info);
  standardNote->setObjectName("surfaceStyleStandardNote");
  auto* usageLabel = new QLabel(info);
  usageLabel->setObjectName("surfaceStyleUsage");
  auto* explanation = new QLabel(
      "Editing a style changes every surface using it. To change one surface only, open this editor from "
      "that surface's Edit... button in the Surfaces manager (it copies the style first when others still "
      "share it). Contours, the border and the triangle network are generated from the triangulation each "
      "time the style changes \xe2\x80\x94 they are never objects in the drawing, are never saved to the file, "
      "and never appear in a selection. Use EXTRACT to turn the displayed contours into real polylines.", info);
  explanation->setWordWrap(true);
  auto* infoForm = new QFormLayout;
  infoForm->addRow("Name", nameEdit);
  infoLayout->addLayout(infoForm);
  infoLayout->addWidget(standardNote);
  infoLayout->addWidget(usageLabel);
  infoLayout->addWidget(explanation);
  infoLayout->addStretch();
  tabs->addTab(info, "Information");

  // --- Contours ---------------------------------------------------------------------------------
  auto* contours = new QWidget(tabs);
  auto* contoursLayout = new QVBoxLayout(contours);
  auto* intervalBox = new QGroupBox("Interval", contours);
  auto* intervalForm = new QFormLayout(intervalBox);
  const auto makeInterval = [&](const char* name) {
    auto* spin = new QDoubleSpinBox(intervalBox);
    spin->setObjectName(name);
    spin->setDecimals(4);
    spin->setRange(-1.0e9, 1.0e9);
    return spin;
  };
  auto* minorInterval = makeInterval("surfaceStyleMinorInterval");
  auto* majorInterval = makeInterval("surfaceStyleMajorInterval");
  intervalForm->addRow(QString("Minor interval (%1)").arg(unitText), minorInterval);
  intervalForm->addRow(QString("Major interval (%1)").arg(unitText), majorInterval);
  auto* intervalProblem = new QLabel(intervalBox);
  intervalProblem->setObjectName("surfaceStyleIntervalProblem");
  intervalProblem->setWordWrap(true);
  intervalProblem->setStyleSheet("color:#c54646;");
  intervalForm->addRow(intervalProblem);
  contoursLayout->addWidget(intervalBox);
  auto* appearanceBox = new QGroupBox("Appearance", contours);
  auto* appearanceLayout = new QVBoxLayout(appearanceBox);
  appearanceLayout->addWidget(makeComponentTable(
      appearanceBox,
      {{"Minor contour", "Drawn at every whole multiple of the minor interval that is not also a major contour.", minorContour, "minor"},
       {"Major contour", "Drawn at every whole multiple of the major interval. This is the line a plan reader takes an elevation from, so it is usually the heavier one.", majorContour, "major"}}));
  contoursLayout->addWidget(appearanceBox);
  auto* smoothing = new QSpinBox(contours);
  smoothing->setObjectName("surfaceStyleSmoothPasses");
  smoothing->setRange(0, 5);
  smoothing->setToolTip("Chaikin corner-cutting on displayed contours only (0\xe2\x80\x93" "5). Does not change the TIN.");
  auto* labelSpacing = new QDoubleSpinBox(contours);
  labelSpacing->setObjectName("surfaceStyleLabelSpacing");
  labelSpacing->setDecimals(2);
  labelSpacing->setRange(0.0, 1.0e9);
  labelSpacing->setToolTip("Spacing along major contours. 0 turns labels off. Labels are overlay text, not entities.");
  auto* contourForm = new QFormLayout;
  contourForm->addRow("Smooth passes", smoothing);
  contourForm->addRow(QString("Label spacing (%1)").arg(unitText), labelSpacing);
  contoursLayout->addLayout(contourForm);
  contoursLayout->addStretch();
  tabs->addTab(contours, "Contours");

  tabs->addTab(componentPage({{"Triangles", "Every edge of the triangulation. Off by default \xe2\x80\x94 the triangulation is the model, the contours are the drawing.", triangles, "triangles"}}), "Triangles");
  tabs->addTab(componentPage({{"Border", "The outline of the triangulated area, including the rim of any hole a hide boundary leaves. Not the convex hull.", border, "border"}}), "Borders");
  tabs->addTab(componentPage({{"Points", "The surface's triangulation vertices. Off by default: the source points are already drawn as survey points.", points, "points"}}), "Points");

  // --- Analysis ---------------------------------------------------------------------------------
  auto* analysis = new QWidget(tabs);
  auto* analysisLayout = new QVBoxLayout(analysis);
  auto* bandingBox = new QGroupBox("Banding", analysis);
  auto* bandingLayout = new QVBoxLayout(bandingBox);
  auto* analysisType = new QComboBox(bandingBox);
  analysisType->setObjectName("surfaceStyleAnalysisType");
  analysisType->addItems({"None", "Elevation", "Slope", "Direction", "Slope angle"});
  analysisType->setToolTip(QString("Colours every triangle by None, Elevation (%1), Slope (%), Direction (deg), or Slope angle (deg), "
                                   "driven by the range table below. A triangle takes ONE colour \xe2\x80\x94 its centroid elevation, "
                                   "or its plane's grade or aspect \xe2\x80\x94 never a gradient across it.").arg(unitText));
  auto* typeForm = new QFormLayout;
  typeForm->addRow("Type", analysisType);
  bandingLayout->addLayout(typeForm);
  analysisLayout->addWidget(bandingBox);

  // A range table editor: rows of (upper bound, colour), kept strictly ascending after every edit --
  // the same repair GsIo applies to a hand-edited file on load -- so AssignBand's precondition holds.
  struct BandEditor {
    QWidget* box = nullptr;
    QTableWidget* table = nullptr;
    QLabel* note = nullptr;
    QPushButton* add = nullptr;
    std::function<std::vector<SurfaceBand>*()> get;
    std::function<QString()> header;
  };
  const auto makeBandEditor = [&](QWidget* parent, const QString& key, std::function<std::vector<SurfaceBand>*()> get,
                                  std::function<QString()> header) {
    auto editor = std::make_shared<BandEditor>();
    editor->get = std::move(get);
    editor->header = std::move(header);
    editor->box = new QWidget(parent);
    auto* layout = new QVBoxLayout(editor->box);
    layout->setContentsMargins(0, 0, 0, 0);
    editor->table = new QTableWidget(editor->box);
    editor->table->setObjectName("surfaceStyleBands_" + key);
    editor->table->setColumnCount(3);
    editor->table->verticalHeader()->setVisible(false);
    editor->table->horizontalHeader()->setStretchLastSection(true);
    editor->table->setSelectionMode(QAbstractItemView::NoSelection);
    editor->table->setMinimumHeight(150);
    editor->add = new QPushButton("Add band", editor->box);
    editor->add->setObjectName("surfaceStyleAddBand_" + key);
    editor->note = new QLabel(editor->box);
    editor->note->setWordWrap(true);
    layout->addWidget(editor->table);
    layout->addWidget(editor->add, 0, Qt::AlignLeft);
    layout->addWidget(editor->note);
    const auto sortAscending = [](std::vector<SurfaceBand>* bands) {
      std::sort(bands->begin(), bands->end(), [](const SurfaceBand& a, const SurfaceBand& b) { return a.upperBound < b.upperBound; });
    };
    QObject::connect(editor->add, &QPushButton::clicked, editor->box, [editor, sortAscending, &touch] {
      std::vector<SurfaceBand>* bands = editor->get();
      SurfaceBand band;
      band.upperBound = bands->empty() ? 0.0 : bands->back().upperBound + 1.0;
      bands->push_back(band);
      sortAscending(bands);
      touch();
    });
    reloaders.push_back([editor, key, sortAscending, &touch] {
      std::vector<SurfaceBand>* bands = editor->get();
      editor->table->setHorizontalHeaderLabels({editor->header(), "Color", ""});
      editor->table->setRowCount(static_cast<int>(bands->size()));
      for (int row = 0; row < static_cast<int>(bands->size()); ++row) {
        auto* bound = new QDoubleSpinBox(editor->table);
        bound->setObjectName(QString("surfaceStyleBound_%1_%2").arg(key).arg(row));
        bound->setDecimals(4);
        bound->setRange(-1.0e9, 1.0e9);
        bound->setValue((*bands)[static_cast<size_t>(row)].upperBound);
        editor->table->setCellWidget(row, 0, bound);
        auto* color = new QComboBox(editor->table);
        color->setObjectName(QString("surfaceStyleBandColor_%1_%2").arg(key).arg(row));
        surfaceui::FillColorCombo(color, (*bands)[static_cast<size_t>(row)].color);
        editor->table->setCellWidget(row, 1, color);
        auto* remove = new QPushButton("Remove", editor->table);
        remove->setObjectName(QString("surfaceStyleRemoveBand_%1_%2").arg(key).arg(row));
        editor->table->setCellWidget(row, 2, remove);
        QObject::connect(bound, &QDoubleSpinBox::editingFinished, editor->table, [editor, row, bound, sortAscending, &touch] {
          std::vector<SurfaceBand>* list = editor->get();
          if (row >= static_cast<int>(list->size()) || (*list)[static_cast<size_t>(row)].upperBound == bound->value()) return;
          (*list)[static_cast<size_t>(row)].upperBound = bound->value();
          sortAscending(list);
          touch();
        });
        QObject::connect(color, &QComboBox::activated, editor->table, [editor, row, color, &touch](int index) {
          std::vector<SurfaceBand>* list = editor->get();
          if (row >= static_cast<int>(list->size())) return;
          (*list)[static_cast<size_t>(row)].color = S(color->itemData(index).toString());
          touch();
        });
        QObject::connect(remove, &QPushButton::clicked, editor->table, [editor, row, &touch] {
          std::vector<SurfaceBand>* list = editor->get();
          if (row >= static_cast<int>(list->size())) return;
          list->erase(list->begin() + row);
          touch();
        });
      }
    });
    return editor;
  };
  const auto elevationBound = [unitText] { return QString("Upper bound (%1)").arg(unitText); };
  auto bandEditor = makeBandEditor(analysis, "bands", [&current] { return &current().bands; }, [&current, elevationBound] {
    switch (current().analysisMode) {
    case SurfaceAnalysisMode::Elevation: return elevationBound();
    case SurfaceAnalysisMode::Direction:
    case SurfaceAnalysisMode::SlopeAngle: return QString("Upper bound (deg)");
    default: return QString("Upper bound (%)");
    }
  });
  analysisLayout->addWidget(bandEditor->box);
  auto* arrowsBox = new QGroupBox("Slope Arrows", analysis);
  auto* arrowsLayout = new QVBoxLayout(arrowsBox);
  auto* arrowsOn = new QCheckBox("Show slope arrows", arrowsBox);
  arrowsOn->setObjectName("surfaceStyleSlopeArrows");
  arrowsOn->setToolTip("One arrow per triangle, pointing downhill, coloured by grade below. A triangle flatter than a 0.1% grade draws no arrow.");
  arrowsLayout->addWidget(arrowsOn);
  auto arrowEditor = makeBandEditor(arrowsBox, "arrows", [&current] { return &current().arrowBands; }, [] { return QString("Upper bound (%)"); });
  arrowsLayout->addWidget(arrowEditor->box);
  analysisLayout->addWidget(arrowsBox);
  analysisLayout->addStretch();
  tabs->addTab(analysis, "Analysis");

  // --- Display ----------------------------------------------------------------------------------
  tabs->addTab(componentPage({{"Triangles", {}, triangles, "display_triangles"},
                              {"Border", {}, border, "display_border"},
                              {"Minor contour", {}, minorContour, "display_minor"},
                              {"Major contour", {}, majorContour, "display_major"},
                              {"Points", {}, points, "display_points"}},
                             "Every component of this style, in one place. The per-tab pages edit the same values."),
               "Display");

  // --- Summary ----------------------------------------------------------------------------------
  auto* summary = new QWidget(tabs);
  auto* summaryLayout = new QVBoxLayout(summary);
  auto* summaryText = new QLabel(summary);
  summaryText->setObjectName("surfaceStyleSummary");
  summaryText->setWordWrap(true);
  summaryLayout->addWidget(summaryText);
  summaryLayout->addStretch();
  tabs->addTab(summary, "Summary");

  // --- wiring -----------------------------------------------------------------------------------
  bool intervalsOk = true;
  const auto checkIntervals = [&] {
    std::string why;
    intervalsOk = SurfaceStyles::IntervalsCompatible(minorInterval->value(), majorInterval->value(), &why, unit);
    intervalProblem->setText(intervalsOk ? QString() : Q(why));
    ok->setEnabled(intervalsOk);
    ok->setToolTip(intervalsOk ? QString() : Q(why));
  };
  const auto fillList = [&] {
    const QSignalBlocker blocker(list);
    list->clear();
    for (const SurfaceStyle& style : m_State.surfaceStyles) {
      list->addItem(Q(style.name));
      QListWidgetItem* item = list->item(list->count() - 1);
      const int used = usage(style.name);
      if (used > 0) item->setToolTip(QString("%1 surface(s) draw with this style.").arg(used));
    }
    list->setCurrentRow(selected);
  };
  reloaders.push_back([&] {
    const SurfaceStyle& style = current();
    const bool standard = style.name == SurfaceStyles::kStandardName;
    {
      const QSignalBlocker a(nameEdit), b(minorInterval), c(majorInterval), d(smoothing), e(labelSpacing), f(analysisType), g(arrowsOn);
      nameEdit->setText(Q(style.name));
      minorInterval->setValue(style.minorIntervalFt);
      majorInterval->setValue(style.majorIntervalFt);
      smoothing->setValue(style.contourSmoothPasses);
      labelSpacing->setValue(style.contourLabelSpacingFt);
      analysisType->setCurrentIndex(static_cast<int>(style.analysisMode));
      arrowsOn->setChecked(style.slopeArrowsOn);
    }
    nameEdit->setEnabled(!standard);
    standardNote->setVisible(standard);
    usageLabel->setText(QString("%1 surface(s) draw with this style.").arg(usage(style.name)));
    bandEditor->box->setVisible(style.analysisMode != SurfaceAnalysisMode::None);
    bandEditor->note->setText(style.bands.empty() ? "No bands: every triangle draws in the plain Triangles colour."
                                                  : "A value above the last bound draws in the plain Triangles colour, not the top band.");
    arrowEditor->box->setVisible(style.slopeArrowsOn);
    arrowEditor->note->setText(style.arrowBands.empty() ? "No bands: every arrow draws in the surface's own colour." : QString());
    deleteStyle->setEnabled(!standard);
    deleteStyle->setToolTip(standard ? "Standard cannot be deleted \xe2\x80\x94 it is what an unresolved style name falls back to."
                                     : usage(style.name) > 0
                                           ? "Surfaces using this style will fall back to Standard. They keep the name, so re-creating a style with that name adopts them back."
                                           : QString());
    const auto shown = [](const SurfaceComponentStyle& c) { return QString(c.visible ? "yes" : "no"); };
    summaryText->setText(
        QString("Style: %1<br>Contour interval: minor %2 ft, major %3 ft<br><br>Triangles: %4<br>Border: %5<br>Minor contour: %6<br>"
                "Major contour: %7<br>Points: %8<br><br>%9 surface(s) draw with this style.")
            .arg(Q(style.name).toHtmlEscaped(), Q(SurfaceStyles::FormatFt(style.minorIntervalFt)), Q(SurfaceStyles::FormatFt(style.majorIntervalFt)),
                 shown(style.triangles), shown(style.border), shown(style.minorContour), shown(style.majorContour), shown(style.points))
            .arg(usage(style.name)));
    checkIntervals();
  });

  QObject::connect(list, &QListWidget::currentRowChanged, &dialog, [&](int row) {
    if (row < 0 || row >= static_cast<int>(m_State.surfaceStyles.size())) return;
    selected = row;
    status->clear();
    reloadAll();  // the interval fields are re-seeded from the style: an unfinished pair is dropped
  });
  const auto renameSelected = [&] {
    const std::string wanted = S(nameEdit->text());
    if (wanted == current().name) return;
    if (surfedit::RenameStyle(m_State, static_cast<size_t>(selected), wanted, m_Log, /*recordUndo=*/false)) {
      status->clear();
      dirty = true;
      fillList();
      reloadAll();
    } else {
      status->setText(Q(m_Log.empty() ? std::string() : m_Log.back()));
      const QSignalBlocker blocker(nameEdit);
      nameEdit->setText(Q(current().name));
    }
  };
  QObject::connect(nameEdit, &QLineEdit::editingFinished, &dialog, renameSelected);
  const auto intervalsChanged = [&] {
    checkIntervals();
    if (!intervalsOk) return;
    SurfaceStyle& style = current();
    if (style.minorIntervalFt == minorInterval->value() && style.majorIntervalFt == majorInterval->value()) return;
    surfedit::SetStyleIntervals(m_State, static_cast<size_t>(selected), minorInterval->value(), majorInterval->value(), false);
    dirty = true;
    m_Viewport->update();
  };
  QObject::connect(minorInterval, &QDoubleSpinBox::valueChanged, &dialog, [&](double) { intervalsChanged(); });
  QObject::connect(majorInterval, &QDoubleSpinBox::valueChanged, &dialog, [&](double) { intervalsChanged(); });
  QObject::connect(smoothing, &QSpinBox::valueChanged, &dialog, [&](int value) {
    current().contourSmoothPasses = value;
    touch();
  });
  QObject::connect(labelSpacing, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) {
    current().contourLabelSpacingFt = std::max(0.0, value);
    touch();
  });
  QObject::connect(analysisType, &QComboBox::activated, &dialog, [&](int index) {
    current().analysisMode = static_cast<SurfaceAnalysisMode>(index);
    touch();
  });
  QObject::connect(arrowsOn, &QCheckBox::toggled, &dialog, [&](bool on) {
    current().slopeArrowsOn = on;
    touch();
  });
  QObject::connect(newStyle, &QPushButton::clicked, &dialog, [&] {
    QDialog entry(&dialog);
    entry.setObjectName("newSurfaceStyleDialog");
    entry.setWindowTitle("New Surface Style");
    auto* layout = new QVBoxLayout(&entry);
    layout->addWidget(new QLabel("Style name:", &entry));
    auto* name = new QLineEdit("Surface Style 1", &entry);
    name->setObjectName("newSurfaceStyleName");
    layout->addWidget(name);
    auto* duplicate = new QLabel(&entry);
    duplicate->setObjectName("newSurfaceStyleDuplicate");
    duplicate->setStyleSheet("color:#c54646;");
    layout->addWidget(duplicate);
    layout->addWidget(new QLabel("Copied from the selected style.", &entry));
    auto* row = new QHBoxLayout;
    row->addStretch();
    auto* accept = new QPushButton("OK", &entry);
    accept->setObjectName("newSurfaceStyleOk");
    auto* reject = new QPushButton("Cancel", &entry);
    reject->setObjectName("newSurfaceStyleCancel");
    row->addWidget(accept);
    row->addWidget(reject);
    layout->addLayout(row);
    const auto validate = [&] {
      const std::string trimmed = surfedit::TrimStyleName(S(name->text()));
      const bool taken = !trimmed.empty() && SurfaceStyles::Find(m_State.surfaceStyles, trimmed) != nullptr;
      duplicate->setText(taken ? "A style with that name already exists." : QString());
      accept->setEnabled(!trimmed.empty() && !taken);
    };
    QObject::connect(name, &QLineEdit::textChanged, &entry, [&](const QString&) { validate(); });
    QObject::connect(accept, &QPushButton::clicked, &entry, &QDialog::accept);
    QObject::connect(reject, &QPushButton::clicked, &entry, &QDialog::reject);
    validate();
    if (entry.exec() != QDialog::Accepted) return;
    if (surfedit::AddStyle(m_State, static_cast<size_t>(selected), S(name->text()), m_Log, false)) {
      dirty = true;
      selected = static_cast<int>(m_State.surfaceStyles.size()) - 1;
      fillList();
      reloadAll();
    }
  });
  QObject::connect(deleteStyle, &QPushButton::clicked, &dialog, [&] {
    if (!surfedit::DeleteStyle(m_State, static_cast<size_t>(selected), m_Log, false)) return;
    dirty = true;
    selected = std::min(selected, static_cast<int>(m_State.surfaceStyles.size()) - 1);
    BumpCadGpuCache(m_State);
    fillList();
    reloadAll();
  });

  const auto restoreBaseline = [&] {
    m_State.surfaceStyles = baseline;
    for (size_t i = 0; i < baselineRefs.size() && i < m_State.cadSurfaces.size(); ++i) m_State.cadSurfaces[i].styleName = baselineRefs[i];
    BumpCadGpuCache(m_State);
  };
  QObject::connect(ok, &QPushButton::clicked, &dialog, [&] {
    if (!intervalsOk) return;
    if (dirty) {
      // One undo step for the whole session: rewind to the state the window opened with, snapshot
      // it, then put the edited styles back.
      const std::vector<SurfaceStyle> edited = m_State.surfaceStyles;
      std::vector<std::string> editedRefs;
      editedRefs.reserve(m_State.cadSurfaces.size());
      for (const CadSurface& surface : m_State.cadSurfaces) editedRefs.push_back(surface.styleName);
      restoreBaseline();
      PushUndoSnapshot(m_State, "Edit surface styles");
      m_State.surfaceStyles = edited;
      for (size_t i = 0; i < editedRefs.size() && i < m_State.cadSurfaces.size(); ++i) m_State.cadSurfaces[i].styleName = editedRefs[i];
      BumpCadGpuCache(m_State);
    }
    dialog.accept();
  });
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  QObject::connect(&dialog, &QDialog::rejected, &dialog, [&] {
    if (dirty) restoreBaseline();
    m_Viewport->update();
  });

  for (int i = 0; i < static_cast<int>(m_State.surfaceStyles.size()); ++i)
    if (Q(m_State.surfaceStyles[static_cast<size_t>(i)].name) == focusName) selected = i;
  fillList();
  reloadAll();
  dialog.exec();
  m_ModalOpen = outerModal;
  Refresh();
}
