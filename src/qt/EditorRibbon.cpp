#include "EditorWindow.hpp"
#include "EditorViewport.hpp"
#include "RibbonCatalog.hpp"
#include "ModelingRibbon.hpp"
#include <QAbstractItemView>
#include <QLineEdit>
#include "CadBlocks.hpp"
#include "DxfIo.hpp"
#include "DwgIo.hpp"
#include "PdfAttach.hpp"
#include "PdfPlot.hpp"
#include "ProjectFiles.hpp"
#include "cadpiperun.hpp"
#include "HatchPat.hpp"
#include <QColorDialog>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QResizeEvent>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSet>
#include <QSlider>
#include <QSpinBox>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryFile>
#include "DimensionStyle.hpp"
#include "MultileaderStyle.hpp"
#include "cadtablestyle.hpp"
#include "StylePreview.hpp"
#include "CadColor.hpp"
#include "font/ShxFont.hpp"
#include <algorithm>
#include <numeric>
#include <vector>
#include <functional>
#include <QPainterPath>

namespace {
QString Text(const char* text) { return QString::fromUtf8(text); }
bool NeedsNativeDialog(const QString& command) {
  static const QSet<QString> pending = {"@pointGroups", "@volumes",
      "@connectionModes", "@blockPalettes", "@createBlock", "@batchPlot", "@pageSetup", "BEDIT", "INSERT"};
  return pending.contains(command);
}
int SelectedSurface(const AppCommandState& state) {
  for (const auto& item : state.selection)
    if (item.type == SelectedEntity::Type::Surface && item.index >= 0 && item.index < static_cast<int>(state.cadSurfaces.size())) return item.index;
  return -1;
}
int SelectedAlignment(const AppCommandState& state) {
  for (const auto& item : state.selection)
    if (item.type == SelectedEntity::Type::Alignment && item.index >= 0 && item.index < static_cast<int>(state.alignments.size())) return item.index;
  return -1;
}

struct RibbonStyleGalleryModel {
  QStringList entries;
  QString current;
  QString previewSample;
  QString manageLabel;
  QIcon tileIcon;
  int columns = 2;
  std::function<QFont(const QString& entry)> previewFont;
  std::function<QColor(const QString& entry)> previewColor;
  std::function<QIcon(const QString& entry)> previewIcon;
};

constexpr int kStylePreviewPixmapWidth = 96;
constexpr int kStylePreviewPixmapHeight = 52;
constexpr int kStyleGalleryTileWidth = 112;
constexpr int kStyleGalleryTileHeight = 98;
constexpr int kRibbonStyleGalleryWidth = 128;

static bool RibbonUsesLightChrome() {
  return qApp->property("cadLightTheme").toBool();
}

// Thumbnails are painted in logical 96x52 units onto a pixmap with extra device pixels so they stay
// crisp on scaled displays; the icon engine then draws it 1:1.
static QPixmap NewStylePreviewPixmap() {
  const qreal scale = std::max<qreal>(2.0, std::ceil(qApp->devicePixelRatio()));
  QPixmap pixmap(QSize(kStylePreviewPixmapWidth, kStylePreviewPixmapHeight) * scale);
  pixmap.setDevicePixelRatio(scale);
  return pixmap;
}

// A style's own colour as the drawing would show it: ByLayer / ByBlock fall back to the default
// ink (layer "White"), anything else resolves through the same CAD colour table the viewport uses.
static QColor RibbonStyleInk(const std::string& storage) {
  float rgb[3] = {1.f, 1.f, 1.f};
  CadColorResolveRgb(CadColorEffectiveStorage(storage.empty() ? std::string("ByLayer") : storage, "White"),
                     1.f, 1.f, 1.f, rgb);
  return QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
}

// One layer row as the ribbon list shows it: on/off bulb, thaw/freeze sun or snowflake, lock, colour swatch.
static QIcon RibbonLayerRowIcon(const CadLayerRow& row) {
  constexpr int kGlyph = 16;
  constexpr int kGap = 3;
  constexpr int kWidth = kGlyph * 4 + kGap * 3;
  const qreal scale = std::max<qreal>(2.0, std::ceil(qApp->devicePixelRatio()));
  QPixmap pixmap(QSize(kWidth, kGlyph) * scale);
  pixmap.setDevicePixelRatio(scale);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QColor lit(QStringLiteral("#f2c14e")), dim(QStringLiteral("#6c757c")), ice(QStringLiteral("#7cc4ff"));
  const auto cell = [&](int index) { return QRectF(index * (kGlyph + kGap), 0, kGlyph, kGlyph); };

  // Bulb: on = lit glass, off = grey outline.
  {
    const QRectF r = cell(0);
    const QColor c = row.on ? lit : dim;
    painter.setPen(QPen(c, 1.2));
    painter.setBrush(row.on ? QBrush(c) : Qt::NoBrush);
    painter.drawEllipse(QRectF(r.left() + 3.5, r.top() + 1.5, 9, 9));
    painter.setPen(QPen(c, 1.4));
    painter.drawLine(QPointF(r.left() + 6, r.top() + 12.5), QPointF(r.left() + 10, r.top() + 12.5));
    painter.drawLine(QPointF(r.left() + 6.5, r.top() + 14.5), QPointF(r.left() + 9.5, r.top() + 14.5));
  }
  // Thawed = sun, frozen = snowflake.
  {
    const QRectF r = cell(1);
    const QPointF c = r.center();
    if (row.frozen) {
      painter.setPen(QPen(ice, 1.3));
      for (int i = 0; i < 3; ++i) {
        const qreal a = i * 3.14159265358979323846 / 3.0;
        painter.drawLine(QPointF(c.x() - 6 * std::cos(a), c.y() - 6 * std::sin(a)),
                         QPointF(c.x() + 6 * std::cos(a), c.y() + 6 * std::sin(a)));
      }
    } else {
      painter.setPen(QPen(lit, 1.3));
      painter.setBrush(lit);
      painter.drawEllipse(c, 3.2, 3.2);
      for (int i = 0; i < 8; ++i) {
        const qreal a = i * 3.14159265358979323846 / 4.0;
        painter.drawLine(QPointF(c.x() + 5 * std::cos(a), c.y() + 5 * std::sin(a)),
                         QPointF(c.x() + 7.2 * std::cos(a), c.y() + 7.2 * std::sin(a)));
      }
    }
  }
  // Padlock: closed shackle when locked, open when not.
  {
    const QRectF r = cell(2);
    const QColor c = row.locked ? lit : QColor(QStringLiteral("#d9a93c"));
    painter.setPen(QPen(c, 1.5));
    painter.setBrush(Qt::NoBrush);
    QPainterPath shackle;
    if (row.locked) {
      shackle.moveTo(r.left() + 5, r.top() + 7);
      shackle.lineTo(r.left() + 5, r.top() + 5);
      shackle.arcTo(QRectF(r.left() + 5, r.top() + 1.5, 6, 7), 180, -180);
      shackle.lineTo(r.left() + 11, r.top() + 7);
    } else {
      shackle.moveTo(r.left() + 5, r.top() + 7);
      shackle.lineTo(r.left() + 5, r.top() + 5);
      shackle.arcTo(QRectF(r.left() + 5, r.top() + 1.5, 6, 7), 180, -180);
      shackle.lineTo(r.left() + 11, r.top() + 3.5);
    }
    painter.drawPath(shackle);
    painter.setPen(Qt::NoPen);
    painter.setBrush(c);
    painter.drawRoundedRect(QRectF(r.left() + 3, r.top() + 7, 10, 7.5), 1.5, 1.5);
  }
  // Layer colour swatch.
  {
    const QRectF r = cell(3);
    painter.setPen(QPen(QColor(255, 255, 255, 70), 1.0));
    painter.setBrush(RibbonStyleInk(row.color));
    painter.drawRect(QRectF(r.left() + 2, r.top() + 2, 12, 12));
  }
  painter.end();
  return QIcon(pixmap);
}

// Fill a layer combo with one icon per row; the list and the combo's closed face use the same icon.
static void RibbonFillLayerCombo(QComboBox* combo, const std::vector<CadLayerRow>& layers, const QString& selected) {
  const QSignalBlocker block(combo);
  combo->clear();
  combo->setIconSize(QSize(16 * 4 + 3 * 3, 16));
  for (const auto& row : layers)
    combo->addItem(RibbonLayerRowIcon(row), Text(row.name.c_str()));
  if (layers.empty()) {  // a drawing with no layer table still has layer "0"
    CadLayerRow zero;
    zero.name = "0";
    combo->addItem(RibbonLayerRowIcon(zero), QStringLiteral("0"));
  }
  combo->setCurrentText(selected);
}

static void DrawDimArrowHead(QPainter& painter, const QPointF& tip, const QPointF& along, DimArrowType type,
                             const QColor& ink) {
  const qreal u = (painter.transform().m11() > 1.5 ? 1.6 : 1.0) / painter.transform().m11();
  const QPointF dir = along - tip;
  const qreal len = std::hypot(dir.x(), dir.y());
  if (len < 1.e-3)
    return;
  const QPointF unit(dir.x() / len, dir.y() / len);
  const QPointF normal(-unit.y(), unit.x());
  switch (type) {
  case DimArrowType::Tick: {
    painter.setPen(QPen(ink, 1.5 * u));
    painter.drawLine(tip + normal * 4.0, tip - normal * 4.0);
    break;
  }
  case DimArrowType::Dot: {
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    painter.drawEllipse(tip, 2.2, 2.2);
    break;
  }
  case DimArrowType::Open: {
    painter.setPen(QPen(ink, 1.2 * u));
    painter.drawLine(tip, tip + unit * 7.0 + normal * 3.0);
    painter.drawLine(tip, tip + unit * 7.0 - normal * 3.0);
    break;
  }
  case DimArrowType::ClosedBlank: {
    painter.setPen(QPen(ink, 1.0 * u));
    painter.setBrush(Qt::NoBrush);
    QPolygonF head;
    head << tip << tip + unit * 8.0 + normal * 3.0 << tip + unit * 8.0 - normal * 3.0;
    painter.drawPolygon(head);
    break;
  }
  case DimArrowType::None:
    break;
  case DimArrowType::ClosedFilled:
  default: {
    painter.setPen(QPen(ink, 1.0 * u));
    painter.setBrush(ink);
    QPolygonF head;
    head << tip << tip + unit * 8.0 + normal * 3.0 << tip + unit * 8.0 - normal * 3.0;
    painter.drawPolygon(head);
    break;
  }
  }
}

static void PaintDimStylePreview(QPainter& painter, const DimensionStyle& sty) {
  const qreal u = (painter.transform().m11() > 1.5 ? 1.6 : 1.0) / painter.transform().m11();  // constant on-screen stroke
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QColor extInk = RibbonStyleInk(sty.extLineColor);
  const QColor dimInk = RibbonStyleInk(sty.dimLineColor);
  const QColor arrowInk = RibbonStyleInk(sty.arrowColor);
  const QColor textInk = RibbonStyleInk(sty.textColor);
  const int xLeft = 10;
  const int xRight = kStylePreviewPixmapWidth - 10;
  const int yObjectTop = 30;
  const int yObjectBottom = 44;
  const int yDim = 18;
  painter.setPen(QPen(extInk, 1.0 * u));
  painter.drawLine(xLeft, yObjectTop, xLeft, yObjectBottom);
  painter.drawLine(xRight, yObjectTop, xRight, yObjectBottom);
  painter.setPen(QPen(dimInk, 1.0 * u));
  painter.drawLine(xLeft, yDim, xRight, yDim);
  DrawDimArrowHead(painter, QPointF(xLeft, yDim), QPointF(xLeft + 1, yDim), sty.arrowType, arrowInk);
  DrawDimArrowHead(painter, QPointF(xRight, yDim), QPointF(xRight - 1, yDim), sty.arrowType, arrowInk);
  const std::string sample = DimensionStyles::FormatLinearDim(1.0159, sty);
  QFont font(QStringLiteral("Segoe UI"), 7);
  if (!sty.textFont.empty())
    font.setFamily(Text(sty.textFont.c_str()));
  painter.setPen(textInk);
  painter.setFont(font);
  painter.drawText(QRect(4, 2, kStylePreviewPixmapWidth - 8, 14), Qt::AlignCenter, Text(sample.c_str()));
}

static QIcon RibbonDimStylePreviewIcon(const DimensionStyle& sty) {
  QPixmap pixmap = NewStylePreviewPixmap();
  pixmap.fill(QColor(QStringLiteral("#1e2630")));
  QPainter painter(&pixmap);
  PaintDimStylePreview(painter, sty);
  painter.end();
  return QIcon(pixmap);
}

static void PaintMleaderStylePreview(QPainter& painter, const MultileaderStyle& sty, const QString& styleLabel) {
  const qreal u = (painter.transform().m11() > 1.5 ? 1.6 : 1.0) / painter.transform().m11();  // constant on-screen stroke
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  const QColor ink = RibbonStyleInk(std::string());  // the style stores no colour: ByLayer
  const bool spline = styleLabel.contains(QStringLiteral("Sline"), Qt::CaseInsensitive) ||
                      styleLabel.contains(QStringLiteral("Spline"), Qt::CaseInsensitive);
  const QPointF tip(12, 44);
  const QPointF knee(40, 28);
  const QPointF landingEnd(88, 28);
  painter.setPen(QPen(ink, 1.2 * u));
  painter.setBrush(Qt::NoBrush);
  if (spline) {
    QPainterPath leader;
    leader.moveTo(tip);
    leader.cubicTo(QPointF(20, 28), QPointF(28, 40), knee);
    painter.drawPath(leader);
  } else {
    painter.drawLine(tip, knee);
  }
  painter.drawLine(knee, landingEnd);
  const QPointF away = knee - tip;
  const qreal awayLen = std::max<qreal>(std::hypot(away.x(), away.y()), 1.0);
  const QPointF unit(away.x() / awayLen, away.y() / awayLen);
  const QPointF normal(-unit.y(), unit.x());
  const qreal arrowLen = std::clamp<qreal>(sty.arrowSizeInches * 70.0, 5.0, 11.0);
  QPolygonF arrow;
  arrow << tip << tip + unit * arrowLen + normal * (arrowLen * 0.32) << tip + unit * arrowLen - normal * (arrowLen * 0.32);
  painter.setPen(QPen(ink, 1.0 * u));
  painter.setBrush(ink);
  painter.drawPolygon(arrow);
  QFont font(QStringLiteral("Segoe UI"), 8);
  if (!sty.textFont.empty())
    font.setFamily(Text(sty.textFont.c_str()));
  painter.setPen(ink);
  painter.setFont(font);
  painter.drawText(QRect(44, 8, 46, 18), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Text"));
}

static QIcon RibbonMleaderStylePreviewIcon(const MultileaderStyle& sty, const QString& styleLabel) {
  QPixmap pixmap = NewStylePreviewPixmap();
  pixmap.fill(QColor(QStringLiteral("#1e2630")));
  QPainter painter(&pixmap);
  PaintMleaderStylePreview(painter, sty, styleLabel);
  painter.end();
  return QIcon(pixmap);
}

// Table style thumbnail: the style's own colours, column set and a few rows of made-up cell data.
static QIcon RibbonTableStylePreviewIcon(const CadTableStyle& sty) {
  QPixmap pixmap = NewStylePreviewPixmap();
  pixmap.fill(QColor(QStringLiteral("#1e2630")));
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  const QColor grid = RibbonStyleInk(sty.gridLineColor);
  const QColor border = RibbonStyleInk(sty.borderColor);
  const QColor titleInk = RibbonStyleInk(sty.titleTextColor);
  const QColor headerInk = RibbonStyleInk(sty.headerTextColor);
  const QColor dataInk = RibbonStyleInk(sty.dataTextColor);

  QStringList headers;
  QStringList sampleRow;
  if (!sty.columnHeaders.empty()) {
    for (const auto& header : sty.columnHeaders)
      headers << Text(header.c_str());
  } else if (sty.point && !sty.pointColumns.empty()) {
    for (const auto column : sty.pointColumns)
      headers << Text(CadPointTableColumnTitle(column));
  } else {
    headers << QStringLiteral("Item") << QStringLiteral("Qty") << QStringLiteral("Note");
  }
  if (headers.size() > 5)
    headers = headers.mid(0, 5);
  static const char* const kSamples[] = {"101", "5201.4", "3120.7", "84.62", "TOP"};
  for (int i = 0; i < headers.size(); ++i)
    sampleRow << QString::fromLatin1(kSamples[i % 5]);

  const bool hasTitle = !sty.title.empty();
  const QRect frame(5, 4, kStylePreviewPixmapWidth - 10, kStylePreviewPixmapHeight - 8);
  const int rows = (hasTitle ? 1 : 0) + (sty.headerRow ? 1 : 0) + 3;
  const int rowHeight = std::max(6, frame.height() / rows);
  const int cols = std::max<int>(1, static_cast<int>(headers.size()));
  const int colWidth = frame.width() / cols;
  QFont font(QStringLiteral("Segoe UI"));
  font.setPixelSize(std::max(5, rowHeight - 2));

  painter.setFont(font);
  int y = frame.top();
  const auto drawText = [&](const QRect& cell, const QString& text, const QColor& ink, int flags) {
    painter.setPen(ink);
    painter.drawText(cell.adjusted(2, 0, -1, 0), flags | Qt::AlignVCenter,
                     painter.fontMetrics().elidedText(text, Qt::ElideRight, cell.width() - 3));
  };
  if (hasTitle) {
    drawText(QRect(frame.left(), y, frame.width(), rowHeight), Text(sty.title.c_str()), titleInk, Qt::AlignHCenter);
    y += rowHeight;
    painter.setPen(QPen(grid, 1.0));
    painter.drawLine(frame.left(), y, frame.right(), y);
  }
  if (sty.headerRow) {
    for (int c = 0; c < cols; ++c)
      drawText(QRect(frame.left() + c * colWidth, y, colWidth, rowHeight), headers.value(c), headerInk, Qt::AlignLeft);
    y += rowHeight;
    painter.setPen(QPen(grid, 1.0));
    painter.drawLine(frame.left(), y, frame.right(), y);
  }
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < cols; ++c)
      drawText(QRect(frame.left() + c * colWidth, y, colWidth, rowHeight), sampleRow.value(c), dataInk, Qt::AlignLeft);
    y += rowHeight;
    if (r < 2) {
      painter.setPen(QPen(grid, 1.0));
      painter.drawLine(frame.left(), y, frame.right(), y);
    }
  }
  const int bottom = std::min(y, frame.bottom());
  painter.setPen(QPen(grid, 1.0));
  for (int c = 1; c < cols; ++c)
    painter.drawLine(frame.left() + c * colWidth, hasTitle ? frame.top() + rowHeight : frame.top(),
                     frame.left() + c * colWidth, bottom);
  painter.setPen(QPen(border, 1.0));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(QRect(frame.left(), frame.top(), frame.width(), bottom - frame.top()));
  painter.end();
  return QIcon(pixmap);
}

