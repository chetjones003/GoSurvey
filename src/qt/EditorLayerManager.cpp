#include "EditorLayerManager.hpp"

#include "CadColor.hpp"
#include "CadCommands.hpp"
#include "CadLinetype.hpp"
#include "EditorWindow.hpp"
#include "QtAccent.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace {

enum Column : std::uint8_t { Name, On, Frozen, Locked, Color, Linetype, Lineweight, VpFreeze, VpColor, ColumnCount };
constexpr const char* kHeaders[ColumnCount] = {"Name", "On", "Frozen", "Locked", "Color", "Linetype",
                                               "Lineweight (mm)", "VP Freeze", "VP Color"};
constexpr int kColumnWidth[ColumnCount] = {200, 56, 70, 70, 130, 170, 130, 90, 130};
constexpr int kRowHeight = 36;
constexpr QSize kSwatch{30, 16};

class PaletteRefreshFilter final : public QObject {
 public:
  PaletteRefreshFilter(QObject* parent, std::function<void()> refresh) : QObject(parent), m_Refresh(std::move(refresh)) {}
 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (!m_Updating && (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::StyleChange)) {
      m_Updating = true;
      m_Refresh();
      m_Updating = false;
    }
    return QObject::eventFilter(watched, event);
  }
 private:
  std::function<void()> m_Refresh;
  bool m_Updating = false;
};

QString LayerManagerStyle(bool dark) {
  QString style = QtApplyAccent(
      "QLabel#layerManagerTitle { font-size:17px; font-weight:600; }"
      "QLabel#layerManagerSubtitle, QLabel#layerManagerHint { color:@quiet@; }",
      dark) + (dark ? QString("QPushButton:disabled { color:#89979a; background:#242a2f; border:1px solid #343c41; }")
                    : QString("QPushButton:disabled { color:#68727b; background:#edf0f2; border:1px solid #cbd2d8; }")) +
      (dark ? QString() : QString("QLabel#layerManagerSubtitle, QLabel#layerManagerHint { color:#3f4952; }")) +
      QtApplyAccent(
      "QLabel#layerManagerCurrent { font-weight:600; }"
      "QPushButton[primary=\"true\"] { background:@fill@; color:white; font-weight:600; padding:6px 16px; border:0; border-radius:4px; }"
      "QPushButton[primary=\"true\"]:hover { background:@fillHover@; }"
      "QPushButton:disabled { color:palette(placeholder-text); background:palette(alternate-base); border-color:palette(midlight); }"
      "QPushButton[primary=\"true\"]:disabled { color:palette(placeholder-text); background:palette(alternate-base); }"
      "QTableWidget { border:1px solid palette(midlight); border-radius:6px; background:palette(base); outline:0; }"
      "QTableWidget::item { padding:0 12px; border-bottom:1px solid palette(midlight); }"
      "QTableWidget::item:hover { background:palette(alternate-base); }"
      "QTableWidget::item:selected { background:palette(alternate-base); color:palette(text); }"
      "QTableWidget QLineEdit { border:1px solid palette(mid); border-radius:3px; padding:0 4px; }"
      "QTableWidget QLineEdit:focus { border:1px solid @strong@; }"
      "QHeaderView { background:palette(base); }"
      "QHeaderView::section { background:palette(base); color:palette(placeholder-text); font-weight:600; padding:8px 12px; "
      "border:0; border-bottom:1px solid palette(mid); }"
      "QHeaderView::section:hover { color:palette(text); }", dark);
  style += dark
      ? "QPushButton:disabled { color:#7e898f; background:#252a2e; border:1px solid #3d454a; }"
      : "QPushButton:disabled { color:#535f68; background:#e1e6ea; border:1px solid #aeb8c1; }";
  return style;
}

QString FromUtf8(const std::string& value) { return QString::fromUtf8(value.c_str()); }
std::string ToUtf8(const QString& value) { return value.toUtf8().toStdString(); }

bool IsToggleColumn(int column) { return column == On || column == Frozen || column == Locked || column == VpFreeze; }
bool IsColorColumn(int column) { return column == Color || column == VpColor; }

