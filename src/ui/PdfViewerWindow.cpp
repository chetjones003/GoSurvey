#include "PdfViewerWindow.hpp"

#include "CadCommands.hpp"
#include "PdfDocument.hpp"
#include "PdfViewerCore.hpp"
#include "WinFileDialogs.hpp"

#include <GL/glew.h>
#include <imgui.h>

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
constexpr int kStandInMaxSide = 160;
constexpr int kMaxRenderSide = 4096;
constexpr size_t kUploadBytesPerFrame = 3u * 1024 * 1024; // ADR-067 (c): a burst of finished pages cannot make a long frame
constexpr size_t kCacheCapBytes = 256ull * 1024 * 1024;
constexpr int kReadAhead = 6;
constexpr float kThumbItemH = 150.f;

// One render worker per document (ADR-067 (c)): a one-shot thread that drains the latest plan and
// exits when it is empty, so an idle viewer owns no thread.
struct RenderResult {
  RenderRequest req;
  Bitmap bm;
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
      const bool ok = doc->RenderPage(r.page, w, h, res.bm, [this] { return abort.load() || stop.load(); });
      std::lock_guard<std::mutex> lock(m);
      haveCurrent = false;
      if (ok)
        done.push_back(std::move(res));
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
    plan = std::move(p);
    if (!running && !plan.empty()) {
      if (th.joinable())
        th.join();
      running = true;
      th = std::thread([this] { Run(); });
    }
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
  float scrollPx = 0.f;
  std::filesystem::path file;
};

struct Viewer {
  int id = 0;
  std::string path;
  std::string title;
  bool open = true;
  bool focusNext = false;
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
  int curPage = 0;
  float pendingScrollY = -1.f;
  float pendingScrollX = -1.f;
  float lastScrollY = 0.f;
  int scrollDir = 1;
  bool fitWidthPending = true;
  int thumbFirst = 0, thumbLast = -1;
  int pageBox = 1;
  char zoomBuf[16] = "100";
  Clock::time_point openedAt = Clock::now();
  double firstPageMs = -1;
  BenchRun bench;
};

std::vector<std::unique_ptr<Viewer>> g_viewers;
int g_nextId = 1;

void DeleteTextures(const std::vector<PageCache::Entry>& gone) {
  for (const PageCache::Entry& e : gone) {
    const GLuint t = e.handle;
    if (t != 0)
      glDeleteTextures(1, &t);
  }
}

void DestroyViewer(Viewer& v) {
  v.worker->Shutdown();
  DeleteTextures(v.cache.Clear());
}

float PageTopPt(const Viewer& v, int p) { return v.continuous ? v.layout.top[static_cast<size_t>(p)] : 0.f; }

float ContentHeightPx(const Viewer& v) {
  if (v.continuous)
    return v.layout.totalHeight * v.pxPerPt + 2 * kMarginPx;
  return v.layout.size[static_cast<size_t>(v.curPage)].hPt * v.pxPerPt + 2 * kMarginPx;
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

void UploadFinished(Viewer& v, int center, const VisibleRange& keep) {
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
  char line[300];
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

void DrawToolbar(Viewer& v, float viewH) {
  const int n = v.layout.PageCount();
  ImGui::SetNextItemWidth(60.f);
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
    SetZoom(v, v.pxPerPt / 1.25f);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(50.f);
  if (ImGui::InputText("##zoom", v.zoomBuf, sizeof(v.zoomBuf),
                       ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
    const float pct = static_cast<float>(std::atof(v.zoomBuf));
    if (pct > 0.f)
      SetZoom(v, pct / 100.f * kBasePxPerPt);
  }
  ImGui::SameLine();
  ImGui::TextUnformatted("%");
  ImGui::SameLine();
  if (ImGui::Button("+"))
    SetZoom(v, v.pxPerPt * 1.25f);
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
}

void DrawThumbnails(Viewer& v) {
  ImGui::BeginChild("##thumbs", ImVec2(132.f, 0.f), true);
  const int n = v.layout.PageCount();
  ImGuiListClipper clip;
  clip.Begin(n, kThumbItemH);
  v.thumbFirst = n;
  v.thumbLast = -1;
  while (clip.Step()) {
    for (int p = clip.DisplayStart; p < clip.DisplayEnd; ++p) {
      v.thumbFirst = std::min(v.thumbFirst, p);
      v.thumbLast = std::max(v.thumbLast, p);
      ImGui::PushID(p);
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      if (ImGui::Selectable("##t", p == v.curPage, 0, ImVec2(0.f, kThumbItemH - 4.f)))
        GoToPage(v, p);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const PageSize& s = v.layout.size[static_cast<size_t>(p)];
      const float maxW = 100.f, maxH = kThumbItemH - 30.f;
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
  ImGui::BeginChild("##pages", ImVec2(0.f, 0.f), false, ImGuiWindowFlags_HorizontalScrollbar);
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
  if (hovered && !io.KeyCtrl && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.f)) {
    ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
    ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
  }
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || hovered) {
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true))
      GoToPage(v, v.curPage + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true))
      GoToPage(v, v.curPage - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_Home))
      GoToPage(v, 0);
    if (ImGui::IsKeyPressed(ImGuiKey_End))
      GoToPage(v, v.layout.PageCount() - 1);
  }

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
    UploadFinished(v, v.curPage, vis);

    // The plan, then the thumbnail strip's stand-ins behind it.
    auto have = [&](int p) { return v.cache.HaveFor(p, scaleKey); };
    std::vector<RenderRequest> plan = PlanRequests(vis, v.layout.PageCount(), v.scrollDir, kReadAhead, scaleKey, have);
    if (v.showThumbs) {
      for (int p = v.thumbFirst; p <= v.thumbLast; ++p)
        if (!v.cache.HaveFor(p, scaleKey).standIn) {
          const RenderRequest r{p, Level::StandIn, 0};
          if (std::find(plan.begin(), plan.end(), r) == plan.end())
            plan.push_back(r);
        }
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
    }
    dl->AddRect(a, b, IM_COL32(90, 90, 90, 255));
  }

  if (v.bench.active) {
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
}

void DrawPdfViewers(AppCommandState& cmd, std::vector<std::string>& log) {
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
  if (cmd.pdfViewBenchPages > 0) {
    const int pages = cmd.pdfViewBenchPages;
    cmd.pdfViewBenchPages = 0;
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "gosurvey_pdfview_bench.pdf";
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
    if (ImGui::Begin(name, &open)) {
      if (!v.error.empty()) {
        ImGui::TextWrapped("This PDF could not be opened: %s.", v.error.c_str());
        ImGui::TextDisabled("%s", v.path.c_str());
      } else if (!v.loaded) {
        ImGui::TextUnformatted("Opening...");
      } else {
        DrawToolbar(v, ImGui::GetContentRegionAvail().y);
        if (v.showThumbs) {
          DrawThumbnails(v);
          ImGui::SameLine();
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