static QIcon RibbonStylePreviewIcon(const RibbonStyleGalleryModel& data, const QString& entry) {
  if (data.previewIcon)
    return data.previewIcon(entry);
  QFont font(QStringLiteral("Segoe UI"), 11);
  if (data.previewFont)
    font = data.previewFont(entry);
  const QColor ink = data.previewColor ? data.previewColor(entry) : RibbonStyleInk(std::string());
  QPixmap pixmap = NewStylePreviewPixmap();
  pixmap.fill(QColor(QStringLiteral("#1e2630")));
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.setPen(ink);
  painter.setFont(font);
  painter.drawText(QRect(2, 2, kStylePreviewPixmapWidth - 4, kStylePreviewPixmapHeight - 4), Qt::AlignCenter, data.previewSample);
  painter.end();
  return QIcon(pixmap);
}

static void ApplyRibbonStyleGalleryButtonChrome(QToolButton* button) {
  const bool light = RibbonUsesLightChrome();
  button->setProperty("ribbonStyleGallery", true);
  if (light) {
    button->setStyleSheet(
        "QToolButton[ribbonStyleGallery=\"true\"]{padding:1px 22px 1px 6px;border:1px solid #8d9295;"
        "background:#f8f8f8;color:#202020;}"
        "QToolButton[ribbonStyleGallery=\"true\"]::menu-indicator{subcontrol-origin:padding;"
        "subcontrol-position:center right;width:16px;margin-right:2px;}");
  } else {
    button->setStyleSheet(
        "QToolButton[ribbonStyleGallery=\"true\"]{padding:1px 22px 1px 6px;border:1px solid #424b51;"
        "background:#15191c;color:#edf1f3;}"
        "QToolButton[ribbonStyleGallery=\"true\"]::menu-indicator{subcontrol-origin:padding;"
        "subcontrol-position:center right;width:16px;margin-right:2px;}");
  }
}

static void ApplyRibbonStyleGalleryPopupChrome(QMenu* menu, QWidget* host, QScrollArea* scroll, QWidget* gridHost) {
  menu->setStyleSheet(QString());
  const QString surface =
      RibbonUsesLightChrome() ? QStringLiteral("background:#ececec;color:#202020;")
                              : QStringLiteral("background:#252b2f;color:#edf1f3;");
  host->setStyleSheet(surface);
  scroll->setStyleSheet(surface);
  gridHost->setStyleSheet(surface);
}

static QString RibbonStyleTileStyleSheet(bool selected, bool light) {
  if (light) {
    return selected ? QStringLiteral("QToolButton{border:2px solid #e8922a;background:#fff8ef;color:#202020;}")
                    : QStringLiteral("QToolButton{border:1px solid #8d9295;background:#ffffff;color:#202020;}"
                                     "QToolButton:hover{border-color:#e8922a;}");
  }
  return selected ? QStringLiteral("QToolButton{border:2px solid #e8922a;background:#2a3035;color:#edf1f3;}")
                  : QStringLiteral("QToolButton{border:1px solid #424b51;background:#1c2024;color:#edf1f3;}"
                                   "QToolButton:hover{border-color:#e8922a;}");
}

static void SyncRibbonStyleGalleryButton(QToolButton* button, const QString& current) {
  if (button == nullptr)
    return;
  if (button->menu() != nullptr && button->menu()->isVisible())
    return;
  const QString label = current.isEmpty() ? QStringLiteral("Standard") : current;
  const QFontMetrics metrics(button->font());
  button->setText(metrics.elidedText(label, Qt::ElideMiddle, kRibbonStyleGalleryWidth - 30));
  button->setToolTip(label);
}

static QToolButton* CreateRibbonStyleGallery(QWidget* panel, const QString& objectName,
                                             EditorWindow* window,
                                             const std::function<RibbonStyleGalleryModel()>& model) {
  auto* button = new QToolButton(panel);
  button->setObjectName(objectName);
  if (objectName == QStringLiteral("ribbonTextStyleGallery"))
    button->setAccessibleName(QStringLiteral("Text style"));
  else if (objectName == QStringLiteral("ribbonDimStyleGallery"))
    button->setAccessibleName(QStringLiteral("Dimension style"));
  else if (objectName == QStringLiteral("ribbonMleaderStyleGallery"))
    button->setAccessibleName(QStringLiteral("Multileader style"));
  else if (objectName == QStringLiteral("ribbonTableStyleGallery"))
    button->setAccessibleName(QStringLiteral("Table style"));
  else
    button->setAccessibleName(objectName.mid(6));
  button->setPopupMode(QToolButton::InstantPopup);
  button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  button->setArrowType(Qt::DownArrow);
  button->setAutoRaise(true);
  button->setFixedHeight(24);
  button->setFixedWidth(kRibbonStyleGalleryWidth);
  button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  auto* menu = new QMenu(button);
  button->setMenu(menu);
  QObject::connect(menu, &QMenu::aboutToShow, button, [button, window, model, objectName, menu]() {
    const RibbonStyleGalleryModel data = model();
    const bool light = RibbonUsesLightChrome();
    SyncRibbonStyleGalleryButton(button, data.current);
    button->setProperty("ribbonStyleEntries", data.entries);
    menu->clear();
    auto* host = new QWidget(menu);
    host->setObjectName(objectName + QStringLiteral("_popup"));
    auto* hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(6, 6, 6, 4);
    hostLayout->setSpacing(4);
    auto* scroll = new QScrollArea(host);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMaximumHeight(420);
    auto* gridHost = new QWidget(scroll);
    auto* grid = new QGridLayout(gridHost);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    ApplyRibbonStyleGalleryPopupChrome(menu, host, scroll, gridHost);
    const int columns = std::max(1, data.columns);
    // Reserve the tile columns plus the vertical scroll bar so the last column is never clipped.
    const int gridWidth = columns * kStyleGalleryTileWidth + (columns - 1) * grid->horizontalSpacing();
    scroll->setFixedWidth(gridWidth + scroll->verticalScrollBar()->sizeHint().width() + 4);
    int tileIndex = 0;
    const QFontMetrics tileMetrics(QFont(QStringLiteral("Segoe UI"), 8));
    for (const QString& entry : data.entries) {
      auto* tile = new QToolButton(gridHost);
      tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
      tile->setAutoRaise(true);
      tile->setFixedSize(kStyleGalleryTileWidth, kStyleGalleryTileHeight);
      tile->setIconSize(QSize(kStylePreviewPixmapWidth, kStylePreviewPixmapHeight));
      tile->setIcon(RibbonStylePreviewIcon(data, entry));
      tile->setToolTip(entry);
      tile->setText(tileMetrics.elidedText(entry, Qt::ElideMiddle, kStyleGalleryTileWidth - 8));
      const bool selected = entry == data.current;
      tile->setStyleSheet(RibbonStyleTileStyleSheet(selected, light));
      const int row = tileIndex / columns;
      const int col = tileIndex % columns;
      grid->addWidget(tile, row, col);
      ++tileIndex;
      QObject::connect(tile, &QToolButton::clicked, menu, [window, objectName, entry, menu]() {
        window->PickRibbonAnnotStyle(objectName, entry);
        menu->close();
      });
    }
    scroll->setWidget(gridHost);
    hostLayout->addWidget(scroll);
    auto* popupAction = new QWidgetAction(menu);
    popupAction->setDefaultWidget(host);
    menu->addAction(popupAction);
    menu->addSeparator();
    auto* manage = menu->addAction(data.manageLabel);
    QObject::connect(manage, &QAction::triggered, window, [window, objectName]() { window->OpenRibbonStyleManager(objectName); });
  });
  ApplyRibbonStyleGalleryButtonChrome(button);
  SyncRibbonStyleGalleryButton(button, model().current);
  return button;
}

// Expanded panels use three short rows. At smaller widths whole panels become native
// menus containing the very same actions, so resizing never clips or loses a command.
constexpr int kRibbonPanelHeight = 98;
constexpr int kRibbonPageHeight = 102;
constexpr int kRibbonHeight = 130;
constexpr int kRibbonCompactHeaderHeight = 26;
constexpr int kRibbonCaptionBottomSpace = 4;

static bool PanelHasStyleGallery(QWidget* panel) {
  for (auto* button : panel->findChildren<QToolButton*>()) {
    if (button->objectName().endsWith(QStringLiteral("StyleGallery")))
      return true;
  }
  return false;
}

static void FinalizeAnnotateStylePanel(QWidget* panel, int galleryColumn) {
  Q_UNUSED(galleryColumn);
  // Size from the real layout (button text widths vary), never from a guessed per-column width.
  panel->ensurePolished();
  for (auto* child : panel->findChildren<QWidget*>())
    child->ensurePolished();
  panel->layout()->invalidate();
  panel->layout()->activate();
  const int width = std::max(panel->layout()->sizeHint().width(), panel->layout()->minimumSize().width()) + 2;
  panel->setMinimumWidth(width);
  panel->setFixedSize(width, kRibbonPanelHeight);
}

class RibbonPage final : public QWidget {
  struct Panel { QWidget* expanded; QToolButton* collapsed; QMenu* menu; int priority; };
  std::vector<Panel> m_Panels;
  std::vector<int> m_PriorityOrder;
  std::vector<int> m_OverflowPanelIndices;
  QHBoxLayout* m_Row;
  QToolButton* m_More;
  QMenu* m_Overflow;
public:
  explicit RibbonPage(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(0); setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); setFixedHeight(kRibbonPageHeight);
    m_Row = new QHBoxLayout(this); m_Row->setContentsMargins(3, 2, 3, 1); m_Row->setSpacing(2);
    m_More = new QToolButton(this); m_More->setObjectName("ribbonMore"); m_More->setText("More");
    m_More->setIcon(CadCommandIcon("layers.png")); m_More->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_More->setPopupMode(QToolButton::InstantPopup); m_More->setAutoRaise(true); m_More->setFixedSize(62, 90);
    m_Overflow = new QMenu(m_More); m_More->setMenu(m_Overflow); m_More->hide();
    m_Row->addWidget(m_More); m_Row->addStretch();
  }
  void AddPanel(QWidget* panel, const QString& title, const QList<QAction*>& actions, int priority) {
    panel->setParent(this); panel->setProperty("ribbonPanel", true);
    if (panel->minimumWidth() <= 0)
      panel->setFixedSize(panel->sizeHint().width() + 8, kRibbonPanelHeight);
    else
      panel->setFixedSize(panel->minimumWidth(), kRibbonPanelHeight);
    auto* collapsed = new QToolButton(this); collapsed->setObjectName("collapsed_" + panel->objectName());
    collapsed->setText(title); collapsed->setToolTip(title); collapsed->setAutoRaise(true);
    collapsed->setFixedSize(std::clamp(fontMetrics().horizontalAdvance(title) + 22, 66, 155), 92);
    collapsed->setIconSize(QSize(26, 26)); collapsed->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    // Make the full collapsed panel button open its menu; a split button leaves the
    // tiny, style-dependent arrow as the only reliable target at minimum width.
    collapsed->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(title, collapsed); menu->setToolTipsVisible(true); collapsed->setMenu(menu);
    if (!actions.empty()) { collapsed->setIcon(actions.front()->icon()); menu->setIcon(actions.front()->icon()); }
    for (auto* action : actions) menu->addAction(action);
    // Keep live selectors available when their panel collapses, without moving
    // widgets out of the ribbon's layout or duplicating their state handlers.
    for (auto* source : panel->findChildren<QComboBox*>()) {
      auto* widget = new QWidget(menu); auto* row = new QHBoxLayout(widget); row->setContentsMargins(8, 3, 8, 3);
      row->addWidget(new QLabel(source->accessibleName(), widget));
      auto* mirror = new QComboBox(widget); mirror->setMinimumWidth(150); row->addWidget(mirror);
      auto* entry = new QWidgetAction(menu); entry->setDefaultWidget(widget); menu->addAction(entry);
      connect(menu, &QMenu::aboutToShow, mirror, [source, mirror] {
        const QSignalBlocker block(mirror); mirror->clear();
        for (int i = 0; i < source->count(); ++i) mirror->addItem(source->itemIcon(i), source->itemText(i));
        mirror->setCurrentIndex(source->currentIndex());
        mirror->setEnabled(source->isEnabled());
      });
      connect(mirror, &QComboBox::activated, source, [source](int index) {
        source->setCurrentIndex(index);
        QMetaObject::invokeMethod(source, "activated", Qt::DirectConnection, Q_ARG(int, index));
        QMetaObject::invokeMethod(source, "textActivated", Qt::DirectConnection, Q_ARG(QString, source->currentText()));
      });
    }
    for (auto* source : panel->findChildren<QToolButton*>()) {
      if (!source->objectName().endsWith(QStringLiteral("StyleGallery")))
        continue;
      auto* openGallery = menu->addAction(source->accessibleName() + QStringLiteral("..."));
      openGallery->setToolTip(source->toolTip());
      connect(openGallery, &QAction::triggered, source, [source]() {
        if (source->menu() != nullptr)
          source->showMenu();
      });
    }
    for (auto* source : panel->findChildren<QDoubleSpinBox*>()) {
      auto* mirror = new QDoubleSpinBox(menu); mirror->setRange(source->minimum(), source->maximum());
      mirror->setDecimals(source->decimals()); mirror->setSingleStep(source->singleStep()); mirror->setPrefix(source->prefix()); mirror->setSuffix(source->suffix());
      auto* entry = new QWidgetAction(menu); entry->setDefaultWidget(mirror); menu->addAction(entry);
      connect(menu, &QMenu::aboutToShow, mirror, [source, mirror] { const QSignalBlocker block(mirror); mirror->setValue(source->value()); mirror->setEnabled(source->isEnabled()); });
      connect(mirror, &QDoubleSpinBox::valueChanged, source, &QDoubleSpinBox::setValue);
    }
    for (auto* source : panel->findChildren<QSpinBox*>()) {
      auto* mirror = new QSpinBox(menu); mirror->setRange(source->minimum(), source->maximum());
      mirror->setSingleStep(source->singleStep()); mirror->setPrefix(source->prefix()); mirror->setSuffix(source->suffix());
      auto* entry = new QWidgetAction(menu); entry->setDefaultWidget(mirror); menu->addAction(entry);
      connect(menu, &QMenu::aboutToShow, mirror, [source, mirror] { const QSignalBlocker block(mirror); mirror->setValue(source->value()); mirror->setEnabled(source->isEnabled()); });
      connect(mirror, &QSpinBox::valueChanged, source, &QSpinBox::setValue);
    }
    for (auto* source : panel->findChildren<QCheckBox*>()) {
      auto* mirror = new QCheckBox(source->text(), menu);
      auto* entry = new QWidgetAction(menu); entry->setDefaultWidget(mirror); menu->addAction(entry);
      connect(menu, &QMenu::aboutToShow, mirror, [source, mirror] {
        const QSignalBlocker block(mirror); mirror->setChecked(source->isChecked()); mirror->setEnabled(source->isEnabled());
      });
      connect(mirror, &QCheckBox::toggled, source, &QCheckBox::setChecked);
    }
    m_Row->insertWidget(m_Row->count() - 2, panel); m_Row->insertWidget(m_Row->count() - 2, collapsed);
    m_Panels.push_back({panel, collapsed, menu, priority});
    m_PriorityOrder.push_back(static_cast<int>(m_Panels.size() - 1));
    std::stable_sort(m_PriorityOrder.begin(), m_PriorityOrder.end(), [this](int a, int b) {
      return m_Panels[a].priority > m_Panels[b].priority;
    });
    Fit();
  }
  void Fit() {
    if (m_Panels.empty()) return;
    // Style-gallery panels claim their full width first (by priority); the rest collapse or overflow.
    // "More" only costs space when something actually overflows, so try without it first.
    const int panelCount = static_cast<int>(m_Panels.size());
    std::vector<bool> shown, expanded;
    int used = 0;
    const auto place = [&](int reserve) {
      used = 6 + reserve;
      shown.assign(panelCount, false);
      expanded.assign(panelCount, false);
      for (const int i : m_PriorityOrder) {
        if (!PanelHasStyleGallery(m_Panels[i].expanded))
          continue;
        const int cost = m_Panels[i].expanded->width() + 2;
        if (used + cost <= width()) {
          shown[i] = expanded[i] = true;
          used += cost;
        }
      }
      for (const int i : m_PriorityOrder) {
        if (shown[i])
          continue;
        const int cost = m_Panels[i].collapsed->width() + 2;
        if (used + cost <= width()) {
          shown[i] = true;
          used += cost;
        }
      }
    };
    place(0);
    if (std::find(shown.begin(), shown.end(), false) != shown.end())
      place(m_More->width() + 2);
    std::vector<int> overflowPanels;
    for (int i = 0; i < static_cast<int>(m_Panels.size()); ++i) {
      if (!shown[i]) overflowPanels.push_back(i);
    }
    if (overflowPanels != m_OverflowPanelIndices) {
      m_Overflow->clear();
      for (const int i : overflowPanels) m_Overflow->addMenu(m_Panels[i].menu);
      m_OverflowPanelIndices = std::move(overflowPanels);
    }
    if (m_More->isHidden() == !m_OverflowPanelIndices.empty())
      m_More->setVisible(!m_OverflowPanelIndices.empty());
    const auto tryExpand = [&](int i) {
      if (!shown[i] || expanded[i])
        return;
      auto& p = m_Panels[i];
      const int extra = p.expanded->width() - p.collapsed->width();
      if (used + extra <= width()) {
        expanded[i] = true;
        used += extra;
      }
    };
    for (const int i : m_PriorityOrder)
      tryExpand(i);
    for (int i = 0; i < static_cast<int>(m_Panels.size()); ++i) {
      auto& p = m_Panels[i];
      if (p.expanded->isHidden() == expanded[i]) p.expanded->setVisible(expanded[i]);
      const bool collapsedVisible = shown[i] && !expanded[i];
      if (p.collapsed->isHidden() == collapsedVisible) p.collapsed->setVisible(collapsedVisible);
    }
  }
