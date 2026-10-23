#include "EditorPointGroups.hpp"

#include "CadCommands.hpp"
#include "SurveyPoints.hpp"

#include <QDialog>
#include <QAbstractItemView>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <string>
#include <utility>

namespace {

QString FromUtf8(const std::string& value) { return QString::fromUtf8(value.c_str()); }

std::string NextGroupName(const AppCommandState& state) {
  std::string base = "Point Group";
  if (FindPointGroupIndex(state, base) < 0) return base;
  for (int number = 2; number < 10000; ++number) {
    const auto candidate = base + " (" + std::to_string(number) + ")";
    if (FindPointGroupIndex(state, candidate) < 0) return candidate;
  }
  return base;
}

QFrame* Section(QWidget* parent, const QString& title, QVBoxLayout*& contents) {
  auto* frame = new QFrame(parent);
  frame->setObjectName("pointGroupSection");
  frame->setFrameShape(QFrame::StyledPanel);
  auto* layout = new QVBoxLayout(frame);
  layout->setContentsMargins(12, 10, 12, 10);
  layout->setSpacing(6);
  auto* heading = new QLabel(title, frame);
  QFont font = heading->font();
  font.setBold(true);
  heading->setFont(font);
  layout->addWidget(heading);
  contents = layout;
  return frame;
}

} // namespace

