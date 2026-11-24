/// REQ-171/172, ADR-060 — starts a background `.gscloud` build + preview-sample read for the
/// E57/PTS/PTX/LAS/LAZ point cloud at \p path (format decided from the extension — see
/// `PointCloudImportAsync::SourceFormat`). Refuses (logs why, returns false) if an import is
/// already in flight. Call once from the single `POINTCLOUDATTACH` command handler (the sole
/// import entry point for every point-cloud format); \ref TickPointCloudImport reaps the result.
bool StartPointCloudImportAsync(AppCommandState& st, const std::string& path,
                                std::vector<std::string>& log);

/// Call once per frame (main.cpp, beside \ref TickSurfaceRebuilds). Reaps a completed point-cloud
/// import — on success, commits the resulting `CadPointCloud` into `st.drawing.cadPointClouds` (undo
/// snapshot, entity id, GPU-cache bump) exactly as the old synchronous `ImportPointCloudE57` did;
/// on failure or cancellation, logs the reason and commits nothing. A no-op while no import is in
/// flight or the in-flight one has not finished yet.
void TickPointCloudImport(AppCommandState& st, std::vector<std::string>& log);

/// What a progress display shows for an in-flight import (REQ-172,
/// ADR-060): the build's three real phases weighted by rough cost, and an ETA recomputed at most
/// once a second.
struct PointCloudImportProgress {
  /// "Reading scan..." / "Building spatial index..." / "Sampling preview...".
  const char* phase = "Reading scan...";
  /// 0..1, or negative when the point total is unknown (indeterminate bar).
  float fraction = -1.f;
  /// "estimating..." or "~2m 30s remaining"; empty when the total is unknown.
  std::string eta;
};
PointCloudImportProgress UpdatePointCloudImportProgress(AppCommandState::PointCloudImportAsync& job);

/// Which writer an export uses. REQ-172 delivers export for PTS, PTX, LAS and LAZ; E57 is
/// import-only (no writer exists).
enum class PointCloudExportFormat : std::uint8_t { Pts, Ptx, Las, Laz };

/// What `EXPORTPTS` / `EXPORTPTX` / `EXPORTLAS` / `EXPORTLAZ` will write: the selected point
/// clouds, or every cloud in the drawing when none is selected (REQ-172).
struct PointCloudExportPlan {
  int cloudCount = 0;
  /// True when at least one cloud is selected, false when the export covers every cloud.
  bool fromSelection = false;
  /// Sum of the clouds' full point counts (not their bounded preview samples).
  std::int64_t pointCount = 0;
  /// Suggested file name: the cloud's own stem for one cloud, `pointclouds.<ext>` for several.
  std::string defaultFileName;
};
PointCloudExportPlan PlanPointCloudExport(const AppCommandState& st, PointCloudExportFormat format);

/// Writes the planned point clouds to \p pathUtf8 in \p format, logging the outcome in the same
/// words as the typed commands. Returns false (drawing and file untouched on a refusal) if there
/// is nothing to export or the writer fails. Not a drawing edit, so there is nothing to undo.
bool ExportPointCloudsToFile(AppCommandState& st, PointCloudExportFormat format,
                             const std::string& pathUtf8, std::vector<std::string>& log);

/// EXTRACTCENTERLINE (REQ-347): re-evaluates the hover's cylinder-axis fit against every visible
/// point cloud's bounded preview sample under \p ray, writing the result into the active
/// \c stepengine::ExtractCenterlineSession (or clearing it when no acceptable fit is found —
/// too few nearby points, or the residual gate rejects the neighborhood). \p ray must be the same
/// ray the viewport's own hover/click seam already computed (REQ-058), exactly like every other
/// hover-driven command in this file.
void StartExtractCenterlineCommand(AppCommandState& st, std::vector<std::string>& log);
void UpdateExtractCenterlineHover(AppCommandState& st, const ray3d::Ray& ray);
/// Commits the hover's current fit (if valid) as a LINE entity, one undo step. Logs and leaves the
/// command running, with no entity created, when the hover has no valid fit — the same "miss, try
/// again" shape HATCH's own click handler uses.
void SubmitExtractCenterlineViewportPick(AppCommandState& st, std::vector<std::string>& log);
