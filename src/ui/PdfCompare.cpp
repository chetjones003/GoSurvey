#include "PdfCompare.hpp"

#include "WinFileDialogs.hpp"

#include <GL/glew.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pdfview {

namespace {

using Clock = std::chrono::steady_clock;
double MsSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

constexpr int kMaxSide = 4096;                        // the longest side of a comparison image, in pixels
constexpr size_t kUploadBytesPerFrame = 3u * 1024 * 1024; // ADR-067 (c): an image goes up a slice per frame, never in one long frame
constexpr float kMarginPx = 16.f;
constexpr int kFlagsAnnot = 0x01; // FPDF_ANNOT: the page as drawn, without LCD colour fringes (they would read as ink)
constexpr int kBenchFrames = 300;

std::vector<unsigned> g_dead;

ImTextureID TexId(unsigned h) { return static_cast<ImTextureID>(static_cast<std::intptr_t>(h)); }

std::string FileTitle(const std::filesystem::path& p) { return p.filename().u8string(); }

} // namespace

bool PdfCompare::Set::Complete() const {
  for (int i = 0; i < n; ++i)
    if (rowsDone[i] < img[i].h)
      return false;
  return n > 0;
}

void PdfCompare::Retire(Set& s) {
  for (int i = 0; i < 5; ++i)
    if (s.tex[i] != 0)
      g_dead.push_back(s.tex[i]);
  s = Set{};
}

void PdfCompare::DrainGraveyard() {
  for (unsigned t : g_dead) {
    const GLuint g = t;
    glDeleteTextures(1, &g);
  }
  g_dead.clear();
}

PdfCompare::PdfCompare(PdfDocument* base, const std::string& baseTitle, int basePage, const std::filesystem::path& revPath)
    : base_(base), baseTitle_(baseTitle), basePage_(basePage), revPath_(revPath) {
  revTitle_ = FileTitle(revPath);
  opening_ = std::async(std::launch::async, [revPath] { return PdfDocument::Open(revPath); });
}

PdfCompare::~PdfCompare() {
  cancel_.store(true);
  anaCancel_.store(true);
  if (job_.valid())
    job_.wait();
  if (ana_.valid())
    ana_.wait();
  if (saving_.valid())
    saving_.wait();
  if (opening_.valid())
    opening_.wait();
  Retire(cur_);
  Retire(pending_);
  if (deleteRev_) {
    rev_.reset(); // close the file before removing it
    std::error_code ec;
    std::filesystem::remove(revPath_, ec);
  }
}

void PdfCompare::StartBench(int pages, bool deleteRevAfter) {
  bench_ = true;
  benchPages_ = pages;
  deleteRev_ = deleteRevAfter;
  benchT0_ = Clock::now();
}

