  // BCONNECT, BCONNECTEDIT, BLOCKFITTING, and BCONNECTMODE wizard phase and scratch fields live in
  // stepengine::BConnectSession / BConnectEditSession / BlockFittingSession / BConnectModeSession
  // (src/commands/blocks/CadBlocksSessions.hpp).

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
