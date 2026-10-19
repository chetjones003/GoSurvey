// REQ-430 — the Qt PDF window's scale workflows: Set scale, Calibrate, the Check tool, the Scale checks dialog
// (corrections, robust calibration, report) and Snap. Every number comes from the shared pdfview:: services
// (PdfMeasure, PdfScaleCheck, PdfSnap, AnnotSession, PdfDocument::SnapPoints); this file presents them.
#include "PdfWindow.hpp"
#include "PdfCanvas.hpp"
#include "PdfScaleCheck.hpp"
#include "PdfSplit.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDebug>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>

using namespace pdfview;

namespace {

QString Q(const std::string& text) { return QString::fromUtf8(text.c_str()); }

// Dialog text colours on the deep navy surface.
QString BadText() { return "#ff7b72"; }
QString WarnText() { return "#f5b73a"; }
QString GoodText() { return "#5fe08a"; }
// The colour of a verdict on the white sheet.
QColor SheetColor(Verdict verdict) {
  return verdict == Verdict::Good ? QColor("#2e9e4d") : verdict == Verdict::Check ? QColor("#c98a0a") : QColor("#d23a32");
}
// The colour of a verdict in a dialog (brighter on the dark one).
QColor VerdictColor(Verdict verdict) {
  return verdict == Verdict::Good ? QColor(GoodText()) : verdict == Verdict::Check ? QColor(WarnText()) : QColor(BadText());
}
// The button behind "Scale checks (n)": the worst verdict on the page, blue when there are no checks yet.
QString VerdictButtonStyle(bool haveVerdict, Verdict worst) {
  const char* base = !haveVerdict ? "#3373c7" : worst == Verdict::Good ? "#2b8c47" : worst == Verdict::Check ? "#bd8014" : "#bd3833";
  return QString("QPushButton{background:%1;color:white;font-weight:600;border:1px solid rgba(255,255,255,140);"
                 "border-radius:4px;padding:3px 10px;} QPushButton:disabled{background:#59626f;color:#c5ccd6;}").arg(base);
}

std::string Signed(double value, int decimals) { return (value >= 0.0 ? "+" : "-") + FormatValue(std::fabs(value), decimals); }

double MetresPerPt(const PageScale& scale) { return scale.RealPerPoint() * UnitInMetres(scale.realUnit); }

QLabel* ErrorLabel(QWidget* parent) {
  auto* label = new QLabel(parent);
  label->setObjectName("pdfScaleError");
  label->setWordWrap(true);
  label->setStyleSheet("color:" + BadText() + ";");
  label->hide();
  return label;
}
void ShowError(QLabel* label, const QString& text) { label->setText(text); label->setVisible(!text.isEmpty()); }

QComboBox* UnitCombo(QWidget* parent, const char* name, int index) {
  auto* combo = new QComboBox(parent);
  combo->setObjectName(name);
  for (Unit unit : AllUnits()) combo->addItem(UnitName(unit));
  combo->setCurrentIndex(std::clamp(index, 0, combo->count() - 1));
  return combo;
}

QDoubleSpinBox* LengthSpin(QWidget* parent, const char* name, double value) {
  auto* spin = new QDoubleSpinBox(parent);
  spin->setObjectName(name);
  spin->setDecimals(5);
  spin->setRange(0.0, 1.0e9);
  spin->setValue(value);
  spin->setKeyboardTracking(false);
  return spin;
}

// "Apply to: This page / Every page / Pages: [1-5, 9]" - shared by the Set scale dialog and the Known distance box.
struct ScopeChooser {
  QRadioButton* thisPage;
  QRadioButton* everyPage;
  QRadioButton* pages;
  QLineEdit* text;
};
ScopeChooser AddScopeChooser(QDialog* dialog, QVBoxLayout* layout, const PdfWindow::ScaleForm& form) {
  auto* row = new QHBoxLayout();
  row->addWidget(new QLabel("Apply to:", dialog));
  ScopeChooser chooser;
  chooser.thisPage = new QRadioButton("This page", dialog);
  chooser.thisPage->setObjectName("pdfScopeThis");
  chooser.everyPage = new QRadioButton("Every page", dialog);
  chooser.everyPage->setObjectName("pdfScopeAll");
  chooser.pages = new QRadioButton("Pages:", dialog);
  chooser.pages->setObjectName("pdfScopePages");
  chooser.text = new QLineEdit(form.scopeText, dialog);
  chooser.text->setObjectName("pdfScopeText");
  chooser.text->setPlaceholderText("1-5, 9, 12-20");
  chooser.text->setEnabled(form.scope == 2);
  auto* group = new QButtonGroup(dialog); // radios sharing a parent exclude each other: keep this set apart from the dialog's others
  for (QRadioButton* radio : {chooser.thisPage, chooser.everyPage, chooser.pages}) group->addButton(radio);
  (form.scope == 1 ? chooser.everyPage : form.scope == 2 ? chooser.pages : chooser.thisPage)->setChecked(true);
  QObject::connect(chooser.pages, &QRadioButton::toggled, chooser.text, &QLineEdit::setEnabled);
  for (QWidget* widget : std::initializer_list<QWidget*>{chooser.thisPage, chooser.everyPage, chooser.pages, chooser.text})
    row->addWidget(widget);
  layout->addLayout(row);
  return chooser;
}
void ReadScope(const ScopeChooser& chooser, PdfWindow::ScaleForm& form) {
  form.scope = chooser.everyPage->isChecked() ? 1 : chooser.pages->isChecked() ? 2 : 0;
  form.scopeText = chooser.text->text();
}

QTableWidgetItem* Cell(const QString& text, Qt::Alignment alignment = Qt::AlignRight | Qt::AlignVCenter, const QColor* color = nullptr) {
  auto* item = new QTableWidgetItem(text);
  item->setTextAlignment(alignment);
  item->setFlags(item->flags() & ~Qt::ItemIsEditable);
  if (color) item->setForeground(*color);
  return item;
}

QTableWidget* MakeTable(QWidget* parent, const char* name, const QStringList& headers) {
  auto* table = new QTableWidget(0, headers.size() + 1, parent); // the extra, empty last column takes the spare width
  table->setObjectName(name);
  table->setHorizontalHeaderLabels(QStringList(headers) << QString());
  table->setAlternatingRowColors(true);
  table->setShowGrid(false);
  table->verticalHeader()->hide();
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(headers.size(), QHeaderView::Stretch);
  return table;
}

// The look of the scale dialogs: the PDF window's deep navy (issue #906, selected design B).
QString DialogStyle() {
  return QString(
      "QRadioButton::indicator{width:14px;height:14px;border-radius:8px;border:2px solid #6c7fa6;background:#0e131c;}"
      "QRadioButton::indicator:checked{border:2px solid #3b82f6;background:qradialgradient(cx:0.5,cy:0.5,radius:0.5,fx:0.5,fy:0.5,"
      "stop:0 #3b82f6,stop:0.5 #3b82f6,stop:0.6 #0e131c,stop:1 #0e131c);}"
      "QCheckBox::indicator{width:15px;height:15px;border:2px solid #6c7fa6;border-radius:3px;background:#0e131c;}"
      "QCheckBox::indicator:checked{background:#3b82f6;border-color:#3b82f6;}"
      "QDialog{background:#141b28;} QLabel{color:#e8eef9;} QLabel:disabled{color:#8795b0;} QCheckBox,QRadioButton{color:#e8eef9;}"
      "QGroupBox{background:#1b2433;border:1px solid #2f3d58;border-radius:8px;margin-top:14px;padding:10px;color:#e8eef9;}"
      "QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 6px;color:#8fb8ff;font-weight:600;}"
      "QPushButton{background:#25324a;border:1px solid #3b4a68;border-radius:5px;padding:6px 14px;color:#e8eef9;}"
      "QPushButton:hover{background:#2c3b57;} QPushButton:disabled{color:#6c7a94;background:#1b2433;}"
      "QPushButton:default{background:#3b82f6;color:white;border-color:#3b82f6;font-weight:600;}"
      "QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox{background:#0e131c;border:1px solid #3b4a68;border-radius:4px;padding:3px 6px;color:#fff;}"
      "QComboBox QAbstractItemView{background:#0e131c;color:#fff;selection-background-color:#3b82f6;}"
      "QTableWidget{background:#141b28;alternate-background-color:#1b2433;border:1px solid #2f3d58;color:#cfd8ea;"
      "selection-background-color:#27406e;selection-color:white;}"
      "QHeaderView::section{background:#25324a;color:#e8eef9;padding:5px 8px;border:0;border-right:1px solid #3b4a68;font-weight:600;}"
      "QTabWidget::pane{border:1px solid #2f3d58;background:#141b28;} QTabBar::tab{padding:7px 16px;background:#1b2433;color:#cfd8ea;"
      "border-top-left-radius:6px;border-top-right-radius:6px;margin-right:2px;}"
      "QTabBar::tab:selected{background:#3b82f6;color:white;font-weight:600;}");
}

void StyleDialog(QDialog& dialog, QVBoxLayout* layout) {
  dialog.setStyleSheet(DialogStyle());
  layout->setSpacing(10);
}

}  // namespace