/// A painted colour chip: the stored colour inside a thin outline so white and black stay visible on any row.
QPixmap ColorSwatch(const std::string& storage, qreal ratio, const QColor& outline) {
  float rgb[3] = {1.f, 1.f, 1.f};
  CadColorResolveRgb(storage, 1.f, 1.f, 1.f, rgb);
  QPixmap pixmap(kSwatch * ratio);
  pixmap.setDevicePixelRatio(ratio);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(outline, 1));
  painter.setBrush(QColor::fromRgbF(rgb[0], rgb[1], rgb[2]));
  painter.drawRoundedRect(QRectF(0.5, 0.5, kSwatch.width() - 1, kSwatch.height() - 1), 3, 3);
  return pixmap;
}

/// A short line drawn in the layer's own linetype pattern, using the same tessellator as the viewport.
QPixmap LinetypeSample(const std::string& linetype, qreal ratio, const QColor& ink) {
  const int width = 56, height = kSwatch.height();
  QPixmap pixmap(QSize(width, height) * ratio);
  pixmap.setDevicePixelRatio(ratio);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setPen(QPen(ink, 2, Qt::SolidLine, Qt::FlatCap));
  const float y = height / 2.f, rgba[4] = {1.f, 1.f, 1.f, 1.f};
  std::vector<float> verts;
  CadTessellateLinetypeSegmentVc(2.f, y, 0.f, width - 2.f, y, 0.f, linetype, 12.f, rgba, &verts);
  for (size_t i = 0; i + 14 <= verts.size(); i += 14) painter.drawLine(QPointF(verts[i], verts[i + 1]), QPointF(verts[i + 7], verts[i + 8]));
  return pixmap;
}

/// Name cells: the current layer gets a quiet theme-aware tint and a clear accent bar.
class NameDelegate : public QStyledItemDelegate {
public:
  explicit NameDelegate(QObject* parent) : QStyledItemDelegate(parent) {}
  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    const bool isCurrent = index.data(Qt::UserRole + 1).toBool();
    const bool dark = option.palette.color(QPalette::Base).lightness() < 128;
    QStyleOptionViewItem styled = option;
    if (isCurrent && !(option.state & QStyle::State_Selected)) {
      QColor tint(QtAccentFor(dark).fill);
      tint.setAlpha(dark ? 34 : 42);
      styled.backgroundBrush = tint;
    }
    QStyledItemDelegate::paint(painter, styled, index);
    if (isCurrent)
      painter->fillRect(QRect(option.rect.left(), option.rect.top() + 5, 4, option.rect.height() - 10), QColor(QtAccentFor(dark).strong));
  }
};

/// Draws the On/Frozen/Locked/VP Freeze boxes ourselves so an unchecked box is visible and a checked one is
/// unmistakable in every theme (REQ-082), and toggles on a click or Space anywhere in the cell.
class CheckBoxDelegate : public QStyledItemDelegate {
public:
  explicit CheckBoxDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    QStyleOptionViewItem plain = option;
    initStyleOption(&plain, index);
    plain.text.clear();
    plain.features &= ~QStyleOptionViewItem::HasCheckIndicator;
    plain.icon = QIcon();
    const QWidget* widget = option.widget;
    (widget ? widget->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &plain, painter, widget);
    if (!index.data(Qt::CheckStateRole).isValid()) return;
    const bool dark = option.palette.color(QPalette::Base).lightness() < 128;
    const QtAccent& accent = QtAccentFor(dark);
    const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
    const QRectF box(option.rect.center().x() - 8.0, option.rect.center().y() - 8.0, 16.0, 16.0);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QColor edge = option.palette.color(QPalette::Text);
    edge.setAlphaF(0.6);
    painter->setPen(QPen(checked ? QColor(accent.fill) : edge, 1.5));
    painter->setBrush(checked ? QColor(accent.fill) : option.palette.color(QPalette::Base));
    painter->drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    if (checked) {
      painter->setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter->drawPolyline(QPolygonF{box.topLeft() + QPointF(3.5, 8.5), box.topLeft() + QPointF(6.5, 11.5), box.topLeft() + QPointF(12.5, 4.5)});
    }
    painter->restore();
  }

  bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem&, const QModelIndex& index) override {
    if (!(index.flags() & Qt::ItemIsEnabled) || !index.data(Qt::CheckStateRole).isValid()) return false;
    const bool click = event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton;
    const bool key = event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Space;
    if (!click && !key) return false;
    const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
    return model->setData(index, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
  }
};

