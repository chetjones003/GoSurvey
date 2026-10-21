#pragma once
// Surface and surface-style edits as services (issue #893, REQ-068 / REQ-069 / REQ-070 / REQ-072 /
// REQ-075 / REQ-124 / REQ-136 / REQ-361).
//
// The Create Surface, Surface Properties, Surfaces and Surface Style panels apply their
// edits inside their own draw code. A second frontend cannot reuse widgets, so the rules those
// widgets enforce live here instead: what is refused (and the exact message), which value is stored,
// and what one undo step covers. Both frontends call these; neither re-implements them.
//
// **Undo.** Every mutating function validates first and pushes ONE undo snapshot only when it is
// about to change something, so a refusal never leaves an empty undo step. The style functions take
// \c recordUndo = false so a dialog that edits a working copy can record the whole session as a
// single step itself (Qt Surface Style dialog).

#include "CadCommands.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace surfedit {

/// "Surface", then "Surface (2)", "Surface (3)" ... — the first name no surface uses.
[[nodiscard]] std::string NextSurfaceName(const AppCommandState& cmd);

/// Lowest and highest vertex elevation of a built surface; false when it has no triangulation.
[[nodiscard]] bool ElevationRange(const CadSurface& s, double* lo, double* hi);

// --- Create Surface (Toolspace ▸ Create Surface...) -------------------------------------------------

enum class CreateKind : std::uint8_t { Tin = 0, Grid, Corridor, TinVolume, GridVolume };

struct CreateRequest {
  CreateKind kind = CreateKind::Tin;
  std::string name;
  std::string description;
  std::string styleName;
  std::string layer;  ///< The layer picked in the dialog; the Object Layers default when unchanged.
  double gridOriginX = 0.0, gridOriginY = 0.0, gridSpacingX = 10.0, gridSpacingY = 10.0;
  int gridCols = 2, gridRows = 2;
  std::string volumeBase, volumeComparison;  ///< Volume kinds only.
};

/// Creates the surface. Returns an empty string on success, otherwise the message the dialog shows
/// beside OK (nothing is created and the drawing is untouched). \p log receives what the creating
/// command reported; its last line becomes the message when the command refused without a reason.
[[nodiscard]] std::string CreateSurface(AppCommandState& cmd, const CreateRequest& req,
                                        std::vector<std::string>& log);

// --- Surface information and style (Surface Properties ▸ Information, Surfaces ▸ right panel) -------

/// Surface Properties OK / Apply: name, description and style in one undo step. False (and logged)
/// when the name is empty or already taken.
bool ApplySurfaceInfo(AppCommandState& cmd, int si, const std::string& name, const std::string& description,
                      const std::string& styleName, std::vector<std::string>& log);

/// Surfaces ▸ Name. False (and logged) when the name is empty or already taken.
bool RenameSurface(AppCommandState& cmd, int si, const std::string& newName, std::vector<std::string>& log);

/// Surfaces ▸ Style combo.
void AssignSurfaceStyle(AppCommandState& cmd, int si, const std::string& styleName, std::vector<std::string>& log);

/// Surfaces ▸ Rebuild.
void RebuildSurface(AppCommandState& cmd, int si, std::vector<std::string>& log);

/// Surfaces ▸ Delete — one undo step (REQ-068).
void DeleteSurface(AppCommandState& cmd, int si, std::vector<std::string>& log);

/// Surfaces ▸ New surface / New from group... Returns the new index, or -1 (name taken, build empty).
int CreateFromPointGroups(AppCommandState& cmd, const std::string& name, const std::vector<std::string>& groups,
                          std::vector<std::string>& log);

/// Surfaces ▸ New volume surface... Returns the new index, or -1.
int CreateVolumeFromParents(AppCommandState& cmd, const std::string& name, const std::string& baseName,
                            const std::string& comparisonName, std::vector<std::string>& log);

// --- Definition (Surfaces ▸ tree, Surface Properties ▸ Definition) -----------------------------------

