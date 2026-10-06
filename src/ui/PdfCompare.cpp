#include "PdfCompare.hpp"

#include <GL/glew.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pdfview {

namespace {

using Clock = std::chrono::steady_clock;
double MsSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

constexpr int kMaxSide = 3000;                        // the longest side of a comparison image, in pixels
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
  for (int i = 0; i < 3; ++i)
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
  if (job_.valid())
    job_.wait();
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
    pdfalign::TintImage(out.img[0], out.img[1], out.img[2]);
    out.n = 3;
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
  if (ImGui::RadioButton("Blink", mode_ == Mode::Blink))
    mode_ = Mode::Blink;
  ImGui::SameLine();
  if (mode_ == Mode::Opacity) {
    ImGui::SetNextItemWidth(150.f);
    ImGui::SliderFloat("##cmpop", &opacity_, 0.f, 1.f, "revision %.2f");
  } else if (mode_ == Mode::Blink) {
    ImGui::SetNextItemWidth(150.f);
    ImGui::SliderFloat("##cmphz", &blinkHz_, 0.25f, 6.f, "%.2f blinks/s");
    ImGui::SameLine();
    ImGui::TextDisabled("hold B = base, R = revision");
  } else {
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.f), "red: only in the base");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.35f, 0.55f, 1.f, 1.f), "blue: only in the revision");
    ImGui::SameLine();
    ImGui::TextDisabled("grey: in both");
  }

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
    xf_ = pdfalign::Transform{};
    alignNote_ = "Not adjusted: the two pages are laid on each other at their lower-left corners.";
    pick_ = Pick::None;
    pickCount_ = 0;
    ++version_;
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
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || hovered;
    bool showRev = false, showBoth = false;
    if (mode_ == Mode::Blink) {
      showRev = std::fmod(ImGui::GetTime() * static_cast<double>(blinkHz_), 1.0) >= 0.5;
      if (focused && !io.WantTextInput) {
        if (ImGui::IsKeyDown(ImGuiKey_B))
          showRev = false;
        else if (ImGui::IsKeyDown(ImGuiKey_R))
          showRev = true;
      }
    } else if (mode_ == Mode::Opacity) {
      showBoth = true;
    }
    if (mode_ == Mode::Tint) {
      dl->AddImage(TexId(cur_.tex[2]), a, b);
    } else if (showBoth) {
      dl->AddImage(TexId(cur_.tex[0]), a, b);
      dl->AddImage(TexId(cur_.tex[1]), a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(opacity_ * 255.f)));
    } else {
      dl->AddImage(TexId(cur_.tex[showRev ? 1 : 0]), a, b);
    }
  }
  dl->AddRect(a, b, IM_COL32(90, 90, 90, 255));

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
  UploadSlice();
  DrawSheet(log);

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
