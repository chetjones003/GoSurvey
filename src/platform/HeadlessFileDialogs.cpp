// Headless implementation of the native file-dialog seam (REQ-203 / ADR-031 (b′)).
//
// This is the SAME header the windowed application implements in WinFileDialogs.cpp; which one a
// program gets is decided by which target links it. That is the whole mechanism — no interface, no
// injection, no virtual dispatch. A platform header exists precisely so a second platform can
// answer it, and "no desktop" is a platform.
//
// A dialog must never open here: a modal window in a fuzz run is an infinite hang, not a failure,
// and a hang is the one outcome a harness cannot triage. So every function answers from a queue the
// transcript fills (`DIALOG OPEN <path>` / `DIALOG SAVE <path>` / `DIALOG CANCEL`) and returns
// false — a cancelled dialog — once the queue is empty.
//
// Of the twelve functions in the header, two (`BrowseOpenFileGltfUtf8`, `BrowseOpenFileBlockUtf8`)
// are reachable from the Commands layer; the rest are called from the UI, which headless does not
// link. They are all implemented anyway, because a seam that covers most of a header is a link
// error waiting for whichever command grows a new call.

#include "WinFileDialogs.hpp"

#include "HeadlessFileDialogs.hpp"

#include <cstring>
#include <deque>
#include <string>

namespace {

std::deque<headless::DialogAnswer>& Queue() {
  static std::deque<headless::DialogAnswer> q;
  return q;
}

/// Copy \p s into the caller's buffer, always NUL-terminating. Returns false if it would not fit,
/// which is reported rather than truncated: a silently shortened path names a different file
/// (REQ-201).
bool Emit(const std::string& s, char* out, size_t cap) {
  if (!out || cap == 0)
    return false;
  if (s.size() + 1 > cap)
    return false;
  std::memcpy(out, s.c_str(), s.size() + 1);
  return true;
}

/// Pop the next queued answer. Returns false when the queue is empty (== the user cancelled) or
/// when the queued answer was an explicit CANCEL.
bool NextAnswer(char* out, size_t cap) {
  auto& q = Queue();
  if (q.empty())
    return false;
  const headless::DialogAnswer a = q.front();
  q.pop_front();
  if (a.cancelled)
    return false;
  return Emit(a.path, out, cap);
}

}  // namespace

namespace headless {

void QueueDialogAnswer(const std::string& path) {
  Queue().push_back(DialogAnswer{path, false});
}

void QueueDialogCancel() {
  Queue().push_back(DialogAnswer{std::string(), true});
}

void ClearDialogAnswers() {
  Queue().clear();
}

size_t PendingDialogAnswers() {
  return Queue().size();
}

}  // namespace headless

// --- The seam itself --------------------------------------------------------------------------
// Save dialogs ignore the suggested name for the same reason open dialogs ignore the filter: the
// transcript already said exactly which path it wants, and a harness that second-guesses it would
// write somewhere the transcript cannot predict or clean up.

bool BrowseOpenFileCsvUtf8(char* utf8Out, size_t utf8Cap) { return NextAnswer(utf8Out, utf8Cap); }

bool BrowseSaveFileCsvUtf8(char* utf8Out, size_t utf8Cap, const char*) {
  return NextAnswer(utf8Out, utf8Cap);
}

bool BrowseOpenFileDxfUtf8(char* utf8Out, size_t utf8Cap) { return NextAnswer(utf8Out, utf8Cap); }

bool BrowseSaveFileDxfUtf8(char* utf8Out, size_t utf8Cap, const char*) {
  return NextAnswer(utf8Out, utf8Cap);
}

bool BrowseOpenFileDwgUtf8(char* utf8Out, size_t utf8Cap) { return NextAnswer(utf8Out, utf8Cap); }

bool BrowseSaveFileDwgUtf8(char* utf8Out, size_t utf8Cap, const char*) {
  return NextAnswer(utf8Out, utf8Cap);
}

bool BrowseOpenFileGstUtf8(char* utf8Out, size_t utf8Cap) { return NextAnswer(utf8Out, utf8Cap); }

bool BrowseOpenFilePdfUtf8(char* utf8Out, size_t utf8Cap) { return NextAnswer(utf8Out, utf8Cap); }

bool BrowseSaveFilePdfUtf8(char* utf8Out, size_t utf8Cap, const char*) {
  return NextAnswer(utf8Out, utf8Cap);
}

bool BrowseOpenFileFbkUtf8(char* utf8Out, size_t utf8Cap) { return NextAnswer(utf8Out, utf8Cap); }

bool BrowseOpenFileGltfUtf8(char* utf8Out, size_t utf8Cap) { return NextAnswer(utf8Out, utf8Cap); }

bool BrowseOpenFileBlockUtf8(char* utf8Out, size_t utf8Cap) { return NextAnswer(utf8Out, utf8Cap); }
