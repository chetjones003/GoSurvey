#pragma once
#include <QMainWindow>
#include "CadCommands.hpp"

class EditorViewport;
class QTabBar;
class QLineEdit;
class QPlainTextEdit;
class QTreeWidget;
class QComboBox;
class QLabel;
class QStackedWidget;
class QAction;
class QMenu;
class FloatingCommandBar;

class EditorWindow final : public QMainWindow {
public:
  EditorWindow();
  ~EditorWindow() override;
  void Submit(const QString& command);
  void OpenDrawing(const QString& path);
  AppCommandState& State() { return m_State; }
  bool RunSmokeChecks(QString& error);
protected:
  void closeEvent(QCloseEvent* event) override;
  void showEvent(QShowEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
private:
  QAction* CommandAction(const QString& label, const QString& icon, const QString& command,
                        const QKeySequence& shortcut = {});
  void BuildRibbon();
  void BuildTitleBar(QMenu* applicationMenu);
  void UpdateChromeIcons();
  void BuildDocks();
  void Refresh();
  void NewDrawing(std::uint32_t projectUid = 0);
  void SwitchDrawing(int index);
  void CloseDrawing(int index);
  bool SaveDrawing(bool saveAs = false);
  bool ConfirmSave();
  void ShowWhatsNew();
  void ShowPdf();
  void ShowAdjustment();
  std::uint32_t OpenProject(const QString& path, bool createDrawing);
  void NewProject();
  void RefreshProjects();
  void ServiceUiRequests();
  void ShowLayers();
  void ShowUnits();
  void ShowQuickSelect();
  void ShowCreatePoints();
  void ShowPoints(bool database);
  void ShowProjectPointWarning();
  void ShowPointTransfer(bool importing);
  void ShowSurveyReports();
  void ShowPasteWarning();
  bool m_ModalOpen = false;
  AppCommandState m_State;
  std::vector<std::string> m_Log;
  std::size_t m_LogShown = 0;
  EditorViewport* m_Viewport = nullptr;
  QTabBar* m_Tabs = nullptr;
  QStackedWidget* m_Pages = nullptr;
  QPlainTextEdit* m_Transcript = nullptr;
  QLineEdit* m_Command = nullptr;
  FloatingCommandBar* m_CommandBar = nullptr;
  QLabel* m_WindowTitle = nullptr;
  QTreeWidget* m_Toolspace = nullptr;
  QTreeWidget* m_Properties = nullptr;
  QComboBox* m_Layer = nullptr;
  QLabel* m_Coordinates = nullptr;
  bool m_Refreshing = false;
  bool m_TextDialogOpen = false;
  QStringList m_CommandHistory;
  int m_HistoryIndex = 0;
};

QString QtAsset(const QString& relative);