// The dialogs are static functions of one friend class so they can reach the window's scale state.
class PdfScaleDialogs {
public:
  static void SetScale(PdfWindow& w);
  /// 0 cancelled, 1 scale set, 2 scale set and the Check tool wanted.
  static int KnownDistance(PdfWindow& w);
  /// True when a check (or robust dimension) was added.
  static bool CheckPopup(PdfWindow& w);
  static void Checks(PdfWindow& w, int tab);
};

// ---------------------------------------------------------------------------------------------------
// The window's scale members
// ---------------------------------------------------------------------------------------------------

const PageScale* PdfWindow::EffectiveScale(int page) const {
  const auto changed = m_Annotations.Scales().find(page);
  if (changed != m_Annotations.Scales().end()) return changed->second.Valid() ? &changed->second : nullptr;
  const auto original = m_FileScales.scales.find(page);
  return original != m_FileScales.scales.end() ? &original->second : nullptr;
}

// The scale on each page that has a dimension: a dimension's label is worked out from it when saved.
std::map<int, PageScale> PdfWindow::ScalesInForce() const {
  std::map<int, PageScale> inForce;
  for (const auto& annotation : m_Annotations.Items())
    if (annotation.IsDimension())
      if (const PageScale* scale = EffectiveScale(annotation.page)) inForce[annotation.page] = *scale;
  return inForce;
}

QPointF PdfWindow::PageToCanvas(float x, float y) const {
  const auto size = m_Document->Sizes()[static_cast<std::size_t>(m_PageNumber->value() - 1)];
  const double scaleX = static_cast<double>(m_Page->width()) / size.wPt;
  const double scaleY = static_cast<double>(m_Page->height()) / size.hPt;
  return {x * scaleX, (size.hPt - y) * scaleY};
}

void PdfWindow::BuildScaleToolbar() {
  addToolBarBreak(Qt::TopToolBarArea);
  auto* bar = addToolBar("PDF scale");
  bar->setObjectName("pdfScaleTools");
  bar->setMovable(false);
  auto* setScale = bar->addAction("Set scale...", this, [this] { ShowSetScale(); });
  setScale->setObjectName("pdfSetScaleAction");
  setScale->setToolTip("Set the drawing scale of this page (preset, typed ratio, or calibrate from two points)");
  m_ScaleLabel = new QLabel("Open a PDF", bar);
  m_ScaleLabel->setObjectName("pdfScaleLabel");
  m_ScaleLabel->setMinimumWidth(180);
  bar->addWidget(m_ScaleLabel);
  m_ChecksButton = new QPushButton("Scale checks (0)", bar);
  m_ChecksButton->setObjectName("pdfScaleChecksButton");
  m_ChecksButton->setToolTip("See how this page's scale agrees with the dimensions you checked,\ncorrect it, or run a robust calibration");
  connect(m_ChecksButton, &QPushButton::clicked, this, [this] { ShowChecks(0); });
  bar->addWidget(m_ChecksButton);
  m_ShowChecksBox = new QCheckBox("Show on sheet", bar);
  m_ShowChecksBox->setObjectName("pdfShowChecks");
  m_ShowChecksBox->setChecked(true);
  m_ShowChecksBox->setToolTip("Show or hide the check lines and labels drawn on the sheet");
  connect(m_ShowChecksBox, &QCheckBox::toggled, this, [this] { m_Page->update(); });
  bar->addWidget(m_ShowChecksBox);
  bar->addSeparator();
  m_SnapAction = bar->addAction("Snap: off");
  m_SnapAction->setObjectName("pdfSnapAction");
  m_SnapAction->setCheckable(true);
  m_SnapAction->setShortcut(QKeySequence(Qt::Key_F3));
  m_SnapAction->setToolTip("Snap to the ends and corners of the drawing (F3)");
  connect(m_SnapAction, &QAction::toggled, this, [this] { ToggleSnap(); });
  m_SnapStatus = new QLabel(bar);
  m_SnapStatus->setObjectName("pdfSnapStatus");
  bar->addWidget(m_SnapStatus);
  bar->setStyleSheet(
      "QLabel#pdfScaleLabel{background:#25324a;border:1px solid #3b4a68;border-radius:10px;padding:3px 12px;color:#cfe3ff;font-weight:600;}"
      "QToolButton:checked{background:#1f7a3e;color:white;border-radius:4px;} QLabel#pdfSnapStatus{color:#f5b73a;}");
  m_SnapPoll = new QTimer(this);
  m_SnapPoll->setInterval(30);
  connect(m_SnapPoll, &QTimer::timeout, this, [this] { UpdateSnap(m_LastPointer); });
}

void PdfWindow::SetTool(int tool, const QString& status) {
  m_Tool = tool;
  m_Draft.clear();
  m_SnapPrevValid = false;
  for (QAction* action : m_ToolGroup->actions())
    if (action->data().toInt() == tool) action->setChecked(true);
  statusBar()->showMessage(status);
  m_Page->update();
}

// Esc: a pick in progress first, then the tool.
void PdfWindow::EscapeTool() {
  if (m_Tool != kToolCalibrate && !m_Draft.empty()) {
    m_Draft.clear();
    m_Page->update();
    return;
  }
  SetTool(-2, "Select annotations; drag to move or resize.");
}

void PdfWindow::SceneChanged() {
  ++m_Revision;
  m_SelectedCheck = -1;
  setWindowModified(HasUnsavedChanges());
  UpdateScaleDisplay();
  m_Page->update();
}

void PdfWindow::UpdateScaleDisplay() {
  if (!m_ScaleLabel) return;
  if (!m_Document) {
    m_ScaleLabel->setText("Open a PDF");
    m_ChecksButton->setEnabled(false);
    return;
  }
  const int page = m_PageNumber->value() - 1;
  const PageScale* scale = EffectiveScale(page);
  QString text;
  if (scale) {
    text = "Scale: " + Q(scale->RatioText());
    if (!scale->note.empty()) text += " (" + Q(scale->note) + ")";
  } else {
    const auto unusable = m_Annotations.Scales().count(page) ? m_FileScales.unusable.end() : m_FileScales.unusable.find(page);
    text = unusable != m_FileScales.unusable.end() ? "Unscaled (" + Q(unusable->second) + ")" : QString("Unscaled");
  }
  m_ScaleLabel->setText(text);
  int count = 0;
  bool haveVerdict = false;
  Verdict worst = Verdict::Good;
  for (const ScaleCheck& check : m_Annotations.Checks()) {
    if (check.page != page) continue;
    ++count;
    if (!scale) continue;
    const Verdict verdict = EvaluateCheck(check, *scale, m_Limits).verdict;
    if (!haveVerdict || static_cast<int>(verdict) > static_cast<int>(worst)) worst = verdict;
    haveVerdict = true;
  }
  m_ChecksButton->setText(QString("Scale checks (%1)").arg(count));
  m_ChecksButton->setEnabled(scale != nullptr);
  m_ChecksButton->setStyleSheet(VerdictButtonStyle(haveVerdict, worst));
}

void PdfWindow::ShowSetScale() {
  if (!m_Document) return;
  PdfScaleDialogs::SetScale(*this);
}

void PdfWindow::BeginCalibrate() {
  SetTool(kToolCalibrate, "Click two points a known distance apart on the page.");
}

void PdfWindow::FinishCalibrationPicks() {
  m_CalPage = m_PageNumber->value() - 1;
  for (int i = 0; i < 2; ++i) {
    m_CalX[i] = m_Draft[static_cast<std::size_t>(i)].first;
    m_CalY[i] = m_Draft[static_cast<std::size_t>(i)].second;
  }
  const int result = PdfScaleDialogs::KnownDistance(*this); // the two points stay drawn while the distance is asked
  const PageScale* scale = EffectiveScale(m_CalPage);
  if (result == 2) {
    m_CheckTarget = 0;
    SetTool(kToolCheck, "Scale set. Click the two ends of another dimension to check it.");
  } else {
    SetTool(-1, result == 1 && scale ? "Scale set: " + Q(scale->RatioText()) : QString("Browse pages"));
  }
}

void PdfWindow::FinishCheckPicks() {
  const bool added = PdfScaleDialogs::CheckPopup(*this);
  m_Draft.clear();
  m_Page->update();
  if (m_CheckTarget == 1) { // the robust calibration's dimension: back to its dialog, added or not
    SetTool(-1, "Browse pages");
    ShowChecks(1);
  } else if (added) {
    statusBar()->showMessage("Check added.");
  }
}

void PdfWindow::ShowChecks(int tab) {
  if (!m_Document) return;
  PdfScaleDialogs::Checks(*this, tab);
  UpdateScaleDisplay();
  m_Page->update();
}

bool PdfWindow::ScopePages(const ScaleForm& form, std::vector<int>& pages, QString& error) const {
  const int count = m_Document->PageCount();
  pages.clear();
  if (form.scope == 0) {
    pages.push_back(std::clamp(m_PageNumber->value() - 1, 0, count - 1));
  } else if (form.scope == 1) {
    for (int i = 0; i < count; ++i) pages.push_back(i);
  } else {
    const PageListResult list = ParsePageList(form.scopeText.toUtf8().toStdString(), count);
    if (!list.error.empty()) { error = Q(list.error); return false; }
    pages = list.pages;
  }
  return true;
}

