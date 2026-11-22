  // -------------------------------------------------------------------------
  // BCONNECT wizard (issue #496): one value prompted per step. The final step either picks a flat
  // solid face (reusing the existing bconnectAwaitingFace pick mechanism) or accepts typed
  // "x,y,z,nx,ny,nz".
  // -------------------------------------------------------------------------
  enum class BConnectPhase : std::uint8_t {
    WaitName,
    WaitNominalSize,
    WaitRole,
    WaitEngagement,
    WaitCompatTag,
    WaitPoint,
  } bconnectPhase = BConnectPhase::WaitName;

  // -------------------------------------------------------------------------
  // BCONNECTEDIT wizard (issue #496): one value prompted per step, each showing the port's
  // current value with Enter-to-keep, plus a remove option.
  // -------------------------------------------------------------------------
  enum class BConnectEditPhase : std::uint8_t {
    WaitConnName,
    WaitRemoveConfirm, ///< Only reached when the typed connection name exists.
    WaitNominalSize,
    WaitRole,
    WaitEngagement,
    WaitCompatTag,
  } bconnectEditPhase = BConnectEditPhase::WaitConnName;
  std::string bconnectEditConnName;
  std::string bconnectEditNominalSizePending;
  CadBlockConnectionRole bconnectEditRolePending = CadBlockConnectionRole::Inlet;
  float bconnectEditEngagementPending = 0.f;
  std::string bconnectEditCompatTagPending;

  // -------------------------------------------------------------------------
  // BLOCKFITTING wizard (issue #496): one value prompted per step, each showing the current value
  // with Enter-to-keep.
  // -------------------------------------------------------------------------
  enum class BlockFittingPhase : std::uint8_t {
    WaitPartType,
    WaitNominalSize,
    WaitPressureClass,
    WaitPartNumber,
  } blockFittingPhase = BlockFittingPhase::WaitPartType;
  CadPipePartType blockFittingPartTypePending = CadPipePartType::None;
  std::string blockFittingNominalSizePending;
  CadPipePressureClass blockFittingPressureClassPending = CadPipePressureClass::None;
  std::string blockFittingPartNumberPending;

  // -------------------------------------------------------------------------
  // BCONNECTMODE wizard (issue #496): one value prompted per step, instead of a
  // single comma-separated command line.
  // -------------------------------------------------------------------------
  enum class BConnectModePhase : std::uint8_t {
    WaitConnName,
    WaitModeName,
    WaitRemoveConfirm, ///< Only reached when the typed mode name already exists.
    WaitTarget,
    WaitRole,
    WaitEngagement,
    WaitCompatTag,
    WaitIsDefault,
  } bconnectModePhase = BConnectModePhase::WaitConnName;
  std::string bconnectModeConnName;
  std::string bconnectModeModeName;
  bool bconnectModeEditingExisting = false;
  CadConnectionModeTarget bconnectModeTargetPending = CadConnectionModeTarget::GenericPort;
  CadBlockConnectionRole bconnectModeRolePending = CadBlockConnectionRole::Inlet;
  float bconnectModeEngagementPending = 0.f;
  std::string bconnectModeCompatTagPending;

  DrawingGeometrySnapshot blockEditModelStash;
  /// \c cadGpuRevision at the last clean point of the session (enter / BSAVE). A different value
  /// means unsaved edits — drives the BCLOSE Save/Don't-Save/Cancel prompt.
  std::uint32_t blockEditCleanRevision = 0;
  /// \c undoStack size when the session was entered. Session edits push snapshots of the block's
  /// geometry (not the drawing's); those are dropped on close so a later Undo cannot restore block
  /// content into model space.
  std::size_t blockEditUndoMark = 0;
  bool blockEditCloseAsked = false;   ///< UI shows the close modal while true.
  /// Camera to restore when the session closes (the view BEDIT was invoked from).
  double blockEditCamPanX = 0.0, blockEditCamPanY = 0.0, blockEditCamPanZ = 0.0;
  float  blockEditCamZoom = 1.f, blockEditCamAz = 0.f, blockEditCamEl = 90.f, blockEditCamRoll = 0.f;
  std::vector<std::string> blockRecent;
  std::vector<std::string> blockFavorites;
  std::string blockLibraryFilter;
  /// ATTDEF records collected by the last DXF/DWG import. Transient — not saved, not undo.
  std::vector<CadBlockAttrDef> importedDxfAttrDefs;

  /// In-place TABLE cell editor (viewport overlay). Not a command; Enter commits, Esc cancels.
  bool tableCellEditorOpen = false;
  int tableCellEditorIndex = -1;
  int tableCellEditorCell = -1;
  std::string tableCellEditorBuf;
  bool tableCellEditorFocusRequest = false;
