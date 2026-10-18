#include "EditorWindow.hpp"
#include "EditorViewport.hpp"
#include "EditorPointGroups.hpp"
#include "CadCommands.hpp"
#include "CadColor.hpp"
#include "NonDismissableDialog.hpp"
#include "Version.hpp"
#include "update/UpdateService.hpp"
#include "UserPrefs.hpp"
#include "geo/CoordinateSystems.hpp"
#include "util/PlotScales.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QPlainTextEdit>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTimer>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QStackedWidget>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QFrame>
#include <QStyle>
#include <algorithm>
#include <cmath>
#include <limits>
#include <climits>
#include <array>
#include <cstdio>

void EditorWindow::ShowSignInGate() {
  if (findChild<QDialog *>("signInGate"))
    return;
  auto *dialog = new NonDismissableDialog(this);
  dialog->setObjectName("signInGate");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Sign in to GoSurvey");
  dialog->setWindowModality(Qt::ApplicationModal);
  dialog->setWindowFlag(Qt::WindowCloseButtonHint, false);
  dialog->setFixedSize(700, 370);
  auto *layout = new QHBoxLayout(dialog);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto *brandPanel = new QWidget(dialog);
  brandPanel->setObjectName("signInBrandPanel");
  brandPanel->setFixedWidth(250);
  brandPanel->setStyleSheet("QWidget#signInBrandPanel { background:#243a53; }");
  auto *brandLayout = new QVBoxLayout(brandPanel);
  brandLayout->setContentsMargins(26, 28, 26, 24);
  auto *logo = new QLabel(brandPanel);
  logo->setObjectName("signInLogo");
  logo->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  logo->setPixmap(QIcon(QtAsset("icons/app.ico")).pixmap(54, 54));
  brandLayout->addWidget(logo);
  auto *brand = new QLabel("GoSurvey", brandPanel);
  brand->setObjectName("signInBrand");
  brand->setStyleSheet("color:#f4f7fb;");
  QFont brandFont = brand->font();
  brandFont.setPointSize(21);
  brandFont.setBold(true);
  brand->setFont(brandFont);
  brandLayout->addWidget(brand);
  auto *brandTitle = new QLabel("Make GoSurvey yours", brandPanel);
  brandTitle->setStyleSheet("color:#f4f7fb;");
  QFont brandTitleFont = brandTitle->font();
  brandTitleFont.setPointSize(14);
  brandTitleFont.setBold(true);
  brandTitle->setFont(brandTitleFont);
  brandTitle->setWordWrap(true);
  brandLayout->addSpacing(8);
  brandLayout->addWidget(brandTitle);
  auto *brandBody = new QLabel("Sign in to identify your account and prepare "
                               "for future cloud-sync features.",
                               brandPanel);
  brandBody->setWordWrap(true);
  brandBody->setStyleSheet("color:#d0d9e4;");
  brandLayout->addWidget(brandBody);
  brandLayout->addStretch();
  layout->addWidget(brandPanel);

  auto *choicePanel = new QWidget(dialog);
  auto *choiceLayout = new QVBoxLayout(choicePanel);
  choiceLayout->setContentsMargins(32, 30, 32, 26);
  choiceLayout->setSpacing(12);
  auto *title = new QLabel("Sign in to GoSurvey", choicePanel);
  QFont titleFont = title->font();
  titleFont.setPointSize(17);
  titleFont.setBold(true);
  title->setFont(titleFont);
  choiceLayout->addWidget(title);
  auto *body = new QLabel(
      "Choose an option to continue. GoSurvey opens the secure sign-in page in "
      "your browser. "
      "Google, Microsoft, and email/password sign-in are available there.",
      choicePanel);
  body->setWordWrap(true);
  choiceLayout->addWidget(body);
  choiceLayout->addStretch();
  auto *error = new QLabel(choicePanel);
  error->setObjectName("signInError");
  error->setWordWrap(true);
  error->setStyleSheet("color:#c54646;");
  choiceLayout->addWidget(error);
  auto *signIn = new QPushButton("Sign In", choicePanel);
  signIn->setObjectName("signInButton");
  signIn->setDefault(true);
  signIn->setMinimumHeight(40);
  signIn->setStyleSheet(
      "QPushButton { background:#438fda; color:white; font-weight:600; "
      "padding:9px; border:0; border-radius:4px; } QPushButton:hover { "
      "background:#347cc3; }");
  choiceLayout->addWidget(signIn);
  auto *decline = new QPushButton("Don't Sign In", choicePanel);
  decline->setObjectName("declineSignInButton");
  decline->setMinimumHeight(38);
  choiceLayout->addWidget(decline);
  auto *state = new QLabel("You will not be asked again if you skip. You can "
                           "sign in later from Settings → Account.",
                           choicePanel);
  state->setWordWrap(true);
  state->setStyleSheet("color:#777f89;");
  choiceLayout->addWidget(state);
  layout->addWidget(choicePanel, 1);
  connect(signIn, &QPushButton::clicked, this,
          [this] { m_State.authSignInRequested = true; });
  connect(decline, &QPushButton::clicked, this,
          [this] { m_State.authDeclineRequested = true; });
  auto *refresh = new QTimer(dialog);
  refresh->setInterval(50);
  connect(refresh, &QTimer::timeout, dialog,
          [this, dialog, signIn, decline, error] {
            error->setText(QString::fromUtf8(m_State.authError.c_str()));
            const bool busy = m_State.authBusy;
            signIn->setEnabled(!busy);
            decline->setEnabled(!busy);
            signIn->setText(m_State.authInteractiveBusy ? "Waiting for browser…"
                                                        : "Sign In");
            if (m_State.authGateResolved)
              dialog->accept();
          });
  refresh->start();
  dialog->show();
}