// Applies \p scale to the chosen pages as one undo step. A scale that is not valid clears them.
bool PdfWindow::ApplyScale(const PageScale& scale, const ScaleForm& form, const ScaleCheck* calibration, QString& error) {
  std::vector<int> pages;
  if (!ScopePages(form, pages, error)) return false;
  std::map<int, PageScale> change;
  for (int page : pages) change[page] = scale;
  m_Annotations.SetScales(change, calibration); // a calibration counts as the first scale check (REQ-394)
  qInfo().noquote() << (scale.Valid() ? "PDF scale: " + Q(scale.RatioText()) + " set on " + QString::number(pages.size()) + " page(s)"
                                      : "PDF scale: cleared on " + QString::number(pages.size()) + " page(s)");
  SceneChanged();
  return true;
}

// ---------------------------------------------------------------------------------------------------
// Snap (REQ-391 clause 5): the nearest end or corner of the page's own line work
// ---------------------------------------------------------------------------------------------------

void PdfWindow::ToggleSnap() {
  m_SnapOn = m_SnapAction->isChecked();
  m_SnapAction->setText(m_SnapOn ? "Snap: on" : "Snap: off");
  m_SnapHit = false;
  m_SnapPrevValid = false;
  if (m_SnapOn && m_Document) StartSnapRead(m_PageNumber->value() - 1);
  UpdateSnap(m_LastPointer);
  m_Page->update();
}

// Reads a page's ends and corners in the background. One at a time per page; a few pages are kept and the
// farthest finished one is dropped to make room.
void PdfWindow::StartSnapRead(int page) {
  if (!m_Document || page < 0 || page >= m_Document->PageCount()) return;
  SnapPage& existing = m_SnapPages[page];
  if (existing.started) return;
  while (m_SnapPages.size() > 4) {
    int farthest = -1;
    for (const auto& [number, other] : m_SnapPages)
      if (number != page && !other.reading.valid() && (farthest < 0 || std::abs(number - page) > std::abs(farthest - page))) farthest = number;
    if (farthest < 0) break;
    m_SnapPages.erase(farthest);
  }
  SnapPage& current = m_SnapPages[page];
  current.started = true;
  PdfDocument* document = m_Document.get();
  const auto cancel = current.cancel;
  current.reading = std::async(std::launch::async, [document, page, cancel]() -> std::shared_ptr<SnapIndex> {
    std::vector<SnapPoint> points;
    try {
      if (!document->SnapPoints(page, points, [cancel] { return cancel->load(); })) return nullptr;
    } catch (...) { return nullptr; }
    auto index = std::make_shared<SnapIndex>();
    index->Build(std::move(points));
    return index;
  });
  m_SnapPoll->start();
}

void PdfWindow::UpdateSnap(QPointF point) {
  const bool hadHit = m_SnapHit;
  const float oldX = m_SnapX, oldY = m_SnapY;
  m_LastPointer = point;
  m_PointerIn = true;
  m_SnapHit = false;
  const bool placing = m_Tool >= 0;
  if (m_SnapOn && m_Document && !m_Page->image.isNull()) {
    const int page = m_PageNumber->value() - 1;
    SnapPage& snap = m_SnapPages[page];
    if (!snap.started) StartSnapRead(page);
    if (snap.reading.valid() && snap.reading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      snap.index = snap.reading.get();
      snap.failed = snap.index == nullptr || snap.index->Size() == 0;
    }
    if (!snap.reading.valid()) m_SnapPoll->stop();
    if (placing && snap.index) {
      const auto size = m_Document->Sizes()[static_cast<std::size_t>(page)];
      const float k = static_cast<float>(m_Page->width()) / size.wPt; // screen pixels per point
      const float px = static_cast<float>(point.x()) / k, py = size.hPt - static_cast<float>(point.y()) / k;
      SnapIndex::Pt hit;
      const float radius = 10.f / k;
      const bool found = snap.index->Nearest(px, py, radius, hit, 6.f / k); // points within 6 screen pixels merge into the strongest
      if (found) {
        // Stay on the last point while the pointer is still near it, unless another is clearly closer (5 screen pixels).
        if (m_SnapPrevValid) {
          const float dPrev = std::hypot(m_SnapPrevX - px, m_SnapPrevY - py), dNew = std::hypot(hit.first - px, hit.second - py);
          if (dPrev <= radius && dNew > dPrev - 5.f / k) hit = {m_SnapPrevX, m_SnapPrevY};
        }
        m_SnapHit = true;
        m_SnapX = m_SnapPrevX = hit.first;
        m_SnapY = m_SnapPrevY = hit.second;
        m_SnapPrevValid = true;
      }
    }
  }
  if (!m_SnapHit) m_SnapPrevValid = false;
  QString status;
  if (m_SnapOn && m_Document) {
    const auto found = m_SnapPages.find(m_PageNumber->value() - 1);
    if (found != m_SnapPages.end() && found->second.reading.valid()) status = "reading this page...";
    else if (found != m_SnapPages.end() && found->second.failed) status = "this page has nothing to snap to";
  }
  if (m_SnapStatus->text() != status) m_SnapStatus->setText(status);
  if (hadHit != m_SnapHit || (m_SnapHit && (oldX != m_SnapX || oldY != m_SnapY)) || m_Tool == kToolCheck) m_Page->update();
}

// ---------------------------------------------------------------------------------------------------
// Drawing on the sheet: checks, robust dimensions, the picks in progress and the snap marker
// ---------------------------------------------------------------------------------------------------

void PdfWindow::DrawScaleOverlay(QPainter& painter) {
  if (!m_Document || m_Page->image.isNull()) return;
  const int page = m_PageNumber->value() - 1;
  const PageScale* scale = EffectiveScale(page);
  auto at = [&](float x, float y) { return PageToCanvas(x, y); };
  // A label as a solid chip in the check's colour with dark text, so it reads on any drawing.
  auto chip = [&](QPointF where, const QString& text, const QColor& colour) {
    QFont font = painter.font();
    font.setPixelSize(14);
    painter.setFont(font);
    const QRectF box = QFontMetricsF(font).boundingRect(text).adjusted(-5, -3, 5, 3).translated(where.x() + 5, where.y() + 5);
    painter.setPen(QPen(QColor(0, 0, 0, 200), 1.5));
    painter.setBrush(colour);
    painter.drawRoundedRect(box, 4, 4);
    painter.setPen(QColor(10, 10, 10));
    painter.drawText(box, Qt::AlignCenter, text);
  };
  auto dimension = [&](const ScaleCheck& check, const QColor& colour, const QString& text, bool selected) {
    const QPointF a = at(check.x0, check.y0), b = at(check.x1, check.y1);
    painter.setPen(QPen(colour, selected ? 4 : 2));
    painter.drawLine(a, b);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawEllipse(a, 3.5, 3.5);
    painter.drawEllipse(b, 3.5, 3.5);
    chip((a + b) / 2.0, text, colour);
  };
  if (m_ShowChecksBox->isChecked()) {
    const auto& checks = m_Annotations.Checks();
    for (std::size_t i = 0; i < checks.size(); ++i) {
      const ScaleCheck& check = checks[i];
      if (check.page != page) continue;
      QColor colour(150, 150, 150);
      QString text = "no scale";
      if (scale) {
        const CheckResult result = EvaluateCheck(check, *scale, m_Limits);
        colour = SheetColor(result.verdict);
        text = Q(FormatValue(result.measured, 3) + " vs " + FormatValue(result.stated, 3) + " " + UnitLabel(check.unit) + "  (" +
                 Signed(result.pct, 2) + " %)");
      }
      dimension(check, colour, text, static_cast<int>(i) == m_SelectedCheck);
    }
    for (std::size_t i = 0; i < m_RobustPicks.size(); ++i) {
      const ScaleCheck& check = m_RobustPicks[i].first;
      if (check.page != page) continue;
      dimension(check, m_RobustPicks[i].second ? QColor(60, 140, 255) : QColor(150, 150, 150),
                Q("#" + std::to_string(i + 1) + "  " + FormatValue(check.stated, 3) + " " + UnitLabel(check.unit)), false);
    }
  }
  if (m_Tool == kToolCheck && !m_Draft.empty()) { // the dimension being picked
    const QPointF first = at(m_Draft[0].first, m_Draft[0].second);
    const QPointF second = m_Draft.size() == 2 ? at(m_Draft[1].first, m_Draft[1].second) : m_SnapHit ? at(m_SnapX, m_SnapY) : m_LastPointer;
    painter.setPen(QPen(QColor(60, 140, 255), 2));
    painter.drawLine(first, second);
    painter.setBrush(QColor(60, 140, 255));
    painter.drawEllipse(first, 4, 4);
  }
  if (m_Tool == kToolCalibrate && !m_Draft.empty()) { // the calibration points picked so far
    const QColor orange(255, 160, 0);
    if (m_Draft.size() == 2) {
      painter.setPen(QPen(orange, 2));
      painter.drawLine(at(m_Draft[0].first, m_Draft[0].second), at(m_Draft[1].first, m_Draft[1].second));
    }
    for (const auto& pt : m_Draft) {
      painter.setPen(QPen(QColor(0, 0, 0, 200), 1.5));
      painter.setBrush(orange);
      painter.drawEllipse(at(pt.first, pt.second), 5, 5);
    }
  }
  if (m_SnapHit) { // the end or corner the next click will take
    const QPointF c = at(m_SnapX, m_SnapY);
    const QRectF box(c.x() - 6, c.y() - 6, 12, 12);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 0, 0, 220), 3.5));
    painter.drawRect(box);
    painter.setPen(QPen(QColor(60, 255, 90), 1.8));
    painter.drawRect(box);
  }
}

