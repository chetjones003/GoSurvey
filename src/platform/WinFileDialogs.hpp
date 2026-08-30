#pragma once

#include <cstddef>

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

bool BrowseOpenFileGsUtf8(char* utf8Out, size_t utf8Cap);

bool BrowseSaveFileGsUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);

bool BrowseOpenFilePdfUtf8(char* utf8Out, size_t utf8Cap);

/// UTF-8 path for saving a plotted PDF; suggests default file name.
bool BrowseSaveFilePdfUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);

/// Autodesk Field Book raw-data file (*.fbk).
bool BrowseOpenFileFbkUtf8(char* utf8Out, size_t utf8Cap);

/// glTF / GLB 3D model for import (REQ-065).
bool BrowseOpenFileGltfUtf8(char* utf8Out, size_t utf8Cap);

/// Block definition import — `.gs`, `.dxf`, or `.dwg` (WBLOCK drawings included).
bool BrowseOpenFileBlockUtf8(char* utf8Out, size_t utf8Cap);
