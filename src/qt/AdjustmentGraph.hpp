#pragma once

#include "traverse/TraverseCommandLang.hpp"
#include <QWidget>
#include <functional>

class QGraphicsScene;
class QGraphicsView;
class QTreeWidget;
class QLabel;
class QLineEdit;
class QTableWidget;
class QDoubleSpinBox;
class QComboBox;
class QListWidget;
class QCheckBox;
class QSpinBox;

/// Qt view of the adjustment command document; parsing and ordering stay in Commands.
class AdjustmentGraph final : public QWidget {
public:
  explicit AdjustmentGraph(QWidget* parent);
  ~AdjustmentGraph() override;
  void SetText(const QString& text, const QString& error = {});
  bool PersistLayout();
  std::function<void(const QString&)> textChanged;
private:
  void Rebuild();
  void SelectCommand(const QString& id);
  void SelectFrame(const QString& id);
  void RefreshProperties();
  bool Publish(const travcmd::CommandDocument& document);
  void SetCollapsed(const QString& id, bool collapsed);
  void UpdateGroupVisibility();
  void Arrange(bool reset);
  void MoveSelection();
  void UpdateWires();
  void MoveCanvasItem(const QString& id, bool frame, const QPointF& position);
  void RefreshOrdering(const travcmd::CommandDocument& document, bool updateSequence = true);
  travcmd::CommandDocument m_Document;
  QGraphicsScene* m_Scene;
  QGraphicsView* m_Canvas;
  QTreeWidget* m_Outline;
  QLabel* m_Status;
  QWidget* m_Controls;
  QLineEdit* m_CommandText;
  QTableWidget* m_Values;
  QLabel* m_EditError;
  QLabel* m_Provenance;
  QDoubleSpinBox* m_PositionX;
  QDoubleSpinBox* m_PositionY;
  QDoubleSpinBox* m_FrameWidth;
  QDoubleSpinBox* m_FrameHeight;
  QLineEdit* m_FrameLabel;
  QComboBox* m_VisualGroup;
  QLabel* m_OrderingWarnings;
  QListWidget* m_Sequence;
  QCheckBox* m_ManualOrder;
  QSpinBox* m_ManualRank;
  QString m_Selected;
  QString m_SelectedFrame;
  bool m_Current = false;
  bool m_KeepView = false;
};
