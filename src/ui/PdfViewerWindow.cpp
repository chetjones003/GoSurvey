#include "PdfViewerWindow.hpp"

#include "CadCommands.hpp"
#include "PdfDocument.hpp"
#include "PdfSplit.hpp"
#include "PdfViewerCore.hpp"
#include "WinFileDialogs.hpp"

#include <GL/glew.h>
#include <imgui.h>
#include <imgui_internal.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

using namespace pdfview;

namespace {

using Clock = std::chrono::steady_clock;
double MsSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

constexpr float kBasePxPerPt = 96.f / 72.f; // zoom 100 % = 96 dpi
constexpr float kGapPt = 8.f;
constexpr float kMarginPx = 16.f;
constexpr int kStandInMaxSide = 256;  // sharp enough for the largest thumbnail the sidebar can show
constexpr int kSliceMs = 40;           // a sharp render pauses this often so waiting drafts can draw
constexpr int kDraftBudgetMs = 120;   // a first stand-in stops here and keeps what it has drawn
constexpr int kMaxRenderSide = 4096;
constexpr size_t kUploadBytesPerFrame = 3u * 1024 * 1024; // ADR-067 (c): a burst of finished pages cannot make a long frame
constexpr size_t kCacheCapBytes = 256ull * 1024 * 1024;
constexpr int kReadAhead = 6;
constexpr float kThumbWidthDefault = 190.f;
constexpr float kThumbWidthMin = 120.f;
constexpr float kThumbWidthMax = 340.f;

// One render worker per document (ADR-067 (c)): a one-shot thread that drains the latest plan and
// exits when it is empty, so an idle viewer owns no thread.
struct RenderResult {
  RenderRequest req;
  Bitmap bm;
  bool failed = false; ///< PDFium could not render it (not merely cancelled)
};

struct Worker {
  std::mutex m;
  std::vector<RenderRequest> plan;
  RenderRequest current{};
  bool haveCurrent = false;
  bool running = false;
  std::atomic<bool> abort{false};
  std::atomic<bool> stop{false};
  std::deque<RenderResult> done;
  std::thread th;
  PdfDocument* doc = nullptr;

  static void Dims(const PageSize& s, const RenderRequest& r, int& w, int& h) {
    float pxPerPt;
    if (r.level == Level::StandIn)
      pxPerPt = static_cast<float>(kStandInMaxSide) / std::max(s.wPt, s.hPt);
    else
      pxPerPt = PxPerPtFor(r.scaleKey);
    const float cap = static_cast<float>(kMaxRenderSide) / std::max(s.wPt, s.hPt);
    pxPerPt = std::min(pxPerPt, cap);
    w = std::max(1, static_cast<int>(std::lround(s.wPt * pxPerPt)));
    h = std::max(1, static_cast<int>(std::lround(s.hPt * pxPerPt)));
  }

  /// Draws the drafts at the front of the queue. Called from inside a sharp render's pause.
  RenderRequest nested{};     ///< the draft being drawn between slices (not queued, not yet in `done`)
  bool haveNested = false;

  void RunWaitingDrafts() {
    for (;;) {
      RenderRequest r;
      {
        std::lock_guard<std::mutex> lock(m);
        if (stop.load() || plan.empty() || plan.front().level != Level::StandIn || plan.front().refine)
          return;
        r = plan.front();
        plan.erase(plan.begin());
        nested = r;
        haveNested = true;
      }
      int w = 0, h = 0;
      Dims(doc->Sizes()[static_cast<size_t>(r.page)], r, w, h);
      RenderResult res;
      res.req = r;
      const bool ok = doc->RenderPage(r.page, w, h, res.bm, [this] { return stop.load(); }, 0, kDraftBudgetMs);
      std::lock_guard<std::mutex> lock(m);
      haveNested = false;
      if (ok)
        done.push_back(std::move(res));
    }
  }

  void Run() {
    for (;;) {
      RenderRequest r;
      {
        std::lock_guard<std::mutex> lock(m);
        if (stop.load() || plan.empty()) {
          running = false;
          haveCurrent = false;
          return;
        }
        r = plan.front();
        plan.erase(plan.begin());
        current = r;
        haveCurrent = true;
        abort.store(false);
      }
      int w = 0, h = 0;
      Dims(doc->Sizes()[static_cast<size_t>(r.page)], r, w, h);
      RenderResult res;
      res.req = r;
      // A stand-in has no use for annotations or LCD text fringes; a first one is a time-boxed draft.
      const bool stand = r.level == Level::StandIn;
      const int flags = stand ? 0 : PdfDocument::kDisplayFlags;
      const int budget = stand && !r.refine ? kDraftBudgetMs : 0;
      // A long sharp render is cut into slices; between slices any waiting draft (the thumbnail strip) draws.
      const std::function<void()> between = [this] { RunWaitingDrafts(); };
      const bool ok = doc->RenderPage(r.page, w, h, res.bm, [this] { return abort.load() || stop.load(); }, flags,
                                      budget, stand ? 0 : kSliceMs, stand ? std::function<void()>() : between);
      // A sharp render also yields a full-quality stand-in for nothing: shrink it instead of drawing the page again.
      RenderResult derived;
      if (ok && !stand && !res.bm.partial) {
        derived.req = RenderRequest{r.page, Level::StandIn, 0, true};
        derived.bm = Downscale(res.bm, kStandInMaxSide);
      }
      std::lock_guard<std::mutex> lock(m);
      haveCurrent = false;
      if (ok) {
        done.push_back(std::move(res));
        if (!derived.bm.bgra.empty())
          done.push_back(std::move(derived));
      } else if (!abort.load() && !stop.load()) {
        res.failed = true; // a real failure: report it so the planner stops asking
        res.bm = {};
        done.push_back(std::move(res));
      }
    }
  }

