#pragma once

// REQ-392 — compare two revisions of a sheet inside a PDF viewer window: the revision laid over the base in
// Tint, Opacity, or either sheet alone with its changes marked, lined up by hand with one or two matching points. The images are made on a worker
// thread and uploaded a slice per frame, so panning and zooming never wait on them.

#include "PdfAlign.hpp"
#include "PdfDiff.hpp"
#include "PdfDocument.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace pdfview {

class PdfCompare {
public:
  /// \p base must outlive this object. The revision opens on a worker; the view shows "Opening..." until it is ready.
  PdfCompare(PdfDocument* base, const std::string& baseTitle, int basePage, const std::filesystem::path& revPath);
  ~PdfCompare();
  PdfCompare(const PdfCompare&) = delete;
  PdfCompare& operator=(const PdfCompare&) = delete;

  /// Draw the comparison's bar and sheet inside the current window. Returns false once the user closed it.
  bool Draw(std::vector<std::string>& log);

  /// `BENCH PDFCOMPARE`: zoom and pan the overlay for a fixed run and report the viewer's cost per frame.
  /// \p deleteRevAfter removes the revision file (a generated one) when the comparison is destroyed.
  void StartBench(int pages, bool deleteRevAfter);
  /// `BENCH PDFDIFF`: align a generated 36 x 24 in sheet pair automatically, find the changes, and report the time and
  /// the worst viewer frame while it ran.
  void StartDiffBench(bool deleteRevAfter);
  bool BenchFinished() const { return benchDone_; }

  /// Free textures queued for deletion. Call at the start of a frame, and once more before the GL context goes.
  static void DrainGraveyard();

private:
  struct Key {
    int scaleKey = 0;
    int version = 0;
    int basePage = 0;
    int revPage = 0;
    bool raw = false;
    bool operator==(const Key& o) const {
      return scaleKey == o.scaleKey && version == o.version && basePage == o.basePage && revPage == o.revPage && raw == o.raw;
    }
  };
  struct JobOut {
    bool ok = false;
    Key key;
    int n = 0;
    Bitmap img[5]; ///< overlay: base, aligned revision, tint, base-only ink (blue), revision-only ink (red). raw: the revision page alone
    float wPt = 0.f, hPt = 0.f;
  };
  struct Set {
    Key key;
    int n = 0;
    unsigned tex[5] = {0, 0, 0, 0, 0};
    Bitmap img[5];
    int rowsDone[5] = {0, 0, 0, 0, 0};
    float wPt = 0.f, hPt = 0.f;
    bool Complete() const;
  };
  enum class Mode { Tint, Opacity, Base, Revision };
  enum class Task { None, Align, Find };
  struct AnaOut {
    Task task = Task::None;
    bool ok = false;
    bool cancelled = false;
    pdfalign::AutoResult align;
    pdfdiff::Result found;
    double ms = 0;
  };
  enum class Pick { None, OneBase, OneRev, TwoBase1, TwoRev1, TwoBase2, TwoRev2 };

  void PollOpen(std::vector<std::string>& log);
  void PumpJob();
  void UploadSlice();
  void DrawBar(bool& keepOpen);
  void DrawSheet(std::vector<std::string>& log);
  bool WantRaw() const { return pick_ == Pick::OneRev || pick_ == Pick::TwoRev1 || pick_ == Pick::TwoRev2; }
  void BeginPick(Pick p);
  void PumpAnalysis(std::vector<std::string>& log);
  void StartTask(Task t);
  void DrawChangesBar();
  void DrawChangesList();
  void SaveMarkups();
  void ClearChanges();
  void TakePick(pdfalign::Pt p);
  static void Retire(Set& s);

  PdfDocument* base_;
  std::string baseTitle_, revTitle_;
  int basePage_ = 0, revPage_ = 0;
  std::filesystem::path revPath_;
  std::future<PdfDocument::OpenResult> opening_;
  std::unique_ptr<PdfDocument> rev_;
  std::string error_;

  pdfalign::Transform xf_;
  int version_ = 0;
  std::string alignNote_ = "Not adjusted: the two pages are laid on each other at their lower-left corners.";
  Pick pick_ = Pick::None;
  pdfalign::Pt picks_[4];
  int pickCount_ = 0;

  // REQ-393: automatic alignment and change detection (one analysis at a time, on a worker, cancellable, with progress)
  std::future<AnaOut> ana_;
  bool anaRunning_ = false;
  Task anaTask_ = Task::None;
  std::atomic<bool> anaCancel_{false};
  std::atomic<float> anaProgress_{0.f};
  int anaBasePage_ = 0, anaRevPage_ = 0, anaVersion_ = 0;
  bool alignWanted_ = true; ///< run the automatic alignment as soon as the revision is open (and after a page change)
  bool autoMatched_ = false;
  double autoConfidence_ = 0.0;
  pdfalign::Transform autoXf_;
  bool lowConfidence_ = false;
  pdfdiff::Settings diffSettings_;
  std::vector<pdfdiff::Region> regions_;
  bool haveChanges_ = false;
  int selRegion_ = -1;
  float listW_ = 290.f; ///< the change list's width, dragged by its edge like the thumbnail strip
  int centerReq_ = -1;
  bool showHighlights_ = true;
  std::future<std::string> saving_; ///< Write changes as markups, on a worker
  std::string savedNote_;

  Mode mode_ = Mode::Tint;
  float opacity_ = 0.5f;

  float pxPerPt_ = 96.f / 72.f;
  bool panning_ = false; ///< the middle button is held on the sheet
  float pendX_ = -1.f, pendY_ = -1.f;
  bool fitPending_ = true;

  std::future<JobOut> job_;
  std::atomic<bool> cancel_{false};
  bool jobRunning_ = false;
  Key jobKey_;
  Set cur_;
  bool haveCur_ = false;
  Set pending_;
  bool havePending_ = false;
  Key failedKey_;
  bool hasFailed_ = false; ///< a render of failedKey_ failed: not asked for again
  std::string status_;

  // bench
  bool bench_ = false;
  bool diffBench_ = false;
  bool diffBenchFinding_ = false;
  double diffBenchAlignMs_ = 0;
  double diffBenchWorstFrameMs_ = 0;
  bool benchDone_ = false;
  bool deleteRev_ = false;
  int benchPages_ = 0;
  int benchFrame_ = 0;
  std::chrono::steady_clock::time_point benchT0_;
  double firstShownMs_ = -1;
  std::vector<double> costMs_;
};

} // namespace pdfview