void PdfCompare::PollOpen(std::vector<std::string>& log) {
  if (!opening_.valid() || opening_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    return;
  PdfDocument::OpenResult r = opening_.get();
  if (r.doc != nullptr) {
    rev_ = std::move(r.doc);
    revPage_ = 0;
    log.push_back("PDF viewer: comparing with " + revTitle_ + " (" + std::to_string(rev_->PageCount()) + " pages).");
  } else {
    error_ = r.error;
    log.push_back("PDF viewer: cannot open " + revTitle_ + " to compare - " + r.error);
  }
}

void PdfCompare::StartDiffBench(bool deleteRevAfter) {
  diffBench_ = true;
  deleteRev_ = deleteRevAfter;
  benchT0_ = Clock::now();
}

void PdfCompare::ClearChanges() {
  regions_.clear();
  haveChanges_ = false;
  selRegion_ = -1;
  centerReq_ = -1;
}

// REQ-393 clause 6: the automatic alignment and the change search run on one worker at a time, can be cancelled, and
// report progress; the UI thread only starts them and collects the result.
void PdfCompare::StartTask(Task t) {
  anaCancel_.store(false);
  anaProgress_.store(0.f);
  anaTask_ = t;
  anaRunning_ = true;
  anaBasePage_ = basePage_;
  anaRevPage_ = revPage_;
  anaVersion_ = version_;
  PdfDocument* b = base_;
  PdfDocument* r = rev_.get();
  const int bp = basePage_, rp = revPage_;
  const pdfalign::Transform xf = xf_;
  const pdfdiff::Settings settings = diffSettings_;
  std::atomic<bool>* cancel = &anaCancel_;
  std::atomic<float>* prog = &anaProgress_;
  ana_ = std::async(std::launch::async, [=]() {
    AnaOut out;
    out.task = t;
    const Clock::time_point t0 = Clock::now();
    const auto isCancelled = [cancel] { return cancel->load(); };
    const PageSize bs = b->Sizes()[static_cast<size_t>(bp)];
    const PageSize rs = r->Sizes()[static_cast<size_t>(rp)];
    const auto fit = [](const PageSize& s, double ppp, int side, int& w, int& h) {
      ppp = std::min(ppp, static_cast<double>(side) / std::max(s.wPt, s.hPt));
      w = std::max(1, static_cast<int>(std::lround(s.wPt * ppp)));
      h = std::max(1, static_cast<int>(std::lround(s.hPt * ppp)));
      return ppp;
    };
    Bitmap bBmp, rBmp;
    int bw = 0, bh = 0, rw = 0, rh = 0;
    if (t == Task::Align) {
      // A coarse picture of each sheet is enough to find a shift, a scale and a small turn.
      const double ppp = std::min(1.0, 3000.0 / std::max({bs.wPt, bs.hPt, rs.wPt, rs.hPt}));
      const double bppp = fit(bs, ppp, 100000, bw, bh), rppp = fit(rs, ppp, 100000, rw, rh);
      if (!b->RenderPage(bp, bw, bh, bBmp, isCancelled, kFlagsAnnot)) {
        out.cancelled = isCancelled();
        return out;
      }
      prog->store(0.3f);
      if (!r->RenderPage(rp, rw, rh, rBmp, isCancelled, kFlagsAnnot)) {
        out.cancelled = isCancelled();
        return out;
      }
      prog->store(0.5f);
      (void)rppp;
      out.align = pdfalign::AutoAlign(bBmp, bs.hPt, rBmp, rs.hPt, bppp, isCancelled);
      out.cancelled = isCancelled();
      out.ok = !out.cancelled;
    } else {
      const double bppp = fit(bs, 1.5, 4000, bw, bh);
      if (!b->RenderPage(bp, bw, bh, bBmp, isCancelled, kFlagsAnnot)) {
        out.cancelled = isCancelled();
        return out;
      }
      prog->store(0.2f);
      const double rppp = fit(rs, bppp * std::max(0.05, xf.Scale()), 6000, rw, rh);
      if (!r->RenderPage(rp, rw, rh, rBmp, isCancelled, kFlagsAnnot)) {
        out.cancelled = isCancelled();
        return out;
      }
      prog->store(0.4f);
      Bitmap aligned;
      pdfalign::ResampleAligned(rBmp, rs.hPt, static_cast<float>(rppp), xf, bw, bh, static_cast<float>(bppp), bs.hPt, aligned);
      std::vector<uint8_t>().swap(rBmp.bgra);
      prog->store(0.5f);
      out.found = pdfdiff::FindChanges(bBmp, aligned, bppp, bs.hPt, settings, isCancelled,
                                       [prog](float f) { prog->store(0.5f + 0.5f * f); });
      out.cancelled = out.found.cancelled;
      out.ok = !out.cancelled;
    }
    out.ms = MsSince(t0);
    return out;
  });
}

void PdfCompare::PumpAnalysis(std::vector<std::string>& log) {
  if (anaRunning_ && ana_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    AnaOut out = ana_.get();
    anaRunning_ = false;
    const bool stale = anaBasePage_ != basePage_ || anaRevPage_ != revPage_;
    if (stale) {
      if (out.task == Task::Align)
        alignWanted_ = true; // the pages changed while it ran: do it again for the new pair
    } else if (!out.ok) {
      if (!out.cancelled)
        status_ = out.task == Task::Align ? "A page could not be rendered for the alignment." : "A page could not be rendered to find the changes.";
    } else if (out.task == Task::Align) {
      autoConfidence_ = out.align.confidence;
      autoMatched_ = out.align.matched;
      lowConfidence_ = !out.align.matched;
      autoXf_ = out.align.matched ? out.align.xf : pdfalign::Transform{};
      xf_ = autoXf_;
      ++version_;
      ClearChanges();
      char note[240];
      if (out.align.matched)
        std::snprintf(note, sizeof(note), "Aligned automatically (confidence %.0f %%): scale %.5f, rotation %.3f degrees.", out.align.confidence * 100.0,
                      xf_.Scale(), xf_.RotationDeg());
      else
        std::snprintf(note, sizeof(note), "Check alignment: no automatic match (confidence %.0f %%). The pages are laid corner on corner; line them up by hand.",
                      out.align.confidence * 100.0);
      alignNote_ = note;
      if (diffBench_) {
        diffBenchAlignMs_ = out.ms;
        diffBenchFinding_ = true;
        StartTask(Task::Find);
      }
    } else if (anaVersion_ == version_) {
      regions_ = std::move(out.found.regions);
      haveChanges_ = true;
      selRegion_ = -1;
      char msg[160];
      std::snprintf(msg, sizeof(msg), "PDF viewer: %zu change region(s) found in %.1f s.", regions_.size(), out.ms / 1000.0);
      log.push_back(msg);
      if (diffBench_ && diffBenchFinding_) {
        char line[300];
        std::snprintf(line, sizeof(line),
                      "BENCH PDFDIFF 36 x 24 in line-work sheets: automatic alignment %.0f ms + find changes %.0f ms = %.0f ms (target 10000) | %zu regions | worst viewer frame while it ran %.2f ms (target 16)",
                      diffBenchAlignMs_, out.ms, diffBenchAlignMs_ + out.ms, regions_.size(), diffBenchWorstFrameMs_);
        log.push_back(line);
        std::fprintf(stderr, "%s\n", line);
        benchDone_ = true;
      }
    }
  }
  if (!anaRunning_ && alignWanted_ && rev_ != nullptr) {
    alignWanted_ = false;
    StartTask(Task::Align);
  }
  if (saving_.valid() && saving_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    const std::string err = saving_.get();
    savedNote_ = err.empty() ? "Markups written to the copy of the revision." : "Could not write the markups: " + err + ".";
    log.push_back("PDF viewer: " + savedNote_);
  }
}

void PdfCompare::SaveMarkups() {
  if (regions_.empty() || saving_.valid())
    return;
  const std::string stem = revPath_.stem().u8string() + "-changes.pdf";
  char dest[1024] = {};
  if (!BrowseSaveFilePdfUtf8(dest, sizeof(dest), stem.c_str()) || dest[0] == '\0')
    return;
  savedNote_ = "Saving...";
  // The marks go on a Save As COPY of the revision, in the revision's own points; the original files are never changed.
  std::vector<Annot> marks = pdfdiff::RegionsToMarkups(regions_, revPage_, xf_.Inverse());
  const std::filesystem::path src = revPath_, dst = std::filesystem::u8path(dest);
  saving_ = std::async(std::launch::async, [src, dst, marks] { return SaveAnnotated(src, marks, dst); });
}

void PdfCompare::BeginPick(Pick p) {
  pick_ = p;
  pickCount_ = 0;
  fitPending_ = true;
}

void PdfCompare::TakePick(pdfalign::Pt p) {
  picks_[pickCount_++] = p;
  switch (pick_) {
  case Pick::OneBase: pick_ = Pick::OneRev; break;
  case Pick::OneRev:
    xf_ = pdfalign::FromOnePoint(picks_[1], picks_[0]);
    alignNote_ = "Adjusted by one matching point (shift only).";
    pick_ = Pick::None;
    pickCount_ = 0;
    ++version_;
    ClearChanges();
    break;
  case Pick::TwoBase1: pick_ = Pick::TwoRev1; break;
  case Pick::TwoRev1: pick_ = Pick::TwoBase2; break;
  case Pick::TwoBase2: pick_ = Pick::TwoRev2; break;
  case Pick::TwoRev2: {
    pdfalign::Transform t;
    std::string why;
    if (pdfalign::FromTwoPoints(picks_[1], picks_[3], picks_[0], picks_[2], t, why)) {
      xf_ = t;
      char note[200];
      std::snprintf(note, sizeof(note), "Adjusted by two matching points: scale %.5f, rotation %.3f degrees.", t.Scale(), t.RotationDeg());
      alignNote_ = note;
      ++version_;
      ClearChanges();
    } else {
      status_ = "Alignment not changed: " + why + ".";
    }
    pick_ = Pick::None;
    pickCount_ = 0;
    break;
  }
  default: break;
  }
  fitPending_ = true;
}

void PdfCompare::PumpJob() {
  const bool raw = WantRaw();
  Key want;
  want.scaleKey = ScaleKeyFor(pxPerPt_);
  want.version = version_;
  want.basePage = basePage_;
  want.revPage = revPage_;
  want.raw = raw;

  if (jobRunning_ && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    JobOut o = job_.get();
    jobRunning_ = false;
    if (o.ok) {
      if (havePending_)
        Retire(pending_);
      pending_ = Set{};
      pending_.key = o.key;
      pending_.n = o.n;
      pending_.wPt = o.wPt;
      pending_.hPt = o.hPt;
      for (int i = 0; i < o.n; ++i) {
        pending_.img[i] = std::move(o.img[i]);
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pending_.img[i].w, pending_.img[i].h, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        pending_.tex[i] = t;
      }
      havePending_ = true;
    } else if (!cancel_.load()) {
      failedKey_ = o.key;
      hasFailed_ = true;
      status_ = "A page could not be rendered for the comparison.";
    }
  }

  if (jobRunning_) {
    if (!(jobKey_ == want))
      cancel_.store(true); // what is wanted has moved on: stop this one, a new one starts when it ends
    return;
  }
  if ((haveCur_ && cur_.key == want) || (havePending_ && pending_.key == want) || (hasFailed_ && failedKey_ == want) || rev_ == nullptr)
    return;

  cancel_.store(false);
  jobKey_ = want;
  jobRunning_ = true;
  PdfDocument* b = base_;
  PdfDocument* r = rev_.get();
  const pdfalign::Transform xf = xf_;
  std::atomic<bool>* cancel = &cancel_;
  job_ = std::async(std::launch::async, [=]() {
    JobOut out;
    out.key = want;
    const auto isCancelled = [cancel] { return cancel->load(); };
    const auto fit = [](const PageSize& s, float ppp, int& w, int& h) {
      ppp = std::min(ppp, static_cast<float>(kMaxSide) / std::max(s.wPt, s.hPt));
      w = std::max(1, static_cast<int>(std::lround(s.wPt * ppp)));
      h = std::max(1, static_cast<int>(std::lround(s.hPt * ppp)));
      return ppp;
    };
    const float want0 = PxPerPtFor(want.scaleKey);
    const PageSize bs = b->Sizes()[static_cast<size_t>(want.basePage)];
    const PageSize rs = r->Sizes()[static_cast<size_t>(want.revPage)];
    if (want.raw) {
      int w = 0, h = 0;
      fit(rs, want0, w, h);
      if (!r->RenderPage(want.revPage, w, h, out.img[0], isCancelled, kFlagsAnnot))
        return out;
      out.n = 1;
      out.wPt = rs.wPt;
      out.hPt = rs.hPt;
      out.ok = true;
      return out;
    }
    int bw = 0, bh = 0;
    const float bppp = fit(bs, want0, bw, bh);
    if (!b->RenderPage(want.basePage, bw, bh, out.img[0], isCancelled, kFlagsAnnot))
      return out;
    // The revision is drawn at the base's pixel density times the transform's scale, so resampling is about 1:1.
    int rw = 0, rh = 0;
    const float rppp = fit(rs, bppp * static_cast<float>(std::max(0.05, xf.Scale())), rw, rh);
    Bitmap revRaw;
    if (!r->RenderPage(want.revPage, rw, rh, revRaw, isCancelled, kFlagsAnnot))
      return out;
    if (isCancelled())
      return out;
    pdfalign::ResampleAligned(revRaw, rs.hPt, rppp, xf, bw, bh, bppp, bs.hPt, out.img[1]);
    uint8_t blue[4], red[4];
    pdfalign::TintBgra(pdfalign::Ink::BaseOnly, blue);
    pdfalign::TintBgra(pdfalign::Ink::RevOnly, red);
    // The text runs of each sheet decide what is coloured whole (a changed number is not left half coloured).
    std::vector<ObjBox> baseText, revText;
    b->TextBoxes(want.basePage, baseText, isCancelled);
    r->TextBoxes(want.revPage, revText, isCancelled);
    const std::vector<pdfalign::PixRect> baseUnits = pdfalign::BoxesToPixels(baseText, pdfalign::Transform{}, bs.hPt, bppp, bw, bh);
    const std::vector<pdfalign::PixRect> revUnits = pdfalign::BoxesToPixels(revText, xf, bs.hPt, bppp, bw, bh);
    pdfalign::MarkOnlyIn(out.img[0], out.img[1], blue, out.img[3], bppp, baseUnits);
    pdfalign::MarkOnlyIn(out.img[1], out.img[0], red, out.img[4], bppp, revUnits);
    pdfalign::TintFromMarks(out.img[0], out.img[1], out.img[3], out.img[4], out.img[2]);
    out.n = 5;
    out.wPt = bs.wPt;
    out.hPt = bs.hPt;
    out.ok = true;
    return out;
  });
}

void PdfCompare::UploadSlice() {
  if (!havePending_)
    return;
  size_t budget = kUploadBytesPerFrame;
  Set& s = pending_;
  for (int i = 0; i < s.n && budget > 0; ++i) {
    Bitmap& bm = s.img[i];
    if (s.rowsDone[i] >= bm.h)
      continue;
    const size_t rowBytes = static_cast<size_t>(bm.w) * 4u;
    const int rows = std::min(bm.h - s.rowsDone[i], static_cast<int>(std::max<size_t>(1, budget / rowBytes)));
    glBindTexture(GL_TEXTURE_2D, s.tex[i]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, s.rowsDone[i], bm.w, rows, GL_BGRA, GL_UNSIGNED_BYTE,
                    &bm.bgra[static_cast<size_t>(s.rowsDone[i]) * rowBytes]);
    glBindTexture(GL_TEXTURE_2D, 0);
    s.rowsDone[i] += rows;
    budget -= std::min(budget, static_cast<size_t>(rows) * rowBytes);
  }
  if (!s.Complete())
    return;
  for (int i = 0; i < s.n; ++i)
    std::vector<uint8_t>().swap(s.img[i].bgra); // the pixels are on the GPU now
  if (haveCur_)
    Retire(cur_);
  cur_ = std::move(pending_);
  pending_ = Set{};
  haveCur_ = true;
  havePending_ = false;
  if (firstShownMs_ < 0.0)
    firstShownMs_ = MsSince(benchT0_);
}

void PdfCompare::DrawBar(bool& keepOpen) {
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Comparing  %s  with  %s", baseTitle_.c_str(), revTitle_.c_str());
  ImGui::SameLine();
  if (ImGui::Button("Close comparison"))
    keepOpen = false;

  if (rev_ == nullptr)
    return;
  int bp = basePage_ + 1, rp = revPage_ + 1;
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Base page");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70.f);
  if (ImGui::InputInt("##cmpbp", &bp, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
    basePage_ = std::clamp(bp, 1, base_->PageCount()) - 1;
    BeginPick(Pick::None);
    ClearChanges();
    alignWanted_ = true;
    anaCancel_.store(true);
  }
  ImGui::SameLine();
  ImGui::Text("of %d", base_->PageCount());
  ImGui::SameLine();
  ImGui::TextUnformatted("  Revision page");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70.f);
  if (ImGui::InputInt("##cmprp", &rp, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
    revPage_ = std::clamp(rp, 1, rev_->PageCount()) - 1;
    BeginPick(Pick::None);
    ClearChanges();
    alignWanted_ = true;
    anaCancel_.store(true);
  }
  ImGui::SameLine();
  ImGui::Text("of %d", rev_->PageCount());

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Show:");
  ImGui::SameLine();
  if (ImGui::RadioButton("Tint", mode_ == Mode::Tint))
    mode_ = Mode::Tint;
  ImGui::SameLine();
  if (ImGui::RadioButton("Opacity", mode_ == Mode::Opacity))
    mode_ = Mode::Opacity;
  ImGui::SameLine();
  if (ImGui::RadioButton("Base", mode_ == Mode::Base))
    mode_ = Mode::Base;
  ImGui::SameLine();
  if (ImGui::RadioButton("Revision", mode_ == Mode::Revision))
    mode_ = Mode::Revision;
  ImGui::SameLine();
  if (mode_ == Mode::Opacity) {
    ImGui::SetNextItemWidth(150.f);
    ImGui::SliderFloat("##cmpop", &opacity_, 0.f, 1.f, "revision %.2f");
  } else if (mode_ == Mode::Base) {
    ImGui::TextColored(ImVec4(0.35f, 0.55f, 1.f, 1.f), "blue: only in the base (what the revision removed)");
  } else if (mode_ == Mode::Revision) {
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.f), "red: only in the revision (what it added)");
  } else if (mode_ == Mode::Tint) {
    ImGui::TextColored(ImVec4(0.35f, 0.55f, 1.f, 1.f), "blue: only in the base");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.f), "red: only in the revision");
    ImGui::SameLine();
    ImGui::TextDisabled("grey: in both");
  }

  DrawChangesBar();

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Line up:");
  ImGui::SameLine();
  if (ImGui::Button("One point"))
    BeginPick(Pick::OneBase);
  ImGui::SameLine();
  if (ImGui::Button("Two points"))
    BeginPick(Pick::TwoBase1);
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    xf_ = autoXf_;
    alignNote_ = autoMatched_ ? "Back to the automatic alignment." : "Not adjusted: the two pages are laid on each other at their lower-left corners.";
    pick_ = Pick::None;
    pickCount_ = 0;
    ++version_;
    ClearChanges();
  }
  ImGui::SameLine();
  if (pick_ != Pick::None && ImGui::Button("Cancel picking"))
    BeginPick(Pick::None);
  ImGui::SameLine();
  ImGui::TextUnformatted("|");
  ImGui::SameLine();
  if (ImGui::Button("-"))
    pxPerPt_ = std::clamp(pxPerPt_ / 1.25f, 0.05f, 16.f);
  ImGui::SameLine();
  if (ImGui::Button("+"))
    pxPerPt_ = std::clamp(pxPerPt_ * 1.25f, 0.05f, 16.f);
  ImGui::SameLine();
  if (ImGui::Button("Fit page"))
    fitPending_ = true;

  const char* prompt = nullptr;
  switch (pick_) {
  case Pick::OneBase: prompt = "Click a point on the BASE sheet."; break;
  case Pick::OneRev: prompt = "Now click the SAME point on the REVISION sheet."; break;
  case Pick::TwoBase1: prompt = "Point 1 of 2: click a point on the BASE sheet."; break;
  case Pick::TwoRev1: prompt = "Point 1 of 2: click the SAME point on the REVISION sheet."; break;
  case Pick::TwoBase2: prompt = "Point 2 of 2: click a second point on the BASE sheet, far from the first."; break;
  case Pick::TwoRev2: prompt = "Point 2 of 2: click the SAME second point on the REVISION sheet."; break;
  default: break;
  }
  if (prompt != nullptr)
    ImGui::TextColored(ImVec4(1.f, 0.78f, 0.25f, 1.f), "%s", prompt);
  else
    ImGui::TextDisabled("%s", alignNote_.c_str());
  if (!status_.empty()) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.3f, 1.f), "%s", status_.c_str());
  }
}