protected:
  void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); Fit(); }
  void showEvent(QShowEvent* event) override { QWidget::showEvent(event); Fit(); }
};

void UpdateRibbonTabButton(QWidget* wrapper) {
  auto* button = wrapper->findChild<QToolButton*>("ribbonTabMenu");
  Q_ASSERT(button);
  if (!button) return;
  button->setText(QString::fromUtf8("☰  Tabs"));
}

void UpdateRibbonTabMenu(QWidget* wrapper) {
  UpdateRibbonTabButton(wrapper);
  auto* ribbon = wrapper->findChild<QTabWidget*>("ribbon");
  auto* button = wrapper->findChild<QToolButton*>("ribbonTabMenu");
  if (!ribbon || !button) return;
  const int current = ribbon->currentIndex();
  auto* menu = button->menu();
  if (!menu) return;
  QString signature;
  for (int i = 0; i < ribbon->count(); ++i) {
    if (!ribbon->isTabVisible(i)) continue;
    signature += QString::number(i) + QLatin1Char(':') + ribbon->tabText(i) + QLatin1Char(';');
  }
  if (menu->property("ribbonTabSignature").toString() == signature) return;
  menu->clear();
  for (int i = 0; i < ribbon->count(); ++i) {
    if (!ribbon->isTabVisible(i)) continue;
    auto* action = menu->addAction(ribbon->tabText(i));
    action->setCheckable(true);
    action->setChecked(i == current);
    action->setProperty("ribbonIndex", i);
    action->setObjectName("ribbonTabMenuAction_" + ribbon->widget(i)->property("ribbonTab").toString());
    QObject::connect(action, &QAction::triggered, ribbon, [ribbon, i] { ribbon->setCurrentIndex(i); });
  }
  menu->setProperty("ribbonTabSignature", signature);
}

void FitRibbonChrome(QWidget* wrapper) {
  auto* ribbonColumn = wrapper->findChild<QWidget*>("ribbonColumn");
  auto* ribbon = wrapper->findChild<QTabWidget*>("ribbon");
  auto* shared = wrapper->findChild<QWidget*>("ribbonSharedArea");
  auto* header = wrapper->findChild<QWidget*>("ribbonTabMenuHeader");
  auto* tabMenu = wrapper->findChild<QToolButton*>("ribbonTabMenu");
  auto* compactLayers = wrapper->findChild<QToolButton*>("ribbonCompactLayers");
  Q_ASSERT(ribbonColumn && ribbon && shared && header && tabMenu && compactLayers);
  if (!ribbonColumn || !ribbon || !shared || !header || !tabMenu || !compactLayers) return;

  auto* tabs = ribbon->tabBar();
  int needed = 0;
  int visibleCount = 0;
  for (int i = 0; i < ribbon->count(); ++i) if (tabs->isTabVisible(i)) {
    needed += tabs->tabRect(i).width();
    ++visibleCount;
  }
  if (visibleCount > 1) needed += (visibleCount - 1) * tabs->style()->pixelMetric(QStyle::PM_TabBarTabHSpace);
  if (needed <= 0) needed = tabs->sizeHint().width();

  const int sharedWidth = shared->sizeHint().width();
  const int gap = 2;
  const bool sharedFits = wrapper->width() >= needed + sharedWidth + gap;
  const bool compact = !sharedFits;
  // Line the shared Layers panel up with the ribbon pane beside it: same top edge, same height.
  if (auto* current = ribbon->currentWidget(); current && shared->layout()) {
    const int top = std::max(0, current->mapTo(wrapper, QPoint(0, 0)).y() - 1);
    const int height = std::max(1, ribbon->mapTo(wrapper, QPoint(0, ribbon->height())).y() - top);
    if (shared->layout()->contentsMargins().top() != top)
      shared->layout()->setContentsMargins(0, top, 0, 0);
    if (auto* layers = shared->findChild<QWidget*>("ribbonSharedLayers"); layers && layers->height() != height)
      layers->setFixedHeight(height);
  }
  if (shared->isHidden() == sharedFits) shared->setVisible(sharedFits);
  if (header->isHidden() == compact) header->setVisible(compact);
  if (compactLayers->isHidden() == compact) compactLayers->setVisible(compact);
  if (tabs->isHidden() == !compact) tabs->setVisible(!compact);
  if (wrapper->property("ribbonChromeCompact").toBool() != compact) {
    ribbon->setFixedHeight(compact ? kRibbonHeight - kRibbonCompactHeaderHeight : kRibbonHeight);
    wrapper->setProperty("ribbonChromeCompact", compact);
    if (auto* docRow = wrapper->findChild<QWidget*>("drawingTabRow"))
      wrapper->setMinimumHeight(ribbon->height() + docRow->height());
  }
  UpdateRibbonTabMenu(wrapper);
}

class RibbonResizeFilter final : public QObject {
  QWidget* m_Wrapper;
  bool m_Queued = false;
public:
  RibbonResizeFilter(QWidget* wrapper) : QObject(wrapper), m_Wrapper(wrapper) {}
  void Schedule() {
    if (m_Queued) return;
    m_Queued = true;
    QTimer::singleShot(0, this, [this] { m_Queued = false; FitRibbonChrome(m_Wrapper); });
  }
protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == m_Wrapper && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) Schedule();
    return false;
  }
};
}

// Large previews for the style managers: the same 96x52 drawing, scaled to any size.
namespace {
QPixmap ScaledStylePreview(QSize size, qreal dpr, const std::function<void(QPainter&)>& paint) {
  QPixmap pixmap(size * dpr);
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(QColor(QStringLiteral("#1e2630")));
  QPainter painter(&pixmap);
  painter.scale(size.width() / static_cast<qreal>(kStylePreviewPixmapWidth),
                size.height() / static_cast<qreal>(kStylePreviewPixmapHeight));
  paint(painter);
  return pixmap;
}
}  // namespace

// A drop-down list that opens as a normal, scrolling popup in the application's colours (ribbon and status-bar combos).
QString PopupComboStyleSheet() {
  const bool light = qApp->property("cadLightTheme").toBool();
  return QStringLiteral("QComboBox { combobox-popup: 0; }"
      "QComboBox QAbstractItemView { background:%1; color:%2; border:1px solid %3; outline:0; padding:2px; "
      "selection-background-color:%4; selection-color:%2; }"
      "QComboBox QAbstractItemView::item { min-height:26px; padding:2px 8px; border-radius:3px; }"
      "QComboBox QAbstractItemView::item:hover { background:%5; }"
      "QComboBox QAbstractItemView QScrollBar:vertical { background:transparent; width:10px; margin:2px; }"
      "QComboBox QAbstractItemView QScrollBar::handle:vertical { background:%3; border-radius:3px; min-height:24px; }"
      "QComboBox QAbstractItemView QScrollBar::add-line:vertical, QComboBox QAbstractItemView QScrollBar::sub-line:vertical { height:0; }")
      .arg(light ? "#ffffff" : "#20262a", light ? "#202020" : "#edf1f3", light ? "#b6bcc1" : "#4a555c",
           light ? "#dfe7ee" : "#35424b", light ? "#eef2f5" : "#2a343b");
}

QPixmap DimStylePreviewPixmap(const DimensionStyle& style, QSize size, qreal dpr) {
  if (size.width() < 200)
    return ScaledStylePreview(size, dpr, [&](QPainter& p) { PaintDimStylePreview(p, style); });
  QPixmap pixmap(size * dpr);
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(QColor("#202830"));
  QPainter p(&pixmap);
  p.setRenderHint(QPainter::Antialiasing);
  p.scale(size.width() / 380.0, size.height() / 250.0);
  p.setPen(QPen(QColor("#a8a53b"), .6));
  p.drawPolyline(QPolygonF{QPointF(65, 65), QPointF(150, 65), QPointF(150, 85),
      QPointF(220, 85), QPointF(295, 220), QPointF(115, 220), QPointF(65, 175), QPointF(65, 65)});
  p.drawEllipse(QPointF(125, 160), 35, 35);
  const QColor lineInk = RibbonStyleInk(style.dimLineColor);
  const QColor textInk = RibbonStyleInk(style.textColor);
  const QColor extInk = RibbonStyleInk(style.extLineColor);
  auto dimension = [&](QPointF a, QPointF b, QPointF da, QPointF db, const QString& label) {
    p.setPen(QPen(extInk, .8));
    if (!StyleFlag(style.props, "suppressExt1")) p.drawLine(a, da);
    if (!StyleFlag(style.props, "suppressExt2")) p.drawLine(b, db);
    const QPointF mid = (da + db) * .5;
    p.setPen(QPen(lineInk, .8));
    if (!StyleFlag(style.props, "suppressDim1")) p.drawLine(da, mid);
    if (!StyleFlag(style.props, "suppressDim2")) p.drawLine(mid, db);
    DrawDimArrowHead(p, da, db, style.arrowType, lineInk);
    DrawDimArrowHead(p, db, da, style.arrowType, lineInk);
    p.save(); p.translate(mid);
    p.rotate(std::atan2(db.y() - da.y(), db.x() - da.x()) * 180.0 / 3.14159265358979323846);
    QFont font("Segoe UI"); font.setPixelSize(13); p.setFont(font); p.setPen(textInk);
    p.drawText(QRectF(-85, -23, 170, 21), Qt::AlignCenter, label); p.restore();
  };
  dimension({65, 65}, {150, 65}, {65, 29}, {150, 29},
      QString::fromUtf8(DimensionStyles::FormatLinearDim(.5, style).c_str()));
  dimension({65, 65}, {65, 175}, {28, 175}, {28, 65},
      QString::fromUtf8(DimensionStyles::FormatLinearDim(.8, style).c_str()));
  dimension({220, 85}, {295, 220}, {260, 63}, {335, 198}, QStringLiteral("59°50′57″"));
  p.setPen(QPen(lineInk, .8)); p.drawLine(75, 205, 42, 235);
  DrawDimArrowHead(p, {75, 205}, {42, 235}, style.arrowType, lineInk);
  p.setPen(textInk); p.drawText(QPointF(10, 246), QStringLiteral("R0.4"));
  if (StyleProperty(style.props, "centerMark", "Mark") != "None") {
    p.setPen(QPen(lineInk, .8)); p.drawLine(110, 160, 140, 160); p.drawLine(125, 145, 125, 175);
  }
  return pixmap;
}

QPixmap MleaderStylePreviewPixmap(const MultileaderStyle& style, const QString& label, QSize size, qreal dpr) {
  return ScaledStylePreview(size, dpr, [&](QPainter& p) { PaintMleaderStylePreview(p, style, label); });
}

QPixmap TextStylePreviewPixmap(const TextStyle& style, QSize size, qreal dpr) {
  QPixmap pixmap(size * dpr); pixmap.setDevicePixelRatio(dpr); pixmap.fill(QColor("#202830"));
  QPainter p(&pixmap); p.setRenderHint(QPainter::Antialiasing);
  const std::string sample = "AaBbYyZz 0123456789";
  const float capHeight = std::min<float>(size.height() * .45f, size.width() / 18.f);
  p.translate(12, size.height() * .7);
  if (StyleFlag(style.props, "backwards")) { p.translate(size.width() - 24, 0); p.scale(-1, 1); }
  if (StyleFlag(style.props, "upsideDown")) p.scale(1, -1);
  p.scale(style.widthFactor, 1);
  p.shear(-std::tan(style.obliqueDeg * 3.14159265358979323846 / 180.0), 0);
  p.setPen(QPen(RibbonStyleInk(std::string()), style.bold ? 1.8 : 1.0));
  if (auto* font = Shx::Resolve(style.fontFamily.empty() ? "romans.shx" : style.fontFamily); font && font->valid()) {
    const float scale = capHeight / font->capHeight();
    float x = 0;
    for (const unsigned char character : sample) {
      const auto* glyph = font->glyph(character); if (!glyph) continue;
      for (const auto& stroke : glyph->strokes) {
        QPolygonF path;
        for (const auto& point : stroke) path << QPointF(x + point.x * scale, -point.y * scale);
        p.drawPolyline(path);
      }
      x += glyph->advance * scale;
    }
  } else {
    QFont qtFont(Text(style.fontFamily.c_str())); qtFont.setPixelSize(std::max(1, qRound(capHeight)));
    qtFont.setBold(style.bold); qtFont.setItalic(style.italic); p.setFont(qtFont);
    p.drawText(QPointF(0, 0), Text(sample.c_str()));
  }
  return pixmap;
}

