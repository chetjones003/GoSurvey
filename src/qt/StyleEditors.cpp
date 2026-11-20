#include "StyleEditors.hpp"

#include "CadColor.hpp"
#include "StylePreview.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <vector>

namespace {

QString Q(const std::string& s) { return QString::fromUtf8(s.c_str()); }
std::string S(const QString& s) { return s.toUtf8().toStdString(); }

const QStringList kLinetypes = {"ByLayer", "ByBlock", "Continuous", "Dashed", "Center", "Hidden", "Phantom", "DashDot"};
const QStringList kLineweights = {"ByLayer", "ByBlock", "Default", "0.00 mm", "0.05 mm", "0.09 mm", "0.13 mm", "0.15 mm",
                                  "0.18 mm", "0.20 mm", "0.25 mm", "0.30 mm", "0.35 mm", "0.40 mm", "0.50 mm",
                                  "0.60 mm", "0.70 mm", "0.80 mm", "1.00 mm", "1.40 mm", "2.00 mm"};
const QStringList kColors = {"ByLayer", "ByBlock", "Red", "Yellow", "Green", "Cyan", "Blue", "Magenta", "White"};
const QStringList kDecimalPrecision = {"0", "0.0", "0.00", "0.000", "0.0000", "0.00000", "0.000000", "0.0000000", "0.00000000"};
const QStringList kAngularFormats = {"Decimal Degrees", "Degrees Minutes Seconds", "Gradians", "Radians", "Surveyor's Units"};
const QStringList kAngularPrecision = {"0", "0.0", "0.00", "0.000", "0.0000", "0d", "0d00'", "0d00'00\"", "0d00'00.0\"", "0d00'00.00\""};
const QStringList kUnitFormats = {"Decimal", "Architectural", "Engineering", "Fractional"};

QColor ResolveColor(const QString& value, const QColor& fallback = QColor(255, 255, 255)) {
  if (value.isEmpty() || value == QLatin1String("None")) return fallback;
  float rgb[3] = {fallback.redF(), fallback.greenF(), fallback.blueF()};
  CadColorResolveRgb(CadColorEffectiveStorage(S(value), "White"), rgb[0], rgb[1], rgb[2], rgb);
  return QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
}

QIcon Swatch(const QString& value) {
  QPixmap pixmap(14, 14);
  if (value == QLatin1String("None")) {
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(QColor(150, 158, 164));
    painter.drawRect(0, 0, 13, 13);
  } else {
    pixmap.fill(ResolveColor(value));
  }
  return QIcon(pixmap);
}

// Reads and writes the widgets of one style window by key. Every widget reports through `touch` so the window can
// redraw its preview and refresh which controls are enabled.
class Binder {
 public:
  using Load = std::function<QString(const QString& key, const QString& def)>;
  explicit Binder(Load load) : m_Load(std::move(load)) {}

  std::function<void()> onChange;
  QMap<QString, std::function<QString()>> readers;
  QMap<QString, std::function<void(const QString&)>> writers;

  QString Value(const QString& key) const { return readers.contains(key) ? readers[key]() : QString(); }
  bool Checked(const QString& key) const { return Value(key) == QLatin1String("1"); }
  void EnableWhen(QWidget* widget, const QString& key, std::function<bool(const QString&)> predicate) {
    m_Updaters.push_back([this, widget, key, predicate] { widget->setEnabled(predicate(Value(key))); });
  }
  void RefreshEnabled() { for (const auto& update : m_Updaters) update(); }
  void SetAll(const std::function<QString(const QString&)>& valueFor) {
    m_Quiet = true;
    for (auto it = writers.begin(); it != writers.end(); ++it) it.value()(valueFor(it.key()));
    m_Quiet = false;
    RefreshEnabled();
  }

  QComboBox* Combo(QWidget* parent, const QString& key, const QStringList& options, const QString& def) {
    auto* box = new QComboBox(parent);
    box->setObjectName(key);
    box->addItems(options);
    const QString value = m_Load(key, def);
    if (box->findText(value) < 0) box->addItem(value);
    box->setCurrentText(value);
    readers[key] = [box] { return box->currentText(); };
    writers[key] = [box](const QString& v) { if (box->findText(v) < 0) box->addItem(v); box->setCurrentText(v); };
    QObject::connect(box, &QComboBox::currentTextChanged, parent, [this] { Touch(); });
    return box;
  }

  QComboBox* Color(QWidget* parent, const QString& key, const QString& def, bool allowNone = false) {
    auto* box = new QComboBox(parent);
    box->setObjectName(key);
    QStringList options = kColors;
    if (allowNone) options.prepend("None");
    for (const QString& option : options) box->addItem(Swatch(option), option);
    const QString value = m_Load(key, def);
    if (box->findText(value) < 0) box->addItem(Swatch(value), value);
    box->addItem(QStringLiteral("Select Color…"));
    box->setCurrentText(value);
    readers[key] = [box] { return box->currentText() == QLatin1String("Select Color…") ? QString() : box->currentText(); };
    writers[key] = [box](const QString& v) {
      const int existing = box->findText(v);
      if (existing < 0) box->insertItem(box->count() - 1, Swatch(v), v);
      box->setCurrentText(v);
    };
    auto* last = new QString(value);  // owned by the combo through the lambda below
    QObject::connect(box, &QObject::destroyed, [last] { delete last; });
    QObject::connect(box, QOverload<int>::of(&QComboBox::activated), parent, [this, box, last](int index) {
      if (index == box->count() - 1) {
        const QColor picked = QColorDialog::getColor(ResolveColor(*last), box->window(), "Select Color");
        if (picked.isValid()) {
          const QString hex = picked.name();
          if (box->findText(hex) < 0) box->insertItem(box->count() - 1, Swatch(hex), hex);
          box->setCurrentText(hex);
        } else {
          box->setCurrentText(*last);
        }
      }
      *last = box->currentText();
      Touch();
    });
    return box;
  }

  QCheckBox* Check(QWidget* parent, const QString& key, const QString& text, bool def) {
    auto* box = new QCheckBox(text, parent);
    box->setObjectName(key);
    box->setChecked(m_Load(key, def ? "1" : "0") == QLatin1String("1"));
    readers[key] = [box] { return QString(box->isChecked() ? "1" : "0"); };
    writers[key] = [box](const QString& v) { box->setChecked(v == QLatin1String("1")); };
    QObject::connect(box, &QCheckBox::toggled, parent, [this] { Touch(); });
    return box;
  }

  QDoubleSpinBox* Double(QWidget* parent, const QString& key, double def, double min, double max, int decimals = 4, double step = 0.01) {
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(key);
    box->setRange(min, max);
    box->setDecimals(decimals);
    box->setSingleStep(step);
    box->setValue(m_Load(key, QString::number(def, 'g', 10)).toDouble());
    readers[key] = [box] { return QString::number(box->value(), 'g', 10); };
    writers[key] = [box](const QString& v) { box->setValue(v.toDouble()); };
    QObject::connect(box, &QDoubleSpinBox::valueChanged, parent, [this] { Touch(); });
    return box;
  }

