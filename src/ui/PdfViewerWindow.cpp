#include "PdfViewerWindow.hpp"

#include "CadCommands.hpp"
#include "PdfDocument.hpp"
#include "FontRegistry.hpp"
#include "PdfAnnotate.hpp"
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

// ---------------------------------------------------------------------------------------------------
// REQ-388: annotations. The edits live in `AnnotSession` and are drawn over the page by ImGui, so touching
// a page never re-renders it; only "Save As" writes them into a new PDF.
// ---------------------------------------------------------------------------------------------------

enum class Tool { Select, Text, Line, Rect, Ellipse, Calibrate, Length, PolyLength, Area, Angle };

bool IsMeasureTool(Tool t) { return t == Tool::Length || t == Tool::PolyLength || t == Tool::Area || t == Tool::Angle; }

Annot::Kind KindOfMeasureTool(Tool t) {
  return t == Tool::Length ? Annot::Kind::Length : t == Tool::PolyLength ? Annot::Kind::PolyLength
                                                 : t == Tool::Area       ? Annot::Kind::Area
                                                                         : Annot::Kind::Angle;
}

struct FontChoice {
  std::string family;
  bool standard; ///< one of the 14 standard PDF fonts (no embedding); otherwise an installed TrueType family
};

// The standard PDF fonts plus the installed TrueType families GoSurvey itself can load (FontReg).
const std::vector<FontChoice>& FontChoices() {
  static const std::vector<FontChoice> list = [] {
    std::vector<FontChoice> out = {{"Helvetica", true}, {"Times", true}, {"Courier", true}};
    for (const char* f : {"Arial", "Times New Roman", "Courier New", "Calibri", "Verdana", "Tahoma", "Consolas",
                          "Georgia", "Segoe UI"}) {
      const std::string p = FontReg::FindTtfPath(f, false, false);
      if (p.size() > 4 && p.compare(p.size() - 4, 4, ".ttf") == 0) // .ttc collections are not embedded
        out.push_back({f, false});
    }
    return out;
  }();
  return list;
}

struct AnnotUi {
  AnnotSession session;
  Tool tool = Tool::Select;
  float color[3] = {0.85f, 0.10f, 0.10f};
  float thickness = 2.f;
  bool fill = false;
  int fontIdx = 0;
  bool bold = false, italic = false;
  float fontSize = 14.f;
  int decimals = 2;                                   ///< REQ-391: digits in a dimension's label
  std::vector<std::pair<float, float>> measurePts;    ///< REQ-391: the dimension being clicked out
  int measurePage = 0;
  int selected = -1;
  enum class Drag { None, Create, Move, Handle } drag = Drag::None;
  int dragPage = 0;
  float px0 = 0.f, py0 = 0.f; ///< where the press landed, in page points
  int handle = -1;
  Annot original, preview;    ///< the item before the drag, and as it looks mid-drag
  bool textPopup = false;     ///< open the text dialog on the next frame
  int textPage = 0;
  float textX = 0.f, textY = 0.f;
  int textEdit = -1;          ///< item being edited, or -1 for a new note
  char textBuf[1024] = "";
  std::future<std::string> saving;
  std::string saveDest;
  std::string status;
  // REQ-390: page scale. What the file already says, read in the background; the session holds the edits.
  std::future<ScaleRead> readingScales;
  ScaleRead fileScales;
  bool scalesRequested = false;
  bool scalesRead = false;
  bool scaleDialog = false;      ///< open the Set scale dialog on the next frame
  int scaleMode = 0;             ///< 0 preset, 1 typed ratio
  int presetIdx = 8;             ///< index into PresetScales()
  double typedPage = 1.0, typedReal = 20.0;
  int typedPageUnit = 0, typedRealUnit = 1; ///< indexes into AllUnits(): inch, foot
  int scope = 0;                 ///< 0 this page, 1 every page, 2 the pages typed in `scopeBuf`
  char scopeBuf[128] = "";
  std::string scaleError;
  int calCount = 0;              ///< points picked so far (0..2)
  int calPage = 0;
  float calX[2] = {0.f, 0.f}, calY[2] = {0.f, 0.f};
  bool calPopup = false;         ///< both points picked: ask for the real distance
  double calReal = 10.0;
  int calUnit = 1;
  bool closePrompt = false;   ///< the window was closed with unsaved annotations: ask first
  bool closeAfterSave = false;
  bool requestClose = false;
  bool discardOk = false;
};

unsigned PackColor(const float c[3]) {
  const auto b = [](float v) { return static_cast<unsigned>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
  return (b(c[0]) << 16) | (b(c[1]) << 8) | b(c[2]);
}
void UnpackColor(unsigned rgb, float c[3]) {
  c[0] = static_cast<float>((rgb >> 16) & 0xFF) / 255.f;
  c[1] = static_cast<float>((rgb >> 8) & 0xFF) / 255.f;
  c[2] = static_cast<float>(rgb & 0xFF) / 255.f;
}
ImU32 ToImCol(unsigned rgb, int a = 255) {
  return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, a);
}

// A note's box follows its text and size: anchored at its top-left corner.
void FitTextBox(Annot& a) {
  float w = 0.f, h = 0.f;
  EstimateTextBox(a.text, a.fontSize, w, h);
  const float l = std::min(a.x0, a.x1), t = std::max(a.y0, a.y1);
  a.x0 = l;
  a.x1 = l + w;
  a.y1 = t;
  a.y0 = t - h;
}

// Font, colour, width and fill from the tool settings onto an annotation.
void ApplyStyle(const AnnotUi& u, Annot& a) {
  a.color = PackColor(u.color);
  if (a.kind == Annot::Kind::Text) {
    const FontChoice& f = FontChoices()[static_cast<size_t>(std::clamp(u.fontIdx, 0, static_cast<int>(FontChoices().size()) - 1))];
    a.font = f.family;
    a.bold = u.bold;
    a.italic = u.italic;
    a.fontSize = std::clamp(u.fontSize, 4.f, 200.f);
    a.fontFile = f.standard ? std::string() : FontReg::FindTtfPath(f.family, u.bold, u.italic);
    FitTextBox(a);
  } else if (a.IsDimension()) {
    a.thickness = u.thickness;
    a.fontSize = std::clamp(u.fontSize, 4.f, 200.f);
    a.decimals = std::clamp(u.decimals, 0, 6);
  } else {
    a.thickness = u.thickness;
    a.fill = a.kind != Annot::Kind::Line && u.fill;
  }
}

