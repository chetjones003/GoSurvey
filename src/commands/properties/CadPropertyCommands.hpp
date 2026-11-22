// --- Object isolation (REQ-084 (d) / ADR-034) ----------------------------------------------
/// The attributes of a selected entity, or nullptr for a type that carries none (survey points,
/// PDF underlays) or an index that no longer resolves.
const EntityAttributes* CadEntityAttrsForSelected(const AppCommandState& st, const SelectedEntity& e);
/// The attributes a layer / colour edit reads and writes (REQ-352): every type that carries them,
/// solids and pipe runs included — unlike \ref CadEntityAttrsForSelected, which isolation keeps
/// narrower. nullptr for a type with none or an index that no longer resolves.
const EntityAttributes* CadEditableAttrsForSelected(const AppCommandState& st, const SelectedEntity& e);
/// Properties / ribbon layer edit (REQ-352): moves every selected entity that carries attributes —
/// solids and pipe runs included — to \p layer, as one undo step. A new name joins the layer
/// table. Returns how many entities changed; nothing changed pushes no undo.
int CadApplyLayerToSelection(AppCommandState& st, const std::string& layer);
/// Properties colour edit (REQ-352): the same, for the colour storage string ("ByLayer", an ACI
/// name, `#RRGGBB`).
int CadApplyColorToSelection(AppCommandState& st, const std::string& color);
/// What \ref CadSelectionLayer returns when the selected objects sit on more than one layer.
inline constexpr const char* kCadSelectionLayerVaries = "*VARIES*";
/// The layer the selected objects share, \ref kCadSelectionLayerVaries when they differ, or "" when
/// nothing selected carries a layer — the ribbon Layers combo's preview (REQ-352).
[[nodiscard]] std::string CadSelectionLayer(const AppCommandState& st);
/// The ribbon Layers combo's pick (REQ-352, AutoCAD's rule): with objects selected that carry a
/// layer, moves them to \p layer (one undo step) and leaves the current layer alone; with none,
/// sets the current layer for new geometry, as it always has.
void CadRibbonPickLayer(AppCommandState& st, const std::string& layer, std::vector<std::string>& log);