  QSpinBox* Int(QWidget* parent, const QString& key, int def, int min, int max) {
    auto* box = new QSpinBox(parent);
    box->setObjectName(key);
    box->setRange(min, max);
    box->setValue(m_Load(key, QString::number(def)).toInt());
    readers[key] = [box] { return QString::number(box->value()); };
    writers[key] = [box](const QString& v) { box->setValue(v.toInt()); };
    QObject::connect(box, &QSpinBox::valueChanged, parent, [this] { Touch(); });
    return box;
  }

  QLineEdit* Text(QWidget* parent, const QString& key, const QString& def) {
    auto* edit = new QLineEdit(m_Load(key, def), parent);
    edit->setObjectName(key);
    readers[key] = [edit] { return edit->text(); };
    writers[key] = [edit](const QString& v) { edit->setText(v); };
    QObject::connect(edit, &QLineEdit::textChanged, parent, [this] { Touch(); });
    return edit;
  }

  // A column of radio buttons; the value is the checked button's text.
  std::vector<QRadioButton*> Radios(QWidget* parent, QLayout* into, const QString& key, const QStringList& options, const QString& def) {
    auto* group = new QButtonGroup(parent);
    std::vector<QRadioButton*> buttons;
    const QString value = m_Load(key, def);
    for (const QString& option : options) {
      auto* button = new QRadioButton(option, parent);
      button->setObjectName(key + ":" + option);
      button->setChecked(option == value);
      group->addButton(button);
      into->addWidget(button);
      buttons.push_back(button);
      QObject::connect(button, &QRadioButton::toggled, parent, [this] { Touch(); });
    }
    readers[key] = [buttons] { for (auto* b : buttons) if (b->isChecked()) return b->text(); return QString(); };
    writers[key] = [buttons](const QString& v) { for (auto* b : buttons) b->setChecked(b->text() == v); };
    return buttons;
  }

 private:
  void Touch() {
    if (m_Quiet) return;
    RefreshEnabled();
    if (onChange) onChange();
  }
  Load m_Load;
  bool m_Quiet = false;
  std::vector<std::function<void()>> m_Updaters;
};

QGroupBox* Group(const QString& title, QVBoxLayout* into, QLayout* inner) {
  auto* box = new QGroupBox(title);
  box->setLayout(inner);
  into->addWidget(box);
  return box;
}

QFormLayout* Form() {
  auto* form = new QFormLayout;
  form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  return form;
}

struct Page {
  QVBoxLayout* left = nullptr;
  QVBoxLayout* right = nullptr;
  QVBoxLayout* bottom = nullptr;
  QLabel* preview = nullptr;
};

// A tab with a left column of groups and a right column that starts with the preview (when wanted).
Page MakePage(QTabWidget* tabs, const QString& title, bool withPreview) {
  auto* widget = new QWidget(tabs);
  auto* root = new QVBoxLayout(widget);
  auto* row = new QHBoxLayout;
  root->addLayout(row);
  Page page;
  page.left = new QVBoxLayout;
  page.right = new QVBoxLayout;
  row->addLayout(page.left, 1);
  row->addLayout(page.right, 1);
  page.bottom = new QVBoxLayout;
  root->addLayout(page.bottom);
  root->addStretch(1);
  if (withPreview) {
    page.preview = new QLabel(widget);
    page.preview->setMinimumSize(380, 205);
    page.preview->setMaximumHeight(240);
    page.preview->setAlignment(Qt::AlignCenter);
    page.preview->setFrameShape(QFrame::StyledPanel);
    page.right->addWidget(page.preview);
  }
  tabs->addTab(widget, title);
  return page;
}

void FinishPage(const Page& page) {
  page.left->addStretch(1);
  page.right->addStretch(1);
}

QDialog* MakeDialog(QWidget* parent, const QString& title, const QString& objectName, QSize size) {
  auto* dialog = new QDialog(parent);
  dialog->setObjectName(objectName);
  dialog->setWindowTitle(title);
  dialog->resize(size);
  return dialog;
}

QDialogButtonBox* AddButtons(QDialog* dialog, QVBoxLayout* root, bool help = true) {
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | (help ? QDialogButtonBox::Help : QDialogButtonBox::NoButton), dialog);
  root->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  QObject::connect(buttons, &QDialogButtonBox::helpRequested, dialog, [dialog] {
    QMessageBox::information(dialog, "Help", "Style settings are described in the in-app help under Annotation Styles.");
  });
  return buttons;
}

// ---------------------------------------------------------------------------------------------------------------
// Dimension styles

QString DimLoad(const DimensionStyle& s, const QString& key, const QString& def) {
  if (key == "dimLineColor") return Q(s.dimLineColor);
  if (key == "dimLineType") return Q(s.dimLineType);
  if (key == "extLineColor") return Q(s.extLineColor);
  if (key == "extLineType1") return Q(s.extLineType);
  if (key == "arrow1") return QString(DimArrowTypeName(s.arrowType));
  if (key == "arrowSize") return QString::number(s.arrowSizeInches, 'g', 6);
  if (key == "textStyle") return s.textFont.empty() ? QString("Standard") : Q(s.textFont);
  if (key == "textColor") return Q(s.textColor);
  if (key == "textHeight") return QString::number(s.textSizeInches, 'g', 6);
  if (key == "textVertical") return s.textAlign == DimTextAlign::Above ? "Above" : s.textAlign == DimTextAlign::Beside ? "Outside" : "Centered";
  if (key == "unitFormat") return DimUnitFormatName(s.unitFormat);
  if (key == "unitPrecision") return kDecimalPrecision.value(std::clamp(s.unitPrecision, 0, 8));
  if (key == "scaleFactor") return QString::number(s.unitScale, 'g', 10);
  if (key == "suffix") return Q(s.unitSuffix);
  const auto it = s.props.find(S(key));
  return it == s.props.end() ? def : Q(it->second);
}

void DimStore(DimensionStyle& s, const QString& key, const QString& value) {
  if (key == "dimLineColor") s.dimLineColor = S(value);
  else if (key == "dimLineType") s.dimLineType = S(value);
  else if (key == "extLineColor") s.extLineColor = S(value);
  else if (key == "extLineType1") s.extLineType = S(value);
  else if (key == "arrow1") {
    for (int i = 0; i <= static_cast<int>(DimArrowType::None); ++i)
      if (value == DimArrowTypeName(static_cast<DimArrowType>(i))) s.arrowType = static_cast<DimArrowType>(i);
  } else if (key == "arrowSize") s.arrowSizeInches = value.toFloat();
  else if (key == "textStyle") s.textFont = value == "Standard" ? std::string() : S(value);
  else if (key == "textColor") s.textColor = S(value);
  else if (key == "textHeight") s.textSizeInches = value.toFloat();
  else if (key == "textVertical") s.textAlign = value == "Above" ? DimTextAlign::Above : value == "Outside" ? DimTextAlign::Beside : DimTextAlign::Center;
  else if (key == "unitFormat") {
    for (int i = 0; i <= static_cast<int>(DimUnitFormat::Fractional); ++i)
      if (value == DimUnitFormatName(static_cast<DimUnitFormat>(i))) s.unitFormat = static_cast<DimUnitFormat>(i);
  } else if (key == "unitPrecision") s.unitPrecision = std::max(0, static_cast<int>(kDecimalPrecision.indexOf(value)));
  else if (key == "scaleFactor") s.unitScale = value.toDouble();
  else if (key == "suffix") s.unitSuffix = S(value);
  else s.props[S(key)] = S(value);
}

}  // namespace