void PdfCompare::DrawChangesBar() {
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Changes:");
  ImGui::SameLine();
  ImGui::BeginDisabled(anaRunning_);
  if (ImGui::Button("Align automatically"))
    alignWanted_ = true;
  ImGui::SameLine();
  if (ImGui::Button("Find changes"))
    StartTask(Task::Find);
  ImGui::EndDisabled();
  if (anaRunning_) {
    ImGui::SameLine();
    ImGui::ProgressBar(anaProgress_.load(), ImVec2(130.f, 0.f), anaTask_ == Task::Align ? "aligning" : "comparing");
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      anaCancel_.store(true);
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70.f);
  ImGui::InputDouble("##tol", &diffSettings_.toleranceMm, 0.0, 0.0, "tol %.1f mm");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Tolerance: marks that differ by less than this (in millimetres on paper) are treated as the same.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70.f);
  ImGui::InputDouble("##minsz", &diffSettings_.minSizeMm, 0.0, 0.0, "min %.1f mm");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Smallest area to report: differences smaller than this (in millimetres on paper) are ignored as specks.");
  diffSettings_.toleranceMm = std::clamp(diffSettings_.toleranceMm, 0.2, 10.0);
  diffSettings_.minSizeMm = std::clamp(diffSettings_.minSizeMm, 0.0, 50.0);
  if (haveChanges_) {
    ImGui::SameLine();
    ImGui::Checkbox("Highlights", &showHighlights_);
    ImGui::SameLine();
    const int n = static_cast<int>(regions_.size());
    const bool step = !regions_.empty();
    ImGui::BeginDisabled(!step);
    bool prev = ImGui::Button("Previous"), next = false;
    ImGui::SameLine();
    next = ImGui::Button("Next");
    ImGui::EndDisabled();
    if (step && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput) {
      next = next || ImGui::IsKeyPressed(ImGuiKey_N);
      prev = prev || ImGui::IsKeyPressed(ImGuiKey_P);
    }
    if (step && (next || prev)) {
      selRegion_ = next ? (selRegion_ + 1) % n : (selRegion_ <= 0 ? n - 1 : selRegion_ - 1);
      centerReq_ = selRegion_;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!step || saving_.valid());
    if (ImGui::Button("Write changes as markups..."))
      SaveMarkups();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Save a copy of the revision PDF with a box drawn around each area that differs.\nThe original files are not changed.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (n == 0)
      ImGui::TextUnformatted("No differences found.");
    else
      ImGui::Text("%d area%s differ.  N / P = next / previous", n, n == 1 ? "" : "s");
  }
  if (!savedNote_.empty())
    ImGui::TextDisabled("%s", savedNote_.c_str());
  if (lowConfidence_)
    ImGui::TextColored(ImVec4(1.f, 0.62f, 0.2f, 1.f), "Check alignment: the sheets do not match closely enough to trust the changes found. Line them up by hand first.");
  if (haveChanges_)
    ImGui::TextDisabled("The boxes mark places where the two drawings look different; they do not say what the change means. "
                        "Results depend on how well the sheets line up, and a different scale or a scanned sheet can show false areas.");
}