void EditorWindow::ShowSettings() {
  if (findChild<QDialog *>("applicationSettings")) {
    auto *existing = findChild<QDialog *>("applicationSettings");
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto *dialog = new QDialog(this);
  dialog->setObjectName("applicationSettings");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Settings");
  dialog->resize(560, 500);
  auto *layout = new QVBoxLayout(dialog);
  layout->setContentsMargins(20, 18, 20, 16);
  layout->setSpacing(12);
  auto *heading = new QLabel("Account", dialog);
  heading->setObjectName("accountSettingsHeading");
  QFont headingFont = heading->font();
  headingFont.setPointSize(16);
  headingFont.setBold(true);
  heading->setFont(headingFont);
  layout->addWidget(heading);
  auto *description = new QLabel(dialog);
  description->setWordWrap(true);
  description->setText(
      "Sign in with Google, Microsoft, or an email and password to identify "
      "this account. Nothing in GoSurvey is currently gated on signing in.");
  layout->addWidget(description);

  auto *identityCard = new QFrame(dialog);
  identityCard->setObjectName("accountIdentityCard");
  identityCard->setFrameShape(QFrame::StyledPanel);
  auto *identityLayout = new QHBoxLayout(identityCard);
  identityLayout->setContentsMargins(14, 12, 14, 12);
  auto *logo = new QLabel(identityCard);
  logo->setObjectName("accountSettingsLogo");
  logo->setPixmap(QIcon(QtAsset("icons/app.ico")).pixmap(40, 40));
  identityLayout->addWidget(logo);
  auto *identityText = new QVBoxLayout();
  auto *brand = new QLabel("GoSurvey", identityCard);
  QFont accountBrandFont = brand->font();
  accountBrandFont.setBold(true);
  brand->setFont(accountBrandFont);
  identityText->addWidget(brand);
  auto *status = new QLabel(identityCard);
  status->setObjectName("accountSignInStatus");
  identityText->addWidget(status);
  identityLayout->addLayout(identityText, 1);
  auto *action = new QPushButton(identityCard);
  action->setObjectName("accountSignInAction");
  action->setMinimumWidth(100);
  identityLayout->addWidget(action, 0, Qt::AlignVCenter);
  layout->addWidget(identityCard);

  auto *error = new QLabel(dialog);
  error->setObjectName("accountSignInError");
  error->setWordWrap(true);
  error->setStyleSheet("color:#c54646;");
  layout->addWidget(error);

  // REQ-080 / REQ-425 §6: what leaves this machine, in plain words. Text, not a checkbox: there is
  // no opt-out by decision (ADR-032), and a toggle that did nothing — or wording that implied
  // consent nobody is asked for — would be worse than saying so plainly. The email sentence
  // follows the live sign-in state, so the notice is never a stale promise.
  auto *usageBox = new QFrame(dialog);
  usageBox->setObjectName("usageDataSection");
  auto *usageLayout = new QVBoxLayout(usageBox);
  usageLayout->setSpacing(8);
  auto *usageHeading = new QLabel("What GoSurvey sends", usageBox);
  usageHeading->setObjectName("usageDataHeading");
  QFont usageHeadingFont = headingFont;
  usageHeadingFont.setPointSize(12);
  usageHeading->setFont(usageHeadingFont);
  auto *usageSummary = new QLabel(
      "GoSurvey reports usage so development can be aimed at what people actually run. Every "
      "report includes a random ID that identifies this installation, the version, whether you "
      "are on stable or beta, and that you are on Windows — once when installed, and each time "
      "you start GoSurvey after that.",
      usageBox);
  usageSummary->setObjectName("usageDataSummary");
  usageSummary->setWordWrap(true);
  auto *usageEmail = new QLabel(usageBox);
  usageEmail->setObjectName("usageDataEmail");
  usageEmail->setWordWrap(true);
  auto *usageCrash = new QLabel(
      "If GoSurvey crashes or cannot start, it also sends the error report it shows you. That "
      "report can include file locations on this computer, such as your user folder.",
      usageBox);
  usageCrash->setObjectName("usageDataCrashReports");
  usageCrash->setWordWrap(true);
  auto syncUsage = [this, usageEmail] {
    usageEmail->setText(
        m_State.authSignedIn
            ? "Because you are signed in (see Account above), your email is included too. Sign "
              "out and usage reports go back to fully anonymous."
            : "Usage reports never send your name, company, computer name, file names, "
              "drawings, survey data, or location. Your email is included only when you are "
              "signed in — you are currently signed out, so they stay fully anonymous.");
  };
  syncUsage();
  // Framed like the account card, so it reads as a notice rather than a setting.
  usageBox->setFrameShape(QFrame::StyledPanel);
  usageLayout->setContentsMargins(14, 12, 14, 12);
  auto *titleRow = new QHBoxLayout();
  auto *mark = new QLabel(usageBox);
  mark->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(22, 22));
  titleRow->addWidget(mark);
  titleRow->addWidget(usageHeading, 1);
  usageLayout->addLayout(titleRow);
  usageLayout->addWidget(usageSummary);
  usageLayout->addWidget(usageEmail);
  usageLayout->addWidget(usageCrash);

  // REQ-077: the update check is switchable, and switching it off means no network request is
  // ever made — not a suppressed prompt. The version is shown here too, so a user reporting a
  // problem has somewhere to read it off.
  // REQ-084: the right-click preferences live in their own dialog, reached from here as in 0.7.4.
  auto *rightClickRow = new QHBoxLayout();
  auto *rightClickButton = new QPushButton("Right-click Customization...", dialog);
  rightClickButton->setObjectName("settingsRightClickCustomization");
  connect(rightClickButton, &QPushButton::clicked, this, [this] { ShowRightClickCustomization(); });
  rightClickRow->addWidget(rightClickButton);
  rightClickRow->addStretch();
  layout->addLayout(rightClickRow);

  layout->addStretch();
  auto *divider = new QFrame(dialog);
  divider->setFrameShape(QFrame::HLine);
  layout->addWidget(divider);
  auto *updatesHeading = new QLabel("Updates", dialog);
  updatesHeading->setObjectName("updatesSettingsHeading");
  updatesHeading->setFont(headingFont);
  layout->addWidget(updatesHeading);
  auto *versionLabel = new QLabel(QString("Version ") + GOSURVEY_VERSION_FULL, dialog);
  versionLabel->setObjectName("updateVersionLabel");
  versionLabel->setStyleSheet("color:#777f89;");
  layout->addWidget(versionLabel);
  auto *checkEnabled = new QCheckBox("Check for updates on startup", dialog);
  checkEnabled->setObjectName("updateCheckEnabled");
  layout->addWidget(checkEnabled);
  auto *useBeta = new QCheckBox("Include beta releases", dialog);
  useBeta->setObjectName("updateUseBeta");
  layout->addWidget(useBeta);
  auto *skippedRow = new QHBoxLayout();
  auto *skippedLabel = new QLabel(dialog);
  skippedLabel->setObjectName("updateSkippedLabel");
  skippedRow->addWidget(skippedLabel);
  // Without this the only way out of a skip is editing the prefs file by hand.
  auto *clearSkip = new QPushButton("Clear", dialog);
  clearSkip->setObjectName("updateClearSkip");
  skippedRow->addWidget(clearSkip);
  skippedRow->addStretch();
  layout->addLayout(skippedRow);
  auto *updatesError = new QLabel(dialog);
  updatesError->setObjectName("updateSettingsError");
  updatesError->setWordWrap(true);
  updatesError->setStyleSheet("color:#c54646;");
  layout->addWidget(updatesError);
  auto syncUpdates = [this, checkEnabled, useBeta, skippedLabel, clearSkip] {
    const QSignalBlocker blockCheck(checkEnabled);
    const QSignalBlocker blockBeta(useBeta);
    checkEnabled->setChecked(m_State.updatePrefs.enabled);
    useBeta->setChecked(m_State.updatePrefs.useBetaChannel);
    useBeta->setEnabled(m_State.updatePrefs.enabled);
    const bool skipping = !m_State.updatePrefs.skippedVersion.empty();
    skippedLabel->setText("Skipping " + QString::fromUtf8(m_State.updatePrefs.skippedVersion.c_str()));
    skippedLabel->setVisible(skipping);
    clearSkip->setVisible(skipping);
  };
  // Saved immediately, like the Account actions. A failed write puts the control back to what is
  // actually stored, so the dialog never shows a setting that will not survive a restart.
  auto applyUpdatePrefs = [this, updatesError, syncUpdates](const update::UpdatePrefs &next) {
    const update::UpdatePrefs previous = m_State.updatePrefs;
    m_State.updatePrefs = next;
    if (SaveUserStartupPrefs(m_State)) {
      updatesError->clear();
      m_Update->prefs.skippedVersion = next.skippedVersion;
    } else {
      m_State.updatePrefs = previous;
      updatesError->setText("Your update settings could not be saved.");
    }
    syncUpdates();
  };
  connect(checkEnabled, &QCheckBox::toggled, dialog, [this, applyUpdatePrefs](bool on) {
    auto next = m_State.updatePrefs;
    next.enabled = on;
    applyUpdatePrefs(next);
  });
  connect(useBeta, &QCheckBox::toggled, dialog, [this, applyUpdatePrefs](bool on) {
    auto next = m_State.updatePrefs;
    next.useBetaChannel = on;
    applyUpdatePrefs(next);
  });
  connect(clearSkip, &QPushButton::clicked, dialog, [this, applyUpdatePrefs] {
    auto next = m_State.updatePrefs;
    next.skippedVersion.clear();
    applyUpdatePrefs(next);
  });
  syncUpdates();
  // A skip chosen in the update dialog while Settings is open must show up here too.
  auto *updatesSync = new QTimer(dialog);
  updatesSync->setInterval(250);
  connect(updatesSync, &QTimer::timeout, dialog, syncUpdates);
  updatesSync->start();

  // Next to Updates: the other thing here that talks to the network.
  auto *usageDivider = new QFrame(dialog);
  usageDivider->setFrameShape(QFrame::HLine);
  layout->addWidget(usageDivider);
  layout->addWidget(usageBox);

  layout->addStretch();
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  layout->addWidget(buttons);
  connect(action, &QPushButton::clicked, this, [this] {
    if (m_State.authSignedIn)
      m_State.authSignOutRequested = true;
    else
      m_State.authSignInRequested = true;
  });
  auto *refresh = new QTimer(dialog);
  refresh->setInterval(100);
  connect(refresh, &QTimer::timeout, dialog, [this, status, error, action, syncUsage] {
    syncUsage();
    if (m_State.authSignedIn) {
      status->setText("Signed in as " +
                      QString::fromUtf8(m_State.authEmail.c_str()));
      action->setText("Sign Out");
    } else {
      status->setText(m_State.authOptedOut
                          ? "You chose not to sign in at startup."
                          : "Not signed in.");
      action->setText(m_State.authInteractiveBusy ? "Waiting for browser…"
                                                  : "Sign In");
    }
    action->setEnabled(!m_State.authBusy);
    error->setText(QString::fromUtf8(m_State.authError.c_str()));
  });
  refresh->start();
  // The wrapped notices need more height than the fixed starting size; open tall enough that
  // no paragraph is squeezed (about 670 px at this width, which still fits a 720 px screen).
  dialog->resize(dialog->width(), std::max(dialog->height(), dialog->heightForWidth(dialog->width())));
  dialog->show();
}