bool EditDimensionStyleDialog(QWidget* parent, DimensionStyle* style, bool create, const QStringList& textStyleNames) {
  if (!style) return false;
  const DimensionStyle original = *style;
  Binder b([&original](const QString& key, const QString& def) { return DimLoad(original, key, def); });
  QDialog* dialog = MakeDialog(parent, (create ? QString("New Dimension Style") : QString("Modify Dimension Style: ")) + (create ? QString() : Q(style->name)),
                               "dimensionStyleEditor", QSize(880, 760));
  auto* root = new QVBoxLayout(dialog);
  auto* name = new QLineEdit(Q(style->name), dialog);
  name->setObjectName("dimensionStyleName");
  name->setReadOnly(!create);
  if (create) root->addWidget(name); else name->hide();
  auto* tabs = new QTabWidget(dialog);
  std::vector<QLabel*> previews;
  const auto track = [&](const Page& page) { if (page.preview) previews.push_back(page.preview); };
  const QStringList arrowNames = [] { QStringList out; for (int i = 0; i <= static_cast<int>(DimArrowType::None); ++i) out << DimArrowTypeName(static_cast<DimArrowType>(i)); return out; }();
  QStringList textStyles = textStyleNames;
  if (!textStyles.contains("Standard")) textStyles.prepend("Standard");

  // --- Lines
  {
    Page page = MakePage(tabs, "Lines", true);
    auto* dim = Form();
    dim->addRow("Color:", b.Color(dialog, "dimLineColor", "ByLayer"));
    dim->addRow("Linetype:", b.Combo(dialog, "dimLineType", kLinetypes, "ByLayer"));
    dim->addRow("Lineweight:", b.Combo(dialog, "dimLineWeight", kLineweights, "ByLayer"));
    auto* ticks = b.Double(dialog, "extendBeyondTicks", 0.0, 0.0, 1000.0);
    dim->addRow("Extend beyond ticks:", ticks);
    b.EnableWhen(ticks, "arrow1", [](const QString& v) { return v == "Architectural Tick"; });
    dim->addRow("Baseline spacing:", b.Double(dialog, "baselineSpacing", 0.38, 0.0, 1000.0));
    auto* suppress = new QHBoxLayout;
    suppress->addWidget(b.Check(dialog, "suppressDim1", "Dim line 1", false));
    suppress->addWidget(b.Check(dialog, "suppressDim2", "Dim line 2", false));
    dim->addRow("Suppress:", suppress);
    Group("Dimension lines", page.left, dim);
    auto* ext = Form();
    ext->addRow("Color:", b.Color(dialog, "extLineColor", "ByLayer"));
    ext->addRow("Linetype ext line 1:", b.Combo(dialog, "extLineType1", kLinetypes, "ByBlock"));
    ext->addRow("Linetype ext line 2:", b.Combo(dialog, "extLineType2", kLinetypes, "ByBlock"));
    ext->addRow("Lineweight:", b.Combo(dialog, "extLineWeight", kLineweights, "ByBlock"));
    auto* suppressExt = new QHBoxLayout;
    suppressExt->addWidget(b.Check(dialog, "suppressExt1", "Ext line 1", false));
    suppressExt->addWidget(b.Check(dialog, "suppressExt2", "Ext line 2", false));
    ext->addRow("Suppress:", suppressExt);
    auto* more = Form();
    more->addRow("Extend beyond dim lines:", b.Double(dialog, "extendBeyondDimLines", 0.18, 0.0, 1000.0));
    more->addRow("Offset from origin:", b.Double(dialog, "offsetFromOrigin", 0.0625, 0.0, 1000.0));
    auto* fixed = b.Check(dialog, "fixedLengthExt", "Fixed length extension lines", false);
    more->addRow(fixed);
    auto* length = b.Double(dialog, "fixedLengthExtValue", 1.0, 0.0, 100000.0);
    more->addRow("Length:", length);
    b.EnableWhen(length, "fixedLengthExt", [](const QString& v) { return v == "1"; });
    auto* extensionColumns = new QHBoxLayout;
    extensionColumns->addLayout(ext, 1);
    extensionColumns->addLayout(more, 1);
    Group("Extension lines", page.bottom, extensionColumns);
    FinishPage(page);
    track(page);
  }
  // --- Symbols and arrows
  {
    Page page = MakePage(tabs, "Symbols and Arrows", true);
    auto* arrows = Form();
    arrows->addRow("First:", b.Combo(dialog, "arrow1", arrowNames, DimArrowTypeName(DimArrowType::ClosedFilled)));
    arrows->addRow("Second:", b.Combo(dialog, "arrow2", arrowNames, DimArrowTypeName(DimArrowType::ClosedFilled)));
    arrows->addRow("Leader:", b.Combo(dialog, "arrowLeader", arrowNames, DimArrowTypeName(DimArrowType::ClosedFilled)));
    arrows->addRow("Arrow size:", b.Double(dialog, "arrowSize", 0.10, 0.0, 1000.0));
    Group("Arrowheads", page.left, arrows);
    auto* centre = new QVBoxLayout;
    b.Radios(dialog, centre, "centerMark", {"None", "Mark", "Line"}, "Mark");
    auto* centreSize = b.Double(dialog, "centerMarkSize", 0.09, 0.0, 1000.0);
    centre->addWidget(centreSize);
    b.EnableWhen(centreSize, "centerMark", [](const QString& v) { return v != "None"; });
    Group("Center marks", page.left, centre);
    auto* brk = Form();
    brk->addRow("Break size:", b.Double(dialog, "breakSize", 0.125, 0.0, 1000.0));
    Group("Dimension Break", page.left, brk);
    auto* arc = new QVBoxLayout;
    b.Radios(dialog, arc, "arcSymbol", {"Preceding dimension text", "Above dimension text", "None"}, "Preceding dimension text");
    Group("Arc length symbol", page.right, arc);
    auto* radius = Form();
    radius->addRow("Jog angle:", b.Text(dialog, "jogAngle", "N 45d0'0\" E"));
    Group("Radius jog dimension", page.right, radius);
    auto* linear = Form();
    auto* jogRow = new QHBoxLayout;
    jogRow->addWidget(b.Double(dialog, "jogHeightFactor", 1.5, 0.0, 100.0));
    jogRow->addWidget(new QLabel("* Text height"));
    linear->addRow("Jog height factor:", jogRow);
    Group("Linear jog dimension", page.right, linear);
    FinishPage(page);
    track(page);
  }
  // --- Text
  {
    Page page = MakePage(tabs, "Text", true);
    auto* look = Form();
    look->addRow("Text style:", b.Combo(dialog, "textStyle", textStyles, "Standard"));
    look->addRow("Text color:", b.Color(dialog, "textColor", "ByLayer"));
    look->addRow("Fill color:", b.Color(dialog, "textFill", "None", true));
    look->addRow("Text height:", b.Double(dialog, "textHeight", 0.10, 0.0001, 100000.0));
    auto* fraction = b.Double(dialog, "fractionHeightScale", 1.0, 0.01, 100.0);
    look->addRow("Fraction height scale:", fraction);
    b.EnableWhen(fraction, "unitFormat", [](const QString& v) { return v == "Architectural" || v == "Fractional"; });
    look->addRow(b.Check(dialog, "drawFrame", "Draw frame around text", false));
    Group("Text appearance", page.left, look);
    auto* place = Form();
    place->addRow("Vertical:", b.Combo(dialog, "textVertical", {"Centered", "Above", "Outside", "JIS"}, "Centered"));
    place->addRow("Horizontal:", b.Combo(dialog, "textHorizontal", {"Centered", "At Ext Line 1", "At Ext Line 2", "Over Ext Line 1", "Over Ext Line 2"}, "Centered"));
    place->addRow("View Direction:", b.Combo(dialog, "viewDirection", {"Left-to-Right", "Right-to-Left"}, "Left-to-Right"));
    place->addRow("Offset from dim line:", b.Double(dialog, "textOffset", 0.0625, 0.0, 1000.0));
    Group("Text placement", page.left, place);
    auto* align = new QVBoxLayout;
    b.Radios(dialog, align, "textAlignment", {"Horizontal", "Aligned with dimension line", "ISO standard"}, "Aligned with dimension line");
    Group("Text alignment", page.right, align);
    FinishPage(page);
    track(page);
  }
  // --- Fit
  {
    Page page = MakePage(tabs, "Fit", true);
    auto* fit = new QVBoxLayout;
    fit->addWidget(new QLabel("If there isn't enough room to place both text and arrows inside extension lines, the first thing to move outside the extension lines is:"));
    static_cast<QLabel*>(fit->itemAt(0)->widget())->setWordWrap(true);
    b.Radios(dialog, fit, "fitOption", {"Either text or arrows (best fit)", "Arrows", "Text", "Both text and arrows", "Always keep text between ext lines"}, "Either text or arrows (best fit)");
    fit->addWidget(b.Check(dialog, "suppressArrowsNoFit", "Suppress arrows if they don't fit inside extension lines", false));
    Group("Fit options", page.left, fit);
    auto* where = new QVBoxLayout;
    where->addWidget(new QLabel("When text is not in the default position, place it:"));
    b.Radios(dialog, where, "textPlacementFit", {"Beside the dimension line", "Over dimension line, with leader", "Over dimension line, without leader"}, "Beside the dimension line");
    Group("Text placement", page.left, where);
    auto* scale = new QVBoxLayout;
    scale->addWidget(b.Check(dialog, "annotative", "Annotative", true));
    auto radios = b.Radios(dialog, scale, "scaleMode", {"Scale dimensions to layout", "Use overall scale of:"}, "Use overall scale of:");
    auto* overall = b.Double(dialog, "overallScale", 1.0, 0.0001, 100000.0);
    scale->addWidget(overall);
    for (auto* radio : radios) b.EnableWhen(radio, "annotative", [](const QString& v) { return v != "1"; });
    b.EnableWhen(overall, "annotative", [](const QString& v) { return v != "1"; });
    Group("Scale for dimension features", page.right, scale);
    auto* tune = new QVBoxLayout;
    tune->addWidget(b.Check(dialog, "placeTextManually", "Place text manually", false));
    tune->addWidget(b.Check(dialog, "drawDimLineBetween", "Draw dim line between ext lines", false));
    Group("Fine tuning", page.right, tune);
    FinishPage(page);
    track(page);
  }
  // --- Primary units
  {
    Page page = MakePage(tabs, "Primary Units", true);
    auto* linear = Form();
    linear->addRow("Unit format:", b.Combo(dialog, "unitFormat", kUnitFormats, "Decimal"));
    linear->addRow("Precision", b.Combo(dialog, "unitPrecision", kDecimalPrecision, "0.00"));
    auto* fractionFormat = b.Combo(dialog, "fractionFormat", {"Horizontal", "Diagonal", "Not stacked"}, "Horizontal");
    linear->addRow("Fraction format:", fractionFormat);
    b.EnableWhen(fractionFormat, "unitFormat", [](const QString& v) { return v == "Architectural" || v == "Fractional"; });
    linear->addRow("Decimal separator:", b.Combo(dialog, "decimalSeparator", {"'.' (Period)", "',' (Comma)", "' ' (Space)"}, "'.' (Period)"));
    linear->addRow("Round off:", b.Double(dialog, "roundOff", 0.0, 0.0, 1000.0));
    linear->addRow("Prefix:", b.Text(dialog, "prefix", ""));
    linear->addRow("Suffix:", b.Text(dialog, "suffix", ""));
    auto* measure = Form();
    measure->addRow("Scale factor:", b.Double(dialog, "scaleFactor", 1.0, 0.000001, 100000.0, 6));
    measure->addRow(b.Check(dialog, "scaleLayoutOnly", "Apply to layout dimensions only", false));
    auto* zeros = new QGridLayout;
    zeros->addWidget(b.Check(dialog, "zeroLeading", "Leading", false), 0, 0);
    zeros->addWidget(b.Check(dialog, "zeroTrailing", "Trailing", false), 0, 1);
    auto* feet = b.Check(dialog, "zeroFeet", "0 feet", true);
    auto* inches = b.Check(dialog, "zeroInches", "0 inches", true);
    zeros->addWidget(feet, 1, 1);
    zeros->addWidget(inches, 2, 1);
    auto* subFactor = b.Double(dialog, "subUnitsFactor", 100.0, 0.0001, 1000000.0);
    auto* subSuffix = b.Text(dialog, "subUnitSuffix", "");
    zeros->addWidget(new QLabel("Sub-units factor:"), 3, 0);
    zeros->addWidget(subFactor, 4, 0);
    zeros->addWidget(new QLabel("Sub-unit suffix:"), 5, 0);
    zeros->addWidget(subSuffix, 6, 0);
    const auto architecturalOnly = [](const QString& v) { return v == "Architectural" || v == "Engineering"; };
    b.EnableWhen(feet, "unitFormat", architecturalOnly);
    b.EnableWhen(inches, "unitFormat", architecturalOnly);
    b.EnableWhen(subFactor, "zeroLeading", [](const QString& v) { return v == "1"; });
    b.EnableWhen(subSuffix, "zeroLeading", [](const QString& v) { return v == "1"; });
    auto* wrap = new QVBoxLayout;
    wrap->addLayout(linear);
    Group("Linear dimensions", page.left, wrap);
    Group("Measurement scale", page.left, measure);
    Group("Zero suppression", page.left, zeros);
    auto* angular = Form();
    angular->addRow("Units format:", b.Combo(dialog, "angularFormat", kAngularFormats, "Decimal Degrees"));
    angular->addRow("Precision:", b.Combo(dialog, "angularPrecision", kAngularPrecision, "0"));
    auto* angularZeros = new QVBoxLayout;
    angularZeros->addWidget(b.Check(dialog, "angZeroLeading", "Leading", false));
    angularZeros->addWidget(b.Check(dialog, "angZeroTrailing", "Trailing", false));
    auto* angularWrap = new QVBoxLayout;
    angularWrap->addLayout(angular);
    auto* zeroBox = new QGroupBox("Zero suppression");
    zeroBox->setLayout(angularZeros);
    angularWrap->addWidget(zeroBox);
    Group("Angular dimensions", page.right, angularWrap);
    FinishPage(page);
    track(page);
  }
  // --- Alternate units
  {
    Page page = MakePage(tabs, "Alternate Units", true);
    auto* display = b.Check(dialog, "altEnabled", "Display alternate units", false);
    page.left->addWidget(display);
    auto* alt = Form();
    std::vector<QWidget*> altWidgets;
    const auto add = [&](const QString& label, QWidget* w) { alt->addRow(label, w); altWidgets.push_back(w); };
    add("Unit format:", b.Combo(dialog, "altFormat", kUnitFormats, "Decimal"));
    add("Precision", b.Combo(dialog, "altPrecision", kDecimalPrecision, "0.00"));
    add("Multiplier for alt units:", b.Double(dialog, "altMultiplier", 25.4, 0.000001, 1000000.0));
    add("Round distances  to:", b.Double(dialog, "altRound", 0.0, 0.0, 1000.0));
    add("Prefix:", b.Text(dialog, "altPrefix", ""));
    add("Suffix:", b.Text(dialog, "altSuffix", ""));
    Group("Alternate units", page.left, alt);
    auto* zeros = new QGridLayout;
    zeros->addWidget(b.Check(dialog, "altZeroLeading", "Leading", false), 0, 0);
    zeros->addWidget(b.Check(dialog, "altZeroTrailing", "Trailing", false), 0, 1);
    zeros->addWidget(b.Check(dialog, "altZeroFeet", "0 feet", true), 1, 1);
    zeros->addWidget(b.Check(dialog, "altZeroInches", "0 inches", true), 2, 1);
    zeros->addWidget(new QLabel("Sub-units factor:"), 3, 0);
    zeros->addWidget(b.Double(dialog, "altSubUnitsFactor", 100.0, 0.0001, 1000000.0), 4, 0);
    zeros->addWidget(new QLabel("Sub-units suffix:"), 5, 0);
    zeros->addWidget(b.Text(dialog, "altSubUnitSuffix", ""), 6, 0);
    auto* zeroBox = Group("Zero suppression", page.left, zeros);
    auto* placement = new QVBoxLayout;
    auto radios = b.Radios(dialog, placement, "altPlacement", {"After primary value", "Below primary value"}, "After primary value");
    auto* placementBox = Group("Placement", page.right, placement);
    for (QWidget* w : altWidgets) b.EnableWhen(w, "altEnabled", [](const QString& v) { return v == "1"; });
    b.EnableWhen(zeroBox, "altEnabled", [](const QString& v) { return v == "1"; });
    b.EnableWhen(placementBox, "altEnabled", [](const QString& v) { return v == "1"; });
    (void)radios;
    FinishPage(page);
    track(page);
  }
  // --- Tolerances
  {
    Page page = MakePage(tabs, "Tolerances", true);
    auto* tol = Form();
    tol->addRow("Method:", b.Combo(dialog, "tolMethod", {"None", "Symmetrical", "Deviation", "Limits", "Basic"}, "None"));
    std::vector<QWidget*> tolWidgets;
    const auto add = [&](const QString& label, QWidget* w) { tol->addRow(label, w); tolWidgets.push_back(w); };
    add("Precision", b.Combo(dialog, "tolPrecision", kDecimalPrecision, "0.0"));
    add("Upper value:", b.Double(dialog, "tolUpper", 0.0, -100000.0, 100000.0));
    add("Lower value:", b.Double(dialog, "tolLower", 0.0, -100000.0, 100000.0));
    add("Scaling for height:", b.Double(dialog, "tolScaling", 1.0, 0.01, 100.0));
    tol->addRow("Vertical position:", b.Combo(dialog, "tolVertical", {"Bottom", "Middle", "Top"}, "Middle"));
    Group("Tolerance format", page.left, tol);
    auto* tolAlign = new QVBoxLayout;
    auto alignRadios = b.Radios(dialog, tolAlign, "tolAlign", {"Align decimal separators", "Align operational symbols"}, "Align operational symbols");
    auto* alignBox = Group("Tolerance alignment", page.left, tolAlign);
    auto* zeros = new QGridLayout;
    zeros->addWidget(b.Check(dialog, "tolZeroLeading", "Leading", false), 0, 0);
    zeros->addWidget(b.Check(dialog, "tolZeroTrailing", "Trailing", false), 1, 0);
    zeros->addWidget(b.Check(dialog, "tolZeroFeet", "0 feet", true), 0, 1);
    zeros->addWidget(b.Check(dialog, "tolZeroInches", "0 inches", true), 1, 1);
    auto* zeroBox = Group("Zero suppression", page.left, zeros);
    auto* altTol = Form();
    auto* altPrecision = b.Combo(dialog, "altTolPrecision", kDecimalPrecision, "0.00");
    altTol->addRow("Precision:", altPrecision);
    auto* altZeros = new QGridLayout;
    altZeros->addWidget(b.Check(dialog, "altTolZeroLeading", "Leading", false), 0, 0);
    altZeros->addWidget(b.Check(dialog, "altTolZeroTrailing", "Trailing", false), 1, 0);
    altZeros->addWidget(b.Check(dialog, "altTolZeroFeet", "0 feet", true), 0, 1);
    altZeros->addWidget(b.Check(dialog, "altTolZeroInches", "0 inches", true), 1, 1);
    auto* altZeroBox = new QGroupBox("Zero suppression");
    altZeroBox->setLayout(altZeros);
    auto* altWrap = new QVBoxLayout;
    altWrap->addLayout(altTol);
    altWrap->addWidget(altZeroBox);
    auto* altBox = Group("Alternate unit tolerance", page.right, altWrap);
    const auto hasTolerance = [](const QString& v) { return v != "None"; };
    for (QWidget* w : tolWidgets) b.EnableWhen(w, "tolMethod", hasTolerance);
    b.EnableWhen(alignBox, "tolMethod", hasTolerance);
    b.EnableWhen(zeroBox, "tolMethod", hasTolerance);
    b.EnableWhen(altBox, "tolMethod", hasTolerance);
    (void)alignRadios;
    FinishPage(page);
    track(page);
  }
  root->addWidget(tabs, 1);
  AddButtons(dialog, root);

  const auto rebuild = [&](DimensionStyle& target) {
    for (auto it = b.readers.begin(); it != b.readers.end(); ++it) {
      const QString value = it.value()();
      if (!value.isEmpty() || it.key() == "prefix" || it.key() == "suffix") DimStore(target, it.key(), value);
    }
  };
  const auto refreshPreview = [&] {
    DimensionStyle sample = original;
    rebuild(sample);
    const qreal dpr = std::max<qreal>(2.0, dialog->devicePixelRatioF());
    const QPixmap pixmap = DimStylePreviewPixmap(sample, QSize(380, 205), dpr);
    for (QLabel* label : previews) label->setPixmap(pixmap);
  };
  auto* fractionScale = dialog->findChild<QDoubleSpinBox*>("fractionHeightScale");
  auto* toleranceScale = dialog->findChild<QDoubleSpinBox*>("tolScaling");
  QObject::connect(fractionScale, &QDoubleSpinBox::valueChanged, toleranceScale, &QDoubleSpinBox::setValue);
  QObject::connect(toleranceScale, &QDoubleSpinBox::valueChanged, fractionScale, &QDoubleSpinBox::setValue);
  b.onChange = refreshPreview;
  b.RefreshEnabled();
  refreshPreview();

  const bool accepted = dialog->exec() == QDialog::Accepted;
  if (accepted) {
    const QString entered = name->text().trimmed();
    if (entered.isEmpty()) { delete dialog; return false; }
    rebuild(*style);
    style->name = S(entered);
  }
  delete dialog;
  return accepted;
}