void PdfCompare::DrawChangesList() {
  // NoMove: a click or drag on the list must not carry the whole window along.
  ImGui::BeginChild("##cmpchanges", ImVec2(listW_, 0.f), true, ImGuiWindowFlags_NoMove);
  ImGui::Text("Areas that differ (%d)", static_cast<int>(regions_.size()));
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40.f);
  const bool closeList = ImGui::SmallButton("Close");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Close this list and the highlight boxes. Press Find changes to run it again.");
  ImGui::PushTextWrapPos(0.f);
  ImGui::TextDisabled("Click a row to jump to it on the sheet.");
  ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.45f, 1.f), "Green: new in the revision");
  ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.f), "Red: removed from the revision");
  ImGui::TextColored(ImVec4(0.98f, 0.7f, 0.2f, 1.f), "Amber: changed or moved");
  ImGui::PopTextWrapPos();
  ImGui::Separator();
  for (size_t i = 0; i < regions_.size(); ++i) {
    const pdfdiff::Region& r = regions_[i];
    char label[120];
    const char* what = r.kind == pdfdiff::Kind::Added ? "New" : r.kind == pdfdiff::Kind::Removed ? "Removed" : "Changed";
    // Where on the sheet, in words, rather than a size in points: "Changed - top left", "New - bottom", "Changed - large area".
    const PageSize ps = base_->Sizes()[static_cast<size_t>(basePage_)];
    const double cx = (r.x0 + r.x1) * 0.5 / std::max(1.f, ps.wPt), cy = (r.y0 + r.y1) * 0.5 / std::max(1.f, ps.hPt);
    const char* col = cx < 1.0 / 3 ? "left" : cx > 2.0 / 3 ? "right" : "";
    const char* row = cy > 2.0 / 3 ? "top" : cy < 1.0 / 3 ? "bottom" : "";
    char where[48];
    if (r.Width() > 0.25 * ps.wPt || r.Height() > 0.25 * ps.hPt)
      std::snprintf(where, sizeof(where), "large area");
    else if (*row == 0 && *col == 0)
      std::snprintf(where, sizeof(where), "middle");
    else
      std::snprintf(where, sizeof(where), "%s%s%s", row, (*row != 0 && *col != 0) ? " " : "", col);
    std::snprintf(label, sizeof(label), "%zu  %s - %s##reg%zu", i + 1, what, where, i);
    const ImVec4 col = r.kind == pdfdiff::Kind::Added ? ImVec4(0.3f, 0.85f, 0.45f, 1.f)
                       : r.kind == pdfdiff::Kind::Removed ? ImVec4(0.95f, 0.4f, 0.4f, 1.f)
                                                          : ImVec4(0.98f, 0.7f, 0.2f, 1.f);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    if (ImGui::Selectable(label, static_cast<int>(i) == selRegion_)) {
      selRegion_ = static_cast<int>(i);
      centerReq_ = selRegion_;
    }
    ImGui::PopStyleColor();
  }
  ImGui::EndChild();
  if (closeList)
    ClearChanges();
}