// --- CHPROP / MATCHPROP / LAYMCUR, the current colour (REQ-356) ------------------------------
/// True for the types a linetype / lineweight edit applies to: line, circle, arc, ellipse,
/// polyline, annotation and table — the set the Properties panel has always edited them on. Every
/// other type (a solid body, a pipe run, a mesh, ...) takes layer and colour only (REQ-356 item 5).
[[nodiscard]] bool CadLineStyleApplies(SelectedEntity::Type t);
/// Linetype edit on the selection (REQ-356): the \ref CadApplyLayerToSelection shape — one undo
/// step, pushed only when something changes — over the types \ref CadLineStyleApplies names.
/// Returns how many changed; \p skipped (optional) receives how many selected objects the
/// property does not apply to.
int CadApplyLinetypeToSelection(AppCommandState& st, const std::string& linetype, int* skipped = nullptr);
/// The same, for the lineweight in millimetres (\c -1 = ByLayer).
int CadApplyLineweightToSelection(AppCommandState& st, float mm, int* skipped = nullptr);
/// Transparency edit on the selection (0 = opaque, 1 = clear, -1 = ByLayer), over the same types as
/// the line-style edits: one undo step, pushed only when something changes. The 0.7.4 Properties
/// panel wrote transparency with no undo step; both frontends now share this service (#891).
int CadApplyTransparencyToSelection(AppCommandState& st, float transparency, int* skipped = nullptr);
/// What \ref CadSelectionColor returns when the selected objects carry more than one colour.
inline constexpr const char* kCadSelectionColorVaries = "*VARIES*";
/// The colour storage the selected objects share, \ref kCadSelectionColorVaries when they differ,
/// or "" when nothing selected carries one — the ribbon colour combo's preview (REQ-356).
[[nodiscard]] std::string CadSelectionColor(const AppCommandState& st);
/// The ribbon colour combo's pick (REQ-356, REQ-352's rule): recolours a selection (one undo
/// step), leaving the current colour alone; with nothing selected, sets \ref
/// AppCommandState::currentColor.
void CadRibbonPickColor(AppCommandState& st, const std::string& color, std::vector<std::string>& log);
// --- Right-click classification (REQ-054, REQ-084) -------------------------------------------
/// What a right-click in the drawing does. Both frontends classify with \ref CadClassifyRightClick
/// so the preference dialog means the same thing in each (#891).
enum class RightClickAction : std::uint8_t {
  Enter,         ///< Submit an empty line (finish the command, or repeat the last when idle).
  RepeatLast,    ///< Repeat the last command; only returned when there is one to repeat.
  ShortcutMenu,  ///< Open the shortcut menu.
};
/// The verdict of the Default / Edit / Command Mode preferences (REQ-084 (a)) for a right-click
/// PRESS. A mode that says "repeat" with nothing to repeat opens the menu rather than doing nothing.
/// With time-sensitive right-click on and no selection, the caller measures the hold instead
/// (\ref CadRightClickIsTimed) and uses \ref CadRightClickHoldIsMenu.
[[nodiscard]] inline RightClickAction CadClassifyRightClick(const AppCommandState& st, bool hasSelection) {
  using K = AppCommandState::Kind;
  using CM = AppCommandState::RightClickCommandMode;
  if (st.active != K::None)
    return st.rightClickCommandMode == CM::Enter ? RightClickAction::Enter : RightClickAction::ShortcutMenu;
  const bool repeat = hasSelection
      ? st.rightClickEditMode == AppCommandState::RightClickEditMode::RepeatLastCommand
      : st.rightClickDefaultMode == AppCommandState::RightClickDefaultMode::RepeatLastCommand;
  return repeat && st.lastCommand != K::None ? RightClickAction::RepeatLast : RightClickAction::ShortcutMenu;
}
/// True when this right-click is decided by how long the button is held (REQ-084 (b)): the option
/// is on and nothing is selected — a selection still goes through Edit Mode.
[[nodiscard]] inline bool CadRightClickIsTimed(const AppCommandState& st, bool hasSelection) {
  return st.rightClickTimeSensitive && !hasSelection;
}

/// True for \c Kind::ChProp / \c MatchProp / \c LayMCur.
[[nodiscard]] bool IsPropCommandKind(AppCommandState::Kind k);
void StartChPropCommand(AppCommandState& st, std::vector<std::string>& log);
void StartMatchPropCommand(AppCommandState& st, std::vector<std::string>& log);
void StartLayMCurCommand(AppCommandState& st, std::vector<std::string>& log);
/// A property command's typed line — Enter included (an empty \p line). Returns false only when no
/// property command is running.
bool HandlePropCommandTextInput(const std::string& line, AppCommandState& st, std::vector<std::string>& log);
/// The selection changed during a property command's pick step (a click or a closed window). The
/// single-pick steps act at once — LAYMCUR, MATCHPROP's source and each MATCHPROP destination —
/// the way AutoCAD's do; CHPROP's step waits for Enter. Called by the viewport after an
/// accumulate click and by \ref SubmitViewportPick after a window.
void CadPropCommandSelectionChanged(AppCommandState& st, std::vector<std::string>& log);
/// The prompt for the property command's current step.
[[nodiscard]] std::string CadPropCommandPromptText(const AppCommandState& st);
/// Ends a property command (Esc, or its own finish) and clears its pick selection.
void EndPropCommand(AppCommandState& st);
/// True when \p e is currently isolated out. Entity types with no attributes are never hidden.
bool CadSelectedEntityHidden(const AppCommandState& st, const SelectedEntity& e);
/// ISOLATEOBJECTS — hide everything EXCEPT the current selection.
void IsolateSelectedObjects(AppCommandState& st, std::vector<std::string>& log);
/// HIDEOBJECTS — hide the current selection.
void HideSelectedObjects(AppCommandState& st, std::vector<std::string>& log);
/// UNISOLATEOBJECTS — show everything again. Reports when there was nothing hidden (REQ-201).
void EndObjectIsolation(AppCommandState& st, std::vector<std::string>& log);