void EditorWindow::BuildRibbonPages() {
  auto* toolbar = addToolBar("Ribbon"); toolbar->setObjectName("ribbonBar");
  toolbar->setMovable(false); toolbar->setFloatable(false); toolbar->setContentsMargins(0, 0, 0, 0);
  auto* wrapper = new QWidget(toolbar); wrapper->setObjectName("ribbonWrapper"); wrapper->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto* strip = new QHBoxLayout(wrapper); strip->setContentsMargins(0, 0, 0, 0); strip->setSpacing(2);
  auto* ribbonColumn = new QWidget(wrapper); ribbonColumn->setObjectName("ribbonColumn"); ribbonColumn->setMinimumWidth(0);
  auto* ribbonLayout = new QVBoxLayout(ribbonColumn); ribbonLayout->setContentsMargins(0, 0, 0, 0); ribbonLayout->setSpacing(0);
  auto* header = new QWidget(ribbonColumn); header->setObjectName("ribbonTabMenuHeader"); header->setFixedHeight(kRibbonCompactHeaderHeight);
  auto* headerLayout = new QHBoxLayout(header); headerLayout->setContentsMargins(4, 0, 4, 0); headerLayout->setSpacing(0);
  auto* tabMenu = new QToolButton(header); tabMenu->setObjectName("ribbonTabMenu"); tabMenu->setText(QString::fromUtf8("☰  Tabs"));
  tabMenu->setAccessibleName("Ribbon tabs"); tabMenu->setToolTip("Choose a ribbon tab"); tabMenu->setPopupMode(QToolButton::InstantPopup); tabMenu->setAutoRaise(false); tabMenu->setFixedHeight(24);
  auto* tabMenuItems = new QMenu(tabMenu); tabMenu->setMenu(tabMenuItems);
  headerLayout->addWidget(tabMenu); headerLayout->addStretch(); header->hide(); ribbonLayout->addWidget(header);
  auto* ribbon = new QTabWidget(ribbonColumn); ribbon->setObjectName("ribbon"); ribbonLayout->addWidget(ribbon, 1);
  ribbon->tabBar()->setMouseTracking(true);
  ribbon->tabBar()->setAttribute(Qt::WA_Hover, true);
  strip->addWidget(ribbonColumn, 1);
  auto* shared = new QWidget(wrapper); shared->setObjectName("ribbonSharedArea"); shared->setFixedWidth(262);
  auto* sharedColumn = new QVBoxLayout(shared); sharedColumn->setContentsMargins(0, 26, 0, 0); sharedColumn->setSpacing(0);
  auto* layerPage = new RibbonPage(shared); layerPage->setObjectName("ribbonSharedLayers"); sharedColumn->addWidget(layerPage);
  strip->addWidget(shared);
  ribbon->setUsesScrollButtons(false); ribbon->setElideMode(Qt::ElideNone);
  ribbon->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); ribbon->setMinimumWidth(0);
  ribbon->setFixedHeight(kRibbonHeight);
  const QStringList names = {"Home", "Insert", "Annotate", "View", "Manage", "Output", "Survey", "Modeling",
      "Layout", "SurfaceCtx", "SurveyPointCtx", "PointCloudCtx", "BlockEditor", "BlockTest", "GeolocationCtx", "PdfCtx", "HatchCtx",
      "AlignmentCtx"};
  for (const auto& name : names) {
    auto* page = new RibbonPage(ribbon); page->setObjectName("ribbonPage_" + name); page->setProperty("ribbonTab", name);
    QStringList panels;
    for (const auto& item : qtribbon::kButtons) if (Text(item.tab) == name && !panels.contains(Text(item.panel))) panels.append(Text(item.panel));
    if (name == "Manage") {
      auto* layout = qobject_cast<QHBoxLayout*>(page->layout());
      auto* note = new QLabel("Data shortcuts, action recorder, CAD standards and Dynamo are not available in GoSurvey 0.7.2.", page);
      note->setWordWrap(true); note->setMaximumWidth(600); layout->insertWidget(0, note);
    }
    if (name == "PointCloudCtx") panels.prepend("Point Cloud Display");
    if (name == "PointCloudCtx") panels.append("Centerline Fit");
    if (name == "HatchCtx") panels.append("Hatch");
    if (name == "Modeling") panels = {"Primitives", "Create", "Booleans", "Edit", "3D Modify", "Piping"};
    if (name == "PdfCtx") panels = {"Adjust", "Clipping", "Options", "Import"};
    for (const auto& panelName : panels) {
      auto* panel = new QWidget(page); panel->setObjectName("ribbonPanel_" + name + "_" + panelName);
      panel->setAutoFillBackground(true);
      auto* column = new QVBoxLayout(panel); column->setContentsMargins(5, 0, 5, kRibbonCaptionBottomSpace); column->setSpacing(1);
      auto* grid = new QGridLayout; grid->setContentsMargins(0, 0, 0, 0); grid->setHorizontalSpacing(2); grid->setVerticalSpacing(0);
      column->addLayout(grid, 1); int col = 0, row = 0; QList<QAction*> actions;
      for (const auto& item : qtribbon::kButtons) {
        if (Text(item.tab) != name || Text(item.panel) != panelName) continue;
        auto* action = new QAction(CadCommandIcon(Text(item.icon) + ".png"), Text(item.label), this);
        action->setObjectName("ribbon_" + Text(item.id).mid(2)); action->setProperty("ribbonId", Text(item.id));
        action->setProperty("ribbonCommand", Text(item.command)); action->setProperty("cadIcon", Text(item.icon) + ".png");
        action->setProperty("releasePlaceholder", item.placeholder);
        action->setProperty("nativeDialogPending", NeedsNativeDialog(Text(item.command)));
        const QString help = Text(item.tooltip) + (NeedsNativeDialog(Text(item.command)) ? "\nThis command's dialog is not available in this editor yet." : "");
        action->setToolTip(help); action->setStatusTip(help); action->setEnabled(!item.placeholder && !NeedsNativeDialog(Text(item.command)));
        if (Text(item.command) == "@pdfBackground" || Text(item.command) == "@pdfSnap")
          action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, id = Text(item.id)] { RunRibbonAction(id); });
        actions.append(action);
        auto* button = new QToolButton(panel); button->setObjectName("tool_" + Text(item.id).mid(2)); button->setDefaultAction(action);
        button->setAutoRaise(true); button->setAttribute(Qt::WA_AlwaysShowToolTips); button->setAccessibleName(Text(item.label).replace('\n', ' '));
        const bool iconOnly = name == "Home" && !item.large && (panelName == "Draw" || panelName == "Modify" || panelName == "Palettes");
        button->setToolButtonStyle(item.large ? Qt::ToolButtonTextUnderIcon : iconOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        button->setIconSize(item.large ? QSize(30, 30) : QSize(18, 18));
        if (item.large) {
          if (row) { ++col; row = 0; }
          button->setMinimumWidth(58); button->setFixedHeight(73); grid->addWidget(button, 0, col++, 3, 1);
        } else {
          if (iconOnly) button->setFixedWidth(30);
          button->setFixedHeight(24); grid->addWidget(button, row++, col);
          if (row == 3) { row = 0; ++col; }
        }
        // Release flyouts share command actions with the expanded panel and collapsed menu.
        if (Text(item.command).endsWith("Menu") || Text(item.id) == "##ModPipeFit" || Text(item.id) == "##GeoEditLocation" ||
            Text(item.id) == "##GeoMarkPosition" || Text(item.id) == "##TsVisCheck" || Text(item.id) == "##TsXBreak") {
          auto* menu = new QMenu(button); menu->setToolTipsVisible(true);
          auto add = [&](const QString& label, const QString& command, const QString& icon = {}) {
            auto* option = menu->addAction(icon.isEmpty() ? action->icon() : CadCommandIcon(icon + ".png"), label);
            option->setProperty("ribbonCommand", command); option->setToolTip(command);
            option->setProperty("cadIcon", icon.isEmpty() ? action->property("cadIcon") : QVariant(icon + ".png"));
            option->setEnabled(!NeedsNativeDialog(command));
            if (NeedsNativeDialog(command)) option->setToolTip("This command's dialog is not available in this editor yet.");
            connect(option, &QAction::triggered, this, [this, command] { RunRibbonAction(command); });
          };
          const auto id = Text(item.id);
          if (id.endsWith("Isolate")) { add("Isolate Objects", "ISOLATEOBJECTS"); add("Hide Objects", "HIDEOBJECTS"); add("End Object Isolation", "UNISOLATEOBJECTS"); }
          else if (id == "##ModPipeFit") for (const auto* part : modelingribbon::kPipeFitPartTypes) add(Text(part), "PIPEFIT " + Text(part));
          else if (id == "##GeoMap") for (const auto& map : kOnlineMaps) add(Text(map.label), "@map:" + QString::number(static_cast<int>(map.map)), Text(map.icon));
          else if (id == "##GeoEditLocation") { add("Edit Location", "@drawingSettings"); add("Edit Geographic Marker", "@geoReorient"); }
          else if (id == "##GeoMarkPosition") { add("Lat-Long", "GEOMARKLATLONG"); add("Point", "GEOMARKPOINT"); }
          else if (id == "##CdAlignment") {  // REQ-485
            add("Alignment Creation Tools", "CREATEALIGNMENT", "c3d_alignment");
            add("Create Alignment from Objects", "CREATEALIGNMENTFROMOBJECTS", "c3d_alignment");
          }
          else if (id == "##AnnAddTables") {  // REQ-474, REQ-493: Annotate > Add Tables
            add("Add Point Table", "ADDPOINTTABLE");
            menu->addSection("Alignment");
            add("Add Line", "ALIGNMENTTABLE {alignment}, LINE", "c3d_alignment");
            add("Add Curve", "ALIGNMENTTABLE {alignment}, CURVE", "c3d_alignment");
            add("Add Spiral", "ALIGNMENTTABLE {alignment}, SPIRAL", "c3d_alignment");
            add("Add Segment", "ALIGNMENTTABLE {alignment}, SEGMENT", "c3d_alignment");
            add("Add Station Equation", "ALIGNMENTTABLE {alignment}, EQUATIONS", "c3d_alignment");
          }
          else if (id == "##ClipPasteSpecial") {  // Paste Special: the two existing paste variants (issue #805)
            add("Paste Block References", "PASTEBLOCK", "Paste");
            add("Paste at Original Coordinates", "PASTEORIG", "Paste_Original_Coordinates");
          }
          else if (id == "##GeoCaptureArea") { add("Capture Area", "@captureViewport"); add("Pick Area", "@captureArea"); menu->addSeparator(); add("Remove Captured Areas", "@removeCaptures"); }
          else if (id == "##TsAddData") {
            add("Breaklines", "@surfaceBreaklines"); add("Contours", "@surfaceContours"); add("Boundary", "@surfaceBoundary"); add("Point Groups", "@surfaceManager");
            add("Point Files", "@surfaceManager");
          } else if (id == "##TsEditSurf") {
            add("Add Point", "@surfaceAddPoint"); add("Delete Point", "@surfaceDeletePoint"); add("Move Point", "@surfaceMovePoint");
            add("Delete TIN Line", "@surfaceDeleteLine"); add("Swap Edge", "@surfaceSwapEdge"); add("Rebuild", "@surfaceRebuild");
          } else if (id == "##TsVisCheck") {
            add("Line of Sight", "@surfaceVisibilityLos");
            add("Zone of Visual Influence", "@surfaceVisibilityZvi");
          } else if (id == "##TsAddLabels") {
            add("Spot Elevation", "@surfaceLabelSpot"); add("Slope", "@surfaceLabelSlope");
            add("Contour Elevation", "@surfaceLabelContour"); add("Watershed Areas", "@surfaceLabelWatershed");
          } else if (id == "##TsAddLegend") {
            add("Elevation Ranges", "@surfaceLegendElevation"); add("Slope Ranges", "@surfaceLegendSlope");
            add("Slope Arrows", "@surfaceLegendSlopeArrow"); add("Contour Values", "@surfaceLegendContour");
            add("Watershed Areas", "@surfaceLegendWatershed");
          } else if (id == "##TsXBreak") {
            add("List Crossings", "@surfaceCrossingsList");
            add("Resolve Crossing...", "@surfaceCrossingsResolve");
          } else if (id == "##TsExtract") { add("Contours", "@extractContours"); add("Water Drop Path", "WATERDROP EXTRACT"); add("Water Drop Feature Line", "WATERDROP EXTRACT FL"); add("Catchment", "CATCHMENT EXTRACT"); }
          action->setMenu(menu); button->setMenu(menu); button->setPopupMode(id == "##GeoEditLocation" ? QToolButton::MenuButtonPopup : QToolButton::InstantPopup);
        }
      }
      if (row) ++col;
      auto* caption = new QLabel(panelName, panel); caption->setObjectName("ribbonPanelLabel"); caption->setAlignment(Qt::AlignCenter);
      column->addWidget(caption);
      // Live selectors are native Qt controls, kept next to their release commands.
      auto combo = [&](const QString& object, const QStringList& values) {
        auto* result = new QComboBox(panel); result->setObjectName(object); result->addItems(values); result->setMinimumWidth(135); result->setMaximumWidth(170);
        result->setAccessibleName(object.mid(6)); result->setToolTip(object.mid(6));
        // A normal drop-down list (scrolls after a few rows) rather than a full-height popup.
        result->setMaxVisibleItems(14);
        result->setStyleSheet(PopupComboStyleSheet());
        grid->addWidget(result, 0, col++, 1, 1); return result;
      };
      if (name == "Home" && panelName == "Layers") {
        auto* layer = combo("ribbonLayer", {}); layer->view()->setMinimumWidth(340);
        connect(layer, &QComboBox::textActivated, this, [this](const QString& value) { CadRibbonPickLayer(m_State, value.toUtf8().toStdString(), m_Log); Refresh(); });
        auto* color = combo("ribbonColor", {"ByLayer", "ByBlock", "Red", "Yellow", "Green", "Cyan", "Blue", "Magenta", "White"});
        color->setEditable(true); color->lineEdit()->setReadOnly(true);
        for (int i = 2; i < color->count(); ++i) { QPixmap swatch(14, 14); swatch.fill(QColor(color->itemText(i))); color->setItemIcon(i, QIcon(swatch)); }
        grid->removeWidget(color); grid->addWidget(color, 1, col - 2); --col;
        connect(color, &QComboBox::textActivated, this, [this](const QString& value) { CadRibbonPickColor(m_State, value.toUtf8().toStdString(), m_Log); Refresh(); });
      }
      if (name == "View" && panelName == "View") {
        auto* style = combo("ribbonVisualStyle", {"2D Wireframe", "Hidden", "Shaded"});
        connect(style, &QComboBox::activated, this, [this](int index) { Submit(QString("VISUALSTYLE %1").arg(QStringList{"2D", "HIDDEN", "SHADED"}[index])); });
        auto* projection = combo("ribbonProjection", {"Orthographic", "Perspective"});
        connect(projection, &QComboBox::activated, this, [this](int index) { Submit(index ? "PERSPECTIVE ON" : "PERSPECTIVE OFF"); });
        auto* fov = new QSpinBox(panel); fov->setObjectName("ribbonFov"); fov->setRange(1, 179); fov->setPrefix("FOV ");
        fov->setSuffix(" deg"); grid->addWidget(fov, 1, col - 1);
        connect(fov, &QSpinBox::valueChanged, this, [this](int value) { Submit(QString("FOV %1").arg(value)); });
      }
      if (name == "View" && panelName == "Named Views") {
        auto* views = combo("ribbonNamedView", {"Top", "Bottom", "Left", "Right", "Front", "Back", "SW Isometric", "SE Isometric", "NE Isometric", "NW Isometric"});
        views->setEditable(true); views->lineEdit()->setReadOnly(true);
        connect(views, &QComboBox::activated, this, [this](int index) {
          static const float az[] = {0, 0, 270, 90, 0, 180, 45, 315, 135, 225};
          static const float el[] = {90, -90, 0, 0, 0, 0, 35.264f, 35.264f, 35.264f, 35.264f};
          if (index < 10) m_Viewport->SetOrientation(az[index], el[index]);
          else if (index - 10 < static_cast<int>(m_State.namedViews.size())) RestoreNamedView(m_State, m_State.namedViews[index - 10], m_Log);
          Refresh();
        });
      }
      if (name == "Annotate" && panelName == "Text") {
        const int styleCol = col;
        auto* picker = CreateRibbonStyleGallery(panel, QStringLiteral("ribbonTextStyleGallery"), this, [this]() {
          RibbonStyleGalleryModel model;
          model.previewSample = QStringLiteral("AaBbYyZz");
          model.manageLabel = QStringLiteral("Manage Text Styles...");
          model.columns = 3;
          model.tileIcon = CadCommandIcon(QStringLiteral("text.png"));
          TextStyles::EnsureStandard(m_State.textStyles);
          for (const auto& style : m_State.textStyles)
            model.entries.append(Text(style.name.c_str()));
          model.current = Text(m_State.activeTextStyleName.c_str());
          model.previewFont = [this](const QString& entry) {
            QFont font(QStringLiteral("Segoe UI"), 12);
            if (const TextStyle* style = TextStyles::Find(m_State.textStyles, entry.toUtf8().toStdString()))
              if (!style->fontFamily.empty())
                font.setFamily(Text(style->fontFamily.c_str()));
            return font;
          };
          return model;
        });
        grid->setColumnMinimumWidth(styleCol, kRibbonStyleGalleryWidth);
        grid->addWidget(picker, 0, styleCol, 3, 1, Qt::AlignTop);
        FinalizeAnnotateStylePanel(panel, styleCol);
      }
      if (name == "Annotate" && panelName == "Dimensions") {
        const int styleCol = col;
        auto* picker = CreateRibbonStyleGallery(panel, QStringLiteral("ribbonDimStyleGallery"), this, [this]() {
          RibbonStyleGalleryModel model;
          model.previewSample = QStringLiteral("25.40'");
          model.manageLabel = QStringLiteral("Manage Dimension Styles...");
          model.columns = 2;
          model.tileIcon = CadCommandIcon(QStringLiteral("dimstyle.png"));
          for (const auto& style : m_State.dimensionStyles)
            model.entries.append(Text(style.name.c_str()));
          model.current = Text(m_State.activeDimensionStyle.name.c_str());
          model.previewIcon = [this](const QString& entry) {
            const DimensionStyle* style = DimensionStyles::Find(m_State.dimensionStyles, entry.toUtf8().toStdString());
            if (style == nullptr)
              return QIcon();
            return RibbonDimStylePreviewIcon(*style);
          };
          return model;
        });
        grid->setColumnMinimumWidth(styleCol, kRibbonStyleGalleryWidth);
        grid->addWidget(picker, 0, styleCol, 3, 1, Qt::AlignTop);
        FinalizeAnnotateStylePanel(panel, styleCol);
      }
      if (name == "Annotate" && panelName == "Leaders") {
        const int styleCol = col;
        auto* picker = CreateRibbonStyleGallery(panel, QStringLiteral("ribbonMleaderStyleGallery"), this, [this]() {
          RibbonStyleGalleryModel model;
          model.previewSample = QStringLiteral("Default Text");
          model.manageLabel = QStringLiteral("Manage Multileader Styles...");
          model.columns = 2;
          model.tileIcon = CadCommandIcon(QStringLiteral("Multileader.png"));
          for (const auto& style : m_State.multileaderStyles)
            model.entries.append(Text(style.name.c_str()));
          model.current = Text(m_State.activeMultileaderStyle.name.c_str());
          model.previewIcon = [this](const QString& entry) {
            const MultileaderStyle* style = MultileaderStyles::Find(m_State.multileaderStyles, entry.toUtf8().toStdString());
            if (style == nullptr)
              return QIcon();
            return RibbonMleaderStylePreviewIcon(*style, entry);
          };
          return model;
        });
        grid->setColumnMinimumWidth(styleCol, kRibbonStyleGalleryWidth);
        grid->addWidget(picker, 0, styleCol, 3, 1, Qt::AlignTop);
        FinalizeAnnotateStylePanel(panel, styleCol);
      }
      if (name == "Annotate" && panelName == "Tables") {
        const int styleCol = col;
        auto* picker = CreateRibbonStyleGallery(panel, QStringLiteral("ribbonTableStyleGallery"), this, [this]() {
          RibbonStyleGalleryModel model;
          model.manageLabel = QStringLiteral("Manage Table Styles...");
          model.columns = 2;
          model.tileIcon = CadCommandIcon(QStringLiteral("Table.png"));
          for (const auto& style : m_State.tableStyles)
            model.entries.append(Text(style.name.c_str()));
          const std::string active = m_State.tablePendingStyle.empty() ? "Standard" : m_State.tablePendingStyle;
          model.current = Text(active.c_str());
          model.previewIcon = [this](const QString& entry) {
            const CadTableStyle* style = CadTableFindStyle(m_State.tableStyles, entry.toUtf8().toStdString());
            if (style == nullptr)
              return QIcon();
            return RibbonTableStylePreviewIcon(*style);
          };
          return model;
        });
        grid->setColumnMinimumWidth(styleCol, kRibbonStyleGalleryWidth);
        grid->addWidget(picker, 0, styleCol, 3, 1, Qt::AlignTop);
        FinalizeAnnotateStylePanel(panel, styleCol);
      }
      if (name == "View" && panelName == "Coordinates") {
        QStringList frames{"WCS"}; for (const auto& preset : ucs::OrthographicPresets()) frames.append(Text(preset.name));
        auto* frame = combo("ribbonUcs", frames);
        connect(frame, &QComboBox::activated, this, [this](int index) {
          if (index == 0) SetActiveUcs(m_State, ucs::Ucs{}, m_Log);
          else if (index <= 6) SetActiveUcs(m_State, ucs::OrthographicPresets()[index - 1].frame, m_Log);
          else if (index - 7 < static_cast<int>(m_State.ucsNamed.size())) SetActiveUcs(m_State, m_State.ucsNamed[index - 7].frame, m_Log);
          Refresh();
        });
      }
      if (name == "Modeling" && panelName == "Piping") {
        QStringList sizes; for (const auto& entry : kCadPipeNpsTable) sizes.append(QString::number(entry.nps) + "in");
        auto* size = combo("ribbonPipeSize", sizes);
        connect(size, &QComboBox::textActivated, this, [this](const QString& value) { ChoosePipeRunNominalSize(m_State, value.toUtf8().toStdString()); Refresh(); });
      }
      if (name == "PointCloudCtx" && panelName == "Centerline Fit") {
        auto* radius = new QDoubleSpinBox(panel); radius->setObjectName("ribbonCenterlineRadius"); radius->setRange(0.05, 20); radius->setSingleStep(0.1); radius->setPrefix("Radius "); radius->setSuffix(" ft"); grid->addWidget(radius, 0, col);
        connect(radius, &QDoubleSpinBox::valueChanged, this, [this](double value) { m_State.extractCenterlineSearchRadiusFt = value; m_Viewport->update(); });
        auto* tolerance = new QSpinBox(panel); tolerance->setObjectName("ribbonCenterlineTolerance"); tolerance->setRange(1, 90); tolerance->setPrefix("Tolerance "); tolerance->setSuffix(" %"); grid->addWidget(tolerance, 1, col++);
        connect(tolerance, &QSpinBox::valueChanged, this, [this](int value) { m_State.extractCenterlineMaxResidualRatio = value / 100.0; m_Viewport->update(); });
      }
      if (name == "PdfCtx" && panelName == "Adjust") {
        auto* fadeSlider = new QSlider(Qt::Horizontal, panel);
        fadeSlider->setObjectName("ribbonPdfFadeSlider");
        fadeSlider->setRange(0, 100);
        fadeSlider->setAccessibleName("Fade");
        fadeSlider->setToolTip("Underlay opacity (0 = transparent, 100 = opaque).");
        auto* fade = new QSpinBox(panel);
        fade->setObjectName("ribbonPdfFade");
        fade->setRange(0, 100);
        fade->setPrefix("Fade ");
        fade->setSuffix(" %");
        fade->setButtonSymbols(QAbstractSpinBox::NoButtons);
        fade->setFixedWidth(72);
        auto* fadeRow = new QWidget(panel);
        auto* fadeLayout = new QHBoxLayout(fadeRow);
        fadeLayout->setContentsMargins(0, 0, 0, 0);
        fadeLayout->addWidget(fadeSlider, 1);
        fadeLayout->addWidget(fade);
        grid->addWidget(fadeRow, 0, col, 1, 2);
        auto* contrast = new QSpinBox(panel);
        contrast->setObjectName("ribbonPdfContrast");
        contrast->setRange(0, 100);
        contrast->setPrefix("Contrast ");
        contrast->setSuffix(" %");
        contrast->setValue(50);
        contrast->setEnabled(false);
        contrast->setToolTip("Contrast adjustment is not available yet.");
        grid->addWidget(contrast, 1, col, 1, 2);
        auto* mono = new QCheckBox("Monochrome", panel);
        mono->setObjectName("ribbonPdfMonochrome");
        mono->setEnabled(false);
        mono->setToolTip("Monochrome display is not available yet.");
        grid->addWidget(mono, 2, col++, 1, 2);
        connect(fadeSlider, &QSlider::valueChanged, fade, &QSpinBox::setValue);
        connect(fade, qOverload<int>(&QSpinBox::valueChanged), fadeSlider, &QSlider::setValue);
        connect(fade, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
          EditRibbonPdf("fade", QString::number(value / 100.0, 'g', 6));
        });
        panel->setMinimumWidth(190);
      }
      if (name == "PdfCtx" && panelName == "Options") {
        auto* snapLines = new QCheckBox("Lines", panel);
        snapLines->setObjectName("ribbonPdfSnapLines");
        snapLines->setToolTip("Snap to line endpoints on this underlay.");
        grid->addWidget(snapLines, 0, col);
        auto* snapCircles = new QCheckBox("Circles", panel);
        snapCircles->setObjectName("ribbonPdfSnapCircles");
        snapCircles->setToolTip("Snap to circle centers on this underlay.");
        grid->addWidget(snapCircles, 1, col);
        auto* snapText = new QCheckBox("Text", panel);
        snapText->setObjectName("ribbonPdfSnapText");
        snapText->setToolTip("Keep text snap data for this underlay.");
        grid->addWidget(snapText, 2, col++);
        connect(snapLines, &QCheckBox::toggled, this, [this](bool on) { EditRibbonPdf("snapLines", on ? "1" : "0"); });
        connect(snapCircles, &QCheckBox::toggled, this, [this](bool on) { EditRibbonPdf("snapCircles", on ? "1" : "0"); });
        connect(snapText, &QCheckBox::toggled, this, [this](bool on) { EditRibbonPdf("snapText", on ? "1" : "0"); });
        panel->setMinimumWidth(220);
      }
      if (name == "HatchCtx") {
        QStringList patterns{"SOLID"}; QFile file(QtAsset("hatches/acadiso.pat"));
        if (file.open(QIODevice::ReadOnly)) {
          std::vector<hatchpat::Def> definitions; hatchpat::Parse(file.readAll().toStdString(), &definitions);
          for (const auto& definition : definitions) if (!patterns.contains(Text(definition.name.c_str()))) patterns.append(Text(definition.name.c_str()));
        }
        auto* pattern = combo("ribbonHatchPattern", patterns); pattern->setAccessibleName("Hatch pattern");
        connect(pattern, &QComboBox::textActivated, this, [this](const QString& value) { EditRibbonHatch("pattern", value); });
        auto* layer = combo("ribbonHatchLayer", {}); layer->setAccessibleName("Hatch layer");
        connect(layer, &QComboBox::textActivated, this, [this](const QString& value) { EditRibbonHatch("layer", value); });
        auto* angle = new QDoubleSpinBox(panel); angle->setObjectName("ribbonHatchAngle"); angle->setRange(-360, 360); angle->setPrefix("Angle "); angle->setSuffix(" deg"); angle->setKeyboardTracking(false); grid->addWidget(angle, 1, col - 2);
        connect(angle, &QDoubleSpinBox::valueChanged, this, [this](double value) { EditRibbonHatch("angle", QString::number(value, 'g', 15)); });
        auto* scale = new QDoubleSpinBox(panel); scale->setObjectName("ribbonHatchScale"); scale->setDecimals(4); scale->setRange(0.0001, 1e9); scale->setPrefix("Scale "); scale->setKeyboardTracking(false); grid->addWidget(scale, 1, col - 1);
        connect(scale, &QDoubleSpinBox::valueChanged, this, [this](double value) { EditRibbonHatch("scale", QString::number(value, 'g', 15)); });
        auto* transparency = new QSpinBox(panel); transparency->setObjectName("ribbonHatchTransparency"); transparency->setRange(0, 100); transparency->setPrefix("Transparency "); transparency->setSuffix(" %"); grid->addWidget(transparency, 2, col - 1);
        connect(transparency, &QSpinBox::valueChanged, this, [this](int value) { EditRibbonHatch("transparency", QString::number(value / 100.0)); });
        auto* color = new QToolButton(panel); color->setText("Color..."); color->setIcon(CadCommandIcon("Color_Faces.png")); color->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); color->setAutoRaise(true); grid->addWidget(color, 2, col - 2);
        connect(color, &QToolButton::clicked, this, [this] {
          const auto selected = QColorDialog::getColor(QColor::fromRgbF(m_State.hatchColorRgb[0], m_State.hatchColorRgb[1], m_State.hatchColorRgb[2]), this, "Hatch color");
          if (selected.isValid()) EditRibbonHatch("color", selected.name());
        });
      }
      if (name == "PointCloudCtx" && panelName == "Point Cloud Display") {
        auto* size = new QDoubleSpinBox(panel); size->setObjectName("ribbonPointSize"); size->setRange(1, 10); size->setSingleStep(0.5); size->setPrefix("Size "); grid->addWidget(size, 0, col);
        connect(size, &QDoubleSpinBox::valueChanged, this, [this](double value) { m_State.pointCloudDisplay.pointSizePx = static_cast<float>(value); m_Viewport->update(); });
        auto* lod = new QSpinBox(panel); lod->setObjectName("ribbonPointLod"); lod->setRange(100000, 3000000); lod->setSingleStep(100000); lod->setPrefix("LOD "); grid->addWidget(lod, 1, col++);
        connect(lod, &QSpinBox::valueChanged, this, [this](int value) { m_State.pointCloudDisplay.lodTargetPoints = value; m_Viewport->update(); });
        auto* scheme = combo("ribbonPointColor", {"RGB", "Solid", "Elevation", "Intensity"});
        connect(scheme, &QComboBox::activated, this, [this](int index) { m_State.pointCloudDisplay.colorScheme = static_cast<PointCloudColorScheme>(index); m_Viewport->update(); });
      }
      const bool important = panelName == "Draw" || panelName == "Modify" || panelName == "Clipboard" || panelName == "Layers";
      int priority = important ? 10 : std::any_of(actions.begin(), actions.end(), [](auto* a) { return a->isEnabled(); }) ? 5 : 0;
      if (name == "Annotate" &&
          (panelName == "Text" || panelName == "Dimensions" || panelName == "Leaders" || panelName == "Tables"))
        priority = 50;
      else if (name == "Annotate")
        priority = 1;
      if (name == "Home" && panelName == "Layers") layerPage->AddPanel(panel, panelName, actions, priority);
      else page->AddPanel(panel, panelName, actions, priority);
    }
    ribbon->addTab(page, name);
  }
  auto* layerButton = layerPage->findChild<QToolButton*>("collapsed_ribbonPanel_Home_Layers");
  if (!layerButton) {
    qFatal("Ribbon Layers panel was not constructed");
    return;
  }
  QMenu* layerMenu = layerButton->menu();
  if (!layerMenu) {
    qFatal("Ribbon Layers menu was not constructed");
    return;
  }
  auto* compactLayers = new QToolButton(header); compactLayers->setObjectName("ribbonCompactLayers");
  compactLayers->setText("Layers"); compactLayers->setAccessibleName("Layer and color controls");
  compactLayers->setPopupMode(QToolButton::InstantPopup); compactLayers->setMenu(layerMenu);
  headerLayout->insertWidget(1, compactLayers);
  QObject::connect(ribbon, &QTabWidget::currentChanged, tabMenu, [wrapper] {
    UpdateRibbonTabButton(wrapper);
    auto* menu = wrapper->findChild<QToolButton*>("ribbonTabMenu")->menu();
    const int current = wrapper->findChild<QTabWidget*>("ribbon")->currentIndex();
    for (auto* action : menu->actions()) action->setChecked(action->property("ribbonIndex").toInt() == current);
  });
  QObject::connect(tabMenuItems, &QMenu::aboutToShow, wrapper, [wrapper] { UpdateRibbonTabMenu(wrapper); });
  auto* resizeFilter = new RibbonResizeFilter(wrapper); wrapper->installEventFilter(resizeFilter); resizeFilter->Schedule();
  toolbar->addWidget(wrapper);
}

