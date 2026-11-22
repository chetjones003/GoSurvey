/// Copy currently selected CAD entities into \p st.clipboard.  Clears any previous clipboard content.
void CopySelectionToClipboard(AppCommandState& st, std::vector<std::string>& log);
/// Cut (issue #805): copy the selection, then delete it. Refuses with nothing changed when the clipboard
/// cannot carry part of the selection (for example a surface, a feature line, a mesh, a section plane,
/// or a paper viewport). The allow-lists in the .cpp file name every type the clipboard carries.
void CutSelectionToClipboard(AppCommandState& st, std::vector<std::string>& log);
/// Begin PASTE — show cursor-following preview; next viewport click places the clipboard contents.
void StartPasteCommand(AppCommandState& st, std::vector<std::string>& log);
/// Commit a PASTE: place the clipboard at (x,y) in the ACTIVE space's coordinates (world for model, paper
/// inches for a paper layout). Routes the write + new selection by active space (REQ-038, ADR-013).
void CommitClipboardPasteAt(AppCommandState& st, double x, double y, std::vector<std::string>& log);
void ResolveClipboardPastePointConflict(AppCommandState& st, AppCommandState::PointEditPrompt::Answer answer,
                                        std::vector<std::string>& log);
/// Immediately paste clipboard contents at their original (stored) coordinates without interaction.
void StartPasteOrigCommand(AppCommandState& st, std::vector<std::string>& log);
