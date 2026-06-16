#pragma once

#include <cstddef>

/// UTF-8 path output; returns false if cancelled or unavailable (non-Windows).
bool BrowseOpenFileCsvUtf8(char* utf8Out, size_t utf8Cap);

/// UTF-8 path for save; suggests default file name (may include .csv).
bool BrowseSaveFileCsvUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);

bool BrowseOpenFileDxfUtf8(char* utf8Out, size_t utf8Cap);

bool BrowseSaveFileDxfUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);

bool BrowseOpenFileGsUtf8(char* utf8Out, size_t utf8Cap);

bool BrowseSaveFileGsUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);

bool BrowseOpenFilePdfUtf8(char* utf8Out, size_t utf8Cap);

/// UTF-8 path for saving a plotted PDF; suggests default file name.
bool BrowseSaveFilePdfUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8);

/// Autodesk Field Book raw-data file (*.fbk).
bool BrowseOpenFileFbkUtf8(char* utf8Out, size_t utf8Cap);