void EditorWindow::PickRibbonAnnotStyle(const QString& galleryObjectName, const QString& styleName) {
  const std::string name = styleName.toUtf8().toStdString();
  if (galleryObjectName == QStringLiteral("ribbonTextStyleGallery")) {
    if (TextStyles::Find(m_State.textStyles, name) == nullptr)
      return;
    SetActiveTextStyle(m_State, name);
    Refresh();
    return;
  }
  if (galleryObjectName == QStringLiteral("ribbonDimStyleGallery")) {
    const DimensionStyle* style = DimensionStyles::Find(m_State.dimensionStyles, name);
    if (style == nullptr)
      return;
    PushUndoSnapshot(m_State, "DIMSTY current");
    m_State.activeDimensionStyle = *style;
    m_State.dimStyleDraft = *style;
    Refresh();
    return;
  }
  if (galleryObjectName == QStringLiteral("ribbonMleaderStyleGallery")) {
    const MultileaderStyle* style = MultileaderStyles::Find(m_State.multileaderStyles, name);
    if (style == nullptr)
      return;
    PushUndoSnapshot(m_State, "MLEADERSTYLE current");
    m_State.activeMultileaderStyle = *style;
    m_State.mleaderStyleDraft = *style;
    Refresh();
    return;
  }
  if (galleryObjectName == QStringLiteral("ribbonTableStyleGallery")) {
    if (CadTableFindStyle(m_State.tableStyles, name) == nullptr)
      return;
    m_State.tablePendingStyle = name;
    Refresh();
  }
}

