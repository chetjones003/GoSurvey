#pragma once

#include <cstddef>
#include <string>
#include <vector>

/// Point file for import — `.csv` **or** `.txt`, which REQ-083 treats as one format. UTF-8 path
/// output; returns false if cancelled or unavailable (non-Windows). The `Csv` in the name is
/// historical: it opens both, and the extension carries no meaning to the importer.
bool BrowseOpenFileCsvUtf8(char* utf8Out, size_t utf8Cap);

/// UTF-8 path for saving a point file; suggests a default file name (may include `.csv`). REQ-083:
/// a name typed with `.csv` or `.txt` is returned as typed; a name with neither picks up the
/// extension of the filter the user chose.
bool BrowseSaveFileCsvUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);

bool BrowseOpenFileDxfUtf8(char* utf8Out, size_t utf8Cap);

bool BrowseSaveFileDxfUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);

bool BrowseOpenFileDwgUtf8(char* utf8Out, size_t utf8Cap);

bool BrowseSaveFileDwgUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);

/// GoSurvey workspace-template (.gst) picker for the Settings > Files startup-template setting.
/// Not a general drawing-document format — see issue #264.
bool BrowseOpenFileGstUtf8(char* utf8Out, size_t utf8Cap);

/// REQ-374 (#696): a project file (`.gsproj`) for Open Project.
bool BrowseOpenFileGsprojUtf8(char* utf8Out, size_t utf8Cap);

/// REQ-380 (#696 P7): a project pack (`.gspack`) to open, and where to save one.
bool BrowseOpenFileGspackUtf8(char* utf8Out, size_t utf8Cap);
bool BrowseSaveFileGspackUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);

/// REQ-374 (#696): a folder picker (New Project location). UTF-8 path; false if cancelled.
bool BrowseFolderUtf8(char* utf8Out, size_t utf8Cap);

bool BrowseOpenFilePdfUtf8(char* utf8Out, size_t utf8Cap);

/// UTF-8 path for saving a plotted PDF; suggests default file name.
bool BrowseSaveFilePdfUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);

/// UTF-8 path for saving a plain-text report (REQ-423); suggests default file name.
bool BrowseSaveFileTxtUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);

/// UTF-8 path for saving an .rtf report (REQ-423); suggests default file name.
bool BrowseSaveFileRtfUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);

/// Autodesk Field Book raw-data file (*.fbk).
bool BrowseOpenFileFbkUtf8(char* utf8Out, size_t utf8Cap);

/// MicroSurvey STAR*NET data file for import (issue #846, Phase B).
bool BrowseOpenFileStarNetUtf8(char* utf8Out, size_t utf8Cap);

/// Carlson SurvCE/SurvPC raw data file (*.rw5) for import (issue #846, Phase B).
bool BrowseOpenFileRw5Utf8(char* utf8Out, size_t utf8Cap);

/// Trimble JobXML (*.jxl / *.xml) for import (issue #846, Phase B).
bool BrowseOpenFileJobXmlUtf8(char* utf8Out, size_t utf8Cap);

/// Multi-select variants of the four raw-data pickers above (issue #848 follow-up):
/// a .gsdat file can hold the merged legs of several field-day raw files, so the
/// LSA Editor's Import actions let the user pick more than one file of the same
/// format in a single dialog. Returns false (outPaths left empty) if cancelled or
/// unavailable (non-Windows); outPaths is cleared on every call.
bool BrowseOpenFilesFbkUtf8(std::vector<std::string>& outPaths);
bool BrowseOpenFilesStarNetUtf8(std::vector<std::string>& outPaths);
bool BrowseOpenFilesRw5Utf8(std::vector<std::string>& outPaths);
bool BrowseOpenFilesJobXmlUtf8(std::vector<std::string>& outPaths);

/// REQ-403 (#834): a Traverse Editor save file (`.gstrav`) to load.
bool BrowseOpenFileGstravUtf8(char* utf8Out, size_t utf8Cap);

/// REQ-403 (#834): where to save a Traverse Editor session (`.gstrav`).
bool BrowseSaveFileGstravUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);

/// LSA Editor command-text document (`.gsdat`) to open — the GSTC source text
/// itself, as a plain file, distinct from the structured `.gstrav` save format.
bool BrowseOpenFileGsdatUtf8(char* utf8Out, size_t utf8Cap);

/// Where to save a `.gsdat` command-text document.
bool BrowseSaveFileGsdatUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);

/// glTF / GLB 3D model for import (REQ-065).
bool BrowseOpenFileGltfUtf8(char* utf8Out, size_t utf8Cap);

/// Block definition import — `.dxf` or `.dwg` (WBLOCK drawings included).
bool BrowseOpenFileBlockUtf8(char* utf8Out, size_t utf8Cap);

/// E57 point-cloud scan for import (REQ-172). PTS/LAS/LAZ are future increments (D-2026-09-17-d
/// delivers E57 first, then PTS -> PTX -> LAS -> LAZ) — not offered here yet.
bool BrowseOpenFileE57Utf8(char* utf8Out, size_t utf8Cap);

/// PTX (Cyclone-style ASCII, per-scan-setup) point-cloud scan for import (REQ-172, PTX increment,
/// issue #825 increment 2).
bool BrowseOpenFilePtxUtf8(char* utf8Out, size_t utf8Cap);

/// UTF-8 path for exporting the selected point cloud(s) to PTX; suggests a default file name.
bool BrowseSaveFilePtxUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);
