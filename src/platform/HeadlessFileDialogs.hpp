#pragma once

// Queue backing the headless file-dialog seam (REQ-203 / ADR-031 (b′)).
//
// The transcript driver fills this queue from `DIALOG OPEN <path>` / `DIALOG SAVE <path>` /
// `DIALOG CANCEL` lines; the WinFileDialogs.hpp implementation in HeadlessFileDialogs.cpp drains it.
//
// A QUEUE rather than a mode, deliberately: a transcript that opens two files in a row needs two
// different answers, and a single "always return this path" setting cannot give them.

#include <cstddef>
#include <string>

namespace headless {

struct DialogAnswer {
  std::string path;
  bool cancelled = false;
};

/// Queue a path for the next Browse* call.
void QueueDialogAnswer(const std::string& path);

/// Queue an explicit cancellation for the next Browse* call (the dialog returns false).
void QueueDialogCancel();

/// Drop every queued answer — called between transcripts so one run cannot leak into the next.
void ClearDialogAnswers();

/// How many answers remain unconsumed. The driver reports a non-zero count at end of transcript:
/// a queued answer nothing asked for means the transcript and the code disagree about what a
/// command does, which is worth knowing even when the run passes.
size_t PendingDialogAnswers();

}  // namespace headless