  /// Replace the queue with the newest plan. A render in progress for a page that is no longer
  /// wanted is stopped; one still wanted is left to finish.
  void SetPlan(std::vector<RenderRequest> p) {
    std::lock_guard<std::mutex> lock(m);
    if (haveCurrent && std::find(p.begin(), p.end(), current) == p.end())
      abort.store(true);
    // The request in flight is not queued again.
    if (haveCurrent)
      p.erase(std::remove(p.begin(), p.end(), current), p.end());
    if (haveNested)  // a draft being drawn between slices of a sharp render
      p.erase(std::remove(p.begin(), p.end(), nested), p.end());
    plan = std::move(p);
    if (!running && !plan.empty()) {
      if (th.joinable())
        th.join();
      running = true;
      th = std::thread([this] { Run(); });
    }
  }

  /// Requests already rendered and waiting for the UI thread to upload them.
  std::vector<RenderRequest> Waiting() {
    std::lock_guard<std::mutex> lock(m);
    std::vector<RenderRequest> w;
    for (const RenderResult& r : done)
      w.push_back(r.req);
    if (haveNested)
      w.push_back(nested);
    return w;
  }

  void Shutdown() {
    stop.store(true);
    abort.store(true);
    if (th.joinable())
      th.join();
  }
};

struct BenchRun {
  bool active = false;
  int pages = 0;
  Clock::time_point t0;
  double firstShownMs = -1;
  double firstSharpMs = -1;
  std::vector<double> viewerMs;
  std::vector<double> frameMs;
  bool scrolling = false;
  bool finished = false; ///< report written; the viewer closes itself and deletes the file
  bool real = false;     ///< a real file named by the user: no scrolling pass, and the file is never deleted
  double thumbFirstMs = -1;  ///< first strip thumbnail on screen
  double thumbAllMs = -1;    ///< every thumbnail in the visible strip on screen
  std::vector<double> thumbTimes;  ///< when each strip thumbnail count was reached
  float scrollPx = 0.f;
  std::filesystem::path file;
};

struct Viewer {
  int id = 0;
  std::string path;
  std::string title;
  bool open = true;
  bool focusNext = false;
  bool osFramed = true;    ///< floating in its own OS window: the OS draws the title bar, so ImGui draws none
  void* framedHwnd = nullptr;  ///< the OS window whose frame colours were last set
  bool placed = false; ///< first-frame position given; after that the user (or the saved layout) owns it
  std::future<PdfDocument::OpenResult> opening;
  bool loaded = false;
  std::string error;
  std::unique_ptr<PdfDocument> doc;
  std::unique_ptr<Worker> worker = std::make_unique<Worker>();
  Layout layout;
  PageCache cache{kCacheCapBytes};
  float pxPerPt = kBasePxPerPt;
  bool continuous = true;
  bool showThumbs = true;
  float thumbW = kThumbWidthDefault;  ///< sidebar width, dragged by its edge
  bool panning = false;                ///< middle button held on the pages
  int curPage = 0;
  float pendingScrollY = -1.f;
  float pendingScrollX = -1.f;
  float lastScrollY = 0.f;
  float lastScrollX = 0.f;
  float viewW = 800.f, viewH = 600.f; ///< the page canvas, for zooming about its centre
  std::vector<int> failedPages;       ///< pages PDFium could not render (reported once, never retried)
  int scrollDir = 1;
  bool fitWidthPending = true;
  int thumbFirst = 0, thumbLast = -1;
  int pageBox = 1;
  char zoomBuf[16] = "100";
  Clock::time_point openedAt = Clock::now();
  double firstPageMs = -1;
  BenchRun bench;
  // REQ-389: the Split dialog. The copy runs on a one-shot worker so a long file never freezes the UI.
  bool splitOpenRequest = false;
  char splitBuf[256] = "";
  std::string splitError;
  std::future<std::string> splitting;
  std::atomic<int> splitProgress{0};
  int splitTotal = 0;
  std::string splitDest;
};

std::vector<std::unique_ptr<Viewer>> g_viewers;
int g_nextId = 1;
int g_focusedId = 0; ///< the viewer that last had focus: the target of the PDFSPLIT command

// Textures are freed at the START of the next frame: this frame's draw lists (the thumbnail strip is
// recorded before the pages are updated) may still point at them until the frame is rendered.
std::vector<GLuint> g_graveyard;

#if defined(_WIN32)
// The viewer's Windows title bar takes GoSurvey's dark chrome instead of the system's light one
// (Windows 11 honours these; older Windows ignores them and keeps its own frame).
void ApplyOsFrameColors(void* hwndRaw, ImU32 bar) {
  using DwmSetFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
  static DwmSetFn setAttr = [] {
    HMODULE m = ::LoadLibraryW(L"dwmapi.dll");
    return m ? reinterpret_cast<DwmSetFn>(::GetProcAddress(m, "DwmSetWindowAttribute")) : nullptr;
  }();
  if (setAttr == nullptr || hwndRaw == nullptr)
    return;
  HWND hwnd = static_cast<HWND>(hwndRaw);
  const COLORREF caption = RGB(bar & 0xFF, (bar >> 8) & 0xFF, (bar >> 16) & 0xFF);
  const COLORREF text = RGB(225, 228, 232);
  const BOOL dark = TRUE;
  setAttr(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
  setAttr(hwnd, 34 /*DWMWA_BORDER_COLOR*/, &caption, sizeof(caption));
  setAttr(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof(caption));
  setAttr(hwnd, 36 /*DWMWA_TEXT_COLOR*/, &text, sizeof(text));
}
#endif

void DeleteTextures(const std::vector<PageCache::Entry>& gone) {
  for (const PageCache::Entry& e : gone)
    if (e.handle != 0)
      g_graveyard.push_back(e.handle);
}

void DrainGraveyard() {
  for (GLuint t : g_graveyard)
    glDeleteTextures(1, &t);
  g_graveyard.clear();
}

void DestroyViewer(Viewer& v) {
  if (v.splitting.valid())
    v.splitting.wait(); // a split in flight finishes (it reads the file, never changes the source)
  v.worker->Shutdown();
  DeleteTextures(v.cache.Clear());
}

float PageTopPt(const Viewer& v, int p) { return v.continuous ? v.layout.top[static_cast<size_t>(p)] : 0.f; }

float ContentHeightPx(const Viewer& v) {
  if (v.continuous)
    return v.layout.totalHeight * v.pxPerPt + 2 * kMarginPx;
  return v.layout.size[static_cast<size_t>(v.curPage)].hPt * v.pxPerPt + 2 * kMarginPx;
}

void SetZoom(Viewer& v, float pxPerPt);

// The -/+ buttons and the typed %: keep the point at the centre of the view where it is.
void ZoomAboutCentre(Viewer& v, float pxPerPt) {
  const float cy = v.lastScrollY + v.viewH * 0.5f, cx = v.lastScrollX + v.viewW * 0.5f;
  const float ptY = (cy - kMarginPx) / v.pxPerPt, ptX = (cx - kMarginPx) / v.pxPerPt;
  SetZoom(v, pxPerPt);
  v.pendingScrollY = std::max(0.f, ptY * v.pxPerPt + kMarginPx - v.viewH * 0.5f);
  v.pendingScrollX = std::max(0.f, ptX * v.pxPerPt + kMarginPx - v.viewW * 0.5f);
}

void SetZoom(Viewer& v, float pxPerPt) {
  v.pxPerPt = std::clamp(pxPerPt, 0.05f, 16.f);
  std::snprintf(v.zoomBuf, sizeof(v.zoomBuf), "%d", static_cast<int>(std::lround(v.pxPerPt / kBasePxPerPt * 100.f)));
}

void GoToPage(Viewer& v, int page) {
  page = std::clamp(page, 0, v.layout.PageCount() - 1);
  v.curPage = page;
  v.pageBox = page + 1;
  v.pendingScrollY = v.continuous ? v.layout.top[static_cast<size_t>(page)] * v.pxPerPt : 0.f;
}

void UploadFinished(Viewer& v, int center, const VisibleRange& keep, std::vector<std::string>& log) {
  size_t uploadedBytes = 0;
  while (uploadedBytes < kUploadBytesPerFrame) {  // the first upload always runs
    RenderResult res;
    {
      std::lock_guard<std::mutex> lock(v.worker->m);
      if (v.worker->done.empty())
        break;
      res = std::move(v.worker->done.front());
      v.worker->done.pop_front();
    }
    if (res.failed) {
      if (std::find(v.failedPages.begin(), v.failedPages.end(), res.req.page) == v.failedPages.end()) {
        v.failedPages.push_back(res.req.page);
        log.push_back("PDF viewer: page " + std::to_string(res.req.page + 1) + " of " + v.title + " could not be rendered.");
      }
      continue;
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0)
      continue;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, res.bm.w, res.bm.h, 0, GL_BGRA, GL_UNSIGNED_BYTE, res.bm.bgra.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    PageCache::Entry e;
    e.page = res.req.page;
    e.level = res.req.level;
    e.scaleKey = res.req.scaleKey;
    e.bytes = res.bm.bgra.size();
    e.handle = tex;
    e.partial = res.bm.partial;
    DeleteTextures(v.cache.Put(e, center, keep));
    uploadedBytes += res.bm.bgra.size();
  }
}

ImTextureID TexId(uint32_t h) { return static_cast<ImTextureID>(static_cast<std::intptr_t>(h)); }

void StartOpen(Viewer& v) {
  const std::filesystem::path p = std::filesystem::u8path(v.path);
  v.opening = std::async(std::launch::async, [p] { return PdfDocument::Open(p); });
  v.openedAt = Clock::now();
}

void FinishBench(Viewer& v, std::vector<std::string>& log) {
  BenchRun& b = v.bench;
  auto pct = [](std::vector<double> x, double q) {
    if (x.empty())
      return 0.0;
    std::sort(x.begin(), x.end());
    return x[std::min(x.size() - 1, static_cast<size_t>(q * static_cast<double>(x.size())))];
  };
  char line[400];
  if (b.real) {
    std::snprintf(line, sizeof(line),
                  "BENCH PDFVIEW %s: first page shown %.0f ms (sharp %.0f ms) | strip thumbnails: first %.0f ms, all %d visible %.0f ms",
                  v.title.c_str(), b.firstShownMs, b.firstSharpMs, b.thumbFirstMs, std::max(0, v.thumbLast - v.thumbFirst + 1),
                  b.thumbAllMs);
    log.push_back(line);
    std::fprintf(stderr, "%s\n", line);
    for (size_t i = 0; i < b.thumbTimes.size(); ++i)
      std::fprintf(stderr, "  thumbnail %zu on screen at %.0f ms\n", i + 1, b.thumbTimes[i]);
    b.active = false;
    b.finished = true;
    return;
  }
  std::snprintf(line, sizeof(line),
                "BENCH PDFVIEW %d pages: first page shown %.0f ms (sharp %.0f ms; target 250) | viewer cost per frame over %zu frames: p95 %.2f ms, worst %.2f ms (target 16) | whole frame: p95 %.1f ms, worst %.1f ms",
                b.pages, b.firstShownMs, b.firstSharpMs, b.viewerMs.size(), pct(b.viewerMs, 0.95),
                b.viewerMs.empty() ? 0.0 : *std::max_element(b.viewerMs.begin(), b.viewerMs.end()), pct(b.frameMs, 0.95),
                b.frameMs.empty() ? 0.0 : *std::max_element(b.frameMs.begin(), b.frameMs.end()));
  log.push_back(line);
  std::fprintf(stderr, "%s\n", line);
  b.active = false;
  b.finished = true;
}

void DrawToolbar(Viewer& v) {
  const int n = v.layout.PageCount();
  const float viewH = v.viewH;
  // Roomier controls, and buttons that read as buttons against the dark window.
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.f, 8.f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.f, 8.f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.27f, 0.35f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.42f, 0.58f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.48f, 0.80f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.42f, 0.48f, 0.58f, 0.9f));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.14f, 0.18f, 1.f));
  ImGui::Spacing();
  ImGui::SetNextItemWidth(72.f);
  if (ImGui::InputInt("##pg", &v.pageBox, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
    GoToPage(v, v.pageBox - 1);
  ImGui::SameLine();
  ImGui::Text("/ %d", n);
  ImGui::SameLine();
  if (ImGui::Button("<"))
    GoToPage(v, v.curPage - 1);
  ImGui::SameLine();
  if (ImGui::Button(">"))
    GoToPage(v, v.curPage + 1);
  ImGui::SameLine();
  ImGui::TextUnformatted("|");
  ImGui::SameLine();
  if (ImGui::Button("-"))
    ZoomAboutCentre(v, v.pxPerPt / 1.25f);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(72.f);
  if (ImGui::InputText("##zoom", v.zoomBuf, sizeof(v.zoomBuf),
                       ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
    const float pct = static_cast<float>(std::atof(v.zoomBuf));
    if (pct > 0.f)
      ZoomAboutCentre(v, pct / 100.f * kBasePxPerPt);
  }
  ImGui::SameLine();
  ImGui::TextUnformatted("%");
  ImGui::SameLine();
  if (ImGui::Button("+"))
    ZoomAboutCentre(v, v.pxPerPt * 1.25f);
  ImGui::SameLine();
  if (ImGui::Button("Fit width"))
    v.fitWidthPending = true;
  ImGui::SameLine();
  if (ImGui::Button("Fit page")) {
    const PageSize& s = v.layout.size[static_cast<size_t>(v.curPage)];
    SetZoom(v, (viewH - 2 * kMarginPx) / s.hPt);
    GoToPage(v, v.curPage);
  }
  ImGui::SameLine();
  if (ImGui::Checkbox("Continuous", &v.continuous))
    GoToPage(v, v.curPage);
  ImGui::SameLine();
  ImGui::Checkbox("Thumbnails", &v.showThumbs);
  ImGui::SameLine();
  if (ImGui::Button("Split..."))
    v.splitOpenRequest = true;
  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar(4);
  ImGui::Spacing();
}

// REQ-389: choose pages, then "Save As..." writes them as a new PDF. The original is never touched.
void DrawSplitDialog(Viewer& v, std::vector<std::string>& log) {
  char id[64];
  std::snprintf(id, sizeof(id), "Split PDF###pdfsplit%d", v.id);
  if (v.splitOpenRequest) {
    v.splitOpenRequest = false;
    v.splitError.clear();
    ImGui::OpenPopup(id);
  }
  ImGui::SetNextWindowSize(ImVec2(460.f, 0.f), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;
  const bool busy = v.splitting.valid();
  if (busy && v.splitting.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    const std::string err = v.splitting.get();
    if (err.empty()) {
      log.push_back("PDF split: saved " + std::to_string(v.splitTotal) + " page(s) to " + v.splitDest);
      ImGui::CloseCurrentPopup();
    } else {
      v.splitError = err;
      log.push_back("PDF split failed: " + err);
    }
  }
  ImGui::Text("Pages to keep (of %d):", v.layout.PageCount());
  ImGui::BeginDisabled(v.splitting.valid());
  ImGui::SetNextItemWidth(-1.f);
  ImGui::InputTextWithHint("##splitpages", "for example 1-5, 9, 12-20", v.splitBuf, sizeof(v.splitBuf));
  ImGui::EndDisabled();
  ImGui::TextDisabled("Saved as a new PDF; the original file is not changed.");
  if (!v.splitError.empty())
    ImGui::TextColored(ImVec4(1.f, 0.45f, 0.4f, 1.f), "%s", v.splitError.c_str());
  if (v.splitting.valid()) {
    const float f = v.splitTotal > 0 ? static_cast<float>(v.splitProgress.load()) / static_cast<float>(v.splitTotal) : 0.f;
    ImGui::ProgressBar(f, ImVec2(-1.f, 0.f));
  } else {
    if (ImGui::Button("Save As...")) {
      const PageListResult pl = ParsePageList(v.splitBuf, v.layout.PageCount());
      if (!pl.error.empty()) {
        v.splitError = pl.error;
      } else {
        const std::string stem = std::filesystem::u8path(v.path).stem().u8string() + "-split.pdf";
        char out[1024] = {};
        if (BrowseSaveFilePdfUtf8(out, sizeof(out), stem.c_str()) && out[0] != '\0') {
          v.splitError.clear();
          v.splitDest = out;
          v.splitTotal = static_cast<int>(pl.pages.size());
          v.splitProgress = 0;
          const std::filesystem::path src = std::filesystem::u8path(v.path);
          const std::filesystem::path dst = std::filesystem::u8path(v.splitDest);
          v.splitting = std::async(std::launch::async, [src, dst, pages = pl.pages, &prog = v.splitProgress] {
            return SplitPdf(src, pages, dst, &prog);
          });
        }
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void DrawThumbnails(Viewer& v) {
  ImGui::BeginChild("##thumbs", ImVec2(v.thumbW, 0.f), true, ImGuiWindowFlags_NoMove);
  const int n = v.layout.PageCount();
  const float boxW = v.thumbW - 30.f;               // widest a thumbnail may be
  const float itemH = boxW * 0.78f + 30.f;          // room for a landscape sheet and its number
  ImGuiListClipper clip;
  clip.Begin(n, itemH);
  v.thumbFirst = n;
  v.thumbLast = -1;
  while (clip.Step()) {
    for (int p = clip.DisplayStart; p < clip.DisplayEnd; ++p) {
      v.thumbFirst = std::min(v.thumbFirst, p);
      v.thumbLast = std::max(v.thumbLast, p);
      ImGui::PushID(p);
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      if (ImGui::Selectable("##t", p == v.curPage, 0, ImVec2(0.f, itemH - 4.f)))
        GoToPage(v, p);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const PageSize& s = v.layout.size[static_cast<size_t>(p)];
      const float maxW = boxW, maxH = itemH - 30.f;
      const float k = std::min(maxW / s.wPt, maxH / s.hPt);
      const ImVec2 a(pos.x + 4.f, pos.y + 2.f);
      const ImVec2 b(a.x + s.wPt * k, a.y + s.hPt * k);
      if (const PageCache::Entry* e = v.cache.Best(p))
        dl->AddImage(TexId(e->handle), a, b);
      else
        dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255));
      dl->AddRect(a, b, IM_COL32(120, 120, 120, 255));
      char num[16];
      std::snprintf(num, sizeof(num), "%d", p + 1);
      dl->AddText(ImVec2(pos.x + 4.f, b.y + 2.f), ImGui::GetColorU32(ImGuiCol_Text), num);
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
}

void DrawPages(Viewer& v, std::vector<std::string>& log, double* viewerCostMs) {
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  ImGui::SetNextWindowContentSize(ImVec2(std::max(avail.x, v.layout.maxWidth * v.pxPerPt + 2 * kMarginPx), ContentHeightPx(v)));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.30f, 0.30f, 0.32f, 1.f));
  // NoMove: a drag on the page pans the page; it must not also carry the whole window along.
  ImGui::BeginChild("##pages", ImVec2(0.f, 0.f), false, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);
  ImGui::PopStyleColor();

  if (v.fitWidthPending) {
    SetZoom(v, std::max(0.05f, (avail.x - 2 * kMarginPx - ImGui::GetStyle().ScrollbarSize) / std::max(1.f, v.layout.maxWidth)));
    v.fitWidthPending = false;
    GoToPage(v, v.curPage);
  }
  if (v.pendingScrollY >= 0.f) {
    ImGui::SetScrollY(v.pendingScrollY);
    v.pendingScrollY = -1.f;
  }
  if (v.pendingScrollX >= 0.f) {
    ImGui::SetScrollX(v.pendingScrollX);
    v.pendingScrollX = -1.f;
  }
  if (v.bench.active && v.bench.scrolling)
    ImGui::SetScrollY(v.bench.scrollPx);

  const bool hovered = ImGui::IsWindowHovered();
  const ImGuiIO& io = ImGui::GetIO();
  if (hovered && io.KeyCtrl && io.MouseWheel != 0.f) {
    // Zoom about the pointer: the point under it stays under it.
    const ImVec2 wp = ImGui::GetWindowPos();
    const float mx = io.MousePos.x - wp.x, my = io.MousePos.y - wp.y;
    const float ptY = (ImGui::GetScrollY() + my - kMarginPx) / v.pxPerPt;
    const float ptX = (ImGui::GetScrollX() + mx - kMarginPx) / v.pxPerPt;
    SetZoom(v, v.pxPerPt * (io.MouseWheel > 0 ? 1.15f : 1.f / 1.15f));
    v.pendingScrollY = std::max(0.f, ptY * v.pxPerPt + kMarginPx - my);
    v.pendingScrollX = std::max(0.f, ptX * v.pxPerPt + kMarginPx - mx);
  }
  // Pan with the middle mouse button (a plain left click or drag does nothing to the view).
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    v.panning = true;
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    v.panning = false;
  if (v.panning) {
    ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
    ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  }
  if ((ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || hovered) && !io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true))
      GoToPage(v, v.curPage + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true))
      GoToPage(v, v.curPage - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_Home))
      GoToPage(v, 0);
    if (ImGui::IsKeyPressed(ImGuiKey_End))
      GoToPage(v, v.layout.PageCount() - 1);
  }

  v.viewW = ImGui::GetWindowWidth();
  v.viewH = ImGui::GetWindowHeight();
  v.lastScrollX = ImGui::GetScrollX();
  const float scrollY = ImGui::GetScrollY();
  if (scrollY != v.lastScrollY)
    v.scrollDir = scrollY > v.lastScrollY ? 1 : -1;
  v.lastScrollY = scrollY;

  const Clock::time_point costStart = Clock::now();
  const float viewH = ImGui::GetWindowHeight();
  VisibleRange vis;
  if (v.continuous) {
    vis = VisiblePages(v.layout, (scrollY - kMarginPx) / v.pxPerPt, viewH / v.pxPerPt);
    if (!vis.Empty()) {
      const int mid = v.layout.PageAt((scrollY - kMarginPx + viewH / 3.f) / v.pxPerPt);
      if (mid != v.curPage) {
        v.curPage = mid;
        v.pageBox = mid + 1;
      }
    }
  } else {
    vis = {v.curPage, v.curPage};
  }

  const int scaleKey = ScaleKeyFor(v.pxPerPt);
  if (!vis.Empty()) {
    UploadFinished(v, v.curPage, vis, log);

    // The plan, then the thumbnail strip's stand-ins behind it.
    // Pages already rendered and waiting for upload, and pages PDFium cannot render, are not asked for again.
    const std::vector<RenderRequest> waiting = v.worker->Waiting();
    auto have = [&](int p) {
      Have h = v.cache.HaveFor(p, scaleKey);
      for (const RenderRequest& w : waiting)
        if (w.page == p) {
          h.standIn = h.standIn || w.level == Level::StandIn;
          h.displayAtKey = h.displayAtKey || (w.level == Level::Display && w.scaleKey == scaleKey);
        }
      if (std::find(v.failedPages.begin(), v.failedPages.end(), p) != v.failedPages.end())
        return Have{true, true};
      return h;
    };
    // Never plan more sharp pages than the cache can hold, or each upload evicts a page the planner
    // asks for again and the worker renders forever. Stand-ins take ~15 % of the cap.
    const PageSize& cs = v.layout.size[static_cast<size_t>(std::clamp(v.curPage, 0, v.layout.PageCount() - 1))];
    int rw = 0, rh = 0;
    Worker::Dims(cs, RenderRequest{v.curPage, Level::Display, scaleKey}, rw, rh);
    const size_t pageBytes = std::max<size_t>(1, static_cast<size_t>(rw) * static_cast<size_t>(rh) * 4u);
    const int readAhead = ReadAheadThatFits(kCacheCapBytes, pageBytes, vis.last - vis.first + 1, kReadAhead);
    std::vector<RenderRequest> plan = PlanRequests(vis, v.layout.PageCount(), v.scrollDir, readAhead, scaleKey, have);
    if (v.showThumbs && v.thumbLast >= v.thumbFirst) {
      // The strip's drafts go AHEAD of the sharp page renders (a dense sheet can take seconds to draw, and
      // the strip should not wait behind it), nearest the current page first; refining a draft goes last.
      std::vector<int> strip;
      for (int p = v.thumbFirst; p <= v.thumbLast; ++p)
        strip.push_back(p);
      std::stable_sort(strip.begin(), strip.end(),
                       [&](int a, int b) { return std::abs(a - v.curPage) < std::abs(b - v.curPage); });
      std::vector<RenderRequest> drafts, refines;
      for (int p : strip) {
        const Have h = have(p);
        if (h.displayAtKey || h.anyDisplay)
          continue;  // the page's own image serves as its thumbnail
        if (!h.standIn)
          drafts.push_back({p, Level::StandIn, 0, false});
        else if (h.standInPartial)
          refines.push_back({p, Level::StandIn, 0, true});
      }
      // A draft the page plan already holds further back is MOVED forward, not left where it was.
      for (const RenderRequest& r : drafts)
        plan.erase(std::remove(plan.begin(), plan.end(), r), plan.end());
      size_t at = 0;
      while (at < plan.size() && plan[at].level != Level::Display)
        ++at;
      for (const RenderRequest& r : drafts)
        plan.insert(plan.begin() + static_cast<long>(at++), r);
      for (const RenderRequest& r : refines)
        if (std::find(plan.begin(), plan.end(), r) == plan.end())
          plan.push_back(r);
    }
    v.worker->SetPlan(std::move(plan));
  }

  // Draw only the visible pages.
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos(); // scrolls with the content
  const float contentW = std::max(avail.x, v.layout.maxWidth * v.pxPerPt + 2 * kMarginPx);
  for (int p = vis.first; !vis.Empty() && p <= vis.last; ++p) {
    const PageSize& s = v.layout.size[static_cast<size_t>(p)];
    const float w = s.wPt * v.pxPerPt, h = s.hPt * v.pxPerPt;
    const float x = origin.x + (contentW - w) * 0.5f;
    const float y = origin.y + kMarginPx + PageTopPt(v, p) * v.pxPerPt;
    const ImVec2 a(x, y), b(x + w, y + h);
    dl->AddRectFilled(ImVec2(a.x + 3.f, a.y + 3.f), ImVec2(b.x + 3.f, b.y + 3.f), IM_COL32(0, 0, 0, 70));
    if (const PageCache::Entry* e = v.cache.Best(p)) {
      dl->AddImage(TexId(e->handle), a, b);
      if (v.firstPageMs < 0.0 && p == 0) {
        v.firstPageMs = MsSince(v.openedAt);
        if (v.bench.active)
          v.bench.firstShownMs = MsSince(v.bench.t0);
      }
      if (v.bench.active && p == 0 && v.bench.firstSharpMs < 0.0 && e->level == Level::Display && e->scaleKey == scaleKey)
        v.bench.firstSharpMs = MsSince(v.bench.t0);
    } else {
      dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255));
      if (std::find(v.failedPages.begin(), v.failedPages.end(), p) != v.failedPages.end())
        dl->AddText(ImVec2(a.x + 12.f, a.y + 12.f), IM_COL32(160, 40, 40, 255), "This page could not be rendered.");
    }
    dl->AddRect(a, b, IM_COL32(90, 90, 90, 255));
  }

  if (v.bench.active && v.bench.real) {
    BenchRun& b = v.bench;
    int present = 0, wanted = 0;
    for (int p = v.thumbFirst; p <= v.thumbLast; ++p) {
      ++wanted;
      present += v.cache.Best(p) != nullptr ? 1 : 0;
    }
    while (static_cast<int>(b.thumbTimes.size()) < present)
      b.thumbTimes.push_back(MsSince(b.t0));
    if (present > 0 && b.thumbFirstMs < 0.0)
      b.thumbFirstMs = MsSince(b.t0);
    if (wanted > 0 && present == wanted && b.thumbAllMs < 0.0)
      b.thumbAllMs = MsSince(b.t0);
    if ((b.thumbAllMs >= 0.0 && b.firstSharpMs >= 0.0) || MsSince(b.t0) > 120000.0)
      FinishBench(v, log);
  } else if (v.bench.active) {
    BenchRun& b = v.bench;
    if (!b.scrolling && b.firstSharpMs >= 0.0) {
      b.scrolling = true;
      b.scrollPx = 0.f;
    } else if (b.scrolling) {
      b.scrollPx += io.DeltaTime * 40.f * 792.f * v.pxPerPt; // ~40 pages a second: a very fast wheel
      if (b.scrollPx >= ImGui::GetScrollMaxY() && ImGui::GetScrollMaxY() > 0.f)
        FinishBench(v, log);
    }
  }
  *viewerCostMs = MsSince(costStart);
  ImGui::EndChild();
}

} // namespace