void ShowPointGroupManager(QWidget* parent, AppCommandState& state, std::vector<std::string>& log) {
  QDialog dialog(parent);
  dialog.setObjectName("pointGroupManager");
  dialog.setWindowTitle("Point Groups");
  dialog.setWindowModality(Qt::WindowModal);
  dialog.resize(850, 610);

  auto* root = new QVBoxLayout(&dialog);
  root->setContentsMargins(22, 18, 22, 16);
  root->setSpacing(10);
  auto* top = new QHBoxLayout;
  auto* heading = new QLabel("Point Groups", &dialog);
  QFont headingFont = heading->font();
  headingFont.setPointSize(16);
  headingFont.setBold(true);
  heading->setFont(headingFont);
  top->addWidget(heading);
  auto* groupPicker = new QComboBox(&dialog);
  groupPicker->setObjectName("pointGroupPicker");
  groupPicker->setMinimumWidth(190);
  groupPicker->setPlaceholderText("Select a point group");
  top->addWidget(groupPicker);
  top->addStretch();
  auto* create = new QPushButton("＋  New group", &dialog);
  create->setObjectName("pointGroupNew");
  top->addWidget(create);
  root->addLayout(top);

  auto* content = new QHBoxLayout;
  content->setSpacing(12);
  auto* editorColumn = new QVBoxLayout;
  editorColumn->setSpacing(9);
  auto* identityLayout = static_cast<QVBoxLayout*>(nullptr);
  auto* identity = Section(&dialog, "Identity", identityLayout);
  auto* name = new QLineEdit(&dialog);
  name->setObjectName("pointGroupName");
  name->setPlaceholderText("Group name");
  identityLayout->addWidget(new QLabel("Group name", &dialog));
  identityLayout->addWidget(name);
  auto* validation = new QLabel(&dialog);
  validation->setObjectName("pointGroupValidation");
  validation->setStyleSheet("color:#bd6b24;");
  validation->setWordWrap(true);
  identityLayout->addWidget(validation);
  editorColumn->addWidget(identity);

  auto* rulesLayout = static_cast<QVBoxLayout*>(nullptr);
  auto* rules = Section(&dialog, "Membership rules · any rule can match", rulesLayout);
  auto* form = new QFormLayout;
  form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
  form->setSpacing(8);
  auto makeEdit = [&](const char* objectName, const QString& placeholder) {
    auto* edit = new QLineEdit(&dialog);
    edit->setObjectName(objectName);
    edit->setPlaceholderText(placeholder);
    return edit;
  };
  auto* ranges = makeEdit("pointGroupRanges", "1-500, 1200, 1400-1450");
  auto* descriptions = makeEdit("pointGroupDescription", "EG*");
  auto* rawDescriptions = makeEdit("pointGroupRawDescription", "EG*");
  form->addRow("Point numbers", ranges);
  form->addRow("Description wildcard", descriptions);
  form->addRow("Raw description wildcard", rawDescriptions);
  auto* wildcardHelp = new QLabel("Ranges include both endpoints. Wildcards use * for any run and ? for one character; matching ignores case.", &dialog);
  wildcardHelp->setWordWrap(true);
  wildcardHelp->setStyleSheet("color:#78899a;");
  rulesLayout->addLayout(form);
  rulesLayout->addWidget(wildcardHelp);
  auto* ruleHint = new QLabel(&dialog);
  ruleHint->setObjectName("pointGroupRuleHint");
  ruleHint->setWordWrap(true);
  ruleHint->setStyleSheet("color:#78899a;");
  rulesLayout->addWidget(ruleHint);
  auto* picks = new QHBoxLayout;
  auto* addSelection = new QPushButton("＋  Add current selection", &dialog);
  addSelection->setObjectName("pointGroupAddSelection");
  auto* clearPicks = new QPushButton("Clear picks", &dialog);
  clearPicks->setObjectName("pointGroupClearPicks");
  picks->addWidget(addSelection);
  picks->addWidget(clearPicks);
  picks->addStretch();
  rulesLayout->addLayout(picks);
  auto* pickedIds = new QLabel(&dialog);
  pickedIds->setObjectName("pointGroupPickedIds");
  pickedIds->setWordWrap(true);
  pickedIds->setStyleSheet("color:#78899a;");
  rulesLayout->addWidget(pickedIds);
  editorColumn->addWidget(rules);
  editorColumn->addStretch();
  content->addLayout(editorColumn, 3);

  auto* previewColumn = new QVBoxLayout;
  previewColumn->setSpacing(9);
  auto* matchCount = new QLabel(&dialog);
  matchCount->setObjectName("pointGroupMatchCount");
  matchCount->setMinimumHeight(62);
  matchCount->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  matchCount->setMargin(12);
  matchCount->setStyleSheet("QLabel { background:#e6f3ee; color:#18553e; border-radius:6px; font-weight:600; }");
  previewColumn->addWidget(matchCount);
  auto* previewLabel = new QLabel("Member preview", &dialog);
  QFont previewFont = previewLabel->font();
  previewFont.setBold(true);
  previewLabel->setFont(previewFont);
  previewColumn->addWidget(previewLabel);
  auto* memberPreview = new QListWidget(&dialog);
  memberPreview->setObjectName("pointGroupMemberPreview");
  memberPreview->setSelectionMode(QAbstractItemView::NoSelection);
  previewColumn->addWidget(memberPreview, 1);
  content->addLayout(previewColumn, 2);
  root->addLayout(content, 1);

  auto* footer = new QHBoxLayout;
  auto* deleteGroup = new QPushButton("Delete group", &dialog);
  deleteGroup->setObjectName("pointGroupDelete");
  footer->addWidget(deleteGroup);
  footer->addStretch();
  auto* close = new QPushButton("Close", &dialog);
  close->setObjectName("pointGroupClose");
  close->setDefault(true);
  footer->addWidget(close);
  root->addLayout(footer);

  int selected = state.pointGroups.empty() ? -1 : 0;
  bool syncQueued = false;
  bool rangeUndoSaved = false;
  bool descriptionUndoSaved = false;
  bool rawUndoSaved = false;
  std::vector<std::string> groupNames;
  auto update = [&] {
    const bool hasGroup = selected >= 0 && selected < static_cast<int>(state.pointGroups.size());
    std::vector<std::string> currentNames;
    currentNames.reserve(state.pointGroups.size());
    for (const auto& group : state.pointGroups) currentNames.push_back(group.name);
    if (currentNames != groupNames) {
      const QSignalBlocker pickerBlock(groupPicker);
      groupPicker->clear();
      for (const auto& group : state.pointGroups) groupPicker->addItem(FromUtf8(group.name));
      groupNames = std::move(currentNames);
    }
    {
      const QSignalBlocker pickerBlock(groupPicker);
      groupPicker->setCurrentIndex(hasGroup ? selected : -1);
    }
    groupPicker->setEnabled(hasGroup);
    create->setEnabled(true);
    deleteGroup->setEnabled(hasGroup);
    name->setEnabled(hasGroup);
    ranges->setEnabled(hasGroup);
    descriptions->setEnabled(hasGroup);
    rawDescriptions->setEnabled(hasGroup);
    addSelection->setEnabled(hasGroup && !state.selectedSurveyPointIndices.empty());
    clearPicks->setEnabled(hasGroup && !state.pointGroups[static_cast<size_t>(selected)].rule.explicitIds.empty());

    const QSignalBlocker nameBlock(name), rangeBlock(ranges), descriptionBlock(descriptions), rawBlock(rawDescriptions);
    if (!hasGroup) {
      name->clear(); ranges->clear(); descriptions->clear(); rawDescriptions->clear();
      validation->setText(state.pointGroups.empty() ? "No groups yet. Create one to define a set of survey points." : "");
      pickedIds->setText("A point group is a rule, not a fixed list. Matching points imported later join automatically.");
      matchCount->setText("No point group selected");
      matchCount->setStyleSheet("QLabel { background:#e8edf2; color:#35485a; border-radius:6px; font-weight:600; }");
      ruleHint->clear();
      memberPreview->clear();
    } else {
      auto& group = state.pointGroups[static_cast<size_t>(selected)];
      if (!name->hasFocus()) name->setText(FromUtf8(group.name));
      if (!ranges->hasFocus()) ranges->setText(FromUtf8(group.rule.idRangesText));
      if (!descriptions->hasFocus()) descriptions->setText(FromUtf8(group.rule.descriptionMatch));
      if (!rawDescriptions->hasFocus()) rawDescriptions->setText(FromUtf8(group.rule.rawDescriptionMatch));
      std::string picked;
      for (size_t i = 0; i < group.rule.explicitIds.size() && i < 30; ++i)
        picked += (i ? ", " : "Picked ids: ") + std::to_string(group.rule.explicitIds[i]);
      if (group.rule.explicitIds.size() > 30) picked += ", …";
      pickedIds->setText(group.rule.explicitIds.empty() ? "Picked ids: none" : FromUtf8(picked));

      memberPreview->clear();
      const auto members = ResolvePointGroup(state, group, nullptr);
      if (group.rule.empty()) {
        matchCount->setText("No criteria set — this group contains 0 points.");
        matchCount->setStyleSheet("QLabel { background:#fff0d4; color:#76500c; border-radius:6px; font-weight:600; }");
      } else if (members.empty()) {
        matchCount->setText(QString("Matches 0 of %1 survey points").arg(state.surveyPoints.size()));
        matchCount->setStyleSheet("QLabel { background:#fff0d4; color:#76500c; border-radius:6px; font-weight:600; }");
      } else {
        matchCount->setText(QString("%1 matched\nof %2 survey points").arg(members.size()).arg(state.surveyPoints.size()));
        matchCount->setStyleSheet("QLabel { background:#e6f3ee; color:#18553e; border-radius:6px; font-weight:600; }");
      }
      for (size_t i = 0; i < members.size() && i < 12; ++i) {
        const auto& point = state.surveyPoints[static_cast<size_t>(members[i])];
        memberPreview->addItem(QString("%1  ·  %2").arg(point.id).arg(QString::fromUtf8(point.description.c_str())));
      }
      if (members.size() > 12)
        memberPreview->addItem(QString("… %1 more").arg(members.size() - 12));
      if (group.rule.empty()) {
        ruleHint->setText("An empty rule matches nothing, not every point.");
        ruleHint->setStyleSheet("color:#78899a;");
      } else {
        std::vector<std::string> badTokens;
        (void)ParseIdRanges(group.rule.idRangesText, &badTokens);
        QString warning;
        for (const auto& token : badTokens)
          warning += (warning.isEmpty() ? "Ignored invalid point number: \"" : ", \"") + FromUtf8(token) + "\"";
        ruleHint->setText(warning);
        ruleHint->setStyleSheet(warning.isEmpty() ? "color:#78899a;" : "color:#bd6b24;");
      }
    }

  };

  QObject::connect(create, &QPushButton::clicked, &dialog, [&] {
    PushUndoSnapshot(state, "New point group");
    PointGroup group;
    group.name = NextGroupName(state);
    state.pointGroups.push_back(std::move(group));
    selected = static_cast<int>(state.pointGroups.size()) - 1;
    log.push_back("Created point group \"" + state.pointGroups.back().name + "\".");
    update();
  });
  QObject::connect(groupPicker, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
    if (index < 0 || index >= static_cast<int>(state.pointGroups.size())) return;
    selected = index;
    validation->clear();
    update();
  });
  QObject::connect(deleteGroup, &QPushButton::clicked, &dialog, [&] {
    if (selected < 0 || selected >= static_cast<int>(state.pointGroups.size())) return;
    const auto groupName = state.pointGroups[static_cast<size_t>(selected)].name;
    PushUndoSnapshot(state, "Delete point group");
    state.pointGroups.erase(state.pointGroups.begin() + selected);
    log.push_back("Deleted point group \"" + groupName + "\".");
    if (selected >= static_cast<int>(state.pointGroups.size())) selected = static_cast<int>(state.pointGroups.size()) - 1;
    update();
  });
  QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
  QObject::connect(name, &QLineEdit::editingFinished, &dialog, [&] {
    if (selected < 0 || selected >= static_cast<int>(state.pointGroups.size())) return;
    const auto replacement = name->text().trimmed().toUtf8().toStdString();
    const auto current = state.pointGroups[static_cast<size_t>(selected)].name;
    const int clash = FindPointGroupIndex(state, replacement);
    if (replacement.empty()) {
      validation->setText("Point group name cannot be empty.");
      log.push_back("Point group name cannot be empty — keeping \"" + current + "\".");
    } else if (clash >= 0 && clash != selected) {
      validation->setText("A group with this name already exists.");
      log.push_back("A point group named \"" + replacement + "\" already exists — rename refused.");
    } else if (replacement != current) {
      PushUndoSnapshot(state, "Rename point group");
      state.pointGroups[static_cast<size_t>(selected)].name = replacement;
      validation->clear();
      log.push_back("Renamed point group \"" + current + "\" to \"" + replacement + "\".");
    }
    update();
  });

  auto bindRuleEdit = [&](QLineEdit* edit, bool& undoSaved, const char* description,
                          std::string PointGroupRule::*field) {
    QObject::connect(edit, &QLineEdit::textEdited, &dialog, [&, edit, field, description](const QString& text) {
      if (selected < 0 || selected >= static_cast<int>(state.pointGroups.size())) return;
      validation->clear();
      auto& group = state.pointGroups[static_cast<size_t>(selected)];
      auto& value = group.rule.*field;
      const auto newValue = text.toUtf8().toStdString();
      if (value == newValue) return;
      if (!undoSaved) {
        PushUndoSnapshot(state, description);
        undoSaved = true;
      }
      value = newValue;
      if (!syncQueued) {
        syncQueued = true;
        QTimer::singleShot(0, &dialog, [&] { syncQueued = false; update(); });
      }
    });
    QObject::connect(edit, &QLineEdit::editingFinished, &dialog, [&] {
      undoSaved = false;
      if (selected >= 0 && selected < static_cast<int>(state.pointGroups.size()))
        (void)ResolvePointGroup(state, state.pointGroups[static_cast<size_t>(selected)], &log);
      update();
    });
  };
  bindRuleEdit(ranges, rangeUndoSaved, "Edit point group ranges", &PointGroupRule::idRangesText);
  bindRuleEdit(descriptions, descriptionUndoSaved, "Edit point group description rule", &PointGroupRule::descriptionMatch);
  bindRuleEdit(rawDescriptions, rawUndoSaved, "Edit point group raw-description rule", &PointGroupRule::rawDescriptionMatch);

  QObject::connect(addSelection, &QPushButton::clicked, &dialog, [&] {
    if (selected < 0 || selected >= static_cast<int>(state.pointGroups.size())) return;
    auto& group = state.pointGroups[static_cast<size_t>(selected)];
    std::vector<int> additions;
    for (const int pointIndex : state.selectedSurveyPointIndices) {
      if (pointIndex < 0 || static_cast<size_t>(pointIndex) >= state.surveyPoints.size()) continue;
      const int id = state.surveyPoints[static_cast<size_t>(pointIndex)].id;
      if (std::find(group.rule.explicitIds.begin(), group.rule.explicitIds.end(), id) == group.rule.explicitIds.end())
        additions.push_back(id);
    }
    if (additions.empty()) return;
    PushUndoSnapshot(state, "Add points to group");
    group.rule.explicitIds.insert(group.rule.explicitIds.end(), additions.begin(), additions.end());
    log.push_back("Added " + std::to_string(additions.size()) + " point(s) to \"" + group.name + "\".");
    update();
  });
  QObject::connect(clearPicks, &QPushButton::clicked, &dialog, [&] {
    if (selected < 0 || selected >= static_cast<int>(state.pointGroups.size())) return;
    auto& ids = state.pointGroups[static_cast<size_t>(selected)].rule.explicitIds;
    if (ids.empty()) return;
    PushUndoSnapshot(state, "Clear group picks");
    ids.clear();
    update();
  });

  update();
  dialog.exec();
}