enum class DefinitionKind : std::uint8_t { PointGroup = 0, Breakline, Boundary, PointFile };

/// Removes definition item \p index. False when the surface or index no longer exists.
bool RemoveDefinitionItem(AppCommandState& cmd, int si, DefinitionKind kind, size_t index,
                          std::vector<std::string>& log);

/// Moves a breakline or boundary one place (\p delta = -1 / +1). Order is meaningful for boundaries
/// (REQ-069: applied in definition order). False when it would leave the list.
bool MoveDefinitionItem(AppCommandState& cmd, int si, DefinitionKind kind, size_t index, int delta,
                        std::vector<std::string>& log);

/// Adds a point group by name. False when the surface is gone or already has it.
bool AddPointGroup(AppCommandState& cmd, int si, const std::string& groupName, std::vector<std::string>& log);

/// Links a point file (REQ-086) through SURFACEADDFILE, so the window and the command cannot disagree
/// about what linking means. \p layoutName is PNEZD / PENZD / NEZ / ENZ.
void AddPointFile(AppCommandState& cmd, int si, const std::string& path, const std::string& layoutName,
                  bool firstRowIsHeader, std::vector<std::string>& log);

/// Breaks the link of point file \p fileIndex and imports its points (SURFACEIMPORTFILE).
void ImportPointFile(AppCommandState& cmd, int si, size_t fileIndex, std::vector<std::string>& log);

/// One row of the Surface Properties ▸ Definition operations list. \c kind groups rows that may be
/// reordered among themselves (0 group, 1 file, 2 breakline, 3 contour, 4 boundary, 5 swap, 6 added
/// point, 7 deleted point).
struct DefinitionRow {
  const char* type = "";
  std::string param;
  int kind = 0;
  size_t index = 0;
};
[[nodiscard]] std::vector<DefinitionRow> DefinitionRows(const CadSurface& s);

/// True when row \p sel can move by \p delta without crossing into another kind.
[[nodiscard]] bool CanMoveDefinitionRow(const std::vector<DefinitionRow>& rows, int sel, int delta);

/// Moves row \p sel by \p delta (one undo step). False when it cannot move.
bool MoveDefinitionRow(AppCommandState& cmd, int si, const std::vector<DefinitionRow>& rows, int sel, int delta);

// --- Analysis (Surface Properties ▸ Analysis) ------------------------------------------------------

/// Sets the analysis mode on this surface's style, first copying the style when others share it.
void SetAnalysisMode(AppCommandState& cmd, int si, SurfaceAnalysisMode mode, std::vector<std::string>& log);

/// Run: replaces the range table with \p count equal elevation bands (1..64) and selects Elevation.
void RunElevationBands(AppCommandState& cmd, int si, int count, std::vector<std::string>& log);

// --- Surface styles (Surface Style editor) ---------------------------------------------------------

/// \p s without leading/trailing spaces and tabs.
[[nodiscard]] std::string TrimStyleName(const std::string& s);

/// Renames style \p idx and re-points every surface that named it. False (and logged) when the name is
/// empty or taken.
bool RenameStyle(AppCommandState& cmd, size_t idx, const std::string& newName, std::vector<std::string>& log,
                 bool recordUndo = true);

/// Adds a copy of style \p copyFrom called \p name. False when the name is empty or taken.
bool AddStyle(AppCommandState& cmd, size_t copyFrom, const std::string& name, std::vector<std::string>& log,
              bool recordUndo = true);

/// Deletes style \p idx ("Standard" is refused). Surfaces keep the name and fall back to Standard
/// (REQ-070).
bool DeleteStyle(AppCommandState& cmd, size_t idx, std::vector<std::string>& log, bool recordUndo = true);

/// Sets the contour intervals; the caller has already checked them with
/// SurfaceStyles::IntervalsCompatible.
void SetStyleIntervals(AppCommandState& cmd, size_t idx, double minorFt, double majorFt, bool recordUndo = true);

}  // namespace surfedit