void EditorWindow::OpenRibbonStyleManager(const QString& galleryObjectName) {
  if (galleryObjectName == QStringLiteral("ribbonTextStyleGallery")) {
    m_State.showTextStyleManagerWindow = true;
    Refresh();
    return;
  }
  if (galleryObjectName == QStringLiteral("ribbonDimStyleGallery")) {
    StartDimStyleCommand(m_State, m_Log);
    Refresh();
    return;
  }
  if (galleryObjectName == QStringLiteral("ribbonMleaderStyleGallery")) {
    StartMleaderStyleCommand(m_State, m_Log);
    Refresh();
    return;
  }
  if (galleryObjectName == QStringLiteral("ribbonTableStyleGallery"))
    ShowTableStyles();
}

void EditorWindow::RefreshRibbon() {
  auto* ribbon = findChild<QTabWidget*>("ribbon"); if (!ribbon) return;
  const bool drawing = m_State.activeDrawingIdx > 0;
  if (auto* layers = findChild<QWidget*>("ribbonSharedLayers")) layers->setEnabled(drawing);
  const bool paper = m_State.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(m_State);
  const int surface = paper ? -1 : SelectedSurface(m_State);
  int point = -1, cloud = -1, pdf = -1;
  int hatch = -1;
  const int alignment = paper ? -1 : SelectedAlignment(m_State);
  if (!paper) for (const int i : m_State.selectedSurveyPointIndices)
    if (i >= 0 && i < static_cast<int>(m_State.surveyPoints.size())) { point = i; break; }
  for (const auto& item : m_State.selection) {
    if (!paper && item.type == SelectedEntity::Type::PointCloud && item.index >= 0 && item.index < static_cast<int>(m_State.cadPointClouds.size())) cloud = item.index;
    if (item.type == SelectedEntity::Type::PdfUnderlay && item.index >= 0 && item.index < static_cast<int>(m_State.pdfAttachments.size())) pdf = item.index;
    if (item.type == SelectedEntity::Type::FilledRegion && item.index >= 0 && item.index < static_cast<int>(m_State.cadFilledRegions.size())) hatch = item.index;
  }
  const bool hatchActive = hatch >= 0 || m_State.active == AppCommandState::Kind::Hatch;
  // REQ-523 item 9: a Test Block replaces the Block Editor tab with its own Close tab.
  const bool blockTest = m_State.blockTestActive && !m_State.blockEditorName.empty();
  const bool blockEditor = !m_State.blockEditorName.empty() && !blockTest;
  const QString desired = blockTest ? "BlockTest" : blockEditor ? "BlockEditor" : surface >= 0 ? "SurfaceCtx" : point >= 0 ? "SurveyPointCtx" : cloud >= 0 ? "PointCloudCtx" : hatchActive ? "HatchCtx" : alignment >= 0 ? "AlignmentCtx" : QString{};
  const QString previous = ribbon->property("selectedContext").toString();
  const QString active = ribbon->currentWidget() ? ribbon->currentWidget()->property("ribbonTab").toString() : "Home";
  if (desired != previous && !desired.isEmpty() && previous.isEmpty()) ribbon->setProperty("beforeContext", active);
  int desiredIndex = -1;
  for (int i = 0; i < ribbon->count(); ++i) {
    auto* page = ribbon->widget(i); const QString name = page->property("ribbonTab").toString();
    const bool visible = name == "Layout" ? paper : name == "SurfaceCtx" ? surface >= 0 : name == "SurveyPointCtx" ? point >= 0 :
        name == "PointCloudCtx" ? cloud >= 0 : name == "BlockEditor" ? blockEditor : name == "BlockTest" ? blockTest :
        name == "GeolocationCtx" ? GeolocationRibbonTabVisible(m_State) : name == "PdfCtx" ? pdf >= 0 : name == "HatchCtx" ? hatchActive :
        name == "AlignmentCtx" ? alignment >= 0 : true;
    ribbon->setTabVisible(i, visible); page->setEnabled(drawing);
    if (name == desired) desiredIndex = i;
    QString label = name;
    if (name == "SurfaceCtx") label = surface >= 0 ? "TIN Surface: " + Text(m_State.cadSurfaces[surface].name.c_str()) : "TIN Surface";
    else if (name == "SurveyPointCtx") label = point >= 0 ? QString("Survey Point: %1").arg(m_State.surveyPoints[point].id) : "Survey Point";
    else if (name == "PointCloudCtx") label = "Point Cloud";
    else if (name == "BlockEditor") label = "Block Editor";
    else if (name == "BlockTest") label = "Test Block";
    else if (name == "GeolocationCtx") label = "Geolocation";
    else if (name == "PdfCtx") label = "PDF Underlay";
    else if (name == "HatchCtx") label = "Hatch";
    else if (name == "AlignmentCtx")
      label = alignment >= 0 ? "Alignment: " + Text(m_State.alignments[static_cast<std::size_t>(alignment)].name.c_str()) : "Alignment";
    ribbon->setTabText(i, label);
  }
  if (desired != previous) {
    if (desiredIndex >= 0) ribbon->setCurrentIndex(desiredIndex);
    else if (!previous.isEmpty()) {
      const QString restore = ribbon->property("beforeContext").toString();
      for (int i = 0; i < ribbon->count(); ++i) if (ribbon->widget(i)->property("ribbonTab").toString() == restore && ribbon->isTabVisible(i)) { ribbon->setCurrentIndex(i); break; }
    }
    ribbon->setProperty("selectedContext", desired);
  }
  for (auto* action : findChildren<QAction*>()) {
    const auto route = action->property("ribbonCommand").toString();
    if (route == "@captureArea" || route == "@captureViewport") action->setEnabled(m_State.drawingSettings.onlineMap != DrawingSettings::OnlineMap::Off);
    if (route == "@removeCaptures") action->setEnabled(!m_State.drawingSettings.capturedAreas.empty());
    if (route == "UNISOLATEOBJECTS") action->setEnabled(!m_State.hiddenEntityIds.empty());
    const auto id = action->property("ribbonId").toString(); if (id.isEmpty()) continue;
    bool enabled = drawing && !action->property("releasePlaceholder").toBool() && !action->property("nativeDialogPending").toBool();
    if (id == "##RibbonPlotPreview" || id == "##RibbonViewDetails" || id == "##OutDwfxExport") enabled = enabled && paper &&
        m_State.activeSpaceIndex >= 0 && static_cast<size_t>(m_State.activeSpaceIndex) < m_State.paperLayouts.size();
    if (id == "##RibbonPasteHome") enabled = enabled && !m_State.clipboard.empty();
    if (id == "##ClipPasteSpecial") enabled = enabled && !m_State.clipboard.empty();
    if (id == "##RibbonCopyClipHome") enabled = enabled && (!m_State.selection.empty() || !m_State.selectedPaperEntities.empty());
    // REQ-084 (c): Cut is disabled on an empty selection. Survey points are included because Cut copies them.
    if (id == "##ClipCut") {
      // The same paper test CutSelectionToClipboard uses. A selected section plane counts as a selection: Cut
      // refuses it with a message, which a greyed button would not show.
      const bool cutPaper = ActivePaperGeometryTarget(m_State) != nullptr;
      const bool cutHasSelection = cutPaper ? (!m_State.selectedPaperEntities.empty() || !m_State.selectedViewports.empty())
                                            : (!m_State.selection.empty() || !m_State.selectedSurveyPointIndices.empty() ||
                                               m_State.sectionPlaneSelected);
      enabled = enabled && cutHasSelection;
    }
    action->setEnabled(enabled);
    if (id == "##GeoMap") {
      const auto& map = OnlineMapInfoOf(m_State.drawingSettings.onlineMap);
      action->setText(Text(map.label)); action->setProperty("cadIcon", Text(map.icon) + ".png"); action->setIcon(CadCommandIcon(Text(map.icon) + ".png"));
    }
  }
  if (auto* wrapper = findChild<QWidget*>("ribbonWrapper")) FitRibbonChrome(wrapper);
  auto syncCombo = [&](const char* name, const QStringList& values, const QString& selected) {
    if (auto* combo = findChild<QComboBox*>(name)) {
      if (combo->hasFocus() || combo->view()->isVisible()) return;
      const QSignalBlocker block(combo); combo->clear(); combo->addItems(values); combo->setCurrentText(selected);
    }
  };
  QStringList layers; for (const auto& layer : m_State.drawingLayerTable) layers.append(Text(layer.name.c_str()));
  const auto selectedLayer = CadSelectionLayer(m_State);
  if (auto* combo = findChild<QComboBox*>("ribbonLayer"); combo && !combo->hasFocus() && !combo->view()->isVisible())
    RibbonFillLayerCombo(combo, m_State.drawingLayerTable, Text((selectedLayer.empty() ? m_State.currentLayer : selectedLayer).c_str()));
  const auto* region = hatch >= 0 ? &m_State.cadFilledRegions[hatch] : nullptr;
  const auto* attrs = hatch >= 0 && hatch < static_cast<int>(m_State.cadFilledRegionAttrs.size()) ? &m_State.cadFilledRegionAttrs[hatch] : nullptr;
  if (auto* combo = findChild<QComboBox*>("ribbonHatchLayer"); combo && !combo->hasFocus() && !combo->view()->isVisible())
    RibbonFillLayerCombo(combo, m_State.drawingLayerTable, Text((attrs ? attrs->layer : m_State.hatchLayer.empty() ? m_State.currentLayer : m_State.hatchLayer).c_str()));
  if (auto* combo = ribbon->findChild<QComboBox*>("ribbonHatchPattern")) { const QSignalBlocker block(combo); const auto& pattern = region ? region->patternName : m_State.hatchPatternName; combo->setCurrentText(pattern.empty() ? "SOLID" : Text(pattern.c_str())); }
  if (auto* angle = ribbon->findChild<QDoubleSpinBox*>("ribbonHatchAngle")) { const QSignalBlocker block(angle); angle->setValue(region ? region->patternAngleDeg : m_State.hatchAngleDeg); }
  if (auto* scale = ribbon->findChild<QDoubleSpinBox*>("ribbonHatchScale")) { const QSignalBlocker block(scale); scale->setValue(region ? region->patternScale : m_State.hatchScale); }
  if (auto* transparency = ribbon->findChild<QSpinBox*>("ribbonHatchTransparency")) { const QSignalBlocker block(transparency); transparency->setValue(qRound((attrs ? std::max(0.f, attrs->transparency) : m_State.hatchTransparency01) * 100)); }
  if (auto* combo = findChild<QComboBox*>("ribbonColor")) { const QSignalBlocker block(combo); const auto selected = CadSelectionColor(m_State); combo->setCurrentText(Text((selected.empty() ? m_State.currentColor : selected).c_str())); }
  if (auto* textGallery = findChild<QToolButton*>(QStringLiteral("ribbonTextStyleGallery"))) {
    SyncRibbonStyleGalleryButton(textGallery, Text(m_State.activeTextStyleName.c_str()));
    QStringList entries;
    for (const auto& style : m_State.textStyles)
      entries.append(Text(style.name.c_str()));
    textGallery->setProperty("ribbonStyleEntries", entries);
  }
  if (auto* dimGallery = findChild<QToolButton*>(QStringLiteral("ribbonDimStyleGallery"))) {
    SyncRibbonStyleGalleryButton(dimGallery, Text(m_State.activeDimensionStyle.name.c_str()));
    QStringList entries;
    for (const auto& style : m_State.dimensionStyles)
      entries.append(Text(style.name.c_str()));
    dimGallery->setProperty("ribbonStyleEntries", entries);
  }
  if (auto* mleaderGallery = findChild<QToolButton*>(QStringLiteral("ribbonMleaderStyleGallery"))) {
    SyncRibbonStyleGalleryButton(mleaderGallery, Text(m_State.activeMultileaderStyle.name.c_str()));
    QStringList entries;
    for (const auto& style : m_State.multileaderStyles)
      entries.append(Text(style.name.c_str()));
    mleaderGallery->setProperty("ribbonStyleEntries", entries);
  }
  if (auto* tableGallery = findChild<QToolButton*>(QStringLiteral("ribbonTableStyleGallery"))) {
    const std::string activeTable = m_State.tablePendingStyle.empty() ? "Standard" : m_State.tablePendingStyle;
    SyncRibbonStyleGalleryButton(tableGallery, Text(activeTable.c_str()));
    QStringList entries;
    for (const auto& style : m_State.tableStyles)
      entries.append(Text(style.name.c_str()));
    tableGallery->setProperty("ribbonStyleEntries", entries);
  }
  if (auto* combo = ribbon->findChild<QComboBox*>("ribbonVisualStyle")) { const QSignalBlocker block(combo); auto* vp = CurrentViewport(m_State); combo->setCurrentIndex(static_cast<int>(vp ? vp->visualStyle : m_State.viewportVisualStyle)); }
  if (auto* combo = ribbon->findChild<QComboBox*>("ribbonProjection")) { const QSignalBlocker block(combo); combo->setCurrentIndex(m_State.viewportProjection == Camera::Projection::Perspective ? 1 : 0); }
  if (auto* fov = ribbon->findChild<QSpinBox*>("ribbonFov")) { const QSignalBlocker block(fov); fov->setValue(qRound(m_State.viewportFovDeg)); fov->setEnabled(m_State.viewportProjection == Camera::Projection::Perspective); }
  if (auto* combo = ribbon->findChild<QComboBox*>("ribbonPipeSize")) {
    const QSignalBlocker block(combo); combo->setCurrentText(Text(m_State.pipeRunNominalSize.c_str()));
    combo->setEnabled(m_State.active != AppCommandState::Kind::PipeRun || m_State.pipeRunPhase == AppCommandState::PipeRunPhase::WaitNominalSize);
  }
  if (auto* combo = ribbon->findChild<QComboBox*>("ribbonUcs")) {
    const QSignalBlocker block(combo); while (combo->count() > 7) combo->removeItem(7);
    for (const auto& frame : m_State.ucsNamed) combo->addItem(Text(frame.name.c_str()));
    int selected = ucs::FramesMatch(m_State.activeUcs, ucs::Ucs{}) ? 0 : -1;
    if (selected < 0) for (int i = 0; i < 6; ++i) if (ucs::FramesMatch(m_State.activeUcs, ucs::OrthographicPresets()[i].frame)) { selected = i + 1; break; }
    if (selected < 0) for (int i = 0; i < static_cast<int>(m_State.ucsNamed.size()); ++i) if (ucs::FramesMatch(m_State.activeUcs, m_State.ucsNamed[i].frame)) { selected = i + 7; break; }
    if (selected < 0) { combo->addItem("Unnamed"); selected = combo->count() - 1; }
    combo->setCurrentIndex(selected);
  }
  if (auto* combo = ribbon->findChild<QComboBox*>("ribbonNamedView")) {
    const QSignalBlocker block(combo);
    while (combo->count() > 10) combo->removeItem(10);
    for (const auto& view : m_State.namedViews) combo->addItem(Text(view.name.c_str()));
    const auto* current = CurrentNamedView(m_State); combo->setCurrentText(current ? Text(current->name.c_str()) : "Unsaved View");
  }
  if (auto* size = ribbon->findChild<QDoubleSpinBox*>("ribbonPointSize")) { const QSignalBlocker block(size); size->setValue(m_State.pointCloudDisplay.pointSizePx); }
  if (auto* lod = ribbon->findChild<QSpinBox*>("ribbonPointLod")) { const QSignalBlocker block(lod); lod->setValue(m_State.pointCloudDisplay.lodTargetPoints); }
  if (auto* combo = ribbon->findChild<QComboBox*>("ribbonPointColor")) { const QSignalBlocker block(combo); combo->setCurrentIndex(static_cast<int>(m_State.pointCloudDisplay.colorScheme)); }
  if (auto* radius = ribbon->findChild<QDoubleSpinBox*>("ribbonCenterlineRadius")) { const QSignalBlocker block(radius); radius->setValue(m_State.extractCenterlineSearchRadiusFt); }
  if (auto* tolerance = ribbon->findChild<QSpinBox*>("ribbonCenterlineTolerance")) { const QSignalBlocker block(tolerance); tolerance->setValue(qRound(m_State.extractCenterlineMaxResidualRatio * 100)); }
  if (pdf >= 0 && pdf < static_cast<int>(m_State.pdfAttachments.size())) {
    const PdfAttachment& att = m_State.pdfAttachments[static_cast<size_t>(pdf)];
    if (auto* fade = ribbon->findChild<QSpinBox*>("ribbonPdfFade")) {
      const QSignalBlocker block(fade);
      fade->setValue(qRound(std::clamp(att.fade, 0.f, 1.f) * 100.f));
    }
    if (auto* fadeSlider = ribbon->findChild<QSlider*>("ribbonPdfFadeSlider")) {
      const QSignalBlocker block(fadeSlider);
      fadeSlider->setValue(qRound(std::clamp(att.fade, 0.f, 1.f) * 100.f));
    }
    if (auto* lines = ribbon->findChild<QCheckBox*>("ribbonPdfSnapLines")) {
      const QSignalBlocker block(lines);
      lines->setChecked(att.snapLines);
    }
    if (auto* circles = ribbon->findChild<QCheckBox*>("ribbonPdfSnapCircles")) {
      const QSignalBlocker block(circles);
      circles->setChecked(att.snapCircles);
    }
    if (auto* text = ribbon->findChild<QCheckBox*>("ribbonPdfSnapText")) {
      const QSignalBlocker block(text);
      text->setChecked(att.snapText);
    }
    if (auto* show = findChild<QAction*>("ribbon_PdfShowBtn")) {
      show->setCheckable(true);
      const QSignalBlocker block(show);
      show->setChecked(att.showBackground);
      show->setToolTip(att.showBackground
                           ? "Full raster image visible. Click to hide paper (lines only)."
                           : "Lines visible, paper transparent. Click to show paper.");
    }
    if (auto* snapBtn = findChild<QAction*>("ribbon_PdfSnapBtn")) {
      snapBtn->setCheckable(true);
      const QSignalBlocker block(snapBtn);
      snapBtn->setChecked(att.snapEnabled);
      snapBtn->setToolTip(att.snapEnabled ? "Underlay snap is on." : "Underlay snap is off.");
    }
  }
  for (auto* button : findChildren<QToolButton*>()) if (button->objectName().startsWith("collapsed_") && button->menu() && !button->menu()->actions().empty()) {
    button->setIcon(button->menu()->actions().front()->icon()); button->menu()->setIcon(button->icon());
  }
}