/// Ordering of two layers under a header sort: by the value the column shows, never by display text alone
/// (booleans by state, colours by index, lineweight numerically).
int CompareLayers(const CadLayerRow& a, const CadLayerRow& b, int column, const Viewport* viewport) {
  auto three = [](auto x, auto y) { return x < y ? -1 : (y < x ? 1 : 0); };
  auto text = [](const std::string& x, const std::string& y) {
    return FromUtf8(x).compare(FromUtf8(y), Qt::CaseInsensitive);
  };
  auto aci = [](const std::string& storage) { int index = 1000; return CadColorTryGetAci(storage, &index) ? index : 1000; };
  switch (column) {
    case Name: return text(a.name, b.name);
    case On: return three(a.on, b.on);
    case Frozen: return three(a.frozen, b.frozen);
    case Locked: return three(a.locked, b.locked);
    case Color: return three(aci(a.color), aci(b.color));
    case Linetype: return text(a.linetype, b.linetype);
    case Lineweight: return three(a.lineweightMm, b.lineweightMm);
    case VpFreeze: return viewport ? three(IsLayerFrozenInViewport(*viewport, a.name), IsLayerFrozenInViewport(*viewport, b.name)) : 0;
    case VpColor: {
      const auto* x = viewport ? ViewportLayerColorOverride(*viewport, a.name) : nullptr;
      const auto* y = viewport ? ViewportLayerColorOverride(*viewport, b.name) : nullptr;
      return text(x ? *x : std::string(), y ? *y : std::string());
    }
    default: return 0;
  }
}

} // namespace