void EditorWindow::ServiceUiRequests() {
  if (m_State.showCreatePointsWindow) ShowCreatePoints();
  if (m_ModalOpen || m_TextDialogOpen) return;
  if (m_State.blockEditCloseAsked) {
    ShowBlockEditorClosePrompt();
    return;
  }
  if (m_State.blockAuthoringPaletteOpen) ShowBlockAuthoringPalette();
  else if (auto* palette = findChild<QDialog*>("blockAuthoringPalette")) palette->close();
  if (m_State.showConnectionModesWindow) ShowConnectionModes();
  else if (auto* modes = findChild<QDialog*>("connectionModes")) modes->close();
  if (m_State.addDrawingToProjectUid != 0) {
    const auto uid = m_State.addDrawingToProjectUid;
    m_State.addDrawingToProjectUid = 0;
    ShowAddDrawing(uid);
  } else if (m_State.pointEditPrompt.active) {
    ShowProjectPointWarning();
  } else if (m_State.projectAttachPrompt.kind != AppCommandState::ProjectAttachPrompt::Kind::None) {
    ShowProjectAttachPrompt();
  } else if (m_State.pastePrompt.active) {
    ShowPasteWarning();
  } else if (m_State.showLayerManagerWindow) {
    m_State.showLayerManagerWindow = false;
    ShowLayers();
  } else if (m_State.showPointGroupManagerWindow) {
    m_State.showPointGroupManagerWindow = false;
    m_ModalOpen = true;
    ShowPointGroupManager(this, m_State, m_Log);
    m_ModalOpen = false;
    Refresh();
  } else if (m_State.showUnitsWindow || m_State.showDrawingSettingsWindow) {
    m_State.showUnitsWindow = false;
    m_State.showDrawingSettingsWindow = false;
    m_State.drawingSettingsShowUnitsAndZone = false;
    ShowUnits();
  } else if (m_State.showQuickSelectWindow) {
    m_State.showQuickSelectWindow = false;
    ShowQuickSelect();
  } else if (m_State.showSettingsWindow) {
    m_State.showSettingsWindow = false;  // OPTIONS / the shortcut menu's Options...
    ShowSettings();
  } else if (m_State.showSelectionCyclingWindow) {
    m_State.showSelectionCyclingWindow = false;
    ShowSelectionList();
  } else if (m_State.showViewManagerWindow) {
    m_State.showViewManagerWindow = false;
    ShowViewManager();
  } else if (m_State.showViewPointsWindow || m_State.showSurveyPointGridWindow) {
    const bool database = m_State.showSurveyPointGridWindow;
    m_State.showViewPointsWindow = m_State.showSurveyPointGridWindow = false;
    ShowPoints(database);
  } else if (m_State.showImportPointsWindow || m_State.showExportPointsWindow) {
    const bool importing = m_State.showImportPointsWindow;
    m_State.showImportPointsWindow = m_State.showExportPointsWindow = false;
    ShowPointTransfer(importing);
  } else if (m_State.showTraverseEditorWindow || m_State.showTraverseClosureWindow) {
    m_State.showTraverseEditorWindow = m_State.showTraverseClosureWindow = false;
    ShowAdjustment();
  } else if (m_State.showCreateSurfaceWindow) {
    m_State.showCreateSurfaceWindow = false;
    ShowCreateSurface();
  } else if (m_State.showSurfacePropertiesWindow) {
    m_State.showSurfacePropertiesWindow = false;
    ShowSurfaceProperties(m_State.surfacePropertiesIndex);
  } else if (m_State.showSurfaceManagerWindow) {
    m_State.showSurfaceManagerWindow = false;
    ShowSurfaceManager();
  } else if (m_State.showSurfaceStyleWindow) {
    const QString focus = QString::fromUtf8(m_State.surfaceStyleEditorFocusName.c_str());
    const bool surfacesTitle = m_State.surfaceStyleUseSurfacesTitle;
    m_State.showSurfaceStyleWindow = false;
    m_State.surfaceStyleEditorFocusName.clear();
    m_State.surfaceStyleUseSurfacesTitle = false;
    ShowSurfaceStyles(focus, surfacesTitle);
  } else if (m_State.showPageSetupManager) {
    const int layoutIndex = m_State.pageSetupLayoutIdx;
    m_State.showPageSetupManager = false;
    ShowPageSetupManager(layoutIndex);
  } else if (m_State.surveyReportSelectLatestPending) {
    ShowSurveyReports();
  }
}