void EditorWindow::EditRibbonHatch(const QString& field, const QString& value) {
  if (m_State.activeDrawingIdx > 0 && m_State.activeDrawingIdx < static_cast<int>(m_State.drawingTabs.size())) {
    const auto uid = m_State.drawingTabs[m_State.activeDrawingIdx].projectUid;
    for (const auto& project : m_State.openProjects) if (project.uid == uid && project.readOnly) { Refresh(); return; }
  }
  std::vector<int> selected;
  for (const auto& item : m_State.selection) if (item.type == SelectedEntity::Type::FilledRegion && item.index >= 0 && item.index < static_cast<int>(m_State.cadFilledRegions.size())) selected.push_back(item.index);
  if (!selected.empty()) {
    PushUndoSnapshot(m_State, "Edit hatch");
    m_State.cadFilledRegionAttrs.resize(m_State.cadFilledRegions.size());
    for (int index : selected) {
      auto& region = m_State.cadFilledRegions[index]; auto& attrs = m_State.cadFilledRegionAttrs[index];
      if (field == "pattern") region.patternName = value.toUtf8().toStdString();
      else if (field == "layer") attrs.layer = value.toUtf8().toStdString();
      else if (field == "color") attrs.color = value.toStdString();
      else if (field == "angle") region.patternAngleDeg = value.toFloat();
      else if (field == "scale") region.patternScale = value.toFloat();
      else if (field == "transparency") attrs.transparency = value.toFloat();
    }
    BumpCadGpuCache(m_State);
  } else {
    if (field == "pattern") m_State.hatchPatternName = value.toUtf8().toStdString();
    else if (field == "layer") m_State.hatchLayer = value.toUtf8().toStdString();
    else if (field == "angle") m_State.hatchAngleDeg = value.toFloat();
    else if (field == "scale") m_State.hatchScale = value.toFloat();
    else if (field == "transparency") m_State.hatchTransparency01 = value.toFloat();
    else if (field == "color") { const QColor color(value); m_State.hatchColorRgb[0] = color.redF(); m_State.hatchColorRgb[1] = color.greenF(); m_State.hatchColorRgb[2] = color.blueF(); }
  }
  m_Viewport->InvalidateGeometry(); Refresh();
}

void EditorWindow::EditRibbonPdf(const QString& field, const QString& value) {
  if (m_State.activeDrawingIdx > 0 && m_State.activeDrawingIdx < static_cast<int>(m_State.drawingTabs.size())) {
    const auto uid = m_State.drawingTabs[m_State.activeDrawingIdx].projectUid;
    for (const auto& project : m_State.openProjects)
      if (project.uid == uid && project.readOnly) {
        Refresh();
        return;
      }
  }
  std::vector<int> selected;
  for (const auto& item : m_State.selection)
    if (item.type == SelectedEntity::Type::PdfUnderlay && item.index >= 0 &&
        item.index < static_cast<int>(m_State.pdfAttachments.size()))
      selected.push_back(item.index);
  if (selected.empty())
    return;
  PushUndoSnapshot(m_State, "Edit PDF underlay");
  for (const int index : selected) {
    PdfAttachment& att = m_State.pdfAttachments[static_cast<size_t>(index)];
    if (field == "fade")
      att.fade = std::clamp(value.toFloat(), 0.f, 1.f);
    else if (field == "snapLines")
      att.snapLines = (value == "1");
    else if (field == "snapCircles")
      att.snapCircles = (value == "1");
    else if (field == "snapText")
      att.snapText = (value == "1");
    else if (field == "snapEnabled")
      att.snapEnabled = (value == "1");
  }
  BumpCadGpuCache(m_State);
  m_Viewport->update();
  Refresh();
}

