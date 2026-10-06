#pragma once

// REQ-379 (issue #696 P6) — tracked files at the drawing level: the copy-or-link question when a point
// cloud or PDF is attached in a project drawing, recording what a saved drawing holds, putting it back
// when the drawing opens, and the Project Health numbers for the open tabs.
//
// The file rules themselves (where a copy goes, what is recorded, what is missing) are the pure
// io/ProjectFiles; this is the glue that reads and writes AppCommandState. No window here.

#include "CadCommands.hpp"
#include "io/ProjectFiles.hpp"

#include <string>
#include <vector>

/// Clause 1. When the ACTIVE tab is a writable project drawing and \p path lies outside the project
/// folder, queues the Copy / Link / Cancel question (`st.projectAttachPrompt`) and returns true: the
/// caller must stop and wait for the answer. False (nothing queued) for a standalone drawing, a
/// read-only project, a file already inside the project, or an unreadable file — the caller carries on
/// exactly as it did before projects existed.
bool RequestProjectAttach(AppCommandState& st, AppCommandState::ProjectAttachPrompt::Kind kind,
                          const std::string& path, std::vector<std::string>& log);

/// The user's answer. Copy puts the file in the project's folder for its kind (reusing an identical
/// earlier copy); Link keeps it where it is and says it will not travel. On success \p finalPath is the
/// file to attach and true is returned; on failure (REQ-201 in \p log) false, and nothing is attached.
bool ResolveProjectAttach(AppCommandState& st, bool copy, std::vector<std::string>& log, std::string* finalPath);

/// REQ-379 clause 5: Add PDF to project. A PDF already inside the project is tracked at once; one outside
/// raises the copy/link prompt (kind PdfTrack) and is tracked by TrackProjectFile once answered.
/// Returns true when a prompt was raised or the file was tracked.
bool AddPdfToProject(AppCommandState& st, const std::string& path, std::vector<std::string>& log);

/// REQ-379 clause 6: Refresh. Looks for new files in the active drawing's project and, if there are any,
/// raises projectRefreshPrompt (nothing is tracked until the user answers). Logs what it found.
bool RefreshProjectFiles(AppCommandState& st, std::vector<std::string>& log);

/// Tracks the project-relative \p rels in project \p uid (one save). Returns how many were newly tracked.
int TrackProjectFiles(AppCommandState& st, std::uint32_t uid, const std::vector<std::string>& rels,
                      std::vector<std::string>& log);

/// Tracks \p path in project \p uid (no drawing) and saves the .gsproj. False when nothing changed.
bool TrackProjectFile(AppCommandState& st, std::uint32_t uid, const std::string& path, std::vector<std::string>& log);

/// Clause 2. Called after the drawing of tab \p tabIdx was saved to \p savedPath, while that drawing's
/// data is loaded in \p st: records, in the project file, what the drawing now holds (point clouds, PDFs
/// and their placements). A no-op for a standalone drawing, a read-only project, or a drawing saved
/// outside the project folder.
void SyncProjectFilesOnSave(AppCommandState& st, int tabIdx, const std::string& savedPath,
                            std::vector<std::string>& log);

/// Clause 2. Called once on the ACTIVE tab right after \p drawingPath was opened into project
/// \p projectUid: points each point cloud at the file the project tracks for it, re-places the PDFs
/// recorded for the drawing, and reports (REQ-201) any tracked file that is missing. The drawing opens
/// either way. Nothing is marked as changed.
void ApplyProjectFilesOnOpen(AppCommandState& st, std::uint32_t projectUid, const std::string& drawingPath,
                             std::vector<std::string>& log);

/// Clause 4. The project's health, including the open drawings with unsaved changes.
projfiles::Health ProjectHealthFor(const AppCommandState& st, std::uint32_t projectUid);

/// Clause 4. "Copy links into the project" for project \p projectUid, then saves the .gsproj. Returns
/// how many links became in-project; failures are in \p log. False when the project cannot be written.
bool CopyProjectLinksIn(AppCommandState& st, std::uint32_t projectUid, std::vector<std::string>& log,
                        size_t* converted);

/// REQ-381 (#696 P8). Writes a turnover record for project \p projectUid listing \p chosen (tracked item
/// paths) for \p recipient, after Project Health (\p acknowledged = the user accepted its problems), and
/// saves the .gsproj. Reports the outcome in \p log (REQ-201). False when refused or not writable.
bool CreateProjectTurnover(AppCommandState& st, std::uint32_t projectUid, const std::string& recipient,
                           const std::vector<std::string>& chosen, bool acknowledged,
                           std::vector<std::string>& log);