void EditorWindow::ShowViewManager() {
  m_ModalOpen = true;
  QDialog dialog(this);
  dialog.setObjectName("viewManagerDialog");
  dialog.setWindowTitle("Manage Views & UCS");
  dialog.resize(650, 510);
  auto* layout = new QVBoxLayout(&dialog);
  auto* current = new QLabel(&dialog);
  current->setObjectName("viewManagerCurrentView");
  current->setStyleSheet("background:#243a53;color:#f4f7fb;padding:10px 12px;font-weight:600;border-radius:5px;");
  layout->addWidget(current);

  auto* tabs = new QTabWidget(&dialog);
  tabs->setObjectName("viewManagerTabs");
  auto* views = new QListWidget(tabs);
  views->setObjectName("viewManagerSavedViews");
  views->setSelectionMode(QAbstractItemView::SingleSelection);
  views->setSpacing(6);
  views->setStyleSheet("QListWidget{border:0;background:transparent} QListWidget::item{background:#f3f6f8;border:1px solid #d7dee5;border-radius:5px;padding:10px;color:#202a33} QListWidget::item:selected{background:#e4eff7;border:1px solid #3879a2;color:#172d3b}");
  auto* ucsList = new QListWidget(tabs);
  ucsList->setObjectName("viewManagerSavedUcs");
  ucsList->setSelectionMode(QAbstractItemView::SingleSelection);
  ucsList->setSpacing(6);
  ucsList->setStyleSheet(views->styleSheet());
  tabs->addTab(views, "Views");
  tabs->addTab(ucsList, "Coordinate Systems");
  layout->addWidget(tabs, 1);

  auto* actionRow = new QHBoxLayout();
  auto* restore = new QPushButton("Restore", &dialog);
  restore->setObjectName("viewManagerRestore");
  auto* update = new QPushButton("Update", &dialog);
  update->setObjectName("viewManagerUpdate");
  auto* remove = new QPushButton("Delete", &dialog);
  remove->setObjectName("viewManagerDelete");
  auto* save = new QPushButton("New View…", &dialog);
  save->setObjectName("viewManagerNewView");
  actionRow->addWidget(restore);
  actionRow->addWidget(update);
  actionRow->addWidget(remove);
  actionRow->addStretch();
  actionRow->addWidget(save);
  layout->addLayout(actionRow);
  auto* close = new QPushButton("Close", &dialog);
  close->setObjectName("viewManagerClose");
  close->setDefault(true);
  auto* closeRow = new QHBoxLayout();
  closeRow->addStretch();
  closeRow->addWidget(close);
  layout->addLayout(closeRow);

  auto refresh = [&] {
    const auto* active = CurrentNamedView(m_State);
    current->setText(active ? QString("Current view · %1").arg(QString::fromUtf8(active->name.c_str()))
                            : "Current view · Unsaved View");
    const QString selectedView = views->currentItem() ? views->currentItem()->data(Qt::UserRole).toString() : QString();
    const QSignalBlocker blockViews(views);
    const QSignalBlocker blockUcs(ucsList);
    views->clear();
    for (const auto& view : m_State.namedViews) {
      const QString name = QString::fromUtf8(view.name.c_str());
      const bool isCurrent = active && active->name == view.name;
      const QString projection = view.projection == Camera::Projection::Perspective ? "Perspective" : "Orthographic";
      auto* item = new QListWidgetItem(QString("%1%2\n%3 · %4 · %5")
                                           .arg(name, isCurrent ? "   CURRENT" : "", projection,
                                                QString("FOV %1°").arg(view.fovDeg, 0, 'f', 0),
                                                ucs::IsWorld(view.ucs) ? "WCS" : "UCS"),
                                       views);
      item->setData(Qt::UserRole, name);
      item->setSizeHint(QSize(0, 58));
      item->setToolTip(QString("Azimuth %1° · Elevation %2° · %3")
                           .arg(view.azimuthDeg, 0, 'f', 1).arg(view.elevationDeg, 0, 'f', 1)
                           .arg(DescribeUcs(view.ucs).c_str()));
      if (name == selectedView || (selectedView.isEmpty() && isCurrent)) views->setCurrentItem(item);
    }
    if (!views->currentItem() && views->count()) views->setCurrentRow(0);

    const QString selectedUcs = ucsList->currentItem() ? ucsList->currentItem()->data(Qt::UserRole).toString() : QString();
    ucsList->clear();
    for (const auto& named : m_State.ucsNamed) {
      const QString name = QString::fromUtf8(named.name.c_str());
      const bool isCurrent = ucs::FramesMatch(named.frame, m_State.activeUcs);
      auto* item = new QListWidgetItem(QString("%1%2\n%3")
                                           .arg(name, isCurrent ? "   CURRENT" : "", DescribeUcs(named.frame).c_str()),
                                       ucsList);
      item->setData(Qt::UserRole, name);
      item->setSizeHint(QSize(0, 58));
      if (name == selectedUcs || (selectedUcs.isEmpty() && isCurrent)) ucsList->setCurrentItem(item);
    }
    if (!ucsList->currentItem() && ucsList->count()) ucsList->setCurrentRow(0);
    const bool onViews = tabs->currentIndex() == 0;
    const bool hasSelection = onViews ? views->currentItem() != nullptr : ucsList->currentItem() != nullptr;
    const bool currentUcsSelected = !onViews && ucsList->currentRow() >= 0 &&
        ucs::FramesMatch(m_State.activeUcs, m_State.ucsNamed[static_cast<size_t>(ucsList->currentRow())].frame);
    restore->setEnabled(hasSelection && (onViews || !currentUcsSelected));
    update->setVisible(onViews);
    save->setVisible(onViews);
    update->setEnabled(onViews && hasSelection);
    remove->setEnabled(hasSelection);
    remove->setVisible(!onViews || hasSelection);
    if (views->count() == 0 && onViews) current->setText("No saved views · Choose New View to save this view");
    if (ucsList->count() == 0 && !onViews) current->setText("No saved coordinate systems · Save one with UCS N <name>");
  };

  connect(views, &QListWidget::currentRowChanged, &dialog, [&] { refresh(); });
  connect(ucsList, &QListWidget::currentRowChanged, &dialog, [&] { refresh(); });
  connect(tabs, &QTabWidget::currentChanged, &dialog, [&] { refresh(); });
  connect(restore, &QPushButton::clicked, &dialog, [&] {
    if (tabs->currentIndex() == 0 && views->currentRow() >= 0) {
      const auto selected = m_State.namedViews[static_cast<size_t>(views->currentRow())];
      RestoreNamedView(m_State, selected, m_Log);
    } else if (tabs->currentIndex() == 1 && ucsList->currentRow() >= 0) {
      RestoreNamedUcs(m_State, m_State.ucsNamed[static_cast<size_t>(ucsList->currentRow())].name, m_Log);
    }
    Refresh();
    refresh();
  });
  connect(update, &QPushButton::clicked, &dialog, [&] {
    if (views->currentRow() < 0) return;
    const auto name = m_State.namedViews[static_cast<size_t>(views->currentRow())].name;
    ProcessViewCommandLine(m_State, "S " + name, m_Log);
    Refresh();
    refresh();
  });
  connect(remove, &QPushButton::clicked, &dialog, [&] {
    if (tabs->currentIndex() == 0 && views->currentRow() >= 0) {
      const auto name = m_State.namedViews[static_cast<size_t>(views->currentRow())].name;
      ProcessViewCommandLine(m_State, "D " + name, m_Log);
    } else if (tabs->currentIndex() == 1 && ucsList->currentRow() >= 0) {
      DeleteNamedUcs(m_State, m_State.ucsNamed[static_cast<size_t>(ucsList->currentRow())].name, m_Log);
    }
    Refresh();
    refresh();
  });
  connect(save, &QPushButton::clicked, &dialog, [&] {
    bool accepted = false;
    const QString name = QInputDialog::getText(&dialog, "New View", "Save the current camera and coordinate frame as:",
                                                QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    ProcessViewCommandLine(m_State, "S " + name.toUtf8().toStdString(), m_Log);
    Refresh();
    refresh();
  });
  connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
  connect(&dialog, &QDialog::finished, this, [this] { m_State.showViewManagerWindow = false; });
  refresh();
  m_State.showViewManagerWindow = false;
  dialog.exec();
  m_ModalOpen = false;
  Refresh();
}

void EditorWindow::ShowCreatePoints() {
  if (findChild<QDialog*>("createPoints")) return;
  auto* dialog = new QDialog(this);
  dialog->setObjectName("createPoints");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Create Survey Points");
  auto* layout = new QFormLayout(dialog);
  auto* hint = new QLabel("Click in the drawing to place points. Click an existing marker to select it.\nEscape ends point creation.", dialog);
  hint->setWordWrap(true); layout->addRow(hint);
  auto* number = new QSpinBox(dialog); number->setObjectName("nextPointNumber");
  number->setRange(1, std::numeric_limits<int>::max() - 1);
  auto* layer = new QLineEdit(dialog); layer->setObjectName("pointLayer");
  auto* description = new QPlainTextEdit(dialog); description->setObjectName("pointDescription");
  description->setMaximumHeight(85);
  auto* elevation = new QDoubleSpinBox(dialog); elevation->setObjectName("pointElevation");
  elevation->setRange(-1.e9, 1.e9); elevation->setDecimals(6);
  auto* duplicates = new QComboBox(dialog); duplicates->setObjectName("pointDuplicatePolicy");
  duplicates->addItems({"Notify (skip)", "Renumber (next free)", "Merge (update coordinates)", "Overwrite"});
  auto* sequential = new QCheckBox("Sequential numbering", dialog);
  auto* step = new QSpinBox(dialog); step->setRange(1, 1000000);
  layout->addRow("Next point number", number); layout->addRow("Layer", layer);
  layout->addRow("Description", description); layout->addRow("Elevation", elevation);
  layout->addRow("If number exists", duplicates); layout->addRow(sequential); layout->addRow("Number increment", step);
  auto* files = new QHBoxLayout;
  auto* save = new QPushButton("Save points JSON…", dialog);
  auto* load = new QPushButton("Load points JSON…", dialog);
  files->addWidget(save); files->addWidget(load); layout->addRow(files);
  connect(number, &QSpinBox::valueChanged, dialog, [this](int n) { m_State.createPointsNextId = n; });
  connect(layer, &QLineEdit::textEdited, dialog, [this](const QString& s) { m_State.createPointsOpts.layer = s.trimmed().toUtf8().toStdString(); });
  connect(description, &QPlainTextEdit::textChanged, dialog, [this, description] { m_State.createPointsOpts.defaultDescription = description->toPlainText().toUtf8().toStdString(); });
  connect(elevation, &QDoubleSpinBox::valueChanged, dialog, [this](double z) { m_State.createPointsOpts.defaultElevation = static_cast<float>(z); });
  connect(duplicates, &QComboBox::currentIndexChanged, dialog, [this](int index) { m_State.createPointsOpts.duplicatePolicy = SurveyDuplicatePolicy(index); });
  connect(sequential, &QCheckBox::toggled, dialog, [this](bool enabled) { m_State.createPointsOpts.sequentialNumbering = enabled; });
  connect(step, &QSpinBox::valueChanged, dialog, [this](int n) { m_State.createPointsOpts.pointNumberOffset = n; });
  connect(save, &QPushButton::clicked, dialog, [this, dialog] {
    const auto path = QFileDialog::getSaveFileName(dialog, "Save survey points", {}, "Survey points (*.json)");
    if (!path.isEmpty()) { SaveSurveyPointsToJsonFile(m_State, path.toUtf8().constData(), m_Log); Refresh(); }
  });
  connect(load, &QPushButton::clicked, dialog, [this, dialog] {
    if (m_State.activeDrawingIdx <= 0 || m_State.activeDrawingIdx >= static_cast<int>(m_State.drawingTabs.size())) return;
    const auto uid = m_State.drawingTabs[m_State.activeDrawingIdx].projectUid;
    if (std::any_of(m_State.openProjects.begin(), m_State.openProjects.end(), [uid](const auto& p) { return p.uid == uid && p.readOnly; })) {
      QMessageBox::warning(dialog, "Load survey points", "This project is read-only."); return;
    }
    const auto path = QFileDialog::getOpenFileName(dialog, "Load survey points", {}, "Survey points (*.json)");
    if (!path.isEmpty()) { LoadSurveyPointsFromJsonFile(m_State, path.toUtf8().constData(), m_Log); Refresh(); }
  });
  connect(dialog, &QDialog::finished, this, [this] { m_State.showCreatePointsWindow = false; Refresh(); });
  auto* timer = new QTimer(dialog);
  auto sync = [=] {
    if (!m_State.showCreatePointsWindow || m_State.activeDrawingIdx <= 0) { dialog->close(); return; }
    const auto& opts = m_State.createPointsOpts;
    const QSignalBlocker a(number), b(layer), c(description), d(elevation), e(duplicates), f(sequential), g(step);
    if (!number->hasFocus()) number->setValue(m_State.createPointsNextId);
    const auto& objectLayer = m_State.drawingSettings.ObjectLayer(ObjectLayerKind::SurveyPoint);
    layer->setEnabled(!objectLayer.locked);
    if (!layer->hasFocus()) layer->setText(QString::fromUtf8((objectLayer.locked || opts.layer.empty() ? ResolveObjectLayer(m_State.drawingSettings, ObjectLayerKind::SurveyPoint, {}) : opts.layer).c_str()));
    if (!description->hasFocus()) description->setPlainText(QString::fromUtf8(opts.defaultDescription.c_str()));
    if (!elevation->hasFocus()) elevation->setValue(opts.defaultElevation);
    duplicates->setCurrentIndex(int(opts.duplicatePolicy));
    sequential->setChecked(opts.sequentialNumbering);
    if (!step->hasFocus()) step->setValue(opts.pointNumberOffset);
  };
  connect(timer, &QTimer::timeout, dialog, sync);
  sync(); timer->start(100); dialog->show();
}

void EditorWindow::ShowQuickSelect() {
  using S = AppCommandState;
  m_ModalOpen = true;
  QDialog dialog(this);
  dialog.setObjectName("quickSelect");
  dialog.setWindowTitle("Quick Select");
  auto* layout = new QFormLayout(&dialog);
  auto combo = [&](const char* name, const QStringList& labels, int index) {
    auto* widget = new QComboBox(&dialog);
    widget->setObjectName(name); widget->addItems(labels); widget->setCurrentIndex(index);
    return widget;
  };
  auto* scope = combo("selectionScope", {"Entire drawing", "Current selection"}, int(m_State.qsApplyTo));
  auto* type = combo("selectionType", {"All", "Line", "Circle", "Arc", "Ellipse", "Polyline", "Text", "MText", "Aligned dimension", "Linear dimension", "Angular dimension", "Survey point"}, int(m_State.qsObjectType));
  auto* property = new QComboBox(&dialog); property->setObjectName("selectionProperty");
  auto* operation = new QComboBox(&dialog); operation->setObjectName("selectionOperator");
  auto* value = new QComboBox(&dialog); value->setObjectName("selectionValue"); value->setEditable(true);
  auto* mode = combo("selectionMode", {"Include matching objects", "Exclude matching objects"}, int(m_State.qsIncludeMode));
  auto* append = new QCheckBox("Append to current selection", &dialog);
  append->setChecked(m_State.qsAppendToExisting);
  layout->addRow("Apply to", scope); layout->addRow("Object type", type);
  layout->addRow("Property", property); layout->addRow("Operator", operation);
  layout->addRow("Value", value); layout->addRow("Selection", mode); layout->addRow(append);
  const QStringList names{"Layer", "Color", "Length", "Radius", "Closed", "Content", "Point number", "Elevation", "Easting", "Northing", "Description"};
  auto numeric = [&] { const int p = property->currentData().toInt(); return p == 2 || p == 3 || (p >= 6 && p <= 9); };
  auto updateOperators = [&] {
    const int previous = operation->currentData().toInt();
    operation->clear();
    operation->addItem("Equals", 0); operation->addItem("Not equal", 1);
    if (numeric()) { operation->addItem("Less than", 2); operation->addItem("Greater than", 3); }
    operation->addItem("Select all", 4);
    operation->setCurrentIndex(std::max(0, operation->findData(previous)));
    const QString previousValue = value->currentText();
    value->clear();
    if (property->currentData().toInt() == 0) {
      SyncDrawingLayerTableWithGeometry(m_State);
      for (const auto& layer : m_State.drawingLayerTable) value->addItem(QString::fromUtf8(layer.name.c_str()));
    } else if (property->currentData().toInt() == 1) value->addItems({"ByLayer", "ByBlock", "Red", "Yellow", "Green", "Cyan", "Blue", "Magenta", "White"});
    else if (property->currentData().toInt() == 4) value->addItems({"Yes", "No"});
    value->setEditText(previousValue);
  };
  auto updateProperties = [&] {
    const int previous = property->currentData().toInt();
    const QSignalBlocker blocker(property);
    property->clear();
    std::vector<int> allowed{0, 1};
    switch (type->currentIndex()) {
    case 1: allowed.push_back(2); break;
    case 2: case 3: allowed.push_back(3); break;
    case 5: allowed.insert(allowed.end(), {2, 4}); break;
    case 6: case 7: allowed.push_back(5); break;
    case 11: allowed = {0, 6, 7, 8, 9, 10}; break;
    default: break;
    }
    for (int p : allowed) property->addItem(names[p], p);
    property->setCurrentIndex(std::max(0, property->findData(previous)));
    updateOperators();
  };
  connect(type, &QComboBox::currentIndexChanged, &dialog, updateProperties);
  connect(property, &QComboBox::currentIndexChanged, &dialog, updateOperators);
  connect(operation, &QComboBox::currentIndexChanged, &dialog, [&] { value->setEnabled(operation->currentData().toInt() != 4); });
  updateProperties();
  property->setCurrentIndex(std::max(0, property->findData(int(m_State.qsProperty))));
  operation->setCurrentIndex(std::max(0, operation->findData(int(m_State.qsOperator))));
  value->setEditText(QString::fromUtf8(m_State.qsValueBuf));
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    std::string text = value->currentText().toUtf8().toStdString();
    if (operation->currentData().toInt() != 4) {
      bool valid = true;
      if (numeric()) { const double number = value->currentText().toDouble(&valid); valid = valid && std::isfinite(number) && std::abs(number) <= std::numeric_limits<float>::max(); }
      if (property->currentData().toInt() == 1) valid = CadColorStorageFromTyped(text, &text);
      if (text.size() >= sizeof(m_State.qsValueBuf)) valid = false;
      if (!valid) { QMessageBox::warning(&dialog, "Quick Select", "Enter a valid value for the selected property."); return; }
    }
    m_State.qsApplyTo = S::QsApplyTo(scope->currentIndex());
    m_State.qsObjectType = S::QsObjectType(type->currentIndex());
    m_State.qsProperty = S::QsProperty(property->currentData().toInt());
    m_State.qsOperator = S::QsOperator(operation->currentData().toInt());
    m_State.qsIncludeMode = S::QsInclude(mode->currentIndex());
    m_State.qsAppendToExisting = append->isChecked();
    std::snprintf(m_State.qsValueBuf, sizeof(m_State.qsValueBuf), "%s", text.c_str());
    ExecuteQuickSelect(m_State, m_Log);
    dialog.accept();
  });
  dialog.exec();
  m_ModalOpen = false;
  Refresh();
}

