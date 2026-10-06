#pragma once

// REQ-392 — compare two revisions of a sheet inside a PDF viewer window: the revision laid over the base in
// Tint, Opacity or Blink, lined up by hand with one or two matching points. The images are made on a worker
// thread and uploaded a slice per frame, so panning and zooming never wait on them.

#include "PdfAlign.hpp"
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
    Bitmap img[3]; ///< overlay: base, aligned revision, tint. raw: the revision page alone
    float wPt = 0.f, hPt = 0.f;
  };
  struct Set {
    Key key;
    int n = 0;
    unsigned tex[3] = {0, 0, 0};
    Bitmap img[3];
    int rowsDone[3] = {0, 0, 0};
    float wPt = 0.f, hPt = 0.f;
    bool Complete() const;
  };
  enum class Mode { Tint, Opacity, Blink };
  enum class Pick { None, OneBase, OneRev, TwoBase1, TwoRev1, TwoBase2, TwoRev2 };

  void PollOpen(std::vector<std::string>& log);
  void PumpJob();
  void UploadSlice();
  void DrawBar(bool& keepOpen);
  void DrawSheet(std::vector<std::string>& log);
  bool WantRaw() const { return pick_ == Pick::OneRev || pick_ == Pick::TwoRev1 || pick_ == Pick::TwoRev2; }
  void BeginPick(Pick p);
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

  Mode mode_ = Mode::Tint;
  float opacity_ = 0.5f;
  float blinkHz_ = 1.5f;

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
  bool benchDone_ = false;
  bool deleteRev_ = false;
  int benchPages_ = 0;
  int benchFrame_ = 0;
  std::chrono::steady_clock::time_point benchT0_;
  double firstShownMs_ = -1;
  std::vector<double> costMs_;
};

} // namespace pdfview
