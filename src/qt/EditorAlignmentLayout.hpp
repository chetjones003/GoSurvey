#pragma once

#include "alignment/AlignmentDesign.hpp"

#include <QStringList>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QTableWidget;
class QTableWidgetItem;
struct AppCommandState;

/// The Alignment Layout table (REQ-486), shown in a dock like Civil 3D's Panorama: one row per tangent, curve or spiral
/// with its stations, parameters, tangency and parameter constraints, locks, continuity, the value the design criteria
/// require and any failed check. Only the parameters an entity was defined by can be edited (REQ-486.1); the rest are
/// shown greyed. Editing a cell runs the same ALIGNMENTEDIT command the command line does, so the solver, the refusals
/// and the undo step are identical. The table refills whenever the drawing changes, so it stays open while you work.
class AlignmentLayoutPanel final : public QWidget {
 public:
  /// `changed` is called after an edit so the window can redraw the drawing.
  AlignmentLayoutPanel(AppCommandState& state, std::vector<std::string>& log, std::function<void()> changed,
                       QWidget* parent);
  /// Shows the alignment with this name (any capitalisation); empty or unknown keeps the current one.
  void ShowAlignment(const std::string& name);
  /// Refills the table when the drawing changed since it was last filled.
  void Sync();

 private:
  /// What a fill computes once for every row: continuity at each junction, the check set and its findings.
  struct RowContext {
    std::vector<alignment::ContinuityIssue> issues;
    const alignment::AlignmentCheckSet* checkSet = nullptr;
    std::vector<alignment::DesignFinding> findings;
  };
  void FillChooser();
  void Populate();
  void PopulateRow(const alignment::Alignment& a, int row, const RowContext& ctx);
  void MarkFailingRow(int row, const QStringList& found);
  void OnItemChanged(QTableWidgetItem* item);
  [[nodiscard]] const alignment::Alignment* Current() const;

  AppCommandState& state_;
  std::vector<std::string>& log_;
  std::function<void()> changed_;
  QComboBox* chooser_ = nullptr;
  QLabel* summary_ = nullptr;
  QTableWidget* table_ = nullptr;
  QLabel* status_ = nullptr;
  std::uint32_t filledRevision_ = 0;
  int filledDrawing_ = -1;
  bool filledOnce_ = false;
};