bool EditTextStyleDialog(QWidget* parent, TextStyle* style, bool create) {
  if (!style) return false;
  const TextStyle original = *style;
  Binder b([&](const QString& key, const QString& def) {
    if (key == "font") return original.fontFamily.empty() ? QString("romans.shx") : Q(original.fontFamily);
    if (key == "height") return QString::number(original.heightInches);
    if (key == "width") return QString::number(original.widthFactor);
    if (key == "oblique") return QString::number(original.obliqueDeg);
    if (key == "face") return original.bold ? (original.italic ? QString("Bold Italic") : QString("Bold")) : (original.italic ? QString("Italic") : QString("Regular"));
    const auto it = original.props.find(S(key));
    return it == original.props.end() ? def : Q(it->second);
  });
  QDialog dialog(parent);
  dialog.setObjectName("textStyleEditor");
  dialog.setWindowTitle(create ? "New Text Style" : "Modify Text Style: " + Q(style->name));
  dialog.resize(630, 510);
  auto* root = new QVBoxLayout(&dialog);
  if (create) { auto* name = b.Text(&dialog, "name", Q(style->name)); root->addWidget(name); }
  auto* font = Form();
  QStringList fonts = QFontDatabase::families();
  fonts.prepend("simplex.shx"); fonts.prepend("romans.shx");
  auto* fontName = b.Combo(&dialog, "font", fonts, "romans.shx");
  fontName->setEditable(true);
  font->addRow("Font Name:", fontName);
  auto* face = b.Combo(&dialog, "face", {"Regular", "Italic", "Bold", "Bold Italic"}, "Regular");
  font->addRow("Font Style:", face);
  auto* big = b.Check(&dialog, "useBigFont", "Use Big Font", false);
  font->addRow(big);
  auto* bigFont = b.Text(&dialog, "bigFont", "");
  font->addRow("Big Font:", bigFont);
  b.EnableWhen(bigFont, "useBigFont", [](const QString& v) { return v == "1"; });
  b.EnableWhen(face, "font", [](const QString& v) { return !v.endsWith(".shx", Qt::CaseInsensitive); });
  Group("Font", root, font);
  auto* size = Form();
  size->addRow(b.Check(&dialog, "annotative", "Annotative", false));
  auto* orientation = b.Check(&dialog, "matchOrientation", "Match text orientation to layout", false);
  size->addRow(orientation);
  b.EnableWhen(orientation, "annotative", [](const QString& v) { return v == "1"; });
  size->addRow("Height:", b.Double(&dialog, "height", 0.0, 0.0, 100000.0));
  Group("Size", root, size);
  auto* effects = Form();
  effects->addRow(b.Check(&dialog, "upsideDown", "Upside down", false));
  effects->addRow(b.Check(&dialog, "backwards", "Backwards", false));
  effects->addRow(b.Check(&dialog, "vertical", "Vertical", false));
  effects->addRow("Width Factor:", b.Double(&dialog, "width", 1.0, 0.01, 100.0));
  effects->addRow("Oblique Angle:", b.Double(&dialog, "oblique", 0.0, -85.0, 85.0, 1));
  Group("Effects", root, effects);
  auto* preview = new QLabel(&dialog);
  preview->setMinimumHeight(100); preview->setFrameShape(QFrame::StyledPanel);
  root->addWidget(preview);
  AddButtons(&dialog, root);
  const auto rebuild = [&] {
    TextStyle result = original;
    result.fontFamily = S(b.Value("font")); result.heightInches = b.Value("height").toFloat();
    result.widthFactor = b.Value("width").toFloat(); result.obliqueDeg = b.Value("oblique").toFloat();
    result.bold = b.Value("face").contains("Bold"); result.italic = b.Value("face").contains("Italic");
    for (auto it = b.readers.begin(); it != b.readers.end(); ++it)
      if (it.key() != "name" && it.key() != "font" && it.key() != "height" && it.key() != "width" && it.key() != "oblique" && it.key() != "face")
        result.props[S(it.key())] = S(it.value()());
    if (create) result.name = S(b.Value("name").trimmed());
    return result;
  };
  b.onChange = [&] { preview->setPixmap(TextStylePreviewPixmap(rebuild(), QSize(590, 100), dialog.devicePixelRatioF())); };
  b.RefreshEnabled(); b.onChange();
  if (dialog.exec() != QDialog::Accepted) return false;
  TextStyle edited = rebuild();
  if (edited.name.empty()) return false;
  *style = std::move(edited);
  return true;
}

