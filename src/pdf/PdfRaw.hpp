#pragma once

// REQ-390 / ADR-067 addendum 2 — a small reader/editor for the *plain* object text of a PDF that PDFium has just
// written (PDFium rewrites a file with ordinary, uncompressed objects). PDFium's public API cannot read or write
// a page's /VP (Viewport) entry, so page scale is read and written on these bytes. Pure text: no PDFium, no I/O.

#include <map>
#include <string>
#include <vector>

namespace pdfview::raw {

struct Span {
  size_t start = 0; ///< first byte of "<n> <g> obj"
  size_t body = 0;  ///< first byte after "obj"
  size_t end = 0;   ///< first byte of "endobj"
};

/// Every "<n> <g> obj ... endobj" in \p bytes (a later copy of an object number wins).
std::map<int, Span> ScanObjects(const std::string& bytes);

/// The text between "obj" and "endobj" of object \p num, or "" when there is no such object.
std::string Body(const std::string& bytes, const std::map<int, Span>& objs, int num);

/// "<n> <g> R" at \p i (leading whitespace allowed): fills \p num and the index after the R.
bool ParseRef(const std::string& s, size_t i, int& num, size_t& after);

/// The start of the value of key \p key (like "/VP") in dictionary text, or npos. "/Pages" is not "/Page".
size_t FindKey(const std::string& s, const char* key, size_t from = 0);

/// Index just past the dictionary (<<...>>) or array ([...]) that starts at \p pos, strings skipped;
/// npos when it never closes.
size_t SkipValue(const std::string& s, size_t pos);

/// The object numbers of the pages, in page order, walking /Root -> /Pages -> /Kids. False when the tree
/// cannot be followed (the reason is in \p why).
bool PageObjects(const std::string& bytes, const std::map<int, Span>& objs, std::vector<int>& pages, std::string& why);

/// \p text with every "<n> <g> R" replaced by that object's body (up to \p depth levels), so a dictionary
/// that points at others can be read as one piece.
std::string Expand(const std::string& bytes, const std::map<int, Span>& objs, const std::string& text, int depth = 3);

/// Appends a fresh classic cross-reference table and trailer built from the object positions actually in
/// \p bytes, so the file stays valid after object text changed length. "" when it cannot (an encrypted file,
/// or no objects found).
std::string AppendXref(const std::string& bytes);

} // namespace pdfview::raw