void PdfCompare::DrawSheet(std::vector<std::string>& log) {
  (void)log;
  const bool raw = WantRaw();
  const PageSize vs = raw ? rev_->Sizes()[static_cast<size_t>(revPage_)] : base_->Sizes()[static_cast<size_t>(basePage_)];
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  if (fitPending_ && avail.y > 50.f) {
    pxPerPt_ = std::clamp((avail.y - 2 * kMarginPx - ImGui::GetStyle().ScrollbarSize) / vs.hPt, 0.05f, 16.f);
    pendX_ = pendY_ = 0.f;
    fitPending_ = false;
  }

  // Benchmark: zoom in, then pan, and change the zoom twice, for a fixed number of frames.
  if (bench_ && haveCur_ && !benchDone_) {
    if (benchFrame_ == 0 || benchFrame_ == 100 || benchFrame_ == 200)
      pxPerPt_ = std::clamp(pxPerPt_ * (benchFrame_ == 0 ? 2.5f : 1.3f), 0.05f, 16.f);
    pendX_ = std::max(0.f, (pendX_ >= 0.f ? pendX_ : ImGui::GetScrollX()) + 3.f);
    pendY_ = std::max(0.f, (pendY_ >= 0.f ? pendY_ : ImGui::GetScrollY()) + 8.f);
  }

  const float pw = vs.wPt * pxPerPt_, ph = vs.hPt * pxPerPt_;
  const float contentW = std::max(avail.x, pw + 2 * kMarginPx);
  if (centerReq_ >= 0 && centerReq_ < static_cast<int>(regions_.size()) && !raw) { // a region chosen in the list or by N / P
    const pdfdiff::Region& g = regions_[static_cast<size_t>(centerReq_)];
    pendX_ = std::max(0.f, (contentW - pw) * 0.5f + static_cast<float>((g.x0 + g.x1) * 0.5) * pxPerPt_ - avail.x * 0.5f);
    pendY_ = std::max(0.f, kMarginPx + (vs.hPt - static_cast<float>((g.y0 + g.y1) * 0.5)) * pxPerPt_ - avail.y * 0.5f);
  }
  centerReq_ = -1;
  ImGui::SetNextWindowContentSize(ImVec2(contentW, ph + 2 * kMarginPx));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.30f, 0.30f, 0.32f, 1.f));
  ImGui::BeginChild("##cmpsheet", ImVec2(0.f, 0.f), false, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);
  ImGui::PopStyleColor();

  const bool hovered = ImGui::IsWindowHovered();
  const ImGuiIO& io = ImGui::GetIO();
  const auto wantedY = [&] { return pendY_ >= 0.f ? pendY_ : ImGui::GetScrollY(); };
  const auto wantedX = [&] { return pendX_ >= 0.f ? pendX_ : ImGui::GetScrollX(); };
  if (hovered && io.KeyCtrl && io.MouseWheel != 0.f) {
    // Zoom about the pointer: the point under it stays under it.
    const ImVec2 wp = ImGui::GetWindowPos();
    const float mx = io.MousePos.x - wp.x, my = io.MousePos.y - wp.y;
    const float xoff = (contentW - pw) * 0.5f;
    const float ptX = (wantedX() + mx - xoff) / pxPerPt_, ptY = (wantedY() + my - kMarginPx) / pxPerPt_;
    pxPerPt_ = std::clamp(pxPerPt_ * (io.MouseWheel > 0 ? 1.15f : 1.f / 1.15f), 0.05f, 16.f);
    const float newContentW = std::max(avail.x, vs.wPt * pxPerPt_ + 2 * kMarginPx);
    pendX_ = std::max(0.f, ptX * pxPerPt_ + (newContentW - vs.wPt * pxPerPt_) * 0.5f - mx);
    pendY_ = std::max(0.f, ptY * pxPerPt_ + kMarginPx - my);
  }
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    panning_ = true;
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    panning_ = false;
  if (panning_ && hovered) {
    pendY_ = std::max(0.f, wantedY() - io.MouseDelta.y);
    pendX_ = std::max(0.f, wantedX() - io.MouseDelta.x);
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  }

  // Take the wanted scroll now, for drawing and picking (ImGui moves its own scroll next frame).
  const float viewW = ImGui::GetWindowWidth(), viewH = ImGui::GetWindowHeight();
  float scrollY = ImGui::GetScrollY(), scrollX = ImGui::GetScrollX();
  if (pendY_ >= 0.f) {
    scrollY = std::clamp(pendY_, 0.f, std::max(0.f, ph + 2 * kMarginPx - viewH));
    ImGui::SetScrollY(scrollY);
    pendY_ = -1.f;
  }
  if (pendX_ >= 0.f) {
    scrollX = std::clamp(pendX_, 0.f, std::max(0.f, contentW - viewW));
    ImGui::SetScrollX(scrollX);
    pendX_ = -1.f;
  }
  ImVec2 origin = ImGui::GetCursorScreenPos();
  origin.x += ImGui::GetScrollX() - scrollX;
  origin.y += ImGui::GetScrollY() - scrollY;
  const ImVec2 a(origin.x + (contentW - pw) * 0.5f, origin.y + kMarginPx), b(a.x + pw, a.y + ph);

  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(ImVec2(a.x + 3.f, a.y + 3.f), ImVec2(b.x + 3.f, b.y + 3.f), IM_COL32(0, 0, 0, 70));
  const bool shown = haveCur_ && cur_.key.raw == raw;
  if (!shown) {
    dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255));
    dl->AddText(ImVec2(a.x + 12.f, a.y + 12.f), IM_COL32(90, 90, 90, 255), "Rendering the comparison...");
  } else if (raw) {
    dl->AddImage(TexId(cur_.tex[0]), a, b);
  } else {
    if (mode_ == Mode::Tint) {
      dl->AddImage(TexId(cur_.tex[2]), a, b);
    } else if (mode_ == Mode::Opacity) {
      dl->AddImage(TexId(cur_.tex[0]), a, b);
      dl->AddImage(TexId(cur_.tex[1]), a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(opacity_ * 255.f)));
    } else if (mode_ == Mode::Base) { // the base, with what only it has in blue
      dl->AddImage(TexId(cur_.tex[0]), a, b);
      dl->AddImage(TexId(cur_.tex[3]), a, b);
    } else { // the revision, with what only it has in red
      dl->AddImage(TexId(cur_.tex[1]), a, b);
      dl->AddImage(TexId(cur_.tex[4]), a, b);
    }
  }
  dl->AddRect(a, b, IM_COL32(90, 90, 90, 255));

  // REQ-393 clause 3: the change regions, translucent over the sheet (added green, removed red, changed amber). They are in
  // the base's points; on the revision sheet alone they are carried through the alignment.
  if (showHighlights_ && haveChanges_ && shown) {
    const pdfalign::Transform inv = xf_.Inverse();
    for (size_t i = 0; i < regions_.size(); ++i) {
      const pdfdiff::Region& g = regions_[i];
      double lo[2] = {g.x0, g.y0}, hi[2] = {g.x1, g.y1};
      if (raw) {
        lo[0] = lo[1] = 1e30;
        hi[0] = hi[1] = -1e30;
        for (const pdfalign::Pt c : {pdfalign::Pt{g.x0, g.y0}, pdfalign::Pt{g.x1, g.y0}, pdfalign::Pt{g.x0, g.y1}, pdfalign::Pt{g.x1, g.y1}}) {
          const pdfalign::Pt q = inv.Apply(c);
          lo[0] = std::min(lo[0], q.x);
          lo[1] = std::min(lo[1], q.y);
          hi[0] = std::max(hi[0], q.x);
          hi[1] = std::max(hi[1], q.y);
        }
      }
      const ImVec2 p0(a.x + static_cast<float>(lo[0]) * pxPerPt_, a.y + (vs.hPt - static_cast<float>(hi[1])) * pxPerPt_);
      const ImVec2 p1(a.x + static_cast<float>(hi[0]) * pxPerPt_, a.y + (vs.hPt - static_cast<float>(lo[1])) * pxPerPt_);
      const bool sel = static_cast<int>(i) == selRegion_;
      const int rr = g.kind == pdfdiff::Kind::Added ? 40 : g.kind == pdfdiff::Kind::Removed ? 235 : 250;
      const int gg = g.kind == pdfdiff::Kind::Added ? 200 : g.kind == pdfdiff::Kind::Removed ? 60 : 175;
      const int bb = g.kind == pdfdiff::Kind::Added ? 90 : g.kind == pdfdiff::Kind::Removed ? 60 : 30;
      dl->AddRectFilled(p0, p1, IM_COL32(rr, gg, bb, sel ? 95 : 55));
      dl->AddRect(p0, p1, IM_COL32(rr, gg, bb, 235), 0.f, 0, sel ? 3.f : 1.5f);
    }
  }

  // Picking: the click is a point on the sheet being shown, in that page's points.
  if (pick_ != Pick::None) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    if (shown && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.MousePos.x >= a.x && io.MousePos.x <= b.x &&
        io.MousePos.y >= a.y && io.MousePos.y <= b.y)
      TakePick({(io.MousePos.x - a.x) / pxPerPt_, vs.hPt - (io.MousePos.y - a.y) / pxPerPt_});
    // The points picked so far on the sheet being shown.
    for (int i = 0; i < pickCount_; ++i) {
      const bool isRevPick = (i % 2) == 1;
      if (isRevPick != raw)
        continue;
      const ImVec2 c(a.x + static_cast<float>(picks_[i].x) * pxPerPt_, a.y + (vs.hPt - static_cast<float>(picks_[i].y)) * pxPerPt_);
      dl->AddCircleFilled(c, 5.f, IM_COL32(255, 160, 0, 255));
      dl->AddCircle(c, 7.f, IM_COL32(0, 0, 0, 220), 0, 1.5f);
    }
  }
  ImGui::EndChild();
}