bool EditMultileaderStyleDialog(QWidget* parent, MultileaderStyle* style, bool create, const QStringList& textStyleNames) {
  if (!style) return false;
  const MultileaderStyle original = *style;
  Binder b([&](const QString& key, const QString& def) {
    if (key == "textHeight") return QString::number(original.textSizeInches);
    if (key == "arrowSize") return QString::number(original.arrowSizeInches);
    if (key == "landingGap") return QString::number(original.landingGapInches);
    if (key == "annotative") return QString(original.annotativeDefault ? "1" : "0");
    if (key == "textStyle") return original.textFont.empty() ? QString("Standard") : Q(original.textFont);
    const auto it = original.props.find(S(key)); return it == original.props.end() ? def : Q(it->second);
  });
  QDialog dialog(parent);
  dialog.setObjectName("mleaderStyleEditor");
  dialog.setWindowTitle(create ? "New Multileader Style" : "Modify Multileader Style: " + Q(style->name));
  dialog.resize(840, 640);
  auto* root = new QVBoxLayout(&dialog);
  if (create) root->addWidget(b.Text(&dialog, "name", Q(style->name)));
  auto* tabs = new QTabWidget(&dialog);
  std::vector<QLabel*> previews;
  {
    Page page = MakePage(tabs, "Leader Format", true); previews.push_back(page.preview);
    auto* general = Form();
    general->addRow("Type:", b.Combo(&dialog, "leaderType", {"Straight", "Spline", "None"}, "Straight"));
    general->addRow("Color:", b.Color(&dialog, "leaderColor", "ByLayer"));
    general->addRow("Linetype:", b.Combo(&dialog, "leaderLinetype", kLinetypes, "ByLayer"));
    general->addRow("Lineweight:", b.Combo(&dialog, "leaderLineweight", kLineweights, "ByLayer"));
    Group("General", page.left, general);
    auto* arrows = Form();
    arrows->addRow("Symbol:", b.Combo(&dialog, "arrowSymbol", {"Closed Filled", "Closed Blank", "Architectural Tick", "Dot", "Open", "None"}, "Closed Filled"));
    arrows->addRow("Size:", b.Double(&dialog, "arrowSize", .1, 0.0, 100000));
    Group("Arrowhead", page.left, arrows);
    auto* breaks = Form(); breaks->addRow("Break size:", b.Double(&dialog, "breakSize", .125, 0, 100000));
    Group("Leader break", page.left, breaks); FinishPage(page);
  }
  {
    Page page = MakePage(tabs, "Leader Structure", true); previews.push_back(page.preview);
    auto* constraints = Form();
    constraints->addRow(b.Check(&dialog, "maxPointsEnabled", "Maximum leader points", false));
    auto* maxPoints = b.Int(&dialog, "maxPoints", 3, 2, 100);
    constraints->addRow(maxPoints);
    b.EnableWhen(maxPoints, "maxPointsEnabled", [](const QString& value) { return value == "1"; });
    constraints->addRow("First segment angle:", b.Combo(&dialog, "firstAngle", {"Any angle", "Horizontal", "90", "60", "45", "30", "15"}, "Any angle"));
    constraints->addRow("Second segment angle:", b.Combo(&dialog, "secondAngle", {"Any angle", "Horizontal", "90", "60", "45", "30", "15"}, "Any angle"));
    Group("Constraints", page.left, constraints);
    auto* landing = Form();
    landing->addRow(b.Check(&dialog, "landingEnabled", "Automatically include landing", true));
    auto* length = b.Double(&dialog, "landingLength", .25, 0, 100000);
    landing->addRow(b.Check(&dialog, "doglegEnabled", "Set landing distance:", true));
    landing->addRow(length);
    b.EnableWhen(length, "landingEnabled", [](const QString& v) { return v == "1"; });
    Group("Landing settings", page.left, landing);
    auto* scale = new QVBoxLayout;
    scale->addWidget(b.Check(&dialog, "annotative", "Annotative", false));
    auto* overall = b.Double(&dialog, "overallScale", 1, .0001, 100000);
    scale->addWidget(new QLabel("Specify scale:")); scale->addWidget(overall);
    b.EnableWhen(overall, "annotative", [](const QString& v) { return v != "1"; });
    Group("Scale", page.left, scale); FinishPage(page);
  }
  {
    Page page = MakePage(tabs, "Content", true); previews.push_back(page.preview);
    auto* content = Form();
    content->addRow("Multileader type:", b.Combo(&dialog, "contentType", {"Mtext", "Block", "None"}, "Mtext"));
    content->addRow("Default text:", b.Text(&dialog, "defaultText", ""));
    content->addRow("Text style:", b.Combo(&dialog, "textStyle", textStyleNames, "Standard"));
    content->addRow("Text angle:", b.Combo(&dialog, "textAngle", {"Keep horizontal", "As inserted", "Always right reading"}, "Keep horizontal"));
    content->addRow("Text color:", b.Color(&dialog, "textColor", "ByLayer"));
    content->addRow("Text height:", b.Double(&dialog, "textHeight", .1, .0001, 100000));
    content->addRow(b.Check(&dialog, "alwaysLeft", "Always left justify", false));
    content->addRow(b.Check(&dialog, "textFrame", "Frame text", false));
    Group("Text options", page.left, content);
    auto* block = Form();
    block->addRow("Source block:", b.Text(&dialog, "blockName", ""));
    block->addRow("Color:", b.Color(&dialog, "blockColor", "ByLayer"));
    block->addRow("Scale:", b.Double(&dialog, "blockScale", 1, .0001, 100000));
    block->addRow("Attachment:", b.Combo(&dialog, "blockAttachment", {"Center extents", "Insertion point"}, "Center extents"));
    auto* blockBox = Group("Block options", page.right, block);
    b.EnableWhen(blockBox, "contentType", [](const QString& v) { return v == "Block"; });
    blockBox->setVisible(b.Value("contentType") == "Block");
    QObject::connect(dialog.findChild<QComboBox*>("contentType"), &QComboBox::currentTextChanged, blockBox,
        [blockBox](const QString& value) { blockBox->setVisible(value == "Block"); });
    auto* connection = Form();
    connection->addRow("Direction:", b.Combo(&dialog, "attachmentDirection", {"Horizontal", "Vertical"}, "Horizontal"));
    const QStringList attachments = {"Top of top line", "Middle of top line", "Middle of text", "Middle of bottom line", "Bottom of bottom line", "Underline bottom line", "Underline top line", "Bottom of top line", "Underline all lines", "Center", "Lined center"};
    connection->addRow("Left attachment:", b.Combo(&dialog, "leftAttachment", attachments, "Middle of top line"));
    connection->addRow("Right attachment:", b.Combo(&dialog, "rightAttachment", attachments, "Middle of top line"));
    connection->addRow("Landing gap:", b.Double(&dialog, "landingGap", .05, 0, 100000));
    connection->addRow(b.Check(&dialog, "extendToText", "Extend leader to text", false));
    Group("Leader connection", page.left, connection); FinishPage(page);
  }
  root->addWidget(tabs, 1); AddButtons(&dialog, root);
  const auto rebuild = [&] {
    MultileaderStyle result = original;
    for (auto it = b.readers.begin(); it != b.readers.end(); ++it) result.props[S(it.key())] = S(it.value()());
    result.textSizeInches = b.Value("textHeight").toFloat(); result.arrowSizeInches = b.Value("arrowSize").toFloat();
    result.landingGapInches = b.Value("landingGap").toFloat(); result.annotativeDefault = b.Checked("annotative");
    result.textFont = S(b.Value("textStyle"));
    if (!b.Checked("maxPointsEnabled")) result.props["maxPoints"] = "0";
    if (create) result.name = S(b.Value("name").trimmed());
    return result;
  };
  b.onChange = [&] { for (auto* preview : previews) preview->setPixmap(MleaderStylePreviewPixmap(rebuild(), "Multileader", QSize(380, 205), dialog.devicePixelRatioF())); };
  b.RefreshEnabled(); b.onChange();
  if (dialog.exec() != QDialog::Accepted) return false;
  MultileaderStyle edited = rebuild(); if (edited.name.empty()) return false;
  *style = std::move(edited); return true;
}