void OpenPdfInViewer(const std::string& utf8Path) {
  for (auto& v : g_viewers)
    if (v->path == utf8Path) {
      v->focusNext = true;
      return;
    }
  auto v = std::make_unique<Viewer>();
  v->id = g_nextId++;
  v->path = utf8Path;
  v->title = std::filesystem::u8path(utf8Path).filename().u8string();
  v->focusNext = true;
  StartOpen(*v);
  g_viewers.push_back(std::move(v));
}

void ShutdownPdfViewers() {
  for (auto& v : g_viewers)
    DestroyViewer(*v);
  g_viewers.clear();
  DrainGraveyard();
}

void DrawPdfViewers(AppCommandState& cmd, std::vector<std::string>& log) {
  DrainGraveyard();
  if (cmd.pdfViewerPickRequest) {
    cmd.pdfViewerPickRequest = false;
    char tmp[1024] = {};
    if (BrowseOpenFilePdfUtf8(tmp, sizeof(tmp)) && tmp[0] != '\0')
      cmd.pdfViewerOpenRequest = tmp;
  }
  if (!cmd.pdfViewerOpenRequest.empty()) {
    OpenPdfInViewer(cmd.pdfViewerOpenRequest);
    cmd.pdfViewerOpenRequest.clear();
  }
  if (cmd.pdfSplitRequest) {  // PDFSPLIT: open the Split dialog on the viewer that last had focus
    cmd.pdfSplitRequest = false;
    Viewer* target = nullptr;
    for (auto& vp : g_viewers)
      if (vp->loaded && (target == nullptr || vp->id == g_focusedId))
        target = vp.get();
    if (target != nullptr) {
      target->splitOpenRequest = true;
      target->focusNext = true;
    } else {
      log.push_back("PDFSPLIT - open the PDF in the viewer first (PDFVIEW), then run PDFSPLIT.");
    }
  }
  if (!cmd.pdfViewBenchPath.empty()) {  // BENCH PDFVIEW <file>: time a real PDF's first page and thumbnail strip
    const std::string path = cmd.pdfViewBenchPath;
    cmd.pdfViewBenchPath.clear();
    OpenPdfInViewer(path);
    for (auto& vp : g_viewers)
      if (vp->path == path) {
        vp->bench.active = true;
        vp->bench.real = true;
        vp->bench.t0 = Clock::now();
      }
  }
  if (cmd.pdfViewBenchPages > 0) {
    const int pages = cmd.pdfViewBenchPages;
    cmd.pdfViewBenchPages = 0;
    // A name no open viewer can already have, so the new viewer is always the last one in the list.
    static int benchSerial = 0;
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / ("gosurvey_pdfview_bench_" + std::to_string(++benchSerial) + ".pdf");
    {
      const std::string bytes = MakeSyntheticPdf(pages, 400, 50);
      std::ofstream f(file, std::ios::binary);
      f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    OpenPdfInViewer(file.u8string());
    Viewer& v = *g_viewers.back();
    v.bench.active = true;
    v.bench.pages = pages;
    v.bench.t0 = Clock::now();
    v.bench.file = file;
  }

  for (size_t i = 0; i < g_viewers.size();) {
    Viewer& v = *g_viewers[i];
    if (v.opening.valid() && v.opening.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      PdfDocument::OpenResult r = v.opening.get();
      if (r.doc != nullptr) {
        v.doc = std::move(r.doc);
        v.worker->doc = v.doc.get();
        v.layout = Layout::Build(v.doc->Sizes(), kGapPt);
        v.loaded = true;
        log.push_back("PDF viewer: opened " + v.title + " (" + std::to_string(v.doc->PageCount()) + " pages).");
      } else {
        v.error = r.error;
        log.push_back("PDF viewer: cannot open " + v.title + " — " + r.error);
      }
    }

    char name[300];
    std::snprintf(name, sizeof(name), "PDF - %s###pdfview%d", v.title.c_str(), v.id);
    ImGui::SetNextWindowSize(ImVec2(900.f, 700.f), ImGuiCond_FirstUseEver);
    if (v.focusNext) {
      ImGui::SetNextWindowFocus();
      v.focusNext = false;
    }
    bool open = true;
    double cost = 0.0;
    // REQ-387 clause 7: its own Windows window (NoAutoMerge makes ImGui give it a viewport instead of
    // folding it into the main window), with the operating system's frame — minimize, maximize, close
    // and a task-bar entry. Dragging its tab onto a dock slot docks it into GoSurvey's layout.
    ImGuiWindowClass viewerClass;
    viewerClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    viewerClass.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoTaskBarIcon;
    ImGui::SetNextWindowClass(&viewerClass);
    if (!v.placed) {
      const ImGuiViewport* host = ImGui::GetMainViewport();
      const float off = 40.f * static_cast<float>(v.id % 6);
      ImGui::SetNextWindowPos(ImVec2(host->WorkPos.x + 80.f + off, host->WorkPos.y + 60.f + off), ImGuiCond_Always);
      v.placed = true;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    // In its own OS window the operating system draws the title bar and the close button, so ImGui draws no
    // second one. Docked, the tab is the only handle there is, so it comes back. To dock a floating viewer,
    // drag it by any empty part of its toolbar.
    const bool shown = ImGui::Begin(name, &open, v.osFramed ? ImGuiWindowFlags_NoTitleBar : ImGuiWindowFlags_None);
    ImGui::PopStyleVar();
    {
      ImGuiWindow* self = ImGui::GetCurrentWindow();
      v.osFramed = self->ViewportOwned && !self->DockIsActive && self->Viewport != nullptr &&
                   (self->Viewport->Flags & ImGuiViewportFlags_NoDecoration) == 0;
#if defined(_WIN32)
      if (v.osFramed && self->Viewport->PlatformHandleRaw != nullptr && self->Viewport->PlatformHandleRaw != v.framedHwnd) {
        ApplyOsFrameColors(self->Viewport->PlatformHandleRaw, ImGui::GetColorU32(ImGuiCol_MenuBarBg));
        v.framedHwnd = self->Viewport->PlatformHandleRaw;
      }
#endif
    }
    if (shown && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
      g_focusedId = v.id;
    if (shown) {
      if (!v.error.empty()) {
        ImGui::TextWrapped("This PDF could not be opened: %s.", v.error.c_str());
        ImGui::TextDisabled("%s", v.path.c_str());
      } else if (!v.loaded) {
        ImGui::TextUnformatted("Opening...");
      } else {
        DrawToolbar(v);
        DrawSplitDialog(v, log);
        if (v.showThumbs) {
          DrawThumbnails(v);
          // The sidebar's edge: drag it to resize.
          ImGui::SameLine(0.f, 0.f);
          ImGui::InvisibleButton("##thumbsplit", ImVec2(7.f, ImGui::GetContentRegionAvail().y));
          if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
          if (ImGui::IsItemActive())
            v.thumbW = std::clamp(v.thumbW + ImGui::GetIO().MouseDelta.x, kThumbWidthMin, kThumbWidthMax);
          ImGui::GetWindowDrawList()->AddRectFilled(
              ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
              ImGui::IsItemHovered() || ImGui::IsItemActive() ? IM_COL32(90, 130, 190, 255) : IM_COL32(60, 64, 72, 255));
          ImGui::SameLine(0.f, 0.f);
        }
        DrawPages(v, log, &cost);
      }
    }
    ImGui::End();

    if (v.bench.active) {
      v.bench.frameMs.push_back(static_cast<double>(ImGui::GetIO().DeltaTime) * 1000.0);
      if (v.bench.scrolling)
        v.bench.viewerMs.push_back(cost);
    }
    if (!open || v.bench.finished) {
      const std::filesystem::path benchFile = v.bench.file;
      DestroyViewer(v);
      g_viewers.erase(g_viewers.begin() + static_cast<long>(i)); // closes the document
      if (!benchFile.empty()) {
        std::error_code ec;
        std::filesystem::remove(benchFile, ec);
      }
    } else {
      ++i;
    }
  }
}