bool PdfCompare::Draw(std::vector<std::string>& log) {
  const Clock::time_point t0 = Clock::now();
  PollOpen(log);
  bool keepOpen = true;
  DrawBar(keepOpen);
  if (!error_.empty()) {
    ImGui::TextWrapped("The revision could not be opened: %s.", error_.c_str());
    return keepOpen;
  }
  if (rev_ == nullptr) {
    ImGui::TextUnformatted("Opening the revision...");
    return keepOpen;
  }
  PumpJob();
  PumpAnalysis(log);
  UploadSlice();
  if (haveChanges_ && !regions_.empty()) {
    DrawChangesList();
    // The list's edge: drag it to resize, like the thumbnail strip.
    ImGui::SameLine(0.f, 0.f);
    ImGui::InvisibleButton("##cmplistsplit", ImVec2(7.f, ImGui::GetContentRegionAvail().y));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive())
      listW_ = std::clamp(listW_ + ImGui::GetIO().MouseDelta.x, 180.f, 600.f);
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                              ImGui::IsItemHovered() || ImGui::IsItemActive() ? IM_COL32(90, 130, 190, 255) : IM_COL32(60, 64, 72, 255));
    ImGui::SameLine(0.f, 0.f);
  }
  DrawSheet(log);

  if (diffBench_ && !benchDone_)
    diffBenchWorstFrameMs_ = std::max(diffBenchWorstFrameMs_, MsSince(t0));
  if (bench_ && haveCur_ && !benchDone_) {
    costMs_.push_back(MsSince(t0));
    if (++benchFrame_ >= kBenchFrames) {
      std::vector<double> x = costMs_;
      std::sort(x.begin(), x.end());
      char line[300];
      std::snprintf(line, sizeof(line),
                    "BENCH PDFCOMPARE %d pages: first overlay shown %.0f ms | compare cost per frame over %zu frames: p95 %.2f ms, worst %.2f ms (target 16)",
                    benchPages_, firstShownMs_, x.size(), x[std::min(x.size() - 1, static_cast<size_t>(0.95 * static_cast<double>(x.size())))], x.back());
      log.push_back(line);
      std::fprintf(stderr, "%s\n", line);
      benchDone_ = true;
    }
  }
  return keepOpen;
}

} // namespace pdfview