bool EditTableStyleDialog(QWidget* parent, CadTableStyle* style, bool create, const QStringList& textStyleNames) {
  if (!style) return false;
  const CadTableStyle original = *style;
  Binder b([&](const QString& key, const QString& def) {
    const auto it = original.props.find(S(key));
    if (it != original.props.end()) return Q(it->second);
    if (key == "Data.textHeight") return QString::number(original.textHeightInches);
    if (key == "Header.textHeight") return QString::number(original.headerTextHeightInches);
    if (key == "Title.textHeight") return QString::number(original.titleTextHeightInches);
    if (key == "Data.textStyle") return original.textStyleName.empty() ? QString("Standard") : Q(original.textStyleName);
    if (key == "Header.textStyle") return original.headerTextStyleName.empty() ? QString("Standard") : Q(original.headerTextStyleName);
    if (key == "Title.textStyle") return original.titleTextStyleName.empty() ? QString("Standard") : Q(original.titleTextStyleName);
    if (key == "Data.textColor" && !original.dataTextColor.empty()) return Q(original.dataTextColor);
    if (key == "Header.textColor" && !original.headerTextColor.empty()) return Q(original.headerTextColor);
    if (key == "Title.textColor" && !original.titleTextColor.empty()) return Q(original.titleTextColor);
    if (key == "columnWidth") return QString::number(original.columnWidthInches);
    if (key == "rowHeight") return QString::number(original.rowHeightInches);
    return def;
  });
  QDialog dialog(parent); dialog.setObjectName("tableStyleEditor");
  dialog.setWindowTitle(create ? "New Table Style" : "Modify Table Style: " + Q(style->name)); dialog.resize(840, 620);
  auto* root = new QVBoxLayout(&dialog);
  if (create) root->addWidget(b.Text(&dialog, "name", Q(style->name)));
  auto* body = new QHBoxLayout;
  auto* left = new QVBoxLayout;
  auto* right = new QVBoxLayout;
  body->addLayout(left, 1); body->addLayout(right, 1);
  root->addLayout(body, 1);
  auto* general = Form(); general->addRow("Table direction:", b.Combo(&dialog, "direction", {"Down", "Up"}, "Down"));
  general->addRow("Column width:", b.Double(&dialog, "columnWidth", 1.5, .001, 100000));
  general->addRow("Row height:", b.Double(&dialog, "rowHeight", .375, .001, 100000));
  Group("General", left, general);
  auto* preview = new QLabel(&dialog); preview->setMinimumSize(340, 300); preview->setFrameShape(QFrame::StyledPanel); left->addWidget(preview, 1);
  auto* cell = new QComboBox(&dialog); cell->setObjectName("tableCellStyle"); cell->addItems({"Data", "Header", "Title"});
  auto* cellRow = new QHBoxLayout; cellRow->addWidget(new QLabel("Cell styles:")); cellRow->addWidget(cell, 1); right->addLayout(cellRow);
  // Each cell style owns its own controls; changing the selector preserves edits made to other cell styles.
  auto* stack = new QStackedWidget(&dialog);
  for (const QString& kind : {QString("Data"), QString("Header"), QString("Title")}) {
    auto* tabs = new QTabWidget(&dialog);
    auto* generalPage = new QWidget; auto* form = new QFormLayout(generalPage);
    form->addRow("Fill color:", b.Color(&dialog, kind + ".fillColor", "None", true));
    form->addRow("Alignment:", b.Combo(&dialog, kind + ".alignment", {"Top Left", "Top Center", "Top Right", "Middle Left", "Middle Center", "Middle Right", "Bottom Left", "Bottom Center", "Bottom Right"}, "Middle Center"));
    form->addRow("Format:", b.Combo(&dialog, kind + ".format", {"General", "Text", "Decimal", "Currency", "Percentage", "Date"}, "General"));
    form->addRow("Horizontal margin:", b.Double(&dialog, kind + ".horizontalMargin", .06, 0, 100000));
    form->addRow("Vertical margin:", b.Double(&dialog, kind + ".verticalMargin", .06, 0, 100000));
    form->addRow(b.Check(&dialog, kind + ".merge", "Merge cells on row/column creation", kind == "Title"));
    tabs->addTab(generalPage, "General");
    auto* textPage = new QWidget; auto* text = new QFormLayout(textPage);
    text->addRow("Text style:", b.Combo(&dialog, kind + ".textStyle", textStyleNames, "Standard"));
    text->addRow("Text height:", b.Double(&dialog, kind + ".textHeight", .125, .0001, 100000));
    text->addRow("Text color:", b.Color(&dialog, kind + ".textColor", "ByLayer"));
    text->addRow("Text angle:", b.Double(&dialog, kind + ".textAngle", 0, -360, 360, 1));
    tabs->addTab(textPage, "Text");
    auto* bordersPage = new QWidget; auto* borders = new QFormLayout(bordersPage);
    borders->addRow("Lineweight:", b.Combo(&dialog, kind + ".borderWeight", kLineweights, "ByLayer"));
    borders->addRow("Linetype:", b.Combo(&dialog, kind + ".borderType", kLinetypes, "ByLayer"));
    borders->addRow("Color:", b.Color(&dialog, kind + ".borderColor", "ByLayer"));
    borders->addRow(b.Check(&dialog, kind + ".bordersVisible", "Display borders", true));
    borders->addRow(b.Check(&dialog, kind + ".doubleLine", "Double line", false));
    borders->addRow("Spacing:", b.Double(&dialog, kind + ".doubleSpacing", .06, 0, 100000));
    tabs->addTab(bordersPage, "Borders"); stack->addWidget(tabs);
  }
  QObject::connect(cell, &QComboBox::currentIndexChanged, stack, &QStackedWidget::setCurrentIndex);
  right->addWidget(stack, 1);
  auto* cellPreview = new QLabel(&dialog); cellPreview->setMinimumHeight(90); cellPreview->setFrameShape(QFrame::StyledPanel);
  auto* previewBox = new QVBoxLayout; previewBox->addWidget(cellPreview); Group("Cell style preview", right, previewBox);
  AddButtons(&dialog, root);
  b.onChange = [&] {
    QPixmap pixmap(360, 160); pixmap.fill(QColor("#202830")); QPainter p(&pixmap);
    p.setPen(ResolveColor(b.Value("Data.borderColor"))); p.drawRect(10, 10, 339, 139);
    for (int row = 1; row < 4; ++row) p.drawLine(10, 10 + row * 35, 349, 10 + row * 35);
    for (int col = 1; col < 3; ++col) p.drawLine(10 + col * 113, 45, 10 + col * 113, 149);
    p.setPen(ResolveColor(b.Value("Title.textColor"))); p.drawText(QRect(10, 10, 339, 35), Qt::AlignCenter, "Title");
    p.setPen(ResolveColor(b.Value("Header.textColor"))); p.drawText(QRect(10, 45, 339, 35), Qt::AlignCenter, "Header                  Header                  Header");
    p.setPen(ResolveColor(b.Value("Data.textColor"))); p.drawText(QRect(10, 80, 339, 69), Qt::AlignCenter, "Data                     Data                     Data");
    p.end(); preview->setPixmap(pixmap.scaled(360, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    const QString kind = cell->currentText();
    QPixmap sample(340, 80); sample.fill(QColor("#202830")); QPainter painter(&sample);
    painter.setPen(ResolveColor(b.Value(kind + ".borderColor"))); painter.drawRect(60, 10, 220, 60);
    painter.drawLine(170, 10, 170, 70); painter.drawLine(60, 40, 280, 40);
    painter.setPen(ResolveColor(b.Value(kind + ".textColor"))); painter.drawText(QRect(60, 10, 110, 30), Qt::AlignCenter, kind);
    painter.drawText(QRect(170, 40, 110, 30), Qt::AlignCenter, kind); painter.end(); cellPreview->setPixmap(sample);
  };
  QObject::connect(cell, &QComboBox::currentIndexChanged, &dialog, [&] { b.onChange(); });
  b.RefreshEnabled(); b.onChange();
  if (dialog.exec() != QDialog::Accepted) return false;
  CadTableStyle edited = original;
  for (auto it = b.readers.begin(); it != b.readers.end(); ++it) edited.props[S(it.key())] = S(it.value()());
  edited.textHeightInches = b.Value("Data.textHeight").toFloat(); edited.headerTextHeightInches = b.Value("Header.textHeight").toFloat(); edited.titleTextHeightInches = b.Value("Title.textHeight").toFloat();
  edited.textStyleName = S(b.Value("Data.textStyle")); edited.headerTextStyleName = S(b.Value("Header.textStyle")); edited.titleTextStyleName = S(b.Value("Title.textStyle"));
  edited.dataTextColor = S(b.Value("Data.textColor")); edited.headerTextColor = S(b.Value("Header.textColor")); edited.titleTextColor = S(b.Value("Title.textColor"));
  edited.gridLineColor = S(b.Value("Data.borderColor")); edited.borderColor = S(b.Value("Title.borderColor"));
  edited.columnWidthInches = b.Value("columnWidth").toFloat(); edited.rowHeightInches = b.Value("rowHeight").toFloat();
  if (create) edited.name = S(b.Value("name").trimmed());
  if (edited.name.empty()) return false;
  *style = std::move(edited); return true;
}