void ShowLayerManager(QWidget* parent, AppCommandState& state, const std::function<void()>& refresh) {
  SyncDrawingLayerTableWithGeometry(state);
  QDialog dialog(parent);
  dialog.setObjectName("layerManager");
  dialog.setWindowTitle("Layer Properties Manager");
  dialog.setSizeGripEnabled(true);
  dialog.resize(1040, std::clamp(240 + static_cast<int>(state.drawingLayerTable.size()) * kRowHeight, 360, 580));
  dialog.setMinimumSize(560, 320);
  bool dark = dialog.palette().color(QPalette::Window).lightness() < 128;
  dialog.setStyleSheet(LayerManagerStyle(dark));

  auto* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(20, 16, 20, 14);
  layout->setSpacing(10);
  auto* title = new QLabel("Layer Properties Manager", &dialog);
  title->setObjectName("layerManagerTitle");
  auto* subtitle = new QLabel(&dialog);
  subtitle->setObjectName("layerManagerSubtitle");
  layout->addWidget(title);
  layout->addWidget(subtitle);

  auto* toolbar = new QHBoxLayout;
  toolbar->setSpacing(8);
  auto* add = new QPushButton(QIcon(QtAsset("icons/New_Layer.png")), "New layer", &dialog);
  add->setObjectName("layerNew");
  add->setProperty("primary", true);
  auto* erase = new QPushButton("Delete layer", &dialog);
  erase->setObjectName("layerDelete");
  auto* current = new QPushButton("Set current", &dialog);
  current->setObjectName("layerSetCurrent");
  auto* currentLabel = new QLabel(&dialog);
  currentLabel->setObjectName("layerManagerCurrent");
  toolbar->addWidget(add); toolbar->addWidget(erase); toolbar->addWidget(current); toolbar->addStretch(); toolbar->addWidget(currentLabel);
  layout->addLayout(toolbar);

  auto* filter = new QLineEdit(&dialog);
  filter->setObjectName("layerManagerFilter");
  filter->setPlaceholderText("Filter layers by name");
  filter->setAccessibleName("Filter layers");
  auto* filterIcon = filter->addAction(
      QIcon(QtAsset(dark ? "icons/search_dark.svg" : "icons/search_light.svg")), QLineEdit::LeadingPosition);
  filterIcon->setObjectName("layerManagerFilterIcon");
  auto* clearFilter = filter->addAction(dialog.style()->standardIcon(QStyle::SP_LineEditClearButton), QLineEdit::TrailingPosition);
  clearFilter->setObjectName("layerManagerFilterClear");
  clearFilter->setVisible(false);
  QObject::connect(clearFilter, &QAction::triggered, filter, &QLineEdit::clear);
  QObject::connect(filter, &QLineEdit::textChanged, clearFilter, [clearFilter](const QString& text) {
    clearFilter->setVisible(!text.isEmpty());
  });
  layout->addWidget(filter);

  auto* table = new QTableWidget(&dialog);
  table->setObjectName("layerTable");
  table->setColumnCount(ColumnCount);
  QStringList headers;
  for (const char* header : kHeaders) headers << header;
  table->setHorizontalHeaderLabels(headers);
  for (int column = 0; column < ColumnCount; ++column) {
    const bool dataColumn = column == Name || column == Color || column == Linetype || column == Lineweight;
    table->horizontalHeaderItem(column)->setTextAlignment(
        (dataColumn ? Qt::AlignLeft : Qt::AlignHCenter) | Qt::AlignVCenter);
  }
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setAlternatingRowColors(false);
  table->setItemDelegateForColumn(Name, new NameDelegate(table));
  table->setShowGrid(false);
  table->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  table->setIconSize(QSize(56, kSwatch.height()));
  table->setItemDelegateForColumn(On, new CheckBoxDelegate(table));
  table->setItemDelegateForColumn(Frozen, new CheckBoxDelegate(table));
  table->setItemDelegateForColumn(Locked, new CheckBoxDelegate(table));
  table->setItemDelegateForColumn(VpFreeze, new CheckBoxDelegate(table));
  auto* vertical = table->verticalHeader();
  vertical->hide();
  vertical->setSectionResizeMode(QHeaderView::Fixed);
  vertical->setDefaultSectionSize(kRowHeight);
  auto* header = table->horizontalHeader();
  header->setSectionsMovable(true);
  header->setSectionsClickable(true);
  header->setSortIndicatorShown(true);
  header->setSortIndicator(-1, Qt::AscendingOrder);
  header->setMinimumSectionSize(48);
  header->setContextMenuPolicy(Qt::CustomContextMenu);
  header->setSectionResizeMode(QHeaderView::Interactive);
  header->setSectionResizeMode(Name, QHeaderView::Stretch);  // the Name column takes the width the others leave
  for (int column = 0; column < ColumnCount; ++column) if (column != Name) header->resizeSection(column, kColumnWidth[column]);
  layout->addWidget(table, 1);
  auto fitVisibleRows = [&] {
    int visibleRows = 0;
    for (int row = 0; row < table->rowCount(); ++row) visibleRows += table->isRowHidden(row) ? 0 : 1;
    const int rowCount = std::min(visibleRows, 8);
    table->setFixedHeight(header->sizeHint().height() + 4 + rowCount * kRowHeight);
  };
  auto* noMatches = new QLabel("No layers match this search.", &dialog);
  noMatches->setObjectName("layerManagerNoMatches");
  noMatches->setAlignment(Qt::AlignCenter);
  noMatches->hide();
  layout->addWidget(noMatches);

  auto* hint = new QLabel("Click a column heading to sort. Right-click a heading to show or hide columns, or drag it to reorder.", &dialog);
  hint->setObjectName("layerManagerHint");
  layout->addWidget(hint);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  layout->addWidget(buttons);

  // Display order is a permutation of the stored table: sorting never touches state.drawingLayerTable (REQ-082).
  int sortColumn = -1;
  Qt::SortOrder sortOrder = Qt::AscendingOrder;
  const qreal ratio = dialog.devicePixelRatioF();
  auto populate = [&] {
    dark = dialog.palette().color(QPalette::Window).lightness() < 128;
    const QColor outline = dark ? QColor("#59636b") : QColor("#66727d");
    const QColor ink = dark ? QColor("#e2e6ea") : QColor("#414b54");
    const QSignalBlocker blocker(table);
    std::string selected;
    if (table->currentRow() >= 0 && table->item(table->currentRow(), Name))
      selected = ToUtf8(table->item(table->currentRow(), Name)->data(Qt::UserRole).toString());
    Viewport* viewport = CurrentViewport(state);
    table->setColumnHidden(VpFreeze, viewport == nullptr);
    table->setColumnHidden(VpColor, viewport == nullptr);
    std::vector<size_t> order(state.drawingLayerTable.size());
    std::iota(order.begin(), order.end(), size_t{0});
    if (sortColumn >= 0)
      std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const int c = CompareLayers(state.drawingLayerTable[a], state.drawingLayerTable[b], sortColumn, viewport);
        return sortOrder == Qt::AscendingOrder ? c < 0 : c > 0;
      });
    table->clearContents();
    table->setRowCount(static_cast<int>(order.size()));
    int selectedRow = -1;
    for (int row = 0; row < table->rowCount(); ++row) {
      const auto& layer = state.drawingLayerTable[order[static_cast<size_t>(row)]];
      const bool isCurrent = layer.name == state.currentLayer;
      if (layer.name == selected) selectedRow = row;
      auto put = [&](int column, QTableWidgetItem* item) {
        table->setItem(row, column, item);
        return item;
      };
      auto* name = put(Name, new QTableWidgetItem(FromUtf8(layer.name)));
      name->setData(Qt::UserRole, name->text());
      name->setData(Qt::UserRole + 1, isCurrent);
      if (isCurrent) { auto font = name->font(); font.setBold(true); name->setFont(font); name->setToolTip("Current layer"); }
      auto toggle = [&](int column, bool value) {
        auto* item = put(column, new QTableWidgetItem);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setCheckState(value ? Qt::Checked : Qt::Unchecked);
      };
      toggle(On, layer.on); toggle(Frozen, layer.frozen); toggle(Locked, layer.locked);
      auto* color = put(Color, new QTableWidgetItem(FromUtf8(layer.color)));
      color->setData(Qt::DecorationRole, ColorSwatch(layer.color, ratio, outline));
      auto* linetype = put(Linetype, new QTableWidgetItem(FromUtf8(layer.linetype)));
      linetype->setData(Qt::DecorationRole, LinetypeSample(layer.linetype, ratio, ink));
      put(Lineweight, new QTableWidgetItem(layer.lineweightMm < 0.f
          ? QString("Default") : QString::number(layer.lineweightMm, 'g', 4)));
      if (viewport) {
        toggle(VpFreeze, IsLayerFrozenInViewport(*viewport, layer.name));
        const std::string* vpOverride = ViewportLayerColorOverride(*viewport, layer.name);
        auto* vpColor = put(VpColor, new QTableWidgetItem(vpOverride ? FromUtf8(*vpOverride) : QString()));
        if (vpOverride) vpColor->setData(Qt::DecorationRole, ColorSwatch(*vpOverride, ratio, outline));
      }
    }
    const QString filterText = filter->text().trimmed();
    bool anyVisible = false;
    for (int row = 0; row < table->rowCount(); ++row) {
      table->setRowHidden(row, !filterText.isEmpty() && !table->item(row, Name)->text().contains(filterText, Qt::CaseInsensitive));
      anyVisible |= !table->isRowHidden(row);
    }
    noMatches->setVisible(!filterText.isEmpty() && !anyVisible);
    fitVisibleRows();
    if (selectedRow >= 0) table->setCurrentCell(selectedRow, Name);
    int visibleCount = 0;
    for (int row = 0; row < table->rowCount(); ++row) visibleCount += table->isRowHidden(row) ? 0 : 1;
    const int totalCount = static_cast<int>(state.drawingLayerTable.size());
    const QString countText = filterText.isEmpty()
        ? QString("%1 layer%2").arg(totalCount).arg(totalCount == 1 ? "" : "s")
        : QString("%1 of %2 layers").arg(visibleCount).arg(totalCount);
    subtitle->setText(countText + (viewport ? "  ·  editing the VP columns for the current viewport" : QString()));
    currentLabel->setText("Current layer: " + FromUtf8(state.currentLayer));
  };
  populate();
  auto* appearanceFilter = new PaletteRefreshFilter(&dialog, [&] {
    dark = dialog.palette().color(QPalette::Window).lightness() < 128;
    dialog.setStyleSheet(LayerManagerStyle(dark));
    filterIcon->setIcon(QIcon(QtAsset(dark ? "icons/search_dark.svg" : "icons/search_light.svg")));
    populate();
  });
  dialog.installEventFilter(appearanceFilter);

  auto selectedLayerName = [&]() -> std::string {
    const int row = table->currentRow();
    return row < 0 || !table->item(row, Name) ? std::string() : ToUtf8(table->item(row, Name)->data(Qt::UserRole).toString());
  };

  auto updateSelectionActions = [&] {
    const bool hasSelection = table->selectionModel()->hasSelection() && table->currentRow() >= 0 &&
                              !table->isRowHidden(table->currentRow());
    erase->setEnabled(hasSelection);
    current->setEnabled(hasSelection && selectedLayerName() != state.currentLayer);
  };
  QObject::connect(table->selectionModel(), &QItemSelectionModel::currentRowChanged, &dialog,
                   [&](const QModelIndex&, const QModelIndex&) { updateSelectionActions(); });
  QObject::connect(filter, &QLineEdit::textChanged, &dialog, [&](const QString&) {
    const int row = table->currentRow();
    if (row >= 0 && table->isRowHidden(row)) table->clearSelection();
    for (int i = 0; i < table->rowCount(); ++i)
      table->setRowHidden(i, !filter->text().trimmed().isEmpty() &&
                             !table->item(i, Name)->text().contains(filter->text().trimmed(), Qt::CaseInsensitive));
    bool anyVisible = false;
    for (int i = 0; i < table->rowCount(); ++i) anyVisible |= !table->isRowHidden(i);
    noMatches->setVisible(!filter->text().trimmed().isEmpty() && !anyVisible);
    fitVisibleRows();
    layout->activate();
    dialog.resize(dialog.width(), std::clamp(layout->sizeHint().height(), 320, 580));
    int visibleCount = 0;
    for (int i = 0; i < table->rowCount(); ++i) visibleCount += table->isRowHidden(i) ? 0 : 1;
    const int totalCount = table->rowCount();
    const QString countText = filter->text().trimmed().isEmpty()
        ? QString("%1 layer%2").arg(totalCount).arg(totalCount == 1 ? "" : "s")
        : QString("%1 of %2 layers").arg(visibleCount).arg(totalCount);
    subtitle->setText(countText + (CurrentViewport(state) ? "  ·  editing the VP columns for the current viewport" : QString()));
    updateSelectionActions();
  });
  updateSelectionActions();

  QObject::connect(header, &QHeaderView::sortIndicatorChanged, &dialog, [&](int column, Qt::SortOrder order) {
    sortColumn = column; sortOrder = order; populate();
  });
  QObject::connect(header, &QHeaderView::customContextMenuRequested, &dialog, [&](const QPoint& at) {
    QMenu menu(&dialog);
    const bool viewport = CurrentViewport(state) != nullptr;
    for (int column = 0; column < ColumnCount; ++column) {
      if (!viewport && (column == VpFreeze || column == VpColor)) continue;
      auto* action = menu.addAction(kHeaders[column]);
      action->setCheckable(true);
      action->setChecked(!table->isColumnHidden(column));
      QObject::connect(action, &QAction::toggled, &dialog, [&, column](bool shown) {
        int visible = 0;
        for (int other = 0; other < ColumnCount; ++other) visible += table->isColumnHidden(other) ? 0 : 1;
        if (!shown && visible <= 1) return;
        table->setColumnHidden(column, !shown);
      });
    }
    menu.exec(header->mapToGlobal(at));
  });

  QObject::connect(table, &QTableWidget::itemChanged, &dialog, [&](QTableWidgetItem* item) {
    const int row = item->row(), column = item->column();
    // The record is the one named in the row shown, whatever the sort order (REQ-082).
    const auto name = ToUtf8(table->item(row, Name)->data(Qt::UserRole).toString());
    const auto found = std::find_if(state.drawingLayerTable.begin(), state.drawingLayerTable.end(),
        [&](const auto& layer) { return layer.name == name; });
    if (found == state.drawingLayerTable.end()) return;
    std::string error;
    if (column == Name) {
      if (!CadRenameDrawingLayer(state, name, ToUtf8(item->text()), &error)) QMessageBox::warning(&dialog, "Rename layer", FromUtf8(error));
    } else if (column == VpFreeze || column == VpColor) {
      Viewport* viewport = CurrentViewport(state);
      if (!viewport) { populate(); return; }
      if (column == VpFreeze) {
        PushUndoSnapshot(state, "Viewport layer freeze");
        if (item->checkState() == Qt::Checked) FreezeLayerInViewport(*viewport, name); else ThawLayerInViewport(*viewport, name);
        BumpCadGpuCache(state);
      } else {
        const auto typed = ToUtf8(item->text().trimmed());
        std::string storage;
        if (typed.empty() || CadColorIsByLayer(typed)) {
          PushUndoSnapshot(state, "Viewport layer color");
          ClearViewportLayerColor(*viewport, name);
          BumpCadGpuCache(state);
        } else if (CadColorStorageFromTyped(typed, &storage) && !CadColorIsByBlock(storage)) {
          PushUndoSnapshot(state, "Viewport layer color");
          SetViewportLayerColor(*viewport, name, storage);
          BumpCadGpuCache(state);
        } else QMessageBox::warning(&dialog, "Viewport layer color", "Enter a valid color, or leave it empty to remove the override.");
      }
    } else {
      CadLayerRow next = *found;
      bool valid = true;
      if (column == On) next.on = item->checkState() == Qt::Checked;
      if (column == Frozen) next.frozen = item->checkState() == Qt::Checked;
      if (column == Locked) next.locked = item->checkState() == Qt::Checked;
      if (column == Color) valid = CadColorStorageFromTyped(ToUtf8(item->text()), &next.color) && next.color != "ByLayer";
      if (column == Linetype) { next.linetype = ToUtf8(item->text().trimmed()); valid = !next.linetype.empty(); }
      if (column == Lineweight) {
        const QString text = item->text().trimmed();
        if (text.compare("Default", Qt::CaseInsensitive) == 0 || text == "-1") {
          next.lineweightMm = -1.f;
        } else {
          const double value = text.toDouble(&valid);
          valid = valid && std::isfinite(value) && value >= -1 && value <= 10;
          if (valid) next.lineweightMm = static_cast<float>(value);
        }
      }
      if (valid) { PushUndoSnapshot(state, "Layer property"); *found = std::move(next); BumpCadGpuCache(state); }
      else QMessageBox::warning(&dialog, "Layer property", "Enter a valid color, linetype, or lineweight (-1 for default).");
    }
    populate(); refresh();
  });

  // A click on a swatch opens a colour chooser; typing in the cell still works (double-click or F2).
  QObject::connect(table, &QTableWidget::cellClicked, &dialog, [&](int row, int column) {
    if (!IsColorColumn(column) || !table->item(row, column)) return;
    const QRect cell = table->visualRect(table->model()->index(row, column));
    const QPoint at = table->viewport()->mapFromGlobal(QCursor::pos());
    if (at.x() > cell.left() + kSwatch.width() + 14) return;
    float rgb[3] = {1.f, 1.f, 1.f};
    CadColorResolveRgb(ToUtf8(table->item(row, column)->text()), 1.f, 1.f, 1.f, rgb);
    const QColor picked = QColorDialog::getColor(QColor::fromRgbF(rgb[0], rgb[1], rgb[2]), &dialog, "Layer color");
    if (!picked.isValid()) return;
    table->item(row, column)->setText(FromUtf8(CadColorStorageFromRgbPacked(picked.rgb() & 0xFFFFFFu)));
  });

  QObject::connect(add, &QPushButton::clicked, &dialog, [&] {
    bool accepted = false;
    const auto name = QInputDialog::getText(&dialog, "New layer", "Name", QLineEdit::Normal, {}, &accepted);
    if (!accepted) return;
    std::string error;
    if (!CadAddDrawingLayer(state, ToUtf8(name), &error)) QMessageBox::warning(&dialog, "New layer", FromUtf8(error));
    populate(); refresh();
  });
  QObject::connect(erase, &QPushButton::clicked, &dialog, [&] {
    const auto name = selectedLayerName();
    if (name.empty()) return;
    std::string error;
    if (!CadDeleteDrawingLayer(state, name, &error)) QMessageBox::warning(&dialog, "Delete layer", FromUtf8(error));
    populate(); refresh();
  });
  QObject::connect(current, &QPushButton::clicked, &dialog, [&] {
    const auto name = selectedLayerName();
    if (name.empty()) return;
    state.currentLayer = name;
    populate(); refresh();
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  dialog.exec();
}