void EditorWindow::ShowLayers() {
  m_ModalOpen = true;
  SyncDrawingLayerTableWithGeometry(m_State);
  QDialog dialog(this);
  dialog.setObjectName("layerManager");
  dialog.setWindowTitle("Layer Properties Manager");
  dialog.resize(950, 550);
  auto* layout = new QVBoxLayout(&dialog);
  auto* toolbar = new QHBoxLayout;
  auto* add = new QPushButton(QIcon(QtAsset("icons/New_Layer.png")), "New layer", &dialog);
  auto* erase = new QPushButton("Delete layer", &dialog);
  auto* current = new QPushButton("Set current", &dialog);
  toolbar->addWidget(add); toolbar->addWidget(erase); toolbar->addWidget(current); toolbar->addStretch();
  layout->addLayout(toolbar);
  auto* table = new QTableWidget(&dialog);
  table->setObjectName("layerTable");
  table->setColumnCount(7);
  table->setHorizontalHeaderLabels({"Name", "On", "Frozen", "Locked", "Color", "Linetype", "Lineweight (mm)"});
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  layout->addWidget(table);
  auto populate = [&] {
    const QSignalBlocker blocker(table);
    table->setRowCount(static_cast<int>(m_State.drawingLayerTable.size()));
    for (int row = 0; row < table->rowCount(); ++row) {
      const auto& layer = m_State.drawingLayerTable[row];
      auto* name = new QTableWidgetItem(QString::fromUtf8(layer.name.c_str()));
      name->setData(Qt::UserRole, name->text());
      if (layer.name == m_State.currentLayer) { auto font = name->font(); font.setBold(true); name->setFont(font); }
      table->setItem(row, 0, name);
      for (int column = 1; column <= 3; ++column) {
        auto* value = new QTableWidgetItem;
        value->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        value->setCheckState((column == 1 ? layer.on : column == 2 ? layer.frozen : layer.locked) ? Qt::Checked : Qt::Unchecked);
        table->setItem(row, column, value);
      }
      table->setItem(row, 4, new QTableWidgetItem(QString::fromUtf8(layer.color.c_str())));
      table->setItem(row, 5, new QTableWidgetItem(QString::fromUtf8(layer.linetype.c_str())));
      table->setItem(row, 6, new QTableWidgetItem(QString::number(layer.lineweightMm)));
    }
  };
  populate();
  connect(table, &QTableWidget::itemChanged, &dialog, [&](QTableWidgetItem* item) {
    const int row = item->row(), column = item->column();
    const auto name = table->item(row, 0)->data(Qt::UserRole).toString().toUtf8().toStdString();
    const auto found = std::find_if(m_State.drawingLayerTable.begin(), m_State.drawingLayerTable.end(),
        [&](const auto& layer) { return layer.name == name; });
    if (found == m_State.drawingLayerTable.end()) return;
    std::string error;
    if (column == 0) {
      const auto next = item->text().toUtf8().toStdString();
      if (!CadRenameDrawingLayer(m_State, name, next, &error)) QMessageBox::warning(&dialog, "Rename layer", QString::fromUtf8(error.c_str()));
    } else {
      CadLayerRow next = *found;
      bool valid = true;
      if (column == 1) next.on = item->checkState() == Qt::Checked;
      if (column == 2) next.frozen = item->checkState() == Qt::Checked;
      if (column == 3) next.locked = item->checkState() == Qt::Checked;
      if (column == 4) valid = CadColorStorageFromTyped(item->text().toUtf8().toStdString(), &next.color) && next.color != "ByLayer";
      if (column == 5) { next.linetype = item->text().trimmed().toUtf8().toStdString(); valid = !next.linetype.empty(); }
      if (column == 6) {
        const double value = item->text().toDouble(&valid);
        valid = valid && std::isfinite(value) && value >= -1 && value <= 10;
        next.lineweightMm = static_cast<float>(value);
      }
      if (valid) { PushUndoSnapshot(m_State, "Layer property"); *found = std::move(next); BumpCadGpuCache(m_State); }
      else QMessageBox::warning(&dialog, "Layer property", "Enter a valid color, linetype, or lineweight (-1 for default).");
    }
    populate(); Refresh();
  });
  connect(add, &QPushButton::clicked, &dialog, [&] {
    bool accepted = false;
    const auto name = QInputDialog::getText(&dialog, "New layer", "Name", QLineEdit::Normal, {}, &accepted);
    if (!accepted) return;
    std::string error;
    if (!CadAddDrawingLayer(m_State, name.toUtf8().toStdString(), &error))
      QMessageBox::warning(&dialog, "New layer", QString::fromUtf8(error.c_str()));
    populate(); Refresh();
  });
  connect(erase, &QPushButton::clicked, &dialog, [&] {
    if (table->currentRow() < 0) return;
    const auto name = table->item(table->currentRow(), 0)->data(Qt::UserRole).toString().toUtf8().toStdString();
    std::string error;
    if (!CadDeleteDrawingLayer(m_State, name, &error)) QMessageBox::warning(&dialog, "Delete layer", QString::fromUtf8(error.c_str()));
    populate(); Refresh();
  });
  connect(current, &QPushButton::clicked, &dialog, [&] {
    if (table->currentRow() < 0) return;
    m_State.currentLayer = table->item(table->currentRow(), 0)->data(Qt::UserRole).toString().toUtf8().toStdString();
    populate(); Refresh();
  });
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  dialog.exec();
  m_ModalOpen = false;
  Refresh();
}