// ---------------------------------------------------------------------------------------------------
// Set scale
// ---------------------------------------------------------------------------------------------------

void PdfScaleDialogs::SetScale(PdfWindow& w) {
  PdfWindow::ScaleForm& form = w.m_ScaleForm;
  const int page = w.m_PageNumber->value() - 1;
  QDialog dialog(&w);
  dialog.setObjectName("pdfSetScaleDialog");
  dialog.setWindowTitle("Set scale");
  dialog.setMinimumWidth(560);
  auto* layout = new QVBoxLayout(&dialog);
  StyleDialog(dialog, layout);
  const PageScale* current = w.EffectiveScale(page);
  auto* now = new QLabel(QString("Page %1 now: %2").arg(page + 1).arg(current ? Q(current->RatioText()) : QString("unscaled")), &dialog);
  now->setObjectName("pdfSetScaleCurrent");
  layout->addWidget(now);

  auto* modes = new QHBoxLayout();
  auto* presetMode = new QRadioButton("Preset", &dialog);
  presetMode->setObjectName("pdfScaleModePreset");
  auto* typedMode = new QRadioButton("Typed ratio", &dialog);
  typedMode->setObjectName("pdfScaleModeTyped");
  auto* modeGroup = new QButtonGroup(&dialog);
  modeGroup->addButton(presetMode);
  modeGroup->addButton(typedMode);
  (form.mode == 1 ? typedMode : presetMode)->setChecked(true);
  modes->addWidget(presetMode);
  modes->addWidget(typedMode);
  modes->addStretch();
  layout->addLayout(modes);

  auto* presets = new QComboBox(&dialog);
  presets->setObjectName("pdfScalePreset");
  auto* model = new QStandardItemModel(presets);
  presets->setModel(model);
  const auto& list = PresetScales();
  int currentRow = 0;
  for (std::size_t i = 0; i < list.size(); ++i) {
    if (i == 0 || i == 11 || i == 19) { // group captions
      auto* heading = new QStandardItem(i == 0 ? "Architectural" : i == 11 ? "Engineering" : "Metric");
      heading->setFlags(Qt::NoItemFlags);
      QFont font = heading->font();
      font.setBold(true);
      heading->setFont(font);
      model->appendRow(heading);
    }
    auto* item = new QStandardItem(Q(list[i].label));
    item->setData(static_cast<int>(i), Qt::UserRole);
    if (static_cast<int>(i) == std::clamp(form.preset, 0, static_cast<int>(list.size()) - 1)) currentRow = model->rowCount();
    model->appendRow(item);
  }
  presets->setCurrentIndex(currentRow);
  layout->addWidget(presets);

  auto* typed = new QWidget(&dialog);
  auto* typedRow = new QHBoxLayout(typed);
  typedRow->setContentsMargins(0, 0, 0, 0);
  auto* pageValue = LengthSpin(typed, "pdfScaleTypedPage", form.page);
  auto* pageUnit = UnitCombo(typed, "pdfScalePageUnit", form.pageUnit);
  auto* realValue = LengthSpin(typed, "pdfScaleTypedReal", form.real);
  auto* realUnit = UnitCombo(typed, "pdfScaleRealUnit", form.realUnit);
  typedRow->addWidget(pageValue);
  typedRow->addWidget(pageUnit);
  typedRow->addWidget(new QLabel("on the sheet  =", typed));
  typedRow->addWidget(realValue);
  typedRow->addWidget(realUnit);
  typedRow->addWidget(new QLabel("in reality", typed));
  layout->addWidget(typed);
  auto syncMode = [&] { presets->setVisible(presetMode->isChecked()); typed->setVisible(typedMode->isChecked()); };
  QObject::connect(presetMode, &QRadioButton::toggled, &dialog, syncMode);
  syncMode();

  const ScopeChooser scope = AddScopeChooser(&dialog, layout, form);
  QLabel* error = ErrorLabel(&dialog);
  layout->addWidget(error);

  auto* buttons = new QHBoxLayout();
  auto* apply = new QPushButton("Apply", &dialog);
  apply->setObjectName("pdfScaleApply");
  apply->setDefault(true);
  auto* calibrate = new QPushButton("Calibrate from two points...", &dialog);
  calibrate->setObjectName("pdfScaleCalibrate");
  auto* clear = new QPushButton("Clear scale", &dialog);
  clear->setObjectName("pdfScaleClear");
  auto* cancel = new QPushButton("Cancel", &dialog);
  cancel->setObjectName("pdfScaleCancel");
  for (auto* button : {apply, calibrate, clear, cancel}) buttons->addWidget(button);
  layout->addLayout(buttons);

  auto remember = [&] {
    form.mode = typedMode->isChecked() ? 1 : 0;
    const QVariant chosen = presets->currentData(Qt::UserRole);
    if (chosen.isValid()) form.preset = chosen.toInt();
    form.page = pageValue->value();
    form.real = realValue->value();
    form.pageUnit = pageUnit->currentIndex();
    form.realUnit = realUnit->currentIndex();
    ReadScope(scope, form);
  };
  bool startCalibrate = false;
  QObject::connect(apply, &QPushButton::clicked, &dialog, [&] {
    remember();
    PageScale scale;
    if (form.mode == 0) {
      scale = list[static_cast<std::size_t>(std::clamp(form.preset, 0, static_cast<int>(list.size()) - 1))];
    } else {
      scale.pageValue = form.page;
      scale.pageUnit = AllUnits()[static_cast<std::size_t>(form.pageUnit)];
      scale.realValue = form.real;
      scale.realUnit = AllUnits()[static_cast<std::size_t>(form.realUnit)];
    }
    if (!scale.Valid()) { ShowError(error, "Both lengths must be bigger than zero."); return; }
    QString why;
    if (!w.ApplyScale(scale, form, nullptr, why)) { ShowError(error, why); return; }
    dialog.accept();
  });
  QObject::connect(calibrate, &QPushButton::clicked, &dialog, [&] { remember(); startCalibrate = true; dialog.reject(); });
  QObject::connect(clear, &QPushButton::clicked, &dialog, [&] {
    remember();
    QString why;
    if (!w.ApplyScale(PageScale{0.0, Unit::Inch, 0.0, Unit::Foot, "", "", 0.0}, form, nullptr, why)) { ShowError(error, why); return; }
    dialog.accept();
  });
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  dialog.exec();
  remember();
  if (startCalibrate) w.BeginCalibrate();
}

// ---------------------------------------------------------------------------------------------------
// Known distance (after two calibration clicks)
// ---------------------------------------------------------------------------------------------------