// The reverse: the tool settings show the selected annotation's style.
void LoadStyle(AnnotUi& u, const Annot& a) {
  UnpackColor(a.color, u.color);
  if (a.kind == Annot::Kind::Text) {
    u.bold = a.bold;
    u.italic = a.italic;
    u.fontSize = a.fontSize;
    for (size_t i = 0; i < FontChoices().size(); ++i)
      if (FontChoices()[i].family == a.font)
        u.fontIdx = static_cast<int>(i);
  } else if (a.IsDimension()) {
    u.thickness = a.thickness;
    u.fontSize = a.fontSize;
    u.decimals = a.decimals;
  } else {
    u.thickness = a.thickness;
    u.fill = a.fill;
  }
}

// The bounding box of an annotation in page points.
void BoundsOf(const Annot& a, float& l, float& b, float& r, float& t) {
  if (a.IsDimension() && !a.pts.empty()) {
    l = r = a.pts[0].first;
    b = t = a.pts[0].second;
    for (const auto& p : a.pts) {
      l = std::min(l, p.first);
      r = std::max(r, p.first);
      b = std::min(b, p.second);
      t = std::max(t, p.second);
    }
    return;
  }
  l = std::min(a.x0, a.x1);
  r = std::max(a.x0, a.x1);
  b = std::min(a.y0, a.y1);
  t = std::max(a.y0, a.y1);
}

float DistToSegment(float px, float py, float ax, float ay, float bx, float by) {
  const float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
  float t = len2 > 0.f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.f;
  t = std::clamp(t, 0.f, 1.f);
  return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

// Does a click at page point (x, y) land on the annotation? `tol` is the pick slack in points.
bool HitTest(const Annot& a, float x, float y, float tol) {
  const float l = std::min(a.x0, a.x1), r = std::max(a.x0, a.x1), b = std::min(a.y0, a.y1), t = std::max(a.y0, a.y1);
  switch (a.kind) {
  case Annot::Kind::Length:
  case Annot::Kind::PolyLength:
  case Annot::Kind::Area:
  case Annot::Kind::Angle: {
    const bool closed = a.kind == Annot::Kind::Area;
    const size_t n = a.pts.size();
    for (size_t i = 0; i + 1 < n + (closed && n > 2 ? 1 : 0); ++i) {
      const auto& p = a.pts[i];
      const auto& q = a.pts[(i + 1) % n];
      if (DistToSegment(x, y, p.first, p.second, q.first, q.second) <= a.thickness * 0.5f + tol)
        return true;
    }
    if (closed && n > 2) { // inside the area counts too
      bool in = false;
      for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const auto& p = a.pts[i];
        const auto& q = a.pts[j];
        if (((p.second > y) != (q.second > y)) && (x < (q.first - p.first) * (y - p.second) / (q.second - p.second) + p.first))
          in = !in;
      }
      return in;
    }
    return false;
  }
  case Annot::Kind::Line:
    return DistToSegment(x, y, a.x0, a.y0, a.x1, a.y1) <= std::max(a.thickness * 0.5f, 0.f) + tol;
  case Annot::Kind::Text:
    return x >= l - tol && x <= r + tol && y >= b - tol && y <= t + tol;
  case Annot::Kind::Rect: {
    const float e = tol + a.thickness * 0.5f;
    const bool outer = x >= l - e && x <= r + e && y >= b - e && y <= t + e;
    const bool inner = x > l + e && x < r - e && y > b + e && y < t - e;
    return outer && (a.fill || !inner);
  }
  case Annot::Kind::Ellipse: {
    const float rx = std::max(0.5f, (r - l) * 0.5f), ry = std::max(0.5f, (t - b) * 0.5f);
    const float d = std::hypot((x - (l + r) * 0.5f) / rx, (y - (b + t) * 0.5f) / ry);
    if (a.fill && d <= 1.f)
      return true;
    return std::fabs(d - 1.f) * std::min(rx, ry) <= tol + a.thickness * 0.5f;
  }
  }
  return false;
}

// The grips of a selected annotation, in page points: a Line's two ends, a shape's four corners
// (clockwise from top-left), a note's bottom-right corner (it scales the text).
std::vector<std::pair<float, float>> HandlePoints(const Annot& a) {
  if (a.IsDimension())
    return a.pts;
  const float l = std::min(a.x0, a.x1), r = std::max(a.x0, a.x1), b = std::min(a.y0, a.y1), t = std::max(a.y0, a.y1);
  if (a.kind == Annot::Kind::Line)
    return {{a.x0, a.y0}, {a.x1, a.y1}};
  if (a.kind == Annot::Kind::Text)
    return {{r, b}};
  return {{l, t}, {r, t}, {r, b}, {l, b}};
}

void MoveHandle(Annot& a, const Annot& original, int handle, float x, float y) {
  if (a.IsDimension()) {
    if (handle >= 0 && handle < static_cast<int>(a.pts.size()))
      a.pts[static_cast<size_t>(handle)] = {x, y};
  } else if (a.kind == Annot::Kind::Line) {
    (handle == 0 ? a.x0 : a.x1) = x;
    (handle == 0 ? a.y0 : a.y1) = y;
  } else if (a.kind == Annot::Kind::Text) {
    const float top = std::max(original.y0, original.y1);
    const float oldH = std::max(1.f, top - std::min(original.y0, original.y1));
    a = original;
    a.fontSize = std::clamp(original.fontSize * std::max(0.05f, (top - y) / oldH), 4.f, 200.f);
    FitTextBox(a);
  } else {
    const auto corners = HandlePoints(original);
    const auto opp = corners[static_cast<size_t>((handle + 2) % 4)];
    a.x0 = opp.first;
    a.y0 = opp.second;
    a.x1 = x;
    a.y1 = y;
  }
}