void EditorWindow::ShowUnits() {
  if (auto* existing = findChild<QDialog*>("drawingSettingsDialog")) {
    existing->show(); existing->raise(); existing->activateWindow(); return;
  }
  if (m_State.activeDrawingIdx <= 0 || m_State.activeDrawingIdx >= static_cast<int>(m_State.drawingTabs.size())) {
    m_Log.push_back("DRAWINGSETTINGS — open or create a drawing first."); Refresh(); return;
  }
  auto* dialog = new QDialog(this);
  dialog->setObjectName("drawingSettingsDialog");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Drawing Settings - " + QString::fromUtf8(m_State.drawingTabs[m_State.activeDrawingIdx].name.c_str()));
  dialog->setWindowModality(Qt::WindowModal);
  dialog->resize(970, 650);
  DrawingSettings staged = m_State.drawingSettings;
  int stagedUnits = m_State.drawingInsUnits;
  auto* root = new QVBoxLayout(dialog);
  auto* body = new QHBoxLayout;
  auto* navigation = new QListWidget(dialog);
  navigation->setObjectName("drawingSettingsNavigation");
  navigation->setFixedWidth(190);
  navigation->addItems({"Units and Zone", "Transformation", "Object Layers"});
  navigation->setCurrentRow(0);
  auto* pages = new QStackedWidget(dialog);
  pages->setObjectName("drawingSettingsPages");
  body->addWidget(navigation); body->addWidget(pages, 1); root->addLayout(body, 1);

  const std::pair<const char*, int> unitCodes[] = {{"Unitless", 0}, {"Inches", 1}, {"Feet", 2}, {"Millimeters", 4}, {"Meters", 6}};
  auto* unitsPage = new QWidget(dialog);
  auto* unitsForm = new QFormLayout(unitsPage);
  auto* units = new QComboBox(unitsPage); units->setObjectName("drawingUnit");
  for (const auto& code : unitCodes) units->addItem(code.first, code.second);
  units->setCurrentIndex(units->findData(stagedUnits));
  auto* angular = new QComboBox(unitsPage); angular->setObjectName("drawingAngularUnits");
  angular->addItems({"Degrees", "Radians", "Grads"}); angular->setCurrentIndex(static_cast<int>(staged.angularUnits));
  auto* foot = new QComboBox(unitsPage); foot->setObjectName("drawingFootDefinition");
  foot->addItems({"US Survey Foot", "International Foot"}); foot->setCurrentIndex(static_cast<int>(staged.footDefinition));
  auto* scale = new QComboBox(unitsPage); scale->setObjectName("drawingScaleChoice");
  auto* customScale = new QDoubleSpinBox(unitsPage); customScale->setObjectName("drawingCustomScale");
  customScale->setRange(0.000001, 1.e9); customScale->setDecimals(6); customScale->setValue(m_State.modelUnitsPerPlottedInch);
  auto fillScales = [scale, customScale](int insUnits, float current) {
    const QSignalBlocker blocker(scale); scale->clear();
    const auto choices = PlotScaleChoicesFor(insUnits);
    for (const auto& choice : choices) scale->addItem(QString::fromStdString(choice.label), choice.modelUnitsPerPlottedInch);
    const int choice = PlotScaleChoiceIndex(choices, current);
    scale->addItem("Custom", -1);
    scale->setCurrentIndex(choice >= 0 ? choice : scale->count() - 1);
    customScale->setValue(current); customScale->setEnabled(choice < 0);
  };
  fillScales(stagedUnits, m_State.modelUnitsPerPlottedInch);
  connect(units, qOverload<int>(&QComboBox::currentIndexChanged), dialog, [&, fillScales](int) {
    stagedUnits = units->currentData().toInt(); fillScales(stagedUnits, static_cast<float>(customScale->value()));
  });
  connect(scale, qOverload<int>(&QComboBox::currentIndexChanged), dialog, [scale, customScale](int index) {
    const bool custom = index >= 0 && scale->currentData().toInt() == -1;
    customScale->setEnabled(custom);
    if (!custom && index >= 0) customScale->setValue(scale->currentData().toFloat());
  });
  auto* scaleRow = new QWidget(unitsPage); auto* scaleRowLayout = new QHBoxLayout(scaleRow); scaleRowLayout->setContentsMargins(0,0,0,0);
  scaleRowLayout->addWidget(scale, 1); scaleRowLayout->addWidget(customScale);
  auto* scaleInserted = new QCheckBox("Scale objects inserted from other drawings", unitsPage);
  scaleInserted->setChecked(staged.scaleInsertedObjects);
  auto* setVariables = new QCheckBox("Set drawing variables to match", unitsPage);
  setVariables->setChecked(staged.setDrawingVariables);
  unitsForm->addRow("Drawing units (relabels geometry)", units);
  unitsForm->addRow("Angular units", angular);
  unitsForm->addRow("Imperial to Metric conversion", foot);
  unitsForm->addRow("Plot scale", scaleRow);
  unitsForm->addRow(scaleInserted); unitsForm->addRow(setVariables);
  auto* zoneHeading = new QLabel("Coordinate system", unitsPage); zoneHeading->setStyleSheet("font-weight:600; font-size:15px;");
  unitsForm->addRow(zoneHeading);
  auto* category = new QComboBox(unitsPage); category->setObjectName("drawingZoneCategory");
  category->addItem(QString::fromUtf8(geo::kNoZoneCategory), QString());
  for (const auto& name : geo::Categories()) category->addItem(QString::fromUtf8(name.c_str()), QString::fromUtf8(name.c_str()));
  auto* systems = new QComboBox(unitsPage); systems->setObjectName("drawingZoneSystem");
  auto* zoneCode = new QLineEdit(unitsPage); zoneCode->setObjectName("drawingZoneCode");
  auto* zoneDetails = new QLabel(unitsPage); zoneDetails->setObjectName("drawingZoneDetails"); zoneDetails->setWordWrap(true);
  auto updateDetails = [zoneDetails, &staged] {
    if (staged.zoneCode.empty()) { zoneDetails->setText("No Datum, No Projection\nProjection: Unknown projection\nDatum: Unknown Datum"); return; }
    const auto info = geo::FindCoordinateSystem(staged.zoneCode);
    zoneDetails->setText(info ? QString("%1\nProjection: %2\nDatum: %3").arg(QString::fromUtf8(info->description.c_str()), QString::fromUtf8(info->projection.c_str()), QString::fromUtf8(info->datum.c_str()))
                              : QString("%1 (unknown in this dictionary)\nProjection: Unknown projection\nDatum: Unknown Datum").arg(QString::fromUtf8(staged.zoneCode.c_str())));
  };
  auto populateSystems = [systems, &staged, updateDetails](const QString& cat) {
    const QSignalBlocker blocker(systems); systems->clear();
    const auto values = cat.isEmpty() ? std::vector<std::string>{} : geo::CoordinateSystemsIn(cat.toUtf8().toStdString());
    for (const auto& code : values) {
      const auto info = geo::FindCoordinateSystem(code);
      systems->addItem(info ? QString::fromUtf8(info->description.c_str()) : QString::fromUtf8(code.c_str()), QString::fromUtf8(code.c_str()));
    }
    if (!values.empty()) { systems->setCurrentIndex(0); staged.zoneCode = values.front(); }
    else if (cat.isEmpty()) staged.zoneCode.clear();
    updateDetails();
  };
  QString selectedCategory = staged.zoneCode.empty() ? QString() : QString::fromUtf8(geo::CategoryOf(staged.zoneCode).c_str());
  int catIndex = category->findData(selectedCategory);
  category->setCurrentIndex(catIndex >= 0 ? catIndex : 0);
  const std::string initialZoneCode = staged.zoneCode;
  populateSystems(selectedCategory);
  if (!staged.zoneCode.empty()) {
    const auto info = geo::FindCoordinateSystem(initialZoneCode);
    if (info) {
      const int zoneIndex = systems->findData(QString::fromUtf8(initialZoneCode.c_str()));
      if (zoneIndex >= 0) { systems->setCurrentIndex(zoneIndex); staged.zoneCode = initialZoneCode; }
    } else staged.zoneCode = initialZoneCode;
  }
  zoneCode->setText(staged.zoneCode.empty() ? "." : QString::fromUtf8(staged.zoneCode.c_str()));
  connect(category, qOverload<int>(&QComboBox::currentIndexChanged), dialog, [&, populateSystems](int) {
    populateSystems(category->currentData().toString());
    zoneCode->setText(staged.zoneCode.empty() ? "." : QString::fromUtf8(staged.zoneCode.c_str()));
  });
  connect(systems, qOverload<int>(&QComboBox::currentIndexChanged), dialog, [&, updateDetails](int) {
    if (systems->currentIndex() >= 0) staged.zoneCode = systems->currentData().toString().toUtf8().toStdString();
    zoneCode->setText(staged.zoneCode.empty() ? "." : QString::fromUtf8(staged.zoneCode.c_str())); updateDetails();
  });
  connect(zoneCode, &QLineEdit::editingFinished, dialog, [&, populateSystems, updateDetails] {
    const auto typed = zoneCode->text().trimmed().toUtf8().toStdString();
    if (typed.empty() || typed == ".") { staged.zoneCode.clear(); category->setCurrentIndex(0); populateSystems(QString()); zoneCode->setText("."); return; }
    const auto info = geo::FindCoordinateSystem(typed);
    if (!info) { QMessageBox::warning(dialog, "Coordinate system", QString("\"%1\" is not a coordinate system in this dictionary.").arg(QString::fromUtf8(typed.c_str()))); zoneCode->setText(QString::fromUtf8(staged.zoneCode.c_str())); return; }
    staged.zoneCode = info->code; const QString cat = QString::fromUtf8(geo::CategoryOf(info->code).c_str());
    category->setCurrentIndex(category->findData(cat)); populateSystems(cat); systems->setCurrentIndex(systems->findData(QString::fromUtf8(info->code.c_str())));
    zoneCode->setText(QString::fromUtf8(info->code.c_str())); updateDetails();
  });
  unitsForm->addRow("Categories", category); unitsForm->addRow("Available coordinate systems", systems);
  unitsForm->addRow("Selected coordinate system code", zoneCode); unitsForm->addRow(zoneDetails);
  pages->addWidget(unitsPage);

  auto* transformPage = new QWidget(dialog); auto* transformForm = new QFormLayout(transformPage);
  auto* transformOn = new QCheckBox("Apply transform settings", transformPage); transformOn->setChecked(staged.transform.apply);
  auto* seaLevel = new QCheckBox("Apply sea level scale factor", transformPage); seaLevel->setChecked(staged.transform.applySeaLevel);
  auto number = [transformPage](const QString& name, double value) {
    auto* field = new QDoubleSpinBox(transformPage); field->setObjectName(name); field->setRange(-1.e12, 1.e12); field->setDecimals(8); field->setValue(value); return field;
  };
  auto* elevation = number("transformElevation", staged.transform.elevation);
  auto* radius = number("transformRadius", staged.transform.spheroidRadiusM);
  auto* computation = new QComboBox(transformPage); computation->addItems({"Reference Point", "User Defined"}); computation->setCurrentIndex(static_cast<int>(staged.transform.computation));
  auto* userScale = number("transformUserScale", staged.transform.userScaleFactor);
  auto* refLocalX = number("transformRefLocalX", staged.transform.refLocalX);
  auto* refLocalY = number("transformRefLocalY", staged.transform.refLocalY);
  auto* refGridE = number("transformRefGridE", staged.transform.refGridE);
  auto* refGridN = number("transformRefGridN", staged.transform.refGridN);
  auto* refPoint = new QSpinBox(transformPage); refPoint->setRange(0, INT_MAX); refPoint->setValue(staged.transform.refPointNumber);
  auto* rotation = new QComboBox(transformPage); rotation->addItems({"Rotation point", "Specify grid rotation angle"}); rotation->setCurrentIndex(static_cast<int>(staged.transform.rotation));
  auto* rotLocalX = number("transformRotLocalX", staged.transform.rotLocalX);
  auto* rotLocalY = number("transformRotLocalY", staged.transform.rotLocalY);
  auto* rotGridE = number("transformRotGridE", staged.transform.rotGridE);
  auto* rotGridN = number("transformRotGridN", staged.transform.rotGridN);
  auto* rotPoint = new QSpinBox(transformPage); rotPoint->setRange(0, INT_MAX); rotPoint->setValue(staged.transform.rotPointNumber);
  auto* toNorth = number("transformToNorth", staged.transform.toNorthDeg);
  auto* localAzimuth = number("transformLocalAzimuth", staged.transform.localAzimuthDeg);
  auto* gridAzimuth = number("transformGridAzimuth", staged.transform.gridAzimuthDeg);
  auto pickRow = [transformPage](QWidget* first, QWidget* second, QWidget* third, const QString& object) {
    auto* row = new QWidget(transformPage); auto* h = new QHBoxLayout(row); h->setContentsMargins(0,0,0,0);
    h->addWidget(first); if (second) h->addWidget(second); if (third) h->addWidget(third);
    auto* pick = new QPushButton("Pick", row); pick->setObjectName(object); h->addWidget(pick); return row;
  };
  auto* referenceRow = pickRow(refLocalX, refLocalY, refGridE, "pickTransformReference");
  static_cast<QHBoxLayout*>(referenceRow->layout())->addWidget(refGridN);
  static_cast<QHBoxLayout*>(referenceRow->layout())->addWidget(refPoint);
  auto* rotationRow = pickRow(rotLocalX, rotLocalY, rotGridE, "pickTransformRotation");
  static_cast<QHBoxLayout*>(rotationRow->layout())->addWidget(rotGridN);
  static_cast<QHBoxLayout*>(rotationRow->layout())->addWidget(rotPoint);
  auto* northRow = pickRow(toNorth, nullptr, nullptr, "pickTransformNorth");
  auto* localAzRow = pickRow(localAzimuth, gridAzimuth, nullptr, "pickTransformAzimuth");
  transformForm->addRow(transformOn); transformForm->addRow("Zone", new QLabel(staged.zoneCode.empty() ? geo::kNoZoneCategory : staged.zoneCode.c_str(), transformPage));
  transformForm->addRow(seaLevel); transformForm->addRow("Elevation", elevation); transformForm->addRow("Spheroid radius (m)", radius);
  transformForm->addRow("Computation", computation); transformForm->addRow("User grid scale factor", userScale);
  transformForm->addRow("Reference point — local X/Y, grid E/N, point #", referenceRow);
  transformForm->addRow("Rotation", rotation); transformForm->addRow("Rotation point — local X/Y, grid E/N, point #", rotationRow);
  transformForm->addRow("North direction", northRow); transformForm->addRow("Azimuth pair", localAzRow);
  pages->addWidget(transformPage);

  auto* layersPage = new QWidget(dialog); auto* layersLayout = new QVBoxLayout(layersPage);
  auto* layersTable = new QTableWidget(layersPage); layersTable->setObjectName("drawingObjectLayers");
  layersTable->setColumnCount(4); layersTable->setHorizontalHeaderLabels({"Object", "Layer", "Name modifier / value", "Locked"});
  const char* layerNames[] = {"Survey point", "Survey point label", "TIN surface", "Feature line", "Pipe run", "Pipe fitting", "Solid", "Table"};
  layersTable->setRowCount(kObjectLayerKindCount); layersTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  layersTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch); layersTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  layersTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  std::array<QComboBox*, kObjectLayerKindCount> modifiers{};
  std::array<QComboBox*, kObjectLayerKindCount> layerFields{};
  std::array<QLineEdit*, kObjectLayerKindCount> modifierValues{};
  std::array<QCheckBox*, kObjectLayerKindCount> layerLocks{};
  for (int i = 0; i < kObjectLayerKindCount; ++i) {
    const auto& row = staged.objectLayers[static_cast<size_t>(i)];
    layersTable->setItem(i, 0, new QTableWidgetItem(layerNames[i])); layersTable->item(i,0)->setFlags(Qt::ItemIsEnabled);
    auto* layer = new QComboBox(layersTable); layer->setEditable(true); layer->setInsertPolicy(QComboBox::NoInsert);
    for (const auto& entry : m_State.drawingLayerTable) layer->addItem(QString::fromUtf8(entry.name.c_str()));
    layer->setCurrentText(QString::fromUtf8(row.layer.c_str()));
    layerFields[static_cast<size_t>(i)] = layer; layersTable->setCellWidget(i, 1, layer);
    auto* cell = new QWidget(layersTable); auto* h = new QHBoxLayout(cell); h->setContentsMargins(0,0,0,0);
    auto* modifier = new QComboBox(cell); modifier->addItems({"None", "Prefix", "Suffix"}); modifier->setCurrentIndex(static_cast<int>(row.modifier));
    auto* value = new QLineEdit(QString::fromUtf8(row.value.c_str()), cell); value->setEnabled(row.modifier != ObjectLayerRow::Modifier::None);
    connect(modifier, qOverload<int>(&QComboBox::currentIndexChanged), value, [value](int index) { value->setEnabled(index != 0); });
    h->addWidget(modifier); h->addWidget(value, 1); layersTable->setCellWidget(i, 2, cell);
    modifiers[static_cast<size_t>(i)] = modifier; modifierValues[static_cast<size_t>(i)] = value;
    auto* locked = new QCheckBox(layersTable); locked->setChecked(row.locked);
    auto* lockCell = new QWidget(layersTable); auto* lockLayout = new QHBoxLayout(lockCell);
    lockLayout->setContentsMargins(0, 0, 0, 0); lockLayout->addStretch(); lockLayout->addWidget(locked); lockLayout->addStretch();
    layerLocks[static_cast<size_t>(i)] = locked; layersTable->setCellWidget(i, 3, lockCell);
  }
  layersLayout->addWidget(layersTable, 1);
  auto* layerHelp = new QLabel("Use * in a prefix or suffix value to insert the new object's name. Display component controls are not implemented yet.", layersPage);
  layerHelp->setWordWrap(true); layersLayout->addWidget(layerHelp); pages->addWidget(layersPage);
  connect(navigation, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);

  auto* buttons = new QDialogButtonBox(dialog);
  auto* ok = buttons->addButton(QDialogButtonBox::Ok); auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
  auto* apply = buttons->addButton(QDialogButtonBox::Apply);
  ok->setObjectName("drawingSettingsOk"); cancel->setObjectName("drawingSettingsCancel"); apply->setObjectName("drawingSettingsApply");
  root->addWidget(buttons);
  auto* pickTimer = new QTimer(dialog); pickTimer->setInterval(50);
  bool waitingForPick = false;
  auto beginPick = [this, dialog, pickTimer, &waitingForPick](AppCommandState::DrawingSettingsPickState::Target target) {
    if (!StartDrawingSettingsPick(m_State, target, m_Log)) { Refresh(); return; }
    waitingForPick = true; dialog->setWindowModality(Qt::NonModal); dialog->hide(); m_Viewport->setFocus(); pickTimer->start();
  };
  auto* pickRef = referenceRow->findChild<QPushButton*>("pickTransformReference");
  auto* pickRot = rotationRow->findChild<QPushButton*>("pickTransformRotation");
  auto* pickNorth = northRow->findChild<QPushButton*>("pickTransformNorth");
  auto* pickAz = localAzRow->findChild<QPushButton*>("pickTransformAzimuth");
  connect(pickRef, &QPushButton::clicked, dialog, [&, beginPick] { beginPick(AppCommandState::DrawingSettingsPickState::Target::ReferencePoint); });
  connect(pickRot, &QPushButton::clicked, dialog, [&, beginPick] { beginPick(AppCommandState::DrawingSettingsPickState::Target::RotationPoint); });
  connect(pickNorth, &QPushButton::clicked, dialog, [&, beginPick] { beginPick(AppCommandState::DrawingSettingsPickState::Target::ToNorth); });
  connect(pickAz, &QPushButton::clicked, dialog, [&, beginPick] { beginPick(AppCommandState::DrawingSettingsPickState::Target::LocalAzimuth); });
  connect(pickTimer, &QTimer::timeout, dialog, [&, dialog, pickTimer] {
    if (m_State.active == AppCommandState::Kind::DrawingSettingsPick) return;
    pickTimer->stop(); if (!waitingForPick) return; waitingForPick = false;
    auto& result = m_State.drawingSettingsPick;
    if (result.done) {
      const auto target = result.target;
      if (target == AppCommandState::DrawingSettingsPickState::Target::ReferencePoint) {
        refLocalX->setValue(result.worldX); refLocalY->setValue(result.worldY); refPoint->setValue(result.pointNumber);
      } else if (target == AppCommandState::DrawingSettingsPickState::Target::RotationPoint) {
        rotLocalX->setValue(result.worldX); rotLocalY->setValue(result.worldY); rotPoint->setValue(result.pointNumber);
      } else if (target == AppCommandState::DrawingSettingsPickState::Target::ToNorth) toNorth->setValue(result.azimuthDeg);
      else if (target == AppCommandState::DrawingSettingsPickState::Target::LocalAzimuth) localAzimuth->setValue(result.azimuthDeg);
      result.done = false;
    }
    dialog->setWindowModality(Qt::WindowModal); dialog->show(); dialog->raise(); dialog->activateWindow(); Refresh();
  });
  auto applySettings = [&, dialog](bool closeAfter) {
    staged.angularUnits = static_cast<DrawingSettings::AngularUnits>(angular->currentIndex());
    staged.footDefinition = static_cast<DrawingSettings::FootDefinition>(foot->currentIndex());
    staged.scaleInsertedObjects = scaleInserted->isChecked(); staged.setDrawingVariables = setVariables->isChecked();
    if (scale->currentData().toInt() == -1 && customScale->value() <= 0) { QMessageBox::warning(dialog, "Drawing Settings", "The custom scale must be a positive number."); return; }
    const float plotScale = scale->currentData().toInt() == -1 ? static_cast<float>(customScale->value()) : scale->currentData().toFloat();
    auto& transform = staged.transform;
    transform.apply = transformOn->isChecked(); transform.applySeaLevel = seaLevel->isChecked(); transform.elevation = elevation->value();
    transform.spheroidRadiusM = radius->value(); transform.computation = static_cast<DrawingSettings::Transform::Computation>(computation->currentIndex());
    transform.userScaleFactor = userScale->value(); transform.refLocalX = refLocalX->value(); transform.refLocalY = refLocalY->value();
    transform.refGridE = refGridE->value(); transform.refGridN = refGridN->value(); transform.refPointNumber = refPoint->value();
    transform.rotation = static_cast<DrawingSettings::Transform::Rotation>(rotation->currentIndex());
    transform.rotLocalX = rotLocalX->value(); transform.rotLocalY = rotLocalY->value(); transform.rotGridE = rotGridE->value();
    transform.rotGridN = rotGridN->value(); transform.rotPointNumber = rotPoint->value(); transform.toNorthDeg = toNorth->value();
    transform.localAzimuthDeg = localAzimuth->value(); transform.gridAzimuthDeg = gridAzimuth->value();
    for (int i = 0; i < kObjectLayerKindCount; ++i) {
      auto& row = staged.objectLayers[static_cast<size_t>(i)];
      row.layer = layerFields[static_cast<size_t>(i)]->currentText().trimmed().toUtf8().toStdString();
      row.modifier = static_cast<ObjectLayerRow::Modifier>(modifiers[static_cast<size_t>(i)]->currentIndex());
      row.value = modifierValues[static_cast<size_t>(i)]->text().toUtf8().toStdString();
      row.locked = layerLocks[static_cast<size_t>(i)]->isChecked();
    }
    if (const auto error = ValidateObjectLayers(staged); !error.empty()) { QMessageBox::warning(dialog, "Object Layers", QString::fromUtf8(error.c_str())); return; }
    if (const auto error = ValidateDrawingTransform(staged); !error.empty()) { QMessageBox::warning(dialog, "Transformation", QString::fromUtf8(error.c_str())); return; }
    if (ApplyDrawingSettings(m_State, stagedUnits, plotScale, staged, m_Log)) { Refresh(); if (closeAfter) dialog->accept(); }
    else Refresh();
  };
  connect(ok, &QPushButton::clicked, dialog, [applySettings] { applySettings(true); });
  connect(apply, &QPushButton::clicked, dialog, [applySettings] { applySettings(false); });
  connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
  m_ModalOpen = true;
  dialog->exec();
  m_ModalOpen = false;
  Refresh();
}