void EditorWindow::RunRibbonAction(const QString& id) {
  QString command = id;
  for (const auto& item : qtribbon::kButtons) if (Text(item.id) == id) { if (item.placeholder) return; command = Text(item.command); break; }
  const int surface = SelectedSurface(m_State);
  const std::string surfaceName = surface >= 0 ? m_State.cadSurfaces[surface].name : std::string{};
  // REQ-486 / REQ-487: the Alignment tab's commands act on the selected alignment.
  const int selectedAlignment = SelectedAlignment(m_State);
  if (command.contains("{alignment}")) {
    if (selectedAlignment < 0) {
      m_Log.push_back("Select an alignment first, then choose the command again.");
      Refresh();
      return;
    }
    command.replace("{alignment}", Text(m_State.alignments[static_cast<std::size_t>(selectedAlignment)].name.c_str()));
  }
  if (command == "@alignmentGeometryEditor") {
    if (selectedAlignment >= 0) {
      CancelActiveCommand(m_State, m_Log);
      m_State.alignmentLayoutToolsName = m_State.alignments[static_cast<std::size_t>(selectedAlignment)].name;
    }
    Refresh();
    return;
  }
  if (id == "##ModPipeRun") { CancelActiveCommand(m_State, m_Log); StartPipeRunAtCurrentSize(m_State, m_Log); }
  else if (id == "##RibbonInsEdit" || id == "##BeEdit") { CadBlocksOpenEditPicker(m_State, m_Log); ShowBlockEditorPicker(); }
  else if (command == "TOOLSPACE" || command == "@properties") {
    auto* dock = findChild<QDockWidget*>(command == "TOOLSPACE" ? "toolspaceDock" : "propertiesDock");
    if (dock) { dock->show(); dock->raise(); dock->widget()->setFocus(); }
  } else if (command == "@drawingSettings" || command == "@settings") { ShowUnits(); }
  else if (command == "@newView") {
    bool accepted = false; const auto name = QInputDialog::getText(this, "Save view", "View name", QLineEdit::Normal, {}, &accepted);
    if (accepted && !name.trimmed().isEmpty()) Submit("VIEW S " + name.trimmed());
  } else if (command == "@viewManager") m_State.showViewManagerWindow = true;
  else if (command == "@textStyles") m_State.showTextStyleManagerWindow = true;
  else if (command == "@dimStyles") StartDimStyleCommand(m_State, m_Log);
  else if (command == "@leaderStyles") StartMleaderStyleCommand(m_State, m_Log);
  else if (command == "@addLeader") StartMleaderAddLeaderCommand(m_State, m_Log);
  else if (command == "@removeLeader") RemoveLeaderFromSelectedMultileader(m_State, m_Log);
  else if (command == "@editElevations") m_State.showFeatureLineElevWindow = true;
  else if (command == "@createSurface") m_State.showCreateSurfaceWindow = true;
  else if (command == "@pointGroups") m_State.showPointGroupManagerWindow = true;
  else if (command == "@surfaceProperties" && surface >= 0) { m_State.surfacePropertiesIndex = surface; m_State.showSurfacePropertiesWindow = true; }
  else if (command == "@volumes") m_State.volumeDashboard.open = true;
  else if (command == "@connectionModes") m_State.showConnectionModesWindow = true;
  else if (command == "@blockPalettes") m_State.blockAuthoringPaletteOpen = !m_State.blockAuthoringPaletteOpen;
  else if (command == "@createBlock") StartBlockCreateDialog(m_State, m_Log);
  else if (command == "@geoReorient") StartGeoReorientMarkerCommand(m_State, m_Log);
  else if (command == "@geoRemove") {
    if (QMessageBox::question(this, "Remove location", "Remove this drawing's geographic location?", QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes) RemoveGeoLocation(m_State, m_Log);
  } else if (command.startsWith("@map:")) SetOnlineMap(m_State, static_cast<DrawingSettings::OnlineMap>(command.mid(5).toInt()), m_Log);
  else if (command == "@captureArea") StartCaptureMapArea(m_State, true, m_Log);
  else if (command == "@captureViewport") StartCaptureMapArea(m_State, false, m_Log);
  else if (command == "@removeCaptures") RemoveCapturedMapAreas(m_State, m_Log);
  else if (command == "@surfaceManager") m_State.showSurfaceManagerWindow = true;
  else if (command == "@surfaceVisibilityMenu" && surface >= 0) {
    QMessageBox chooser(QMessageBox::Question, "Surface visibility", "Choose an analysis", QMessageBox::NoButton, this);
    auto* los = chooser.addButton("Line of Sight", QMessageBox::AcceptRole);
    auto* zvi = chooser.addButton("Zone of Visual Influence", QMessageBox::AcceptRole);
    chooser.addButton(QMessageBox::Cancel);
    chooser.exec();
    if (chooser.clickedButton() == los) {
      bool accepted = false;
      const QString values = QInputDialog::getText(this, "Surface line of sight", "eyeX, eyeY, eye height, targetX, targetY, target height",
                                                    QLineEdit::Normal, {}, &accepted);
      if (accepted) Submit(Text(("SURFACEVISIBILITY " + surfaceName + ", LOS, " + values.toStdString()).c_str()));
    } else if (chooser.clickedButton() == zvi) {
      bool accepted = false;
      const QString values = QInputDialog::getText(this, "Zone of visual influence", "observerX, observerY, eye height, target height, radius",
                                                    QLineEdit::Normal, {}, &accepted);
      if (accepted) Submit(Text(("SURFACEVISIBILITY " + surfaceName + ", ZVI, " + values.toStdString()).c_str()));
    }
  } else if (command == "@surfaceCrossingsMenu" && surface >= 0) {
    QMessageBox chooser(QMessageBox::Question, "Breakline crossings", "Choose an action", QMessageBox::NoButton, this);
    auto* list = chooser.addButton("List Crossings", QMessageBox::AcceptRole);
    auto* resolve = chooser.addButton("Resolve Crossing", QMessageBox::AcceptRole);
    chooser.addButton(QMessageBox::Cancel);
    chooser.exec();
    if (chooser.clickedButton() == list) Submit(Text(("SURFCROSSBL " + surfaceName + ", LIST").c_str()));
    else if (chooser.clickedButton() == resolve) {
      bool accepted = false;
      const QString values = QInputDialog::getText(this, "Resolve breakline crossing",
          "crossingX, crossingY, HIGHER, LOWER, AVERAGE, TRIM-A, TRIM-B, or elevation",
          QLineEdit::Normal, {}, &accepted);
      if (accepted) Submit(Text(("SURFCROSSBL " + surfaceName + ", RESOLVE, " + values.toStdString()).c_str()));
    }
  }
  else if (command == "@surfaceVisibilityLos" && surface >= 0) {
    bool accepted = false;
    const QString values = QInputDialog::getText(this, "Surface line of sight", "eyeX, eyeY, eye height, targetX, targetY, target height",
                                                  QLineEdit::Normal, {}, &accepted);
    if (accepted) Submit(Text(("SURFACEVISIBILITY " + surfaceName + ", LOS, " + values.toStdString()).c_str()));
  } else if (command == "@surfaceVisibilityZvi" && surface >= 0) {
    bool accepted = false;
    const QString values = QInputDialog::getText(this, "Zone of visual influence", "observerX, observerY, eye height, target height, radius",
                                                  QLineEdit::Normal, {}, &accepted);
    if (accepted) Submit(Text(("SURFACEVISIBILITY " + surfaceName + ", ZVI, " + values.toStdString()).c_str()));
  } else if (command.startsWith("@surfaceLabel") && surface >= 0) {
    const QString kind = command.mid(QString("@surfaceLabel").size()).toUpper();
    if (kind == "WATERSHED") Submit(Text(("SURFLABEL " + surfaceName + ", WATERSHED").c_str()));
    else {
      const QString prompt = kind == "SPOT" ? "X, Y" : kind == "SLOPE" ? "X1, Y1, X2, Y2" : "contour elevation, X, Y";
      bool accepted = false;
      const QString values = QInputDialog::getText(this, "Surface label", prompt, QLineEdit::Normal, {}, &accepted);
      if (accepted) Submit(Text(("SURFLABEL " + surfaceName + ", " + kind.toStdString() + ", " + values.toStdString()).c_str()));
    }
  } else if (command.startsWith("@surfaceLegend") && surface >= 0) {
    const QString kind = command.mid(QString("@surfaceLegend").size()).toUpper();
    bool accepted = false;
    const QString values = QInputDialog::getText(this, "Surface legend", "table insertion X, Y", QLineEdit::Normal, "0, 0", &accepted);
    if (accepted) Submit(Text(("SURFACELEGEND " + surfaceName + ", " + kind.toStdString() + ", " + values.toStdString()).c_str()));
  } else if (command == "@surfaceCrossingsList" && surface >= 0) {
    Submit(Text(("SURFCROSSBL " + surfaceName + ", LIST").c_str()));
  } else if (command == "@surfaceCrossingsResolve" && surface >= 0) {
    bool accepted = false;
    const QString values = QInputDialog::getText(this, "Resolve breakline crossing",
        "crossingX, crossingY, HIGHER, LOWER, AVERAGE, TRIM-A, TRIM-B, or elevation",
        QLineEdit::Normal, {}, &accepted);
    if (accepted) Submit(Text(("SURFCROSSBL " + surfaceName + ", RESOLVE, " + values.toStdString()).c_str()));
  }
  else if ((command == "@surfaceLodLow" || command == "@surfaceLodHigh") && surface >= 0) {
    const bool reduced = command == "@surfaceLodLow";
    SetSurfaceDisplayLod(m_State, static_cast<size_t>(surface), reduced);
  }
  else if (command == "@moveBlocksToSurface" && surface >= 0) {
    MoveSelectedBlockRefsToSurface(m_State, static_cast<size_t>(surface), m_Log);
  }
  else if (command == "@surfaceBreaklines" && surface >= 0) StartDesignateBreaklineCommand(m_State, surfaceName, m_Log);
  else if (command == "@surfaceContours" && surface >= 0) StartDesignateContourCommand(m_State, surfaceName, m_Log);
  else if (command == "@surfaceBoundary" && surface >= 0) StartDesignateBoundaryCommand(m_State, surfaceName, CadBoundaryKind::Outer, m_Log);
  else if (command == "@surfaceAddPoint" && surface >= 0) StartSurfAddPointCommand(m_State, surfaceName, m_Log);
  else if (command == "@surfaceDeletePoint" && surface >= 0) StartSurfDelPointCommand(m_State, surfaceName, m_Log);
  else if (command == "@surfaceMovePoint" && surface >= 0) StartSurfMovePointCommand(m_State, surfaceName, m_Log);
  else if (command == "@surfaceCreateProfile" && surface >= 0) {
    bool accepted = false;
    const QString values = QInputDialog::getText(this, "Create surface profile",
        "alignment name, profile name, interval, view X, view Y, horizontal scale, vertical scale",
        QLineEdit::Normal, {}, &accepted);
    if (accepted) Submit(Text(("CREATEPROFILEFROMSURFACE " + surfaceName + ", " + values.toStdString()).c_str()));
  }
  else if (command == "@surfaceDataShortcut") {
    QMessageBox chooser(QMessageBox::Question, "Surface data shortcut", "Choose a project shortcut action",
                        QMessageBox::NoButton, this);
    auto* create = chooser.addButton("Create Shortcut from Selected Surface", QMessageBox::AcceptRole);
    auto* reference = chooser.addButton("Create Surface Reference", QMessageBox::AcceptRole);
    chooser.addButton(QMessageBox::Cancel);
    chooser.exec();
    if (chooser.clickedButton() == create && surface >= 0)
      CreateSurfaceDataShortcut(m_State, static_cast<size_t>(surface), m_Log);
    else if (chooser.clickedButton() == reference) {
      bool accepted = false;
      const QString values = QInputDialog::getText(this, "Create surface reference",
          "project-relative source drawing, surface ID, reference name", QLineEdit::Normal, {}, &accepted);
      if (accepted) {
        const QStringList fields = values.split(',');
        bool idOk = false;
        const qulonglong id = fields.size() == 3 ? fields[1].trimmed().toULongLong(&idOk) : 0;
        if (!idOk || fields[0].trimmed().isEmpty() || fields[2].trimmed().isEmpty())
          m_Log.push_back("Create Surface Reference — enter a registered source drawing, surface ID, and reference name.");
        else
          CreateSurfaceReference(m_State, fields[0].trimmed().toStdString(), static_cast<std::uint64_t>(id),
                                 fields[2].trimmed().toStdString(), m_Log);
      }
    }
  }
  else if (command == "@surfaceDrapeImage" && surface >= 0) {
    const QString path = QFileDialog::getOpenFileName(this, "Attach image to surface", {},
        "Images (*.png *.jpg *.jpeg *.bmp)");
    if (!path.isEmpty()) {
      bool accepted = false;
      const QString values = QInputDialog::getText(this, "Drape image", "lower-left X, Y, width, height, rotation degrees",
          QLineEdit::Normal, "0, 0, 100, 100, 0", &accepted);
      if (accepted) {
        const QStringList fields = values.split(',');
        double v[5]{};
        bool valid = fields.size() == 5;
        for (int i = 0; valid && i < 5; ++i) {
          bool parsed = false;
          v[i] = fields[i].trimmed().toDouble(&parsed);
          valid = parsed && std::isfinite(v[i]);
        }
        const float localX = valid ? static_cast<float>(v[0] - m_State.worldDocumentOriginX) : 0.f;
        const float localY = valid ? static_cast<float>(v[1] - m_State.worldDocumentOriginY) : 0.f;
        const float width = static_cast<float>(v[2]);
        const float height = static_cast<float>(v[3]);
        const float rotation = static_cast<float>(v[4] * (3.14159265358979323846 / 180.0));
        if (!valid || !std::isfinite(localX) || !std::isfinite(localY) ||
            !std::isfinite(width) || !std::isfinite(height) || !std::isfinite(rotation) ||
            width <= 0.f || height <= 0.f) {
          m_Log.push_back("Drape Image — enter finite coordinates, positive width and height, and a finite rotation.");
        } else if (AttachModelRasterImage(m_State, path.toUtf8().constData(), localX, localY, width, height,
                                          rotation, m_Log)) {
          SetModelRasterDrape(m_State, m_State.cadRasterImages.size() - 1, static_cast<size_t>(surface), m_Log);
        }
      }
    }
  }
  else if (command == "@surfaceDeleteLine" && surface >= 0) StartSurfDelLineCommand(m_State, surfaceName, m_Log);
  else if (command == "@surfaceSwapEdge" && surface >= 0) StartSurfSwapEdgeCommand(m_State, surfaceName, m_Log);
  else if (command == "@surfaceRebuild" && surface >= 0) Submit(Text(("SURFACEREBUILD " + surfaceName).c_str()));
  else if (command == "@extractContours" && surface >= 0) Submit(Text(("EXTRACT " + surfaceName).c_str()));
  else if (command == "@pdfBackground" || command == "@pdfVectorize" || command == "@pdfSnap") {
    for (const auto& item : m_State.selection) if (item.type == SelectedEntity::Type::PdfUnderlay && item.index >= 0 && item.index < static_cast<int>(m_State.pdfAttachments.size())) {
      if (command == "@pdfBackground") { PushUndoSnapshot(m_State, "PDF background"); m_State.pdfAttachments[item.index].showBackground = !m_State.pdfAttachments[item.index].showBackground; BumpCadGpuCache(m_State); }
      else if (command == "@pdfSnap") {
        PushUndoSnapshot(m_State, "PDF snap");
        m_State.pdfAttachments[item.index].snapEnabled = !m_State.pdfAttachments[item.index].snapEnabled;
      }
      else VectorizePdfAttachmentLines(m_State, item.index, m_Log);
      break;
    }
  } else if (command == "@importDxf" || command == "@importDwg") {
    const bool dxf = command == "@importDxf";
    const auto path = QFileDialog::getOpenFileName(this, "Import drawing", {}, dxf ? "DXF (*.dxf)" : "DWG (*.dwg)");
    if (!path.isEmpty()) { if (dxf) ImportDxfFile(m_State, path.toUtf8().constData(), m_Log); else ImportDwgFile(m_State, path.toUtf8().constData(), m_Log); }
  } else if (command == "@exportDxf" || command == "@exportDwg") {
    const bool dxf = command == "@exportDxf";
    const auto path = QFileDialog::getSaveFileName(this, "Export drawing", {}, dxf ? "DXF (*.dxf)" : "DWG (*.dwg)");
    if (!path.isEmpty()) {
      const bool proceed = dxf || ConfirmDwgExport(path);
      if (proceed) { if (dxf) ExportDxfFile(m_State, path.toUtf8().constData(), m_Log); else ExportDwgFile(m_State, path.toUtf8().constData(), m_Log); }
    }
  } else if (command == "@batchPlot") {
    m_State.batchPlotSelected.clear(); if (m_State.activeSpaceIndex >= 0) m_State.batchPlotSelected.push_back(m_State.activeSpaceIndex); m_State.showBatchPlotDialog = true;
  } else if (command == "@plotPreview") {
    PreviewActiveLayout();
  } else if (command == "@viewDetails") {
    ShowActiveLayoutDetails();
  } else if (command == "@exportCurrentLayoutPdf") {
    if (InFloatingModelSpace(m_State) || m_State.activeSpaceIndex < 0 ||
        static_cast<size_t>(m_State.activeSpaceIndex) >= m_State.paperLayouts.size()) {
      m_Log.push_back("PDF Export — switch to a paper layout first.");
    } else {
      QFileDialog dialog(this, "Export current layout to PDF", {}, "PDF (*.pdf)");
      dialog.setObjectName("currentLayoutPdfExportDialog");
      dialog.setAcceptMode(QFileDialog::AcceptSave);
      dialog.setFileMode(QFileDialog::AnyFile);
      dialog.setDefaultSuffix("pdf");
      if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().empty()) {
        const QString outputPath = dialog.selectedFiles().front();
        const QString stagingTemplate = QFileInfo(outputPath).dir().filePath(".gosurvey-pdf-XXXXXX.pdf");
        QTemporaryFile staged(stagingTemplate);
        bool exported = false;
        QString failure;
        if (!staged.open()) {
          failure = "PDF Export — could not create a temporary output file.";
        } else {
          const QString stagedPath = staged.fileName();
          staged.close();
          if (!PlotLayoutsToPdf(m_State, {m_State.activeSpaceIndex}, stagedPath.toUtf8().constData(), m_Log)) {
            failure = m_Log.empty() ? "PDF Export — plotting failed." : QString::fromUtf8(m_Log.back().c_str());
          } else {
            QFile source(stagedPath);
            QSaveFile destination(outputPath);
            if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) {
              failure = "PDF Export — could not open the final output file.";
            } else {
              QByteArray buffer(64 * 1024, Qt::Uninitialized);
              while (!source.atEnd()) {
                const qint64 bytesRead = source.read(buffer.data(), buffer.size());
                if (bytesRead <= 0 || destination.write(buffer.constData(), bytesRead) != bytesRead) {
                  failure = "PDF Export — could not publish the completed PDF.";
                  break;
                }
              }
              source.close();
              if (failure.isEmpty() && !destination.commit())
                failure = "PDF Export — could not commit the completed PDF.";
              else if (!failure.isEmpty())
                destination.cancelWriting();
            }
            if (failure.isEmpty()) {
              exported = true;
              if (!m_Log.empty() && m_Log.back().find("PLOT — wrote ") == 0)
                m_Log.back() = "PLOT — wrote 1 page(s) to " + outputPath.toUtf8().toStdString();
            }
          }
        }
        if (!exported) {
          if (failure.isEmpty()) failure = "PDF Export — failed without producing a PDF.";
          const std::string failureLog = failure.toUtf8().toStdString();
          if (m_Log.empty() || m_Log.back() != failureLog) m_Log.push_back(failureLog);
          QMessageBox::warning(this, "Export PDF", failure);
        }
      }
    }
  } else if (command == "@layoutManager") {
    ShowLayoutManager();
  } else if (command == "@pageSetup") {
    StartPageSetupCommand(m_State, m_Log);
    if (m_State.showPageSetupManager) ShowPageSetupManager(); else QMessageBox::information(this, "Page Setup Manager", QString::fromUtf8(m_Log.back().c_str()));
  } else if (command == "@vpScale") {
    ShowViewportScale();
  } else if (command == "@viewports") {
    if (m_State.activeSpaceIndex < 0) QMessageBox::information(this, "Viewports", "Switch to a paper layout first.");
    else ShowViewportsManager();
  } else if (command == "PLOT") {
    const auto path = QFileDialog::getSaveFileName(this, "Plot layout", {}, "PDF (*.pdf)");
    if (!path.isEmpty()) PlotLayoutsToPdf(m_State, {std::max(0, m_State.activeSpaceIndex)}, path.toUtf8().constData(), m_Log);
  } else if (command == "GRADING") {
    CancelActiveCommand(m_State, m_Log);
    ShowGradingTools();
  } else if (!command.startsWith('@')) {
    CancelActiveCommand(m_State, m_Log); Submit(command);
  }
  Refresh();
}

bool EditorWindow::ConfirmDwgExport(const QString& path) {
  const auto losses = ComputeDwgExportLosses(m_State);
  const bool overwriting = QFileInfo::exists(path);
  QDialog dialog(this);
  dialog.setObjectName("dwgExportWarning");
  dialog.setWindowTitle("Export DWG");
  dialog.setMinimumWidth(440);
  auto* layout = new QVBoxLayout(&dialog);
  auto* formatLabel = new QLabel("DWG format:", &dialog);
  layout->addWidget(formatLabel);
  auto* format = new QComboBox(&dialog);
  format->setObjectName("dwgExportVersion");
  size_t formatCount = 0;
  const auto* formats = DwgExportFormatRows(&formatCount);
  int selectedIndex = -1;
  for (size_t i = 0; i < formatCount; ++i) {
    if (!formats[i].selectable) continue;
    format->addItem(QString("%1 (%2)").arg(Text(formats[i].displayName), Text(formats[i].acTag)),
                    static_cast<int>(formats[i].version));
    if (formats[i].version == m_State.dwgExportVersion) selectedIndex = format->count() - 1;
  }
  if (selectedIndex >= 0) format->setCurrentIndex(selectedIndex);
  layout->addWidget(format);

  auto* details = new QLabel(&dialog);
  details->setObjectName("dwgExportLossDetails");
  details->setWordWrap(true);
  auto updateDetails = [&, details](int index) {
    const auto version = static_cast<DwgSaveVersion>(format->itemData(index).toInt());
    QStringList lines;
    if (!losses.empty()) {
      lines.append(QString("This export drops or changes (as %1):").arg(Text(DwgSaveVersionAcTag(version))));
      for (const auto& loss : losses)
        lines.append(QString("• %1 %2").arg(loss.count).arg(Text(loss.label.c_str())));
    } else {
      lines.append("This drawing has no known DWG export losses.");
    }
    if (overwriting)
      lines.append(QString("\n%1 already exists. Exporting will replace its current contents.").arg(QFileInfo(path).fileName()));
    details->setText(lines.join('\n'));
  };
  updateDetails(format->currentIndex());
  connect(format, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, updateDetails);
  layout->addWidget(details);

  auto* buttons = new QDialogButtonBox(&dialog);
  auto* proceed = buttons->addButton(overwriting ? "Overwrite" : "Export", QDialogButtonBox::AcceptRole);
  proceed->setObjectName("confirmDwgExport");
  auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
  cancel->setObjectName("cancelDwgExport");
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted) return false;
  m_State.dwgExportVersion = static_cast<DwgSaveVersion>(format->currentData().toInt());
  return true;
}