ImFont* FontForNote(const Annot& a) {
  std::string name = a.font == "Helvetica" ? "Arial" : a.font == "Times" ? "Times New Roman" : a.font == "Courier" ? "Courier New" : a.font;
  return FontReg::Resolve(name, a.bold, a.italic);
}

// Draw an annotation over its page. `tl` is the page's top-left on screen, `k` the screen pixels per point.
void DrawAnnot(ImDrawList* dl, const Annot& a, ImVec2 tl, float hPt, float k, bool selected, const PageScale* scale = nullptr,
               const ImVec2* rubberTo = nullptr) {
  const auto S = [&](float x, float y) { return ImVec2(tl.x + x * k, tl.y + (hPt - y) * k); };
  const ImU32 col = ToImCol(a.color);
  const float th = std::max(1.f, a.thickness * k);
  float l, b, r, t;
  BoundsOf(a, l, b, r, t);
  switch (a.kind) {
  case Annot::Kind::Length:
  case Annot::Kind::PolyLength:
  case Annot::Kind::Area:
  case Annot::Kind::Angle: {
    std::vector<ImVec2> sp;
    for (const auto& p : a.pts)
      sp.push_back(S(p.first, p.second));
    if (rubberTo != nullptr)
      sp.push_back(*rubberTo); // the next point follows the pointer while the dimension is being clicked out
    if (sp.size() >= 2)
      dl->AddPolyline(sp.data(), static_cast<int>(sp.size()), col,
                      a.kind == Annot::Kind::Area && sp.size() > 2 ? ImDrawFlags_Closed : ImDrawFlags_None, th);
    for (const ImVec2& p : sp)
      dl->AddCircleFilled(p, std::max(2.f, th * 0.9f), col);
    if (rubberTo == nullptr && DimensionComplete(a)) {
      const std::string label = scale != nullptr || a.kind == Annot::Kind::Angle
                                    ? DimensionLabel(a, scale != nullptr ? *scale : PageScale{})
                                    : std::string("no scale");
      const auto [ax, ay] = DimensionLabelAnchor(a);
      const float fsPx = std::max(4.f, a.fontSize) * k;
      ImFont* font = ImGui::GetFont();
      const ImVec2 ts = font->CalcTextSizeA(fsPx, 1e9f, 0.f, label.c_str());
      const ImVec2 c = S(ax, ay);
      const ImVec2 p0(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
      dl->AddRectFilled(ImVec2(p0.x - 3.f, p0.y - 1.f), ImVec2(p0.x + ts.x + 3.f, p0.y + ts.y + 1.f), IM_COL32(255, 255, 255, 215));
      dl->AddText(font, fsPx, p0, col, label.c_str());
    }
    break;
  }
  case Annot::Kind::Line:
    dl->AddLine(S(a.x0, a.y0), S(a.x1, a.y1), col, th);
    break;
  case Annot::Kind::Rect:
    if (a.fill)
      dl->AddRectFilled(S(l, t), S(r, b), col);
    dl->AddRect(S(l, t), S(r, b), col, 0.f, 0, th);
    break;
  case Annot::Kind::Ellipse: {
    const ImVec2 c = S((l + r) * 0.5f, (b + t) * 0.5f), rad((r - l) * 0.5f * k, (t - b) * 0.5f * k);
    if (a.fill)
      dl->AddEllipseFilled(c, rad, col);
    dl->AddEllipse(c, rad, col, 0.f, 0, th);
    break;
  }
  case Annot::Kind::Text:
    dl->AddText(FontForNote(a), a.fontSize * k, S(l, t), col, a.text.c_str());
    break;
  }
  if (selected) {
    const float pad = 3.f;
    if (a.kind == Annot::Kind::Line)
      dl->AddLine(S(a.x0, a.y0), S(a.x1, a.y1), IM_COL32(60, 140, 255, 110), th + 6.f);
    else if (a.IsDimension()) {
      std::vector<ImVec2> sp;
      for (const auto& p : a.pts)
        sp.push_back(S(p.first, p.second));
      if (sp.size() >= 2)
        dl->AddPolyline(sp.data(), static_cast<int>(sp.size()), IM_COL32(60, 140, 255, 110),
                        a.kind == Annot::Kind::Area && sp.size() > 2 ? ImDrawFlags_Closed : ImDrawFlags_None, th + 6.f);
    } else
      dl->AddRect(ImVec2(S(l, t).x - pad, S(l, t).y - pad), ImVec2(S(r, b).x + pad, S(r, b).y + pad),
                  IM_COL32(60, 140, 255, 255), 0.f, 0, 1.f);
    for (const auto& h : HandlePoints(a)) {
      const ImVec2 p = S(h.first, h.second);
      dl->AddRectFilled(ImVec2(p.x - 4.f, p.y - 4.f), ImVec2(p.x + 4.f, p.y + 4.f), IM_COL32(255, 255, 255, 255));
      dl->AddRect(ImVec2(p.x - 4.f, p.y - 4.f), ImVec2(p.x + 4.f, p.y + 4.f), IM_COL32(60, 140, 255, 255));
    }
  }
}

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
  AnnotUi ann; ///< REQ-388
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
  if (v.ann.saving.valid())
    v.ann.saving.wait(); // so does a Save As
  if (v.ann.readingScales.valid())
    v.ann.readingScales.wait();
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

const PageScale* EffectiveScale(const Viewer& v, int page);

void StartSave(Viewer& v, std::vector<std::string>& log) {
  AnnotUi& u = v.ann;
  const std::string stem = std::filesystem::u8path(v.path).stem().u8string() + "-annotated.pdf";
  char out[1024] = {};
  if (!BrowseSaveFilePdfUtf8(out, sizeof(out), stem.c_str()) || out[0] == 0) {
    u.closeAfterSave = false;
    return;
  }
  u.saveDest = out;
  u.status = "Saving...";
  const std::filesystem::path src = std::filesystem::u8path(v.path), dst = std::filesystem::u8path(u.saveDest);
  std::map<int, PageScale> inForce; // the scale on each page that has a dimension: its label is worked out from it
  for (const Annot& a : u.session.Items())
    if (a.IsDimension())
      if (const PageScale* s = EffectiveScale(v, a.page))
        inForce[a.page] = *s;
  u.saving = std::async(std::launch::async, [src, dst, items = u.session.Items(), scales = u.session.Scales(), inForce] {
    return SaveAnnotated(src, items, dst, scales, inForce);
  });
  (void)log;
}

void SelectAnnot(AnnotUi& u, int index) {
  u.selected = index;
  if (index >= 0 && index < static_cast<int>(u.session.Items().size()))
    LoadStyle(u, u.session.Items()[static_cast<size_t>(index)]);
}

// The scale in force on a page: this session's change if there is one, else what the file says. Null = unscaled.
const PageScale* EffectiveScale(const Viewer& v, int page) {
  const AnnotUi& u = v.ann;
  const auto ov = u.session.Scales().find(page);
  if (ov != u.session.Scales().end())
    return ov->second.Valid() ? &ov->second : nullptr;
  const auto f = u.fileScales.scales.find(page);
  return f != u.fileScales.scales.end() ? &f->second : nullptr;
}

// Which pages a scale change applies to (REQ-390 clause 2). False with a message when the typed range is bad.
bool ScopePages(const Viewer& v, std::vector<int>& pages, std::string& err) {
  const AnnotUi& u = v.ann;
  const int n = v.layout.PageCount();
  pages.clear();
  if (u.scope == 0) {
    pages.push_back(std::clamp(v.curPage, 0, n - 1));
  } else if (u.scope == 1) {
    for (int i = 0; i < n; ++i)
      pages.push_back(i);
  } else {
    const PageListResult pl = ParsePageList(u.scopeBuf, n);
    if (!pl.error.empty()) {
      err = pl.error;
      return false;
    }
    pages = pl.pages;
  }
  return true;
}

void DrawScopeChooser(Viewer& v) {
  AnnotUi& u = v.ann;
  ImGui::TextUnformatted("Apply to:");
  ImGui::SameLine();
  ImGui::RadioButton("This page", &u.scope, 0);
  ImGui::SameLine();
  ImGui::RadioButton("Every page", &u.scope, 1);
  ImGui::SameLine();
  ImGui::RadioButton("Pages:", &u.scope, 2);
  ImGui::SameLine();
  ImGui::BeginDisabled(u.scope != 2);
  ImGui::SetNextItemWidth(140.f);
  ImGui::InputTextWithHint("##scopepages", "1-5, 9, 12-20", u.scopeBuf, sizeof(u.scopeBuf));
  ImGui::EndDisabled();
}

// Applies \p scale to the chosen pages as one undo step. A scale that is not valid clears them.
bool ApplyScaleToScope(Viewer& v, const PageScale& scale, std::vector<std::string>& log) {
  AnnotUi& u = v.ann;
  std::vector<int> pages;
  if (!ScopePages(v, pages, u.scaleError))
    return false;
  std::map<int, PageScale> change;
  for (int p : pages)
    change[p] = scale;
  u.session.SetScales(change);
  u.scaleError.clear();
  log.push_back(scale.Valid() ? "PDF scale: " + scale.RatioText() + " set on " + std::to_string(pages.size()) + " page(s) of " + v.title
                              : "PDF scale: cleared on " + std::to_string(pages.size()) + " page(s) of " + v.title);
  return true;
}

// The Set scale dialog and the Known distance popup that follows two calibration clicks.
void DrawScaleDialogs(Viewer& v, std::vector<std::string>& log) {
  AnnotUi& u = v.ann;
  char id[64];
  std::snprintf(id, sizeof(id), "Set scale###pdfscale%d", v.id);
  if (u.scaleDialog) {
    u.scaleDialog = false;
    u.scaleError.clear();
    u.scalesRequested = true; // reading what the file already has is wanted now
    ImGui::OpenPopup(id);
  }
  ImGui::SetNextWindowSize(ImVec2(520.f, 0.f), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const PageScale* cur = EffectiveScale(v, v.curPage);
    ImGui::Text("Page %d now: %s", v.curPage + 1, cur != nullptr ? cur->RatioText().c_str() : "unscaled");
    ImGui::Separator();
    ImGui::RadioButton("Preset", &u.scaleMode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Typed ratio", &u.scaleMode, 1);
    const auto& presets = PresetScales();
    u.presetIdx = std::clamp(u.presetIdx, 0, static_cast<int>(presets.size()) - 1);
    if (u.scaleMode == 0) {
      ImGui::SetNextItemWidth(240.f);
      if (ImGui::BeginCombo("##preset", presets[static_cast<size_t>(u.presetIdx)].label.c_str())) {
        for (size_t i = 0; i < presets.size(); ++i) {
          if (i == 0 || i == 11 || i == 19)
            ImGui::SeparatorText(i == 0 ? "Architectural" : i == 11 ? "Engineering" : "Metric");
          if (ImGui::Selectable(presets[i].label.c_str(), static_cast<int>(i) == u.presetIdx))
            u.presetIdx = static_cast<int>(i);
        }
        ImGui::EndCombo();
      }
    } else {
      const auto& units = AllUnits();
      const auto unitCombo = [&](const char* label, int& idx) {
        ImGui::SetNextItemWidth(110.f);
        if (ImGui::BeginCombo(label, UnitName(units[static_cast<size_t>(idx)]))) {
          for (size_t i = 0; i < units.size(); ++i)
            if (ImGui::Selectable(UnitName(units[i]), static_cast<int>(i) == idx))
              idx = static_cast<int>(i);
          ImGui::EndCombo();
        }
      };
      ImGui::SetNextItemWidth(90.f);
      ImGui::InputDouble("##pv", &u.typedPage, 0.0, 0.0, "%.5g");
      ImGui::SameLine();
      unitCombo("##pu", u.typedPageUnit);
      ImGui::SameLine();
      ImGui::TextUnformatted("on the sheet  =");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(90.f);
      ImGui::InputDouble("##rv", &u.typedReal, 0.0, 0.0, "%.5g");
      ImGui::SameLine();
      unitCombo("##ru", u.typedRealUnit);
      ImGui::SameLine();
      ImGui::TextUnformatted("in reality");
    }
    DrawScopeChooser(v);
    if (!u.scaleError.empty())
      ImGui::TextColored(ImVec4(1.f, 0.45f, 0.4f, 1.f), "%s", u.scaleError.c_str());
    if (ImGui::Button("Apply")) {
      PageScale s;
      if (u.scaleMode == 0) {
        s = presets[static_cast<size_t>(u.presetIdx)];
      } else {
        s.pageValue = u.typedPage;
        s.pageUnit = AllUnits()[static_cast<size_t>(u.typedPageUnit)];
        s.realValue = u.typedReal;
        s.realUnit = AllUnits()[static_cast<size_t>(u.typedRealUnit)];
      }
      if (!s.Valid())
        u.scaleError = "both lengths must be bigger than zero";
      else if (ApplyScaleToScope(v, s, log))
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Calibrate from two points...")) {
      u.tool = Tool::Calibrate;
      u.calCount = 0;
      u.selected = -1;
      u.status = "Click two points a known distance apart on the page.";
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear scale")) {
      if (ApplyScaleToScope(v, PageScale{0.0, Unit::Inch, 0.0, Unit::Foot, ""}, log))
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  std::snprintf(id, sizeof(id), "Known distance###pdfcal%d", v.id);
  if (u.calPopup) {
    u.calPopup = false;
    u.scaleError.clear();
    ImGui::OpenPopup(id);
  }
  if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const double pts = std::hypot(static_cast<double>(u.calX[1] - u.calX[0]), static_cast<double>(u.calY[1] - u.calY[0]));
    ImGui::Text("The two points are %.1f points (%.3f in) apart on the sheet.", pts, pts / 72.0);
    ImGui::TextUnformatted("In reality they are:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.f);
    ImGui::InputDouble("##calreal", &u.calReal, 0.0, 0.0, "%.5g");
    ImGui::SameLine();
    const auto& units = AllUnits();
    u.calUnit = std::clamp(u.calUnit, 0, static_cast<int>(units.size()) - 1);
    ImGui::SetNextItemWidth(110.f);
    if (ImGui::BeginCombo("##calunit", UnitName(units[static_cast<size_t>(u.calUnit)]))) {
      for (size_t i = 0; i < units.size(); ++i)
        if (ImGui::Selectable(UnitName(units[i]), static_cast<int>(i) == u.calUnit))
          u.calUnit = static_cast<int>(i);
      ImGui::EndCombo();
    }
    DrawScopeChooser(v);
    if (!u.scaleError.empty())
      ImGui::TextColored(ImVec4(1.f, 0.45f, 0.4f, 1.f), "%s", u.scaleError.c_str());
    if (ImGui::Button("Set scale")) {
      const PageScale s = ScaleFromCalibration(pts, u.calReal, units[static_cast<size_t>(u.calUnit)]);
      if (!s.Valid())
        u.scaleError = "the distance must be bigger than zero, and the two points must be apart";
      else if (ApplyScaleToScope(v, s, log)) {
        u.status = "Scale set: " + s.RatioText();
        u.calCount = 0;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      u.calCount = 0;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

// The annotation row under the toolbar: tools, style, undo/redo, Save As, and the two small dialogs
// (note text, unsaved-changes prompt).
void DrawAnnotBar(Viewer& v, std::vector<std::string>& log) {
  AnnotUi& u = v.ann;
  if (u.saving.valid() && u.saving.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    const std::string err = u.saving.get();
    if (err.empty()) {
      u.session.MarkSaved();
      u.status = "Saved a copy: " + u.saveDest;
      log.push_back("PDF annotations: saved " + u.saveDest);
      if (u.closeAfterSave)
        u.requestClose = true;
    } else {
      u.status = "Save As failed: " + err;
      log.push_back("PDF annotations: Save As failed - " + err);
    }
    u.closeAfterSave = false;
  }
  // REQ-390: read the scales the file already has, in the background. Small files at once (after the first
  // page is up); a big one only when the user asks (Set scale / a measure tool), so opening stays fast.
  if (!u.scalesRequested && !u.scalesRead && v.firstPageMs >= 0.0 && MsSince(v.openedAt) > v.firstPageMs + 2000.0) {
    std::error_code ec;
    if (std::filesystem::file_size(std::filesystem::u8path(v.path), ec) < 40ull * 1024 * 1024)
      u.scalesRequested = true;
  }
  if (u.scalesRequested && !u.scalesRead && !u.readingScales.valid()) {
    const std::filesystem::path p = std::filesystem::u8path(v.path);
    u.readingScales = std::async(std::launch::async, [p] { return ReadPageScales(p); });
  }
  if (u.readingScales.valid() && u.readingScales.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    u.fileScales = u.readingScales.get();
    u.scalesRead = true;
    if (!u.fileScales.error.empty())
      log.push_back("PDF scale: could not read the scales in " + v.title + " - " + u.fileScales.error);
    for (const auto& [page, why] : u.fileScales.unusable)
      log.push_back("PDF scale: page " + std::to_string(page + 1) + " of " + v.title + " has a scale that cannot be used - " + why);
  }
  const bool saving = u.saving.valid();
  const auto& items = u.session.Items();
  if (u.selected >= static_cast<int>(items.size()))
    u.selected = -1;

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 5.f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 6.f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
  ImGui::TextUnformatted("Annotate:");
  ImGui::SameLine();
  const auto toolButton = [&](const char* label, Tool t) {
    const bool on = u.tool == t;
    if (on) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.48f, 0.80f, 1.f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.54f, 0.86f, 1.f));
    }
    if (ImGui::Button(label)) {
      u.tool = t;
      u.measurePts.clear();
      u.status.clear();
      if (t != Tool::Select)
        u.selected = -1;
      if (IsMeasureTool(t))
        u.scalesRequested = true; // the page's scale is needed: read what the file has
    }
    if (on)
      ImGui::PopStyleColor(2);
    ImGui::SameLine();
  };
  toolButton("Select", Tool::Select);
  toolButton("Text", Tool::Text);
  toolButton("Line", Tool::Line);
  toolButton("Rectangle", Tool::Rect);
  toolButton("Ellipse", Tool::Ellipse);

  bool changed = false;
  changed |= ImGui::ColorEdit3("##annotcol", u.color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
  ImGui::SameLine();
  const Annot* sel = u.selected >= 0 ? &items[static_cast<size_t>(u.selected)] : nullptr;
  const bool textStyle = u.tool == Tool::Text || (sel != nullptr && sel->kind == Annot::Kind::Text);
  if (textStyle) {
    ImGui::SetNextItemWidth(130.f);
    const auto& fonts = FontChoices();
    u.fontIdx = std::clamp(u.fontIdx, 0, static_cast<int>(fonts.size()) - 1);
    if (ImGui::BeginCombo("##annotfont", fonts[static_cast<size_t>(u.fontIdx)].family.c_str())) {
      for (size_t i = 0; i < fonts.size(); ++i)
        if (ImGui::Selectable(fonts[i].family.c_str(), static_cast<int>(i) == u.fontIdx)) {
          u.fontIdx = static_cast<int>(i);
          changed = true;
        }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Bold", &u.bold);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Italic", &u.italic);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.f);
    changed |= ImGui::DragFloat("##annotsize", &u.fontSize, 0.25f, 4.f, 200.f, "%.0f pt");
  } else {
    ImGui::SetNextItemWidth(110.f);
    changed |= ImGui::SliderFloat("##annotw", &u.thickness, 0.5f, 20.f, "%.1f pt");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Line thickness");
    if (u.tool != Tool::Line && !IsMeasureTool(u.tool) &&
        (sel == nullptr || (sel->kind != Annot::Kind::Line && !sel->IsDimension()))) {
      ImGui::SameLine();
      changed |= ImGui::Checkbox("Fill", &u.fill);
    }
  }
  if (changed && sel != nullptr) {
    Annot a = *sel;
    ApplyStyle(u, a);
    u.session.Replace(u.selected, a);
  }
  ImGui::SameLine();
  ImGui::TextUnformatted("|");
  ImGui::SameLine();
  ImGui::BeginDisabled(!u.session.CanUndo() || saving);
  if (ImGui::Button("Undo")) {
    u.session.Undo();
    u.selected = -1;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!u.session.CanRedo() || saving);
  if (ImGui::Button("Redo")) {
    u.session.Redo();
    u.selected = -1;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(u.selected < 0 || saving);
  if (ImGui::Button("Delete")) {
    u.session.Remove(u.selected);
    u.selected = -1;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!u.session.Dirty() || saving);
  if (ImGui::Button("Save As..."))
    StartSave(v, log);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextUnformatted("|");
  ImGui::SameLine();
  if (ImGui::Button("Set scale..."))
    u.scaleDialog = true;
  ImGui::SameLine();
  {
    const PageScale* cur = EffectiveScale(v, v.curPage);
    const auto unusable = u.fileScales.unusable.find(v.curPage);
    if (cur != nullptr)
      ImGui::Text("Scale: %s", cur->RatioText().c_str());
    else if (!u.scalesRead && u.scalesRequested)
      ImGui::TextDisabled("Scale: reading...");
    else if (unusable != u.fileScales.unusable.end())
      ImGui::TextDisabled("Unscaled (%s)", unusable->second.c_str());
    else
      ImGui::TextDisabled(u.scalesRead ? "Unscaled" : "Scale: not read yet");
  }
  if (!u.status.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", u.status.c_str());
  }
  // REQ-391: the scaled dimension tools, on their own row.
  ImGui::TextUnformatted("Measure:");
  ImGui::SameLine();
  toolButton("Length", Tool::Length);
  toolButton("Polylength", Tool::PolyLength);
  toolButton("Area", Tool::Area);
  toolButton("Angle", Tool::Angle);
  {
    const Annot* ds = u.selected >= 0 ? &items[static_cast<size_t>(u.selected)] : nullptr;
    if (IsMeasureTool(u.tool) || (ds != nullptr && ds->IsDimension())) {
      ImGui::SetNextItemWidth(120.f);
      if (ImGui::SliderInt("##decimals", &u.decimals, 0, 4, "%d decimals") && ds != nullptr && ds->IsDimension()) {
        Annot a = *ds;
        ApplyStyle(u, a);
        u.session.Replace(u.selected, a);
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(70.f);
      if (ImGui::DragFloat("##dimsize", &u.fontSize, 0.25f, 4.f, 200.f, "%.0f pt") && ds != nullptr && ds->IsDimension()) {
        Annot a = *ds;
        ApplyStyle(u, a);
        u.session.Replace(u.selected, a);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Label size");
    }
  }
  ImGui::PopStyleVar(3);
  DrawScaleDialogs(v, log);

  // The note text dialog.
  char id[64];
  std::snprintf(id, sizeof(id), "Text note###pdftext%d", v.id);
  if (u.textPopup) {
    u.textPopup = false;
    ImGui::OpenPopup(id);
  }
  ImGui::SetNextWindowSize(ImVec2(420.f, 0.f), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (ImGui::IsWindowAppearing())
      ImGui::SetKeyboardFocusHere();
    ImGui::InputTextMultiline("##notetext", u.textBuf, sizeof(u.textBuf), ImVec2(400.f, 110.f));
    const bool empty = u.textBuf[0] == 0;
    ImGui::BeginDisabled(empty);
    if (ImGui::Button("OK")) {
      if (u.textEdit >= 0 && u.textEdit < static_cast<int>(items.size())) {
        Annot a = items[static_cast<size_t>(u.textEdit)];
        a.text = u.textBuf;
        FitTextBox(a);
        u.session.Replace(u.textEdit, a);
      } else {
        Annot a;
        a.kind = Annot::Kind::Text;
        a.page = u.textPage;
        a.x0 = u.textX;
        a.y1 = u.textY;
        a.text = u.textBuf;
        ApplyStyle(u, a);
        u.session.Add(a);
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // The unsaved-changes prompt: the window was closed while the annotation list differs from the last save.
  std::snprintf(id, sizeof(id), "Unsaved annotations###pdfclose%d", v.id);
  if (u.closePrompt) {
    u.closePrompt = false;
    ImGui::OpenPopup(id);
  }
  if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("This PDF has annotations that are not saved.");
    ImGui::TextDisabled("The original file is never changed; Save As writes a new copy.");
    ImGui::BeginDisabled(saving);
    if (ImGui::Button("Save As...")) {
      u.closeAfterSave = true;
      StartSave(v, log);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard")) {
      u.discardOk = true;
      u.requestClose = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndDisabled();
    ImGui::EndPopup();
  }
}

struct PageRect {
  int page;
  ImVec2 tl; ///< the page's top-left on screen
  float wPt, hPt;
};

// Mouse and keyboard on the pages: create with the drawing tools, select / move / resize with Select.
void HandleAnnotInput(Viewer& v, const std::vector<PageRect>& rects, bool hovered, bool panning) {
  AnnotUi& u = v.ann;
  const ImGuiIO& io = ImGui::GetIO();
  const float k = v.pxPerPt;
  const auto& items = u.session.Items();
  const auto rectOf = [&](int page) -> const PageRect* {
    for (const PageRect& r : rects)
      if (r.page == page)
        return &r;
    return nullptr;
  };
  const auto toPt = [&](const PageRect& r, float& x, float& y, bool clamp) {
    x = (io.MousePos.x - r.tl.x) / k;
    y = r.hPt - (io.MousePos.y - r.tl.y) / k;
    if (clamp) {
      x = std::clamp(x, 0.f, r.wPt);
      y = std::clamp(y, 0.f, r.hPt);
    }
  };

  // A dimension is complete at its point count; a polyline or area is ended with Enter, a double-click, or (area)
  // a click on its first point.
  const auto finishMeasure = [&] {
    Annot a;
    a.kind = KindOfMeasureTool(u.tool);
    a.page = u.measurePage;
    a.pts = u.measurePts;
    u.measurePts.clear();
    if (!DimensionComplete(a))
      return;
    ApplyStyle(u, a);
    u.session.Add(a);
    u.status.clear();
  };

  // Keys, while no text box has the keyboard.
  if ((ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || hovered) && !io.WantTextInput && u.drag == AnnotUi::Drag::None) {
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && u.session.Undo())
      u.selected = -1;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y) && u.session.Redo())
      u.selected = -1;
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && u.selected >= 0) {
      u.session.Remove(u.selected);
      u.selected = -1;
    }
    if (IsMeasureTool(u.tool) && !u.measurePts.empty() &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)))
      finishMeasure();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      u.selected = -1;
      u.measurePts.clear();
      if (u.tool == Tool::Calibrate) {
        u.tool = Tool::Select;
        u.calCount = 0;
        u.status.clear();
      }
    }
  }

  if (u.drag == AnnotUi::Drag::None) {
    if (!hovered || panning || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsAnyItemActive())
      return;
    for (const PageRect& r : rects) {
      const ImVec2 m = io.MousePos;
      if (m.x < r.tl.x || m.y < r.tl.y || m.x > r.tl.x + r.wPt * k || m.y > r.tl.y + r.hPt * k)
        continue;
      float x, y;
      toPt(r, x, y, false);
      const float tol = 5.f / k;
      if (u.tool == Tool::Select) {
        if (u.selected >= 0 && items[static_cast<size_t>(u.selected)].page == r.page) {
          const auto grips = HandlePoints(items[static_cast<size_t>(u.selected)]);
          for (size_t i = 0; i < grips.size(); ++i)
            if (std::fabs(x - grips[i].first) <= 7.f / k && std::fabs(y - grips[i].second) <= 7.f / k) {
              u.drag = AnnotUi::Drag::Handle;
              u.handle = static_cast<int>(i);
              u.dragPage = r.page;
              u.original = u.preview = items[static_cast<size_t>(u.selected)];
              return;
            }
        }
        int hit = -1;
        for (int i = static_cast<int>(items.size()) - 1; i >= 0; --i)
          if (items[static_cast<size_t>(i)].page == r.page && HitTest(items[static_cast<size_t>(i)], x, y, tol)) {
            hit = i;
            break;
          }
        if (hit < 0) {
          u.selected = -1;
        } else {
          SelectAnnot(u, hit);
          const Annot& a = items[static_cast<size_t>(hit)];
          if (a.kind == Annot::Kind::Text && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            std::snprintf(u.textBuf, sizeof(u.textBuf), "%s", a.text.c_str());
            u.textEdit = hit;
            u.textPopup = true;
          } else {
            u.drag = AnnotUi::Drag::Move;
            u.dragPage = r.page;
            u.px0 = x;
            u.py0 = y;
            u.original = u.preview = a;
          }
        }
      } else if (IsMeasureTool(u.tool)) {
        float cx, cy;
        toPt(r, cx, cy, true);
        if (!u.measurePts.empty() && r.page != u.measurePage) {
          u.status = "Keep all the points on one page.";
          return;
        }
        if (u.measurePts.empty()) {
          if (u.tool != Tool::Angle && EffectiveScale(v, r.page) == nullptr) {
            if (!u.scalesRead) { // the file may already have a scale: read it, then ask again
              u.scalesRequested = true;
              u.status = "Reading this page's scale... click again in a moment.";
            } else {
              u.status = "This page has no scale. Use Set scale... first.";
            }
            return;
          }
          u.measurePage = r.page;
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !u.measurePts.empty()) {
          finishMeasure();
          return;
        }
        if (u.tool == Tool::Area && u.measurePts.size() >= 3 &&
            std::hypot(cx - u.measurePts[0].first, cy - u.measurePts[0].second) * k <= 8.f) {
          finishMeasure();
          return;
        }
        u.measurePts.push_back({cx, cy});
        u.status = u.tool == Tool::PolyLength ? "Click more points; Enter or double-click to finish."
                   : u.tool == Tool::Area     ? "Click more points; Enter, double-click, or the first point to close."
                                              : "";
        if ((u.tool == Tool::Length && u.measurePts.size() == 2) || (u.tool == Tool::Angle && u.measurePts.size() == 3))
          finishMeasure();
      } else if (u.tool == Tool::Calibrate) {
        float cx, cy;
        toPt(r, cx, cy, true);
        if (u.calCount == 0 || r.page == u.calPage) {
          u.calPage = r.page;
          u.calX[u.calCount] = cx;
          u.calY[u.calCount] = cy;
          ++u.calCount;
          if (u.calCount == 2) {
            u.calCount = 2; // kept for drawing while the distance is asked
            u.calPopup = true;
            u.tool = Tool::Select;
            u.status.clear();
          }
        } else {
          u.status = "Pick both points on the same page.";
        }
      } else if (u.tool == Tool::Text) {
        u.textBuf[0] = 0;
        u.textEdit = -1;
        u.textPage = r.page;
        u.textX = x;
        u.textY = y;
        u.textPopup = true;
      } else {
        float cx, cy;
        toPt(r, cx, cy, true);
        Annot a;
        a.kind = u.tool == Tool::Line ? Annot::Kind::Line : u.tool == Tool::Rect ? Annot::Kind::Rect : Annot::Kind::Ellipse;
        a.page = r.page;
        a.x0 = a.x1 = cx;
        a.y0 = a.y1 = cy;
        ApplyStyle(u, a);
        u.drag = AnnotUi::Drag::Create;
        u.dragPage = r.page;
        u.original = u.preview = a;
      }
      return;
    }
    return;
  }

  // A drag in progress.
  const PageRect* r = rectOf(u.dragPage);
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    u.drag = AnnotUi::Drag::None;
    return;
  }
  if (r != nullptr) {
    float x, y;
    toPt(*r, x, y, u.drag != AnnotUi::Drag::Move);
    if (u.drag == AnnotUi::Drag::Create) {
      u.preview.x1 = x;
      u.preview.y1 = y;
    } else if (u.drag == AnnotUi::Drag::Handle) {
      u.preview = u.original;
      MoveHandle(u.preview, u.original, u.handle, x, y);
    } else {
      const float dx = x - u.px0, dy = y - u.py0;
      u.preview = u.original;
      u.preview.x0 += dx;
      u.preview.x1 += dx;
      u.preview.y0 += dy;
      u.preview.y1 += dy;
      for (auto& p : u.preview.pts) {
        p.first += dx;
        p.second += dy;
      }
    }
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    if (u.drag == AnnotUi::Drag::Create) {
      if (std::hypot(u.preview.x1 - u.preview.x0, u.preview.y1 - u.preview.y0) * k >= 4.f)
        u.session.Add(u.preview);
    } else if (u.selected >= 0 && u.preview != u.original) {
      u.session.Replace(u.selected, u.preview);
    }
    u.drag = AnnotUi::Drag::None;
  }
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
  std::vector<PageRect> pageRects;
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
    pageRects.push_back({p, a, s.wPt, s.hPt});
  }

  // REQ-388: annotations over the pages (input first, so a drag is drawn in the frame it moves).
  HandleAnnotInput(v, pageRects, hovered, v.panning);
  {
    const AnnotUi& u = v.ann;
    const auto& items = u.session.Items();
    const bool dragging = u.drag == AnnotUi::Drag::Move || u.drag == AnnotUi::Drag::Handle;
    for (const PageRect& r : pageRects) {
      dl->PushClipRect(r.tl, ImVec2(r.tl.x + r.wPt * v.pxPerPt, r.tl.y + r.hPt * v.pxPerPt), true);
      for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].page != r.page)
          continue;
        const bool isSel = static_cast<int>(i) == u.selected;
        DrawAnnot(dl, dragging && isSel ? u.preview : items[i], r.tl, r.hPt, v.pxPerPt, isSel, EffectiveScale(v, r.page));
      }
      if (u.drag == AnnotUi::Drag::Create && u.dragPage == r.page)
        DrawAnnot(dl, u.preview, r.tl, r.hPt, v.pxPerPt, false);
      if (!u.measurePts.empty() && u.measurePage == r.page && IsMeasureTool(u.tool)) { // the dimension being clicked out
        Annot cur;
        cur.kind = KindOfMeasureTool(u.tool);
        cur.pts = u.measurePts;
        ApplyStyle(u, cur);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        DrawAnnot(dl, cur, r.tl, r.hPt, v.pxPerPt, false, nullptr, &mouse);
      }
      if (u.calCount > 0 && u.calPage == r.page) { // the calibration points picked so far
        const auto S = [&](int i) { return ImVec2(r.tl.x + u.calX[i] * v.pxPerPt, r.tl.y + (r.hPt - u.calY[i]) * v.pxPerPt); };
        if (u.calCount == 2)
          dl->AddLine(S(0), S(1), IM_COL32(255, 160, 0, 255), 2.f);
        for (int i = 0; i < u.calCount; ++i) {
          dl->AddCircleFilled(S(i), 5.f, IM_COL32(255, 160, 0, 255));
          dl->AddCircle(S(i), 7.f, IM_COL32(0, 0, 0, 200), 0, 1.5f);
        }
      }
      dl->PopClipRect();
    }
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
    std::snprintf(name, sizeof(name), "PDF - %s%s###pdfview%d", v.title.c_str(), v.ann.session.Dirty() ? " *" : "", v.id);
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
        DrawAnnotBar(v, log);
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
    if (v.ann.requestClose) {
      open = false;
    } else if (!open && v.loaded && v.ann.session.Dirty() && !v.ann.discardOk) {
      open = true; // unsaved annotations: keep the window and ask (REQ-388 clause 4)
      v.ann.closePrompt = true;
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
