#pragma once
#include <QMainWindow>
#include <functional>
#include <memory>
#include "CadCommands.hpp"
#include "CadDynInput.hpp"
#include "WikiContent.hpp"
#include "PdfCoordExtract.hpp"

namespace auth { struct AuthTask; }
namespace update { struct UpdateState; }
namespace telemetry { struct TelemetryTask; }

class EditorViewport;
class QTabBar;
class QLineEdit;
class QListView;
class QStandardItemModel;
class QResizeEvent;
class QEvent;
class QPlainTextEdit;
class QTreeWidget;
class QScrollArea;
class QComboBox;
class QLabel;
class QStackedWidget;
class QAction;
class QMenu;
class QPoint;
class QGridLayout;
class QWidget;
class FloatingCommandBar;
class QFrame;
class QTimer;

class EditorWindow final : public QMainWindow {
public:
  EditorWindow();
  ~EditorWindow() override;
  void Submit(const QString& command);
  void BeginInteractiveResize();
  void EndInteractiveResize();
  void StartAuth(bool previewSignIn = false);
  /// REQ-077: begins the unattended startup check. Called by the application entry
  /// point (not the constructor) so test windows never touch the network.
  void StartUpdateCheck();
  /// True while the startup check is still waiting on the network, so the entry point can hold
  /// the splash screen until the answer is known (REQ-077: the check gates the session).
  bool UpdateCheckRunning() const;
  /// REQ-080: lets this window send the usage ping once the launch gate resolves. Called by the
  /// application entry point only, so test windows never send one.
  void EnableTelemetry();
  void InitializeStartup();
  void OpenDrawing(const QString& path);
  AppCommandState& State() { return m_State; }
  bool CommitPdfCoordinate(const pdfview::CoordCandidate& candidate);
  std::size_t CommitPdfCoordinates(const std::vector<pdfview::CoordCandidate>& candidates); ///< one redraw for the lot; returns how many were created
  bool RunSmokeChecks(QString& error);
protected:
  void closeEvent(QCloseEvent* event) override;
  void changeEvent(QEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
private:
  bool CreatePdfCoordinate(const pdfview::CoordCandidate& candidate); ///< creates the geometry; no redraw
  void FinishPdfCoordinates();                                         ///< frame the view, redraw, focus
  friend class QtEditorTests;
  QAction* CommandAction(const QString& label, const QString& icon, const QString& command,
                        const QKeySequence& shortcut = {});
  void BuildRibbon();
  void BuildRibbonPages();
  void RefreshRibbon();
  void RefreshToolspace();
  void RunRibbonAction(const QString& id);
  bool ConfirmDwgExport(const QString& path);
  void EditRibbonHatch(const QString& field, const QString& value);
  void BuildTitleBar(QMenu* applicationMenu);
  void UpdateChromeIcons();
  void BuildDocks();
  void BuildDraftingInput();
  void RefreshDraftingInput(const QString& hint = {});
  void ResetDraftingInput();
  void UpdateDraftingInputFromCommandLine(const QString& text);
  void UpdateCommandLineFromDraftingInput();
  [[nodiscard]] QString DraftingInputText() const;
  [[nodiscard]] QString DraftingInputDisplayText() const;
  bool SubmitDraftingInput(bool click = false);
  bool DraftingInputEvent(QObject* watched, QEvent* event);
  void CancelDraftingInput();
  void ShowPolarSettings();
  void ShowSnapOverride(const QPoint& position);
  void ShowContextualHelp();
  void BuildPropertiesPanel();
  void RefreshProperties();
  void Refresh();
  void NewDrawing(std::uint32_t projectUid = 0);
  void CreateDrawingFromTemplate(std::uint32_t projectUid = 0, const QString& templatePath = {});
  void ShowDrawingTemplateChooser();
  void SwitchDrawing(int index);
  void CloseDrawing(int index);
  bool SaveDrawing(bool saveAs = false);
  bool ConfirmSave();
  void ShowWhatsNew();
  void RefreshRecentDrawings();
  void RefreshRecentProjects();
  void MaybeAutoOpenWhatsNew();
  void ShowPdf();
  void ShowAdjustment();
  void ShowProjectSettings(std::uint32_t projectUid);
  void ShowProjectHealth(std::uint32_t projectUid);
  void ShowProjectPack(std::uint32_t projectUid);
  void ShowProjectTurnover(std::uint32_t projectUid);
  void OpenPackedProject();
  void RefreshProjectFiles();
  void ShowAddDrawing(std::uint32_t projectUid, const QString& sourcePath = {});
  std::uint32_t OpenProject(const QString& path, bool createDrawing);
  void NewProject();
  void RefreshProjects();
  void ServiceUiRequests();
  void ShowLayers();
  void ShowUnits();
  void ShowQuickSelect();
  void ShowViewManager();
  void ShowCreatePoints();
  void ShowPoints(bool database);
  void ShowProjectPointWarning();
  void ShowPointTransfer(bool importing);
  void ShowSurveyReports();
  // Issue #893: surface and surface-style dialogs (EditorSurfaces / EditorSurfaceManager / EditorSurfaceStyles).
  void ShowCreateSurface();
  void ShowSurfaceProperties(int surfaceIndex);
  void ShowSurfaceManager();
  void ShowSurfaceStyles(const QString& focusName, bool surfacesTitle);
  void ShowLayoutManager();
  void ShowPageSetupManager(int layoutIndex = -1);
  void ShowPageSetupEditor(int targetIndex);
  void ShowViewportScale();
  void ShowViewportsManager();
  bool ActiveDrawingReadOnly() const;
  void ShowToolspaceContextMenu(const QPoint& position);
  void ShowPasteWarning();
  void ShowProjectAttachPrompt();
  void ShowPdfAttachDialog();
  void AddPdfToProjectFromDisk();
  // Issue #916: point-cloud attach / export dialogs and the background-import progress window.
  void ShowPointCloudAttach();
  void ShowPointCloudExport(int formatIndex = 0);
  void ServicePointCloudImport();
  void ShowSettings();
  void ShowSurveyPointDisplay();
  void ShowDrawingContextMenu(const QPoint& globalPosition);
  void ShowRightClickCustomization();
  void ShowSelectionList();
  void ShowBlockAuthoringPalette();
  void ShowBlockEditorPicker();
  void ShowConnectionModes();
  void ShowBlockEditorClosePrompt();
  void ShowSignInGate();
  void ServiceAuth();
  void ServiceTelemetry();
  void InitUpdates();
  void ServiceUpdate();
  void ShowUpdateDialog();
  bool UpdateBlocksLaunch() const;
  bool m_ModalOpen = false;
  AppCommandState m_State;
  std::vector<std::string> m_Log;
  std::size_t m_LogShown = 0;
  QStringList m_ToolspaceKey;  ///< What the Toolspace tree last listed; see RefreshToolspace().
  EditorViewport* m_Viewport = nullptr;
  QTabBar* m_Tabs = nullptr;
  QTabBar* m_SpaceTabs = nullptr;
  QStackedWidget* m_Pages = nullptr;
  QWidget* m_RecentTiles = nullptr;
  QWidget* m_ProjectTiles = nullptr;
  QLineEdit* m_RecentSearch = nullptr;
  QPlainTextEdit* m_Transcript = nullptr;
  QLineEdit* m_Command = nullptr;
  QListView* m_CommandSuggestions = nullptr;
  QStandardItemModel* m_CommandSuggestionModel = nullptr;
  FloatingCommandBar* m_CommandBar = nullptr;
  QFrame* m_DynamicInput = nullptr;
  QLabel* m_DynamicPrompt = nullptr;
  QLabel* m_DynamicLabels[3]{};
  QLineEdit* m_DynamicFields[3]{};
  dyninput::Group m_DynamicGroup;
  WikiBundle m_WikiBundle;
  bool m_WikiLoaded = false;
  CadDynInputPrompt m_DynamicGeometry;
  QString m_DynamicHint;
  ray3d::Vec3 m_DynamicCursor{};
  bool m_DynamicReset = true;
  QLabel* m_WindowTitle = nullptr;
  QTreeWidget* m_Toolspace = nullptr;
  QScrollArea* m_PropertiesScroll = nullptr;
  std::uint64_t m_PropertiesFingerprint = ~0ull;
  QString m_PropertiesNote;
  QLabel* m_Coordinates = nullptr;
  bool m_Refreshing = false;
  bool m_TextDialogOpen = false;
  bool m_AuthLastAttemptInteractive = false;
  bool m_AuthStarted = false;
  bool m_StartupInitialized = false;
  bool m_WhatsNewAutoOpened = false;
  bool m_RecentListView = false;
  bool m_RecentShowAll = false;
  bool m_ShowAllProjects = false;
  std::unique_ptr<auth::AuthTask> m_AuthTask;
  std::unique_ptr<update::UpdateState> m_Update;
  /// Hands the verified installer to the OS; replaceable so tests never run one.
  std::function<bool(update::UpdateState&)> m_LaunchInstaller;
  bool m_UpdateExitPending = false;
  std::unique_ptr<telemetry::TelemetryTask> m_TelemetryTask;
  /// Sends the REQ-080 ping with the signed-in email (or empty). Unset until EnableTelemetry;
  /// replaceable so tests never send one.
  std::function<void(const std::string& email)> m_SendTelemetry;
  bool m_TelemetrySent = false;
  QStringList m_CommandHistory;
  int m_HistoryIndex = 0;
};

QString QtAsset(const QString& relative);

QIcon CadCommandIcon(const QString& icon);