int PdfScaleDialogs::KnownDistance(PdfWindow& w) {
  PdfWindow::ScaleForm& form = w.m_ScaleForm;
  const double points = std::hypot(static_cast<double>(w.m_CalX[1] - w.m_CalX[0]), static_cast<double>(w.m_CalY[1] - w.m_CalY[0]));
  const auto pageSize = w.m_Document->Sizes()[static_cast<std::size_t>(w.m_CalPage)];
  const double pointsPerPixel = pageSize.wPt / std::max(1, w.m_Page->width());
  QDialog dialog(&w);
  dialog.setObjectName("pdfKnownDistanceDialog");
  dialog.setWindowTitle("Known distance");
  dialog.setMinimumWidth(560);
  auto* layout = new QVBoxLayout(&dialog);
  StyleDialog(dialog, layout);
  auto* apart = new QLabel(QString("The two points are %1 points (%2 in) apart on the sheet.")
                               .arg(points, 0, 'f', 3).arg(points / 72.0, 0, 'f', 4), &dialog);
  apart->setObjectName("pdfCalDistance");
  layout->addWidget(apart);
  const int freehand = (w.m_CalSnapped[0] ? 0 : 1) + (w.m_CalSnapped[1] ? 0 : 1);
  auto* snapState = new QLabel(&dialog);
  snapState->setObjectName("pdfCalSnapState");
  snapState->setWordWrap(true);
  if (freehand == 0) {
    snapState->setText("Both points snapped to the drawing.");
    snapState->setStyleSheet("color:" + GoodText() + ";");
  } else {
    snapState->setText(QString("%1 placed freehand: one screen pixel can be %2 points. Turn Snap on (F3) and pick again for an exact scale.")
                           .arg(freehand == 2 ? "Both points were" : "One point was").arg(pointsPerPixel, 0, 'f', 2));
    snapState->setStyleSheet("color:" + WarnText() + ";");
  }
  layout->addWidget(snapState);

  auto* row = new QHBoxLayout();
  row->addWidget(new QLabel("In reality they are:", &dialog));
  auto* real = LengthSpin(&dialog, "pdfCalReal", form.calReal);
  auto* unit = UnitCombo(&dialog, "pdfCalUnit", form.calUnit);
  row->addWidget(real);
  row->addWidget(unit);
  row->addStretch();
  layout->addLayout(row);
  const ScopeChooser scope = AddScopeChooser(&dialog, layout, form);

  auto* nearestRow = new QHBoxLayout();
  auto* nearestLabel = new QLabel(&dialog);
  nearestLabel->setObjectName("pdfCalNearest");
  auto* useStandard = new QPushButton("Use the standard scale", &dialog);
  useStandard->setObjectName("pdfCalUseStandard");
  nearestRow->addWidget(nearestLabel, 1);
  nearestRow->addWidget(useStandard);
  layout->addLayout(nearestRow);
  QLabel* error = ErrorLabel(&dialog);
  layout->addWidget(error);

  auto* buttons = new QHBoxLayout();
  auto* setScale = new QPushButton("Set scale", &dialog);
  setScale->setObjectName("pdfCalSetScale");
  setScale->setDefault(true);
  auto* setAndCheck = new QPushButton("Set scale and check...", &dialog);
  setAndCheck->setObjectName("pdfCalSetAndCheck");
  auto* cancel = new QPushButton("Cancel", &dialog);
  cancel->setObjectName("pdfCalCancel");
  for (auto* button : {setScale, setAndCheck, cancel}) buttons->addWidget(button);
  layout->addLayout(buttons);

  const PageScale* nearest = nullptr;
  double nearestDiff = 1.0;
  auto refresh = [&] {
    nearest = nullptr;
    nearestDiff = 1.0;
    const PageScale calibrated = ScaleFromCalibration(points, real->value(), AllUnits()[static_cast<std::size_t>(unit->currentIndex())]);
    if (calibrated.Valid())
      for (const PageScale& preset : PresetScales()) {
        const double diff = std::fabs(MetresPerPt(calibrated) / MetresPerPt(preset) - 1.0);
        if (diff < nearestDiff) { nearestDiff = diff; nearest = &preset; }
      }
    const bool offer = nearest != nullptr && nearestDiff < 0.01;
    nearestLabel->setText(offer ? QString("Closest standard scale: %1 (%2 % different).").arg(Q(nearest->label)).arg(nearestDiff * 100.0, 0, 'f', 2) : QString());
    nearestLabel->setVisible(offer);
    useStandard->setVisible(offer);
  };
  QObject::connect(real, &QDoubleSpinBox::valueChanged, &dialog, refresh);
  QObject::connect(unit, &QComboBox::currentIndexChanged, &dialog, refresh);
  refresh();

  int result = 0;
  auto calibration = [&] {
    ScaleCheck check;
    check.page = w.m_CalPage;
    check.x0 = w.m_CalX[0]; check.y0 = w.m_CalY[0]; check.x1 = w.m_CalX[1]; check.y1 = w.m_CalY[1];
    check.stated = real->value();
    check.unit = AllUnits()[static_cast<std::size_t>(unit->currentIndex())];
    check.calibration = true;
    return check;
  };
  auto remember = [&] {
    form.calReal = real->value();
    form.calUnit = unit->currentIndex();
    ReadScope(scope, form);
  };
  auto finish = [&](const PageScale& scale, int outcome) {
    remember();
    if (!scale.Valid()) { ShowError(error, "The distance must be bigger than zero, and the two points must be apart."); return; }
    const ScaleCheck check = calibration();
    QString why;
    if (!w.ApplyScale(scale, form, &check, why)) { ShowError(error, why); return; }
    result = outcome;
    dialog.accept();
  };
  QObject::connect(setScale, &QPushButton::clicked, &dialog, [&] {
    finish(ScaleFromCalibration(points, real->value(), AllUnits()[static_cast<std::size_t>(unit->currentIndex())]), 1);
  });
  QObject::connect(setAndCheck, &QPushButton::clicked, &dialog, [&] {
    finish(ScaleFromCalibration(points, real->value(), AllUnits()[static_cast<std::size_t>(unit->currentIndex())]), 2);
  });
  QObject::connect(useStandard, &QPushButton::clicked, &dialog, [&] { if (nearest) finish(*nearest, 1); });
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  dialog.exec();
  remember();
  return result;
}

// ---------------------------------------------------------------------------------------------------
// The Check tool's popup
// ---------------------------------------------------------------------------------------------------

bool PdfScaleDialogs::CheckPopup(PdfWindow& w) {
  const int page = w.m_PageNumber->value() - 1;
  const PageScale* scale = w.EffectiveScale(page);
  const double points = std::hypot(static_cast<double>(w.m_Draft[1].first - w.m_Draft[0].first),
                                   static_cast<double>(w.m_Draft[1].second - w.m_Draft[0].second));
  QDialog dialog(&w);
  dialog.setObjectName("pdfCheckDialog");
  dialog.setWindowTitle("Check a dimension");
  dialog.setMinimumWidth(560);
  auto* layout = new QVBoxLayout(&dialog);
  StyleDialog(dialog, layout);
  auto* apart = new QLabel(QString("The two points are %1 points (%2 in) apart on the sheet.").arg(points, 0, 'f', 3).arg(points / 72.0, 0, 'f', 4), &dialog);
  apart->setObjectName("pdfCheckDistance");
  layout->addWidget(apart);
  auto* row = new QHBoxLayout();
  row->addWidget(new QLabel("The drawing states:", &dialog));
  auto* value = new QLineEdit(&dialog);
  value->setObjectName("pdfCheckValue");
  value->setPlaceholderText("43'-0 3/4\"   10'6\"   12.5 m   850 mm");
  row->addWidget(value, 1);
  layout->addLayout(row);
  auto* result = new QLabel(&dialog);
  result->setObjectName("pdfCheckResult");
  result->setWordWrap(true);
  layout->addWidget(result);
  auto* buttons = new QHBoxLayout();
  auto* ok = new QPushButton("OK", &dialog);
  ok->setObjectName("pdfCheckOk");
  ok->setDefault(true);
  ok->setEnabled(false);
  auto* cancel = new QPushButton("Cancel", &dialog);
  cancel->setObjectName("pdfCheckCancel");
  buttons->addStretch();
  buttons->addWidget(ok);
  buttons->addWidget(cancel);
  layout->addLayout(buttons);

  ScaleCheck made;
  bool valid = false;
  auto refresh = [&] {
    valid = false;
    result->setStyleSheet(QString());
    const std::string text = value->text().toUtf8().toStdString();
    if (text.empty()) { result->clear(); ok->setEnabled(false); return; }
    ParsedLength parsed;
    std::string why;
    if (!ParseLength(text, parsed, why)) {
      result->setText(Q(why));
      result->setStyleSheet("color:" + BadText() + ";");
      ok->setEnabled(false);
      return;
    }
    made = ScaleCheck{};
    made.page = page;
    made.x0 = w.m_Draft[0].first; made.y0 = w.m_Draft[0].second;
    made.x1 = w.m_Draft[1].first; made.y1 = w.m_Draft[1].second;
    made.stated = parsed.value;
    made.unit = parsed.hasUnit ? parsed.unit : (scale ? scale->realUnit : Unit::Foot);
    valid = true;
    ok->setEnabled(true);
    if (w.m_CheckTarget == 0 && scale) {
      const CheckResult r = EvaluateCheck(made, *scale, w.m_Limits);
      result->setText(Q("Reads " + FormatValue(r.measured, 4) + " " + UnitLabel(made.unit) + ", the drawing says " + FormatValue(r.stated, 4) + " " +
                        UnitLabel(made.unit) + ": difference " + Signed(r.diff, 4) + " " + UnitLabel(made.unit) + " (" + Signed(r.pct, 2) + " %) - " +
                        VerdictName(r.verdict)));
      result->setStyleSheet(QString("color:%1;font-weight:600;").arg(VerdictColor(r.verdict).name()));
    } else {
      result->setText("Added to the robust calibration list; the fit is shown there.");
    }
  };
  QObject::connect(value, &QLineEdit::textChanged, &dialog, refresh);
  bool added = false;
  QObject::connect(ok, &QPushButton::clicked, &dialog, [&] {
    if (!valid) return;
    if (w.m_CheckTarget == 0) {
      w.m_Annotations.AddCheck(made);
      qInfo().noquote() << "PDF scale check:" << Q(FormatValue(made.stated, 4) + " " + UnitLabel(made.unit)) << "stated on page" << made.page + 1;
      w.SceneChanged();
    } else {
      w.m_RobustPicks.push_back({made, true});
      w.m_Page->update();
    }
    added = true;
    dialog.accept();
  });
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  dialog.exec();
  return added;
}

// ---------------------------------------------------------------------------------------------------
// The Scale checks dialog: checks / correction (tab 1) and robust calibration (tab 2)
// ---------------------------------------------------------------------------------------------------

void PdfScaleDialogs::Checks(PdfWindow& w, int tab) {
  const int page = w.m_PageNumber->value() - 1;
  QDialog dialog(&w);
  dialog.setObjectName("pdfChecksDialog");
  dialog.setWindowTitle("Scale checks");
  dialog.resize(900, 660);
  auto* root = new QVBoxLayout(&dialog);
  StyleDialog(dialog, root);
  auto* tabs = new QTabWidget(&dialog);
  tabs->setObjectName("pdfChecksTabs");
  root->addWidget(tabs, 1);

  // ---- Tab 1: the checks -----------------------------------------------------------------------------
  auto* checksTab = new QWidget(tabs);
  auto* checksLayout = new QVBoxLayout(checksTab);
  auto* header = new QLabel(checksTab);
  header->setObjectName("pdfChecksHeader");
  header->setWordWrap(true);
  checksLayout->addWidget(header);
  auto* body = new QWidget(checksTab);
  auto* bodyLayout = new QVBoxLayout(body);
  bodyLayout->setContentsMargins(0, 0, 0, 0);
  checksLayout->addWidget(body, 1);

  auto* limits = new QHBoxLayout();
  auto* good = new QDoubleSpinBox(body);
  good->setObjectName("pdfLimitGood");
  auto* check = new QDoubleSpinBox(body);
  check->setObjectName("pdfLimitCheck");
  for (auto* spin : {good, check}) { spin->setDecimals(2); spin->setRange(0.0, 100.0); spin->setSingleStep(0.05); spin->setKeyboardTracking(false); }
  good->setValue(w.m_Limits.goodPct);
  check->setValue(w.m_Limits.checkPct);
  auto* showOnSheet = new QCheckBox("Show checks on the sheet", body);
  showOnSheet->setObjectName("pdfChecksShowOnSheet");
  showOnSheet->setChecked(w.m_ShowChecksBox->isChecked());
  limits->addWidget(new QLabel("Good up to (%)", body));
  limits->addWidget(good);
  limits->addWidget(new QLabel("Check up to (%)", body));
  limits->addWidget(check);
  limits->addWidget(showOnSheet);
  limits->addStretch();
  bodyLayout->addLayout(limits);

  auto* empty = new QLabel("No checks on this page yet. Use the Check tool: click the two ends of a dimension, then type its printed value.", body);
  empty->setObjectName("pdfChecksEmpty");
  empty->setWordWrap(true);
  bodyLayout->addWidget(empty);
  auto* table = MakeTable(body, "pdfChecksTable", {"#", "Kind", "Drawing says", "Reads", "Difference", "%", "Verdict", "Flag", ""});
  bodyLayout->addWidget(table, 1);
  auto* bestFit = new QLabel(body);
  bestFit->setObjectName("pdfChecksBestFit");
  bestFit->setWordWrap(true);
  bodyLayout->addWidget(bestFit);
  auto* outlierNote = new QLabel(body);
  outlierNote->setObjectName("pdfChecksOutlierNote");
  outlierNote->setWordWrap(true);
  bodyLayout->addWidget(outlierNote);
  auto* limitsNote = new QLabel("A check shows the picked distances agree, not that the drawing is to scale: drawn lines can differ from printed values by a fraction of a percent.", body);
  limitsNote->setObjectName("pdfChecksLimitsNote");
  limitsNote->setWordWrap(true);
  limitsNote->setEnabled(false);
  bodyLayout->addWidget(limitsNote);

  auto* correction = new QGroupBox("Correct the scale (your choice; nothing is changed until you apply)", body);
  correction->setObjectName("pdfCorrection");
  auto* correctionLayout = new QVBoxLayout(correction);
  auto* modes = new QButtonGroup(correction);
  auto* matchRow = new QHBoxLayout();
  auto* matchMode = new QRadioButton("Match this check", correction);
  matchMode->setObjectName("pdfCorrMatch");
  auto* matchIndex = new QComboBox(correction);
  matchIndex->setObjectName("pdfCorrMatchIndex");
  matchRow->addWidget(matchMode);
  matchRow->addWidget(matchIndex);
  matchRow->addStretch();
  correctionLayout->addLayout(matchRow);
  auto* bestMode = new QRadioButton("Best fit", correction);
  bestMode->setObjectName("pdfCorrBestFit");
  auto* withoutMode = new QRadioButton("Best fit without the outlier", correction);
  withoutMode->setObjectName("pdfCorrBestFitWithout");
  auto* fitRow = new QHBoxLayout();
  fitRow->addWidget(bestMode);
  fitRow->addWidget(withoutMode);
  fitRow->addStretch();
  correctionLayout->addLayout(fitRow);
  auto* percentRow = new QHBoxLayout();
  auto* percentMode = new QRadioButton("A percentage", correction);
  percentMode->setObjectName("pdfCorrPercent");
  auto* percent = new QDoubleSpinBox(correction);
  percent->setObjectName("pdfCorrPercentValue");
  percent->setDecimals(3);
  percent->setRange(-99.0, 1000.0);
  percent->setSingleStep(0.05);
  percent->setKeyboardTracking(false);
  percentRow->addWidget(percentMode);
  percentRow->addWidget(percent);
  percentRow->addWidget(new QLabel("% (+ makes every reading larger)", correction));
  percentRow->addStretch();
  correctionLayout->addLayout(percentRow);
  auto* leaveMode = new QRadioButton("Leave the scale as it is", correction);
  leaveMode->setObjectName("pdfCorrLeave");
  correctionLayout->addWidget(leaveMode);
  modes->addButton(matchMode, 0);
  modes->addButton(bestMode, 1);
  modes->addButton(withoutMode, 2);
  modes->addButton(percentMode, 3);
  modes->addButton(leaveMode, 4);
  matchMode->setChecked(true);
  auto* after = new QLabel(correction);
  after->setObjectName("pdfCorrAfter");
  after->setWordWrap(true);
  correctionLayout->addWidget(after);
  auto* afterTable = MakeTable(correction, "pdfCorrTable", {"#", "Would read", "Difference", "%", "Verdict"});
  afterTable->setMaximumHeight(150);
  correctionLayout->addWidget(afterTable);
  auto* tradeoff = new QLabel("Correcting to one check moves the error onto the others: compare the table above with the one before.", correction);
  tradeoff->setEnabled(false);
  tradeoff->setWordWrap(true);
  correctionLayout->addWidget(tradeoff);
  auto* actions = new QHBoxLayout();
  auto* applyCorrection = new QPushButton("Apply correction", correction);
  applyCorrection->setObjectName("pdfCorrApply");
  auto* copyReport = new QPushButton("Copy report", correction);
  copyReport->setObjectName("pdfChecksCopyReport");
  actions->addWidget(applyCorrection);
  actions->addWidget(copyReport);
  actions->addStretch();
  correctionLayout->addLayout(actions);
  bodyLayout->addWidget(correction);
  tabs->addTab(checksTab, "Checks");

  // ---- Tab 2: robust calibration ---------------------------------------------------------------------
  auto* robustTab = new QWidget(tabs);
  auto* robustLayout = new QVBoxLayout(robustTab);
  auto* intro = new QLabel(robustTab);
  intro->setObjectName("pdfRobustIntro");
  intro->setWordWrap(true);
  robustLayout->addWidget(intro);
  auto* paramRow = new QHBoxLayout();
  auto* pickError = new QDoubleSpinBox(robustTab);
  pickError->setObjectName("pdfRobustPick");
  pickError->setDecimals(2);
  pickError->setRange(0.01, 1000.0);
  pickError->setValue(w.m_Robust.pickPt);
  auto* drawingError = new QDoubleSpinBox(robustTab);
  drawingError->setObjectName("pdfRobustDrawing");
  drawingError->setDecimals(3);
  drawingError->setRange(0.0, 100.0);
  drawingError->setValue(w.m_Robust.drawingFraction * 100.0);
  auto* minimum = new QSpinBox(robustTab);
  minimum->setObjectName("pdfRobustMinimum");
  minimum->setRange(2, 50);
  minimum->setValue(std::clamp(w.m_Robust.minDimensions, 2, 50));
  for (auto* spin : std::initializer_list<QAbstractSpinBox*>{pickError, drawingError, minimum}) spin->setKeyboardTracking(false);
  paramRow->addWidget(new QLabel("Pick error (pt)", robustTab));
  paramRow->addWidget(pickError);
  paramRow->addWidget(new QLabel("Drawing error (% of length)", robustTab));
  paramRow->addWidget(drawingError);
  paramRow->addWidget(new QLabel("Minimum", robustTab));
  paramRow->addWidget(minimum);
  paramRow->addStretch();
  robustLayout->addLayout(paramRow);
  auto* robustButtons = new QHBoxLayout();
  auto* addDimension = new QPushButton("Add dimension...", robustTab);
  addDimension->setObjectName("pdfRobustAdd");
  auto* clearList = new QPushButton("Clear list", robustTab);
  clearList->setObjectName("pdfRobustClear");
  robustButtons->addWidget(addDimension);
  robustButtons->addWidget(clearList);
  robustButtons->addStretch();
  robustLayout->addLayout(robustButtons);
  auto* robustTable = MakeTable(robustTab, "pdfRobustTable", {"Use", "#", "Drawing says", "Picked (pt)", "Residual", "%", "Std. residual", ""});
  robustLayout->addWidget(robustTable, 1);
  auto* robustResult = new QLabel(robustTab);
  robustResult->setObjectName("pdfRobustResult");
  robustResult->setWordWrap(true);
  robustLayout->addWidget(robustResult);
  auto* suspectNote = new QLabel("A dimension is Suspect (a wrong pick or a mistyped value). Untick it to re-solve, or keep it.", robustTab);
  suspectNote->setObjectName("pdfRobustSuspect");
  suspectNote->setWordWrap(true);
  suspectNote->setStyleSheet("color:" + BadText() + ";");
  robustLayout->addWidget(suspectNote);
  auto* applyRobust = new QPushButton("Apply", robustTab);
  applyRobust->setObjectName("pdfRobustApply");
  robustLayout->addWidget(applyRobust, 0, Qt::AlignLeft);
  tabs->addTab(robustTab, "Robust calibration");
  tabs->setCurrentIndex(std::clamp(tab, 0, 1));

  auto* closeRow = new QHBoxLayout();
  closeRow->addStretch();
  auto* close = new QPushButton("Close", &dialog);
  close->setObjectName("pdfChecksClose");
  closeRow->addWidget(close);
  root->addLayout(closeRow);
  QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);

  // ---- Refresh (the dialog is a live view of the window's state) --------------------------------------
  bool refreshing = false;
  PageScale pendingScale;       // what "Apply correction" would set
  bool pendingValid = false;
  std::function<void()> refreshChecks, refreshRobust, refreshAll;

  auto pageChecks = [&] {
    std::vector<int> index;
    const auto& all = w.m_Annotations.Checks();
    for (std::size_t i = 0; i < all.size(); ++i)
      if (all[i].page == page) index.push_back(static_cast<int>(i));
    return index;
  };

  refreshChecks = [&] {
    refreshing = true;
    const PageScale* scale = w.EffectiveScale(page);
    const auto& all = w.m_Annotations.Checks();
    const std::vector<int> idx = pageChecks();
    pendingValid = false;
    if (!scale) {
      header->setText(QString("Page %1 has no scale yet. Set one (Set scale...), then check it against dimensions the drawing states.").arg(page + 1));
      body->setVisible(false);
      refreshing = false;
      return;
    }
    body->setVisible(true);
    header->setText(QString("Page %1: %2%3").arg(page + 1).arg(Q(scale->RatioText())).arg(scale->note.empty() ? QString() : " (" + Q(scale->note) + ")"));
    empty->setVisible(idx.empty());
    std::vector<FitObs> fit;
    for (int i : idx) fit.push_back({all[static_cast<std::size_t>(i)].MeasuredPt(), all[static_cast<std::size_t>(i)].StatedMetres()});
    const BestFit best = BestFitScale(fit);

    table->setVisible(!idx.empty());
    table->setRowCount(static_cast<int>(idx.size()));
    for (std::size_t k = 0; k < idx.size(); ++k) {
      const ScaleCheck& c = all[static_cast<std::size_t>(idx[k])];
      const CheckResult r = EvaluateCheck(c, *scale, w.m_Limits);
      const QColor colour = VerdictColor(r.verdict);
      const int row = static_cast<int>(k);
      const QColor red = VerdictColor(Verdict::Blunder);
      table->setItem(row, 0, Cell(QString::number(k + 1), Qt::AlignCenter));
      table->setItem(row, 1, Cell(c.calibration ? "calibration" : "check", Qt::AlignLeft | Qt::AlignVCenter));
      table->setItem(row, 2, Cell(Q(FormatValue(r.stated, 4) + " " + UnitLabel(c.unit))));
      table->setItem(row, 3, Cell(Q(FormatValue(r.measured, 4) + " " + UnitLabel(c.unit))));
      table->setItem(row, 4, Cell(Q(Signed(r.diff, 4) + " " + UnitLabel(c.unit))));
      table->setItem(row, 5, Cell(Q(Signed(r.pct, 2))));
      table->setItem(row, 6, Cell(VerdictName(r.verdict), Qt::AlignCenter, &colour));
      const bool outlier = k < best.outlier.size() && best.outlier[k];
      table->setItem(row, 7, Cell(outlier ? "Outlier" : "", Qt::AlignCenter, &red));
      auto* remove = new QPushButton("Delete", table);
      remove->setObjectName(QString("pdfCheckDelete%1").arg(k));
      const int global = idx[k];
      QObject::connect(remove, &QPushButton::clicked, &dialog, [&, global] {
        w.m_Annotations.RemoveCheck(global);
        w.SceneChanged();
        QTimer::singleShot(0, &dialog, [&] { refreshAll(); }); // not inside the button's own click
      });
      table->setCellWidget(row, 8, remove);
    }

    if (best.valid) {
      PageScale fitted = *scale;
      fitted.label.clear();
      fitted.note.clear();
      fitted.realValue = best.metresPerPt * 72.0 / UnitInMetres(scale->realUnit);
      bestFit->setText(QString("Best fit of %1 checks (longer ones count for more): %2").arg(idx.size()).arg(Q(fitted.RatioText())));
    } else {
      bestFit->clear();
    }
    bestFit->setVisible(best.valid);
    QString note;
    if (best.valid && best.anyOutlier) note = "One or more checks disagree with the rest (marked Outlier): a wrong pick, a mistyped value or a wrong unit.";
    else if (best.valid && idx.size() == 2 && std::fabs(fit[0].metres / fit[0].pts / (fit[1].metres / fit[1].pts) - 1.0) > 0.0025)
      note = "The two checks disagree: add a third to see which one is off.";
    outlierNote->setText(note);
    outlierNote->setStyleSheet("color:" + (best.anyOutlier ? BadText() : WarnText()) + ";");
    outlierNote->setVisible(!note.isEmpty());

    correction->setVisible(!idx.empty());
    if (idx.empty()) { refreshing = false; return; }
    const int keepMatch = std::clamp(matchIndex->currentIndex(), 0, static_cast<int>(idx.size()) - 1);
    matchIndex->clear();
    for (std::size_t k = 0; k < idx.size(); ++k) matchIndex->addItem(QString::number(k + 1));
    matchIndex->setCurrentIndex(keepMatch);
    bestMode->setEnabled(best.valid);
    withoutMode->setEnabled(best.withoutValid);
    if ((modes->checkedId() == 1 && !best.valid) || (modes->checkedId() == 2 && !best.withoutValid)) matchMode->setChecked(true);

    const double currentMetres = MetresPerPt(*scale);
    double factor = 1.0;
    const int mode = modes->checkedId();
    if (mode == 0) factor = FactorMatching(all[static_cast<std::size_t>(idx[static_cast<std::size_t>(matchIndex->currentIndex())])], *scale);
    else if (mode == 1 && best.valid && currentMetres > 0.0) factor = best.metresPerPt / currentMetres;
    else if (mode == 2 && best.withoutValid && currentMetres > 0.0) factor = best.withoutMetresPerPt / currentMetres;
    else if (mode == 3) factor = FactorFromPercent(percent->value());
    std::vector<ScaleCheck> mine;
    for (int i : idx) mine.push_back(all[static_cast<std::size_t>(i)]);
    const std::vector<CheckResult> would = PreviewCorrection(mine, *scale, factor, w.m_Limits);
    const PageScale next = ApplyFactor(*scale, factor);
    after->setText(QString("After this correction: %1%2").arg(Q(next.RatioText())).arg(next.note.empty() ? QString() : "  (" + Q(next.note) + ")"));
    afterTable->setRowCount(static_cast<int>(would.size()));
    for (std::size_t k = 0; k < would.size(); ++k) {
      const int row = static_cast<int>(k);
      const QColor colour = VerdictColor(would[k].verdict);
      afterTable->setItem(row, 0, Cell(QString::number(k + 1), Qt::AlignCenter));
      afterTable->setItem(row, 1, Cell(Q(FormatValue(would[k].measured, 4) + " " + UnitLabel(mine[k].unit))));
      afterTable->setItem(row, 2, Cell(Q(Signed(would[k].diff, 4) + " " + UnitLabel(mine[k].unit))));
      afterTable->setItem(row, 3, Cell(Q(Signed(would[k].pct, 2))));
      afterTable->setItem(row, 4, Cell(VerdictName(would[k].verdict), Qt::AlignCenter, &colour));
    }
    pendingScale = next;
    pendingValid = factor != 1.0 && mode != 4;
    applyCorrection->setEnabled(pendingValid);
    refreshing = false;
  };

  refreshRobust = [&] {
    refreshing = true;
    w.m_Robust.pickPt = std::max(0.01, pickError->value());
    w.m_Robust.drawingFraction = std::max(0.0, drawingError->value()) / 100.0;
    w.m_Robust.minDimensions = minimum->value();
    intro->setText(QString("For a more exact scale: add at least %1 dimensions the drawing states. One scale is solved by least squares; "
                           "long dimensions count for more, because one pick is a small part of them. Nothing changes until you Apply.")
                       .arg(w.m_Robust.minDimensions));
    std::vector<RobustObs> obs;
    for (const auto& [c, use] : w.m_RobustPicks) obs.push_back({c.MeasuredPt(), c.StatedMetres(), use});
    const RobustResult res = SolveRobust(obs, w.m_Robust);
    robustTable->setVisible(!w.m_RobustPicks.empty());
    robustTable->setRowCount(static_cast<int>(w.m_RobustPicks.size()));
    for (std::size_t i = 0; i < w.m_RobustPicks.size(); ++i) {
      const ScaleCheck& c = w.m_RobustPicks[i].first;
      const bool have = res.ok && w.m_RobustPicks[i].second && i < res.residualMetres.size();
      const bool suspect = have && i < res.suspect.size() && res.suspect[i];
      const QColor red = VerdictColor(Verdict::Blunder);
      const int row = static_cast<int>(i);
      auto* use = new QTableWidgetItem();
      use->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
      use->setCheckState(w.m_RobustPicks[i].second ? Qt::Checked : Qt::Unchecked);
      robustTable->setItem(row, 0, use);
      robustTable->setItem(row, 1, Cell(QString::number(i + 1), Qt::AlignCenter));
      robustTable->setItem(row, 2, Cell(Q(FormatValue(c.stated, 4) + " " + UnitLabel(c.unit))));
      robustTable->setItem(row, 3, Cell(Q(FormatValue(c.MeasuredPt(), 3))));
      robustTable->setItem(row, 4, Cell(have ? Q(Signed(res.residualMetres[i] / UnitInMetres(c.unit), 4) + " " + UnitLabel(c.unit)) : QString()));
      robustTable->setItem(row, 5, Cell(have ? Q(Signed(res.residualPct[i], 2)) : QString()));
      robustTable->setItem(row, 6, Cell(have ? Q(Signed(res.z[i], 1)) + (suspect ? "  Suspect" : "") : QString(), Qt::AlignRight | Qt::AlignVCenter, suspect ? &red : nullptr));
      auto* remove = new QPushButton("Remove", robustTable);
      remove->setObjectName(QString("pdfRobustRemove%1").arg(i));
      const int which = static_cast<int>(i);
      QObject::connect(remove, &QPushButton::clicked, &dialog, [&, which] {
        w.m_RobustPicks.erase(w.m_RobustPicks.begin() + which);
        w.m_Page->update();
        QTimer::singleShot(0, &dialog, [&] { refreshRobust(); });
      });
      robustTable->setCellWidget(row, 7, remove);
    }
    bool anySuspect = false;
    for (bool s : res.suspect) anySuspect = anySuspect || s;
    suspectNote->setVisible(res.ok && anySuspect);
    applyRobust->setEnabled(res.ok);
    if (res.ok) {
      const PageScale* scale = w.EffectiveScale(page);
      const Unit unit = scale ? scale->realUnit : w.m_RobustPicks.front().first.unit;
      const PageScale made = ScaleFromRobust(res, unit, scale);
      robustResult->setText(QString("Adjusted scale: %1 +/- %2 %  (%3 dimensions; RMS residual %4 %5)")
                                .arg(Q(made.RatioText())).arg(Q(FormatValue(res.relSigma * 100.0, 3))).arg(res.used)
                                .arg(Q(FormatValue(res.rmsMetres / UnitInMetres(unit), 4))).arg(UnitLabel(unit)));
      robustResult->setStyleSheet(QString());
    } else {
      robustResult->setText(Q(res.why));
      robustResult->setStyleSheet("color:" + WarnText() + ";");
    }
    refreshing = false;
  };
  refreshAll = [&] { refreshChecks(); refreshRobust(); };

  // ---- Wiring --------------------------------------------------------------------------------------
  QObject::connect(good, &QDoubleSpinBox::valueChanged, &dialog, [&] {
    if (refreshing) return;
    w.m_Limits.goodPct = good->value();
    w.m_Limits.checkPct = std::max(check->value(), good->value());
    check->setValue(w.m_Limits.checkPct);
    w.UpdateScaleDisplay();
    w.m_Page->update();
    refreshChecks();
  });
  QObject::connect(check, &QDoubleSpinBox::valueChanged, &dialog, [&] {
    if (refreshing) return;
    w.m_Limits.checkPct = std::max(check->value(), good->value());
    if (check->value() < good->value()) check->setValue(w.m_Limits.checkPct);
    w.UpdateScaleDisplay();
    w.m_Page->update();
    refreshChecks();
  });
  QObject::connect(showOnSheet, &QCheckBox::toggled, &dialog, [&](bool on) { w.m_ShowChecksBox->setChecked(on); });
  QObject::connect(modes, &QButtonGroup::idToggled, &dialog, [&](int, bool on) { if (on && !refreshing) refreshChecks(); });
  QObject::connect(matchIndex, &QComboBox::currentIndexChanged, &dialog, [&] { if (!refreshing) { matchMode->setChecked(true); refreshChecks(); } });
  QObject::connect(percent, &QDoubleSpinBox::valueChanged, &dialog, [&] { if (!refreshing) { percentMode->setChecked(true); refreshChecks(); } });
  QObject::connect(table, &QTableWidget::itemSelectionChanged, &dialog, [&] {
    if (refreshing) return;
    const auto rows = table->selectionModel()->selectedRows();
    const std::vector<int> idx = pageChecks();
    w.m_SelectedCheck = !rows.isEmpty() && rows.first().row() < static_cast<int>(idx.size()) ? idx[static_cast<std::size_t>(rows.first().row())] : -1;
    w.m_Page->update();
  });
  QObject::connect(applyCorrection, &QPushButton::clicked, &dialog, [&] {
    if (!pendingValid) return;
    w.m_Annotations.SetScales({{page, pendingScale}});
    qInfo().noquote() << "PDF scale: corrected page" << page + 1 << "-" << Q(pendingScale.note);
    w.SceneChanged();
    w.statusBar()->showMessage("Scale " + Q(pendingScale.note) + ".");
    refreshAll();
  });
  QObject::connect(copyReport, &QPushButton::clicked, &dialog, [&] {
    const PageScale* scale = w.EffectiveScale(page);
    if (!scale) return;
    std::vector<ScaleCheck> mine;
    for (int i : pageChecks()) mine.push_back(w.m_Annotations.Checks()[static_cast<std::size_t>(i)]);
    const std::string report = ScaleReport(w.windowTitle().toUtf8().toStdString(), page, *scale, mine, w.m_Limits);
    QApplication::clipboard()->setText(Q(report));
    qInfo().noquote() << Q(report);
    w.statusBar()->showMessage("Scale report copied.");
  });
  for (auto* spin : std::initializer_list<QAbstractSpinBox*>{pickError, drawingError, minimum}) {
    if (auto* d = qobject_cast<QDoubleSpinBox*>(spin)) QObject::connect(d, &QDoubleSpinBox::valueChanged, &dialog, [&] { if (!refreshing) refreshRobust(); });
    else if (auto* s = qobject_cast<QSpinBox*>(spin)) QObject::connect(s, &QSpinBox::valueChanged, &dialog, [&] { if (!refreshing) refreshRobust(); });
  }
  QObject::connect(robustTable, &QTableWidget::itemChanged, &dialog, [&](QTableWidgetItem* item) {
    if (refreshing || item->column() != 0 || item->row() >= static_cast<int>(w.m_RobustPicks.size())) return;
    w.m_RobustPicks[static_cast<std::size_t>(item->row())].second = item->checkState() == Qt::Checked;
    w.m_Page->update();
    QTimer::singleShot(0, &dialog, [&] { refreshRobust(); }); // the table's items are replaced: not while one is signalling
  });
  QObject::connect(clearList, &QPushButton::clicked, &dialog, [&] { w.m_RobustPicks.clear(); w.m_Page->update(); refreshRobust(); });
  bool wantDimension = false;
  QObject::connect(addDimension, &QPushButton::clicked, &dialog, [&] { wantDimension = true; dialog.accept(); });
  QObject::connect(applyRobust, &QPushButton::clicked, &dialog, [&] {
    std::vector<RobustObs> obs;
    for (const auto& [c, use] : w.m_RobustPicks) obs.push_back({c.MeasuredPt(), c.StatedMetres(), use});
    const RobustResult res = SolveRobust(obs, w.m_Robust);
    if (!res.ok || w.m_RobustPicks.empty()) return;
    const int robustPage = w.m_RobustPicks.front().first.page;
    const PageScale* scale = w.EffectiveScale(robustPage);
    const PageScale made = ScaleFromRobust(res, scale ? scale->realUnit : w.m_RobustPicks.front().first.unit, scale);
    w.m_Annotations.SetScales({{robustPage, made}});
    qInfo().noquote() << "PDF scale: robust calibration of page" << robustPage + 1 << "-" << Q(made.note);
    w.SceneChanged();
    w.statusBar()->showMessage("Scale " + Q(made.note) + ".");
    dialog.accept();
  });

  refreshAll();
  dialog.exec();
  w.m_SelectedCheck = -1;
  w.m_Page->update();
  if (wantDimension) {
    w.m_CheckTarget = 1;
    w.SetTool(PdfWindow::kToolCheck, "Robust calibration: click the two ends of a dimension the drawing states.");
  }
}
