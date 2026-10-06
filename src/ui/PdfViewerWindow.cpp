#include "PdfViewerWindow.hpp"

#include "CadCommands.hpp"
#include "PdfDocument.hpp"
#include "FontRegistry.hpp"
#include "PdfAnnotate.hpp"
#include "PdfCompare.hpp"
#include "PdfIcons.hpp"
#include "PdfPanelStyle.hpp"
#include "PdfSnap.hpp"
#include "PdfSplit.hpp"
#include "PdfViewerCore.hpp"
#include "ShxDraw.hpp"
#include "WinFileDialogs.hpp"

#include <GL/glew.h>
#include <imgui.h>
#include <imgui_internal.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef DrawText
#undef DrawText // windows.h renames it to DrawTextA; Shx::DrawText (the stroke-font drawer) is the one used here
#endif
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

enum class Tool { Select, Text, Line, Rect, Ellipse, Leader, Calibrate, Length, PolyLength, Area, Angle, Check };

bool IsMeasureTool(Tool t) { return t == Tool::Length || t == Tool::PolyLength || t == Tool::Area || t == Tool::Angle; }

Annot::Kind KindOfMeasureTool(Tool t) {
  return t == Tool::Length ? Annot::Kind::Length : t == Tool::PolyLength ? Annot::Kind::PolyLength
                                                 : t == Tool::Area       ? Annot::Kind::Area
                                                                         : Annot::Kind::Angle;
}

struct FontChoice {
  std::string family;
  bool standard; ///< one of the 14 standard PDF fonts (no embedding); otherwise an installed TrueType family
  bool stroke = false; ///< an SHX stroke font (romans.shx): written to the PDF as strokes, not as a font (REQ-397)
};

// The standard PDF fonts plus the installed TrueType families GoSurvey itself can load (FontReg).
const std::vector<FontChoice>& FontChoices() {
  static const std::vector<FontChoice> list = [] {
    std::vector<FontChoice> out;
    if (Shx::Font* shx = Shx::Resolve("romans.shx"); shx != nullptr && shx->valid())
      out.push_back({"romans.shx", false, true});
    for (const char* f : {"Helvetica", "Times", "Courier"})
      out.push_back({f, true});
    for (const char* f : {"Arial", "Times New Roman", "Courier New", "Calibri", "Verdana", "Tahoma", "Consolas",
                          "Georgia", "Segoe UI"}) {
      const std::string p = FontReg::FindTtfPath(f, false, false);
      if (p.size() > 4 && p.compare(p.size() - 4, 4, ".ttf") == 0) // .ttc collections are not embedded
        out.push_back({f, false});
    }
    // Arial first, so it is the default (REQ-397); without it the first entry (romans.shx or Helvetica) is.
    const auto arial = std::find_if(out.begin(), out.end(), [](const FontChoice& c) { return c.family == "Arial"; });
    if (arial != out.end())
      std::rotate(out.begin(), arial, arial + 1);
    return out;
  }();
  return list;
}

// The ends and corners of one page's line work, read in the background for snapping (REQ-391 clause 5).
struct SnapPage {
  std::future<std::shared_ptr<SnapIndex>> reading;
  std::shared_ptr<SnapIndex> index; ///< null until read (or if reading failed)
  std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
  bool started = false;
  bool failed = false;
};

// REQ-395: the automatic scale audit of one page. The page is read and matched on a worker; the verdicts and the
// consensus are worked out here from the matches, so a changed scale or limit re-judges them at once.
struct AuditUi {
  bool openRequest = false;                        ///< open the Audit scale dialog on the next frame
  std::future<std::shared_ptr<DimMatchSet>> running;
  std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
  std::shared_ptr<std::atomic<int>> stage = std::make_shared<std::atomic<int>>(0); ///< 0 reading, 1 matching
  std::chrono::steady_clock::time_point startedAt;
  int page = -1;                                   ///< the page the running or finished audit is of
  bool failed = false;                             ///< the page could not be read
  bool cancelled = false;
  bool have = false;                               ///< a finished set of matches is held
  DimMatchSet matches;
  DimAudit audit;                                  ///< the judgement of \p matches against \p judgedScale and \p judgedLimits
  PageScale judgedScale;
  CheckLimits judgedLimits;
  bool judged = false;
  bool showOnSheet = true;
  bool listAll = false;                            ///< list every matched dimension, not only the offenders
  int selected = -1;                               ///< highlighted match (index into matches.matches)
  double elapsedMs = 0.0;                          ///< how long the last run took
};

struct AnnotUi {
  AnnotSession session;
  AuditUi audit;                       ///< REQ-395
  bool snapOn = false;                 ///< the Snap toggle (F3)
  std::map<int, SnapPage> snapPages;   ///< the last few pages read
  bool snapHit = false;                ///< the pointer is near a snap point this frame ...
  int snapPage = 0;
  float snapX = 0.f, snapY = 0.f;      ///< ... and this is it, in page points
  bool snapPrevValid = false;          ///< the point the marker sat on last frame, so it can stay there
  int snapPrevPage = 0;
  float snapPrevX = 0.f, snapPrevY = 0.f;
  float measureOffset = 0.f;           ///< the Length being made: where its dimension line goes

  // REQ-394: checking the scale against dimensions the drawing states.
  bool showChecks = true;              ///< draw the checks on the sheet (off hides them to reduce noise)
  CheckLimits limits;                  ///< Good / Check / Blunder limits (settings of this viewer)
  RobustParams robustParams;           ///< robust calibration settings
  std::vector<std::pair<float, float>> checkPts; ///< the Check tool's picked points (0..2)
  int checkPage = 0;
  int checkTarget = 0;                 ///< 0: a check on the page's scale; 1: a dimension for the robust calibration
  bool checkPopup = false;             ///< both points picked: ask for the stated value
  char checkText[96] = "";
  bool checksDialog = false;           ///< open the Scale checks dialog on the next frame
  int checksTab = 0;                   ///< 0 checks, 1 robust calibration
  int correctionMode = 0;              ///< 0 match a check, 1 best fit, 2 best fit without the outlier, 3 typed %, 4 leave
  int matchIdx = 0;                    ///< which check "match this check" uses (index into the page's checks)
  double typedPct = 0.0;
  int selectedCheck = -1;              ///< highlighted row (index into the session's checks)
  std::vector<std::pair<ScaleCheck, bool>> robustPicks; ///< robust calibration: the dimensions, and whether each is used
  std::string checkMessage;
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
  ImVec2 pressMouse{0.f, 0.f}; ///< where the button went down: a drag is only a drag once the pointer has moved
  bool dragMoved = false;
  Annot original, preview;    ///< the item before the drag, and as it looks mid-drag
  bool createSticky = false;  ///< a Line / Rectangle / Ellipse / Leader started by a click: the next click finishes it
  bool textIsLeader = false;  ///< the text dialog is for a new Leader (its tip is `leaderTip`), not a plain note
  std::pair<float, float> leaderTip{0.f, 0.f};
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
  bool calSnapped[2] = {false, false}; ///< whether each calibration point snapped to the drawing or was placed freehand
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
  MeasureText(a.font, a.text, a.fontSize, w, h);
  if (a.kind == Annot::Kind::Leader) { // the box has room round its text
    w += 2.f * kLeaderPad;
    h += 2.f * kLeaderPad;
  }
  const float l = std::min(a.x0, a.x1), t = std::max(a.y0, a.y1);
  a.x0 = l;
  a.x1 = l + w;
  a.y1 = t;
  a.y0 = t - h;
}

// Font, colour, width and fill from the tool settings onto an annotation.
void ApplyStyle(const AnnotUi& u, Annot& a) {
  a.color = PackColor(u.color);
  if (a.kind == Annot::Kind::Leader)
    a.thickness = u.thickness;
  if (a.kind == Annot::Kind::Text || a.kind == Annot::Kind::Leader) {
    const FontChoice& f = FontChoices()[static_cast<size_t>(std::clamp(u.fontIdx, 0, static_cast<int>(FontChoices().size()) - 1))];
    a.font = f.family;
    a.bold = u.bold;
    a.italic = u.italic;
    a.fontSize = std::clamp(u.fontSize, 4.f, 200.f);
    a.fontFile = f.standard || f.stroke ? std::string() : FontReg::FindTtfPath(f.family, u.bold, u.italic);
    FitTextBox(a);
  } else if (a.IsDimension()) {
    a.font = FontChoices()[static_cast<size_t>(std::clamp(u.fontIdx, 0, static_cast<int>(FontChoices().size()) - 1))].family;
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
  if (a.kind == Annot::Kind::Leader)
    u.thickness = a.thickness;
  if (a.kind == Annot::Kind::Text || a.kind == Annot::Kind::Leader) {
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
    for (size_t i = 0; i < FontChoices().size(); ++i)
      if (FontChoices()[i].family == a.font)
        u.fontIdx = static_cast<int>(i);
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
  if (a.kind == Annot::Kind::Leader && !a.pts.empty()) { // the box and the tip
    l = std::min(l, a.pts[0].first);
    r = std::max(r, a.pts[0].first);
    b = std::min(b, a.pts[0].second);
    t = std::max(t, a.pts[0].second);
  }
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
    if (a.kind == Annot::Kind::Length && a.pts.size() == 2) { // the dimension line and the two extension lines
      const DimLine d = LengthDimLine(a);
      const float slack = a.thickness * 0.5f + tol;
      return DistToSegment(x, y, d.x0, d.y0, d.x1, d.y1) <= slack ||
             DistToSegment(x, y, a.pts[0].first, a.pts[0].second, d.x0, d.y0) <= slack ||
             DistToSegment(x, y, a.pts[1].first, a.pts[1].second, d.x1, d.y1) <= slack;
    }
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
  case Annot::Kind::Leader: {
    if (x >= l - tol && x <= r + tol && y >= b - tol && y <= t + tol)
      return true;
    const LeaderGeom g = LeaderLine(a);
    return DistToSegment(x, y, g.sx, g.sy, g.tx, g.ty) <= a.thickness * 0.5f + tol;
  }
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
  if (a.kind == Annot::Kind::Length && a.pts.size() == 2) { // the two points, and the middle of the dimension line
    const DimLine d = LengthDimLine(a);
    return {a.pts[0], a.pts[1], {(d.x0 + d.x1) * 0.5f, (d.y0 + d.y1) * 0.5f}};
  }
  if (a.IsDimension())
    return a.pts;
  const float l = std::min(a.x0, a.x1), r = std::max(a.x0, a.x1), b = std::min(a.y0, a.y1), t = std::max(a.y0, a.y1);
  if (a.kind == Annot::Kind::Line)
    return {{a.x0, a.y0}, {a.x1, a.y1}};
  if (a.kind == Annot::Kind::Text)
    return {{r, b}};
  if (a.kind == Annot::Kind::Leader && !a.pts.empty())
    return {a.pts[0], {r, b}}; // the arrow tip, and the box's corner (it scales the text)
  return {{l, t}, {r, t}, {r, b}, {l, b}};
}

void MoveHandle(Annot& a, const Annot& original, int handle, float x, float y) {
  if (a.kind == Annot::Kind::Length && a.pts.size() == 2 && handle == 2) { // the dimension line: slide it
    const DimLine d = LengthDimLine(original);
    a = original;
    a.offset = (x - original.pts[0].first) * d.nx + (y - original.pts[0].second) * d.ny;
  } else if (a.IsDimension()) {
    if (handle >= 0 && handle < static_cast<int>(a.pts.size()))
      a.pts[static_cast<size_t>(handle)] = {x, y};
  } else if (a.kind == Annot::Kind::Line) {
    (handle == 0 ? a.x0 : a.x1) = x;
    (handle == 0 ? a.y0 : a.y1) = y;
  } else if (a.kind == Annot::Kind::Leader && handle == 0 && !a.pts.empty()) {
    a.pts[0] = {x, y};
  } else if (a.kind == Annot::Kind::Text || a.kind == Annot::Kind::Leader) {
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

// Stroke text as the saved PDF will show it (REQ-397): `text`'s lines, the first line's baseline-left at
// (left, firstBaselineY), turned `rotRad` (counter-clockwise on screen) about `pivot`. `capPx` is the text height.
void DrawStrokeBlock(ImDrawList* dl, Shx::Font& font, float left, float firstBaselineY, float pitchPx, ImVec2 pivot, float rotRad,
                     float capPx, ImU32 col, const std::string& text) {
  const float cr = std::cos(rotRad), sr = std::sin(rotRad), thick = std::max(1.f, capPx * 0.07f);
  size_t from = 0;
  for (int i = 0;; ++i) {
    const size_t nl = text.find('\n', from);
    const std::string line = text.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
    const float dx = left - pivot.x, dy = firstBaselineY + static_cast<float>(i) * pitchPx - pivot.y;
    Shx::DrawText(dl, font, ImVec2(pivot.x + dx * cr - dy * sr, pivot.y + dx * sr + dy * cr), capPx, rotRad, col, line, thick);
    if (nl == std::string::npos)
      break;
    from = nl + 1;
  }
}

// A note's text with its top-left at `topLeft`: SHX strokes when its font is a stroke font, else the TrueType font.
void DrawNoteText(ImDrawList* dl, const Annot& a, ImVec2 topLeft, float k, ImU32 col) {
  if (Shx::Font* sf = StrokeFontFor(a.font, a.text)) {
    const float capPx = a.fontSize * k;
    DrawStrokeBlock(dl, *sf, topLeft.x, topLeft.y + capPx, capPx * kStrokeLineSpacing, topLeft, 0.f, capPx, col, a.text);
    return;
  }
  dl->AddText(FontForNote(a), a.fontSize * k, topLeft, col, a.text.c_str());
}

// The size of a dimension label on screen: the stroke font's own width, else the interface font's.
ImVec2 LabelSizePx(const Annot& a, const std::string& label, float fsPx) {
  if (Shx::Font* sf = StrokeFontFor(a.font, label)) {
    float w = 0.f;
    int lines = 1;
    size_t from = 0;
    while (true) {
      const size_t nl = label.find('\n', from);
      w = std::max(w, Shx::MeasureWidthPx(*sf, label.substr(from, nl == std::string::npos ? std::string::npos : nl - from), fsPx));
      if (nl == std::string::npos)
        break;
      ++lines;
      from = nl + 1;
    }
    return ImVec2(w, fsPx + static_cast<float>(lines - 1) * fsPx * kStrokeLineSpacing);
  }
  return ImGui::GetFont()->CalcTextSizeA(fsPx, 1e9f, 0.f, label.c_str());
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
    if (a.kind == Annot::Kind::Length && a.pts.size() == 2 && rubberTo == nullptr) {
      // A dimension as GoSurvey draws one: extension lines from the two points to the dimension line, the line
      // with an arrow at each end, and the label turned along it.
      const DimLine d = LengthDimLine(a);
      const ImVec2 d0 = S(d.x0, d.y0), d1 = S(d.x1, d.y1);
      if (std::fabs(a.offset) > 0.5f) {
        const float s = a.offset > 0.f ? 1.f : -1.f, gap = std::min(2.f, std::fabs(a.offset) * 0.3f), over = 2.f;
        for (int i = 0; i < 2; ++i) {
          const auto& p = a.pts[static_cast<size_t>(i)];
          dl->AddLine(S(p.first + d.nx * s * gap, p.second + d.ny * s * gap),
                      S((i == 0 ? d.x0 : d.x1) + d.nx * s * over, (i == 0 ? d.y0 : d.y1) + d.ny * s * over), col, th);
        }
      }
      dl->AddLine(d0, d1, col, th);
      const float lenPx = std::hypot(d1.x - d0.x, d1.y - d0.y);
      if (lenPx > 1e-3f) {
        const float ux = (d1.x - d0.x) / lenPx, uy = (d1.y - d0.y) / lenPx;
        const float al = std::clamp(std::max(4.f, a.fontSize) * 0.6f * k, 3.f, lenPx / 3.f), aw = al * 0.3f;
        dl->AddTriangleFilled(d0, ImVec2(d0.x + ux * al - uy * aw, d0.y + uy * al + ux * aw),
                              ImVec2(d0.x + ux * al + uy * aw, d0.y + uy * al - ux * aw), col);
        dl->AddTriangleFilled(d1, ImVec2(d1.x - ux * al - uy * aw, d1.y - uy * al + ux * aw),
                              ImVec2(d1.x - ux * al + uy * aw, d1.y - uy * al - ux * aw), col);
      }
      const std::string label = scale != nullptr ? DimensionLabel(a, *scale) : std::string("no scale");
      const auto [ax, ay] = DimensionLabelAnchor(a);
      const float fsPx = std::max(4.f, a.fontSize) * k;
      ImFont* font = ImGui::GetFont();
      const ImVec2 ts = LabelSizePx(a, label, fsPx);
      const ImVec2 c = S(ax, ay);
      const float rad = -DimensionLabelAngleDeg(a) * 3.14159265f / 180.f, cs = std::cos(rad), sn = std::sin(rad); // screen y is down
      const auto rot = [&](ImVec2 p) { return ImVec2(c.x + (p.x - c.x) * cs - (p.y - c.y) * sn, c.y + (p.x - c.x) * sn + (p.y - c.y) * cs); };
      const ImVec2 p0(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
      const ImVec2 q[4] = {rot(ImVec2(p0.x - 3.f, p0.y - 1.f)), rot(ImVec2(p0.x + ts.x + 3.f, p0.y - 1.f)),
                           rot(ImVec2(p0.x + ts.x + 3.f, p0.y + ts.y + 1.f)), rot(ImVec2(p0.x - 3.f, p0.y + ts.y + 1.f))};
      dl->AddConvexPolyFilled(q, 4, IM_COL32(255, 255, 255, 215));
      if (Shx::Font* sf = StrokeFontFor(a.font, label)) {
        DrawStrokeBlock(dl, *sf, p0.x, p0.y + fsPx, fsPx * kStrokeLineSpacing, c, rad, fsPx, col, label);
      } else {
        const int v0 = dl->VtxBuffer.Size;
        dl->AddText(font, fsPx, p0, col, label.c_str());
        for (int i = v0; i < dl->VtxBuffer.Size; ++i) // turn the text's quads about the label's centre
          dl->VtxBuffer[i].pos = rot(dl->VtxBuffer[i].pos);
      }
      break;
    }
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
      const ImVec2 ts = LabelSizePx(a, label, fsPx);
      const ImVec2 c = S(ax, ay);
      const ImVec2 p0(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
      dl->AddRectFilled(ImVec2(p0.x - 3.f, p0.y - 1.f), ImVec2(p0.x + ts.x + 3.f, p0.y + ts.y + 1.f), IM_COL32(255, 255, 255, 215));
      if (Shx::Font* sf = StrokeFontFor(a.font, label))
        DrawStrokeBlock(dl, *sf, p0.x, p0.y + fsPx, fsPx * kStrokeLineSpacing, c, 0.f, fsPx, col, label);
      else
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
    DrawNoteText(dl, a, S(l, t), k, col);
    break;
  case Annot::Kind::Leader: {
    const float bl = std::min(a.x0, a.x1), br = std::max(a.x0, a.x1), bb = std::min(a.y0, a.y1), bt = std::max(a.y0, a.y1);
    const LeaderGeom g = LeaderLine(a);
    dl->AddRect(S(bl, bt), S(br, bb), col, 0.f, 0, th);
    dl->AddLine(S(g.sx, g.sy), S(g.tx, g.ty), col, th);
    dl->AddTriangleFilled(S(g.tx, g.ty), S(g.w1x, g.w1y), S(g.w2x, g.w2y), col);
    DrawNoteText(dl, a, S(bl + kLeaderPad, bt - kLeaderPad), k, col);
    break;
  }
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
  int framedFrames = 0;        ///< frames the viewer has had its own OS window, counted through the opening maximize steps
  int syncBad = 0;             ///< consecutive frames ImGui's idea of the window differed from the OS's
  int syncLogged = 0;
  bool syncApply = false;      ///< next frame, put ImGui's window where the OS window really is
  ImVec2 syncPos{0.f, 0.f}, syncSize{0.f, 0.f};
  bool maximized = false;     ///< the window has been maximized once, on opening (REQ-397); after that it is the user's
  bool placed = false; ///< first-frame position given; after that the user (or the saved layout) owns it
  std::future<PdfDocument::OpenResult> opening;
  bool loaded = false;
  std::string error;
  std::unique_ptr<PdfDocument> doc;
  std::unique_ptr<PdfCompare> compare; ///< REQ-392: set while this window shows a comparison with another PDF (must die before doc)
  bool comparePickRequest = false;
  std::filesystem::path benchRev;      ///< BENCH PDFCOMPARE: the generated revision to compare with once the base has opened
  int benchRevPages = 0;
  bool benchDiff = false;              ///< BENCH PDFDIFF: the comparison runs the automatic alignment and the change search
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
  PdfCompare::DrainGraveyard();
  for (GLuint t : g_graveyard)
    glDeleteTextures(1, &t);
  g_graveyard.clear();
}

void DestroyViewer(Viewer& v) {
  v.compare.reset(); // stops its worker, which reads the base document
  if (v.splitting.valid())
    v.splitting.wait(); // a split in flight finishes (it reads the file, never changes the source)
  if (v.ann.saving.valid())
    v.ann.saving.wait(); // so does a Save As
  if (v.ann.readingScales.valid())
    v.ann.readingScales.wait();
  v.ann.audit.cancel->store(true); // an audit in flight reads the document: stop it and wait
  if (v.ann.audit.running.valid())
    v.ann.audit.running.wait();
  for (auto& [page, sp] : v.ann.snapPages) { // snap readers hold the document: stop and wait for them
    sp.cancel->store(true);
    if (sp.reading.valid())
      sp.reading.wait();
  }
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
  const float sy = v.pendingScrollY >= 0.f ? v.pendingScrollY : v.lastScrollY; // a scroll already asked for wins
  const float sx = v.pendingScrollX >= 0.f ? v.pendingScrollX : v.lastScrollX;
  const float cy = sy + v.viewH * 0.5f, cx = sx + v.viewW * 0.5f;
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

// Scrolls so the page point (x, y) is in the middle of the view (REQ-395: clicking an offender in the list).
void CentreOnPagePoint(Viewer& v, int page, float x, float y) {
  page = std::clamp(page, 0, v.layout.PageCount() - 1);
  v.curPage = page;
  v.pageBox = page + 1;
  const PageSize& s = v.layout.size[static_cast<size_t>(page)];
  const float contentW = std::max(v.viewW, v.layout.maxWidth * v.pxPerPt + 2 * kMarginPx);
  const float left = (contentW - s.wPt * v.pxPerPt) * 0.5f;
  v.pendingScrollX = std::max(0.f, left + x * v.pxPerPt - v.viewW * 0.5f);
  v.pendingScrollY = std::max(0.f, kMarginPx + (PageTopPt(v, page) + s.hPt - y) * v.pxPerPt - v.viewH * 0.5f);
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
  if (IconButton(Icon::Prev, "", "Previous page"))
    GoToPage(v, v.curPage - 1);
  ImGui::SameLine();
  if (IconButton(Icon::Next, "", "Next page"))
    GoToPage(v, v.curPage + 1);
  ImGui::SameLine();
  ImGui::TextUnformatted("|");
  ImGui::SameLine();
  if (IconButton(Icon::ZoomOut, "", "Zoom out"))
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
  if (IconButton(Icon::ZoomIn, "", "Zoom in"))
    ZoomAboutCentre(v, v.pxPerPt * 1.25f);
  ImGui::SameLine();
  if (IconButton(Icon::FitWidth, "Fit width"))
    v.fitWidthPending = true;
  ImGui::SameLine();
  if (IconButton(Icon::FitPage, "Fit page")) {
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
  if (IconButton(Icon::Split, "Split...", "Save chosen pages as a new PDF"))
    v.splitOpenRequest = true;
  ImGui::SameLine();
  if (IconButton(Icon::Compare, "Compare...", "Compare with another revision of this sheet"))
    v.comparePickRequest = true;
  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar(4);
  ImGui::Spacing();
}

// REQ-389: choose pages, then "Save As..." writes them as a new PDF. The original is never touched.
// A modal dialog that moves only by its title bar. (Dear ImGui lets a window be dragged from anywhere on it by
// default, which makes a click on a table or a button move the whole dialog.) The window is made unmovable and the
// title bar is dragged by hand.
bool BeginDialog(const char* id, ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize) {
  if (!ImGui::BeginPopupModal(id, nullptr, flags | ImGuiWindowFlags_NoMove))
    return false;
  static std::map<ImGuiID, bool> dragging;
  ImGuiWindow* w = ImGui::GetCurrentWindow();
  bool& drag = dragging[w->ID];
  const ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && w->TitleBarRect().Contains(io.MousePos))
    drag = true;
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    drag = false;
  if (drag)
    ImGui::SetWindowPos(ImVec2(w->Pos.x + io.MouseDelta.x, w->Pos.y + io.MouseDelta.y));
  return true;
}

void DrawSplitDialog(Viewer& v, std::vector<std::string>& log) {
  char id[64];
  std::snprintf(id, sizeof(id), "Split PDF###pdfsplit%d", v.id);
  if (v.splitOpenRequest) {
    v.splitOpenRequest = false;
    v.splitError.clear();
    ImGui::OpenPopup(id);
  }
  ImGui::SetNextWindowSize(ImVec2(460.f, 0.f), ImGuiCond_Appearing);
  if (!BeginDialog(id))
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
  PushPanelStyle();
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
  PopPanelStyle();
}

const PageScale* EffectiveScale(const Viewer& v, int page);
void StartSnapRead(Viewer& v, int page);

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
bool ApplyScaleToScope(Viewer& v, const PageScale& scale, std::vector<std::string>& log, const ScaleCheck* calibration = nullptr) {
  AnnotUi& u = v.ann;
  std::vector<int> pages;
  if (!ScopePages(v, pages, u.scaleError))
    return false;
  std::map<int, PageScale> change;
  for (int p : pages)
    change[p] = scale;
  u.session.SetScales(change, calibration); // a calibration counts as the first scale check (REQ-394)
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
  if (BeginDialog(id)) {
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
      if (ApplyScaleToScope(v, PageScale{0.0, Unit::Inch, 0.0, Unit::Foot, "", "", 0.0}, log))
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
  if (BeginDialog(id)) {
    const double pts = std::hypot(static_cast<double>(u.calX[1] - u.calX[0]), static_cast<double>(u.calY[1] - u.calY[0]));
    ImGui::Text("The two points are %.3f points (%.4f in) apart on the sheet.", pts, pts / 72.0);
    const int freehand = (u.calSnapped[0] ? 0 : 1) + (u.calSnapped[1] ? 0 : 1);
    if (freehand == 0)
      ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.5f, 1.f), "Both points snapped to the drawing.");
    else
      ImGui::TextColored(ImVec4(1.f, 0.75f, 0.35f, 1.f),
                         "%s placed freehand: one screen pixel can be %.2f points. Turn Snap on (F3) and pick again for an exact scale.",
                         freehand == 2 ? "Both points were" : "One point was", 0.75);
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
    // The calibration itself is the first scale check: the picked distance against the distance the user typed.
    ScaleCheck calCheck;
    calCheck.page = u.calPage;
    calCheck.x0 = u.calX[0];
    calCheck.y0 = u.calY[0];
    calCheck.x1 = u.calX[1];
    calCheck.y1 = u.calY[1];
    calCheck.stated = u.calReal;
    calCheck.unit = units[static_cast<size_t>(u.calUnit)];
    calCheck.calibration = true;
    // Drawings are almost always plotted at a standard scale: when this calibration is within 1 % of one, say so,
    // and offer it (a short span picked by eye can be a fraction of a percent out, and a long dimension then reads wrong).
    const PageScale calibrated = ScaleFromCalibration(pts, u.calReal, units[static_cast<size_t>(u.calUnit)]);
    const PageScale* nearest = nullptr;
    double nearDiff = 1.0;
    if (calibrated.Valid())
      for (const PageScale& p : PresetScales()) {
        const double diff = std::fabs(calibrated.PointsToReal(1.0) * UnitInMetres(calibrated.realUnit) / (p.PointsToReal(1.0) * UnitInMetres(p.realUnit)) - 1.0);
        if (diff < nearDiff) {
          nearDiff = diff;
          nearest = &p;
        }
      }
    if (nearest != nullptr && nearDiff < 0.01) {
      ImGui::Text("Closest standard scale: %s (%.2f %% different).", nearest->label.c_str(), nearDiff * 100.0);
      if (ImGui::Button("Use the standard scale")) {
        if (ApplyScaleToScope(v, *nearest, log, &calCheck)) {
          u.status = "Scale set: " + nearest->RatioText();
          u.calCount = 0;
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::SameLine();
    }
    bool checkNext = false;
    if (ImGui::Button("Set scale") || (checkNext = ImGui::Button("Set scale and check..."))) {
      const PageScale s = calibrated;
      if (!s.Valid())
        u.scaleError = "the distance must be bigger than zero, and the two points must be apart";
      else if (ApplyScaleToScope(v, s, log, &calCheck)) {
        u.status = "Scale set: " + s.RatioText();
        u.calCount = 0;
        if (checkNext) { // test it against another dimension the drawing states (REQ-394 clause 5)
          u.tool = Tool::Check;
          u.checkTarget = 0;
          u.checkPts.clear();
          u.status = "Scale set. Click the two ends of another dimension to check it.";
        }
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

// ---------------------------------------------------------------------------------------------------
// REQ-394: checking the scale against dimensions the drawing already states
// ---------------------------------------------------------------------------------------------------

ImVec4 VerdictColor(Verdict v) {
  return v == Verdict::Good ? ImVec4(0.35f, 0.85f, 0.45f, 1.f) : v == Verdict::Check ? ImVec4(0.96f, 0.69f, 0.16f, 1.f)
                                                                                      : ImVec4(0.95f, 0.30f, 0.26f, 1.f);
}

std::string Signed(double v, int decimals) { return (v >= 0.0 ? "+" : "-") + FormatValue(std::fabs(v), decimals); }

// Two picked points and the typed text -> a check. False with the reason when the text cannot be read.
bool MakeCheck(const AnnotUi& u, const PageScale* scale, ScaleCheck& out, std::string& why) {
  ParsedLength p;
  if (!ParseLength(u.checkText, p, why))
    return false;
  out = ScaleCheck{};
  out.page = u.checkPage;
  out.x0 = u.checkPts[0].first;
  out.y0 = u.checkPts[0].second;
  out.x1 = u.checkPts[1].first;
  out.y1 = u.checkPts[1].second;
  out.stated = p.value;
  out.unit = p.hasUnit ? p.unit : (scale != nullptr ? scale->realUnit : Unit::Foot);
  return true;
}

// The page's checks as indexes into the session's list.
std::vector<int> PageChecks(const AnnotUi& u, int page) {
  std::vector<int> idx;
  for (size_t i = 0; i < u.session.Checks().size(); ++i)
    if (u.session.Checks()[i].page == page)
      idx.push_back(static_cast<int>(i));
  return idx;
}

double MetresPerPt(const PageScale& s) { return s.RealPerPoint() * UnitInMetres(s.realUnit); }

// A dialog that stands out from the sheet behind it: a lighter body, a bright frame and title, a darker dim.
void PushDialogStyle() {
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.15f, 0.17f, 0.22f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.62f, 1.00f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.16f, 0.34f, 0.60f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.20f, 0.42f, 0.74f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.f, 0.f, 0.f, 0.62f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.f, 12.f));
}
void PopDialogStyle() {
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor(5);
}

// Tables with a clear header, borders and banded rows, and room around the text.
void PushTableStyle() {
  ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0.20f, 0.38f, 0.64f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, ImVec4(0.50f, 0.62f, 0.80f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_TableBorderLight, ImVec4(0.32f, 0.40f, 0.54f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.12f, 0.14f, 0.18f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.18f, 0.21f, 0.28f, 1.f));
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(10.f, 5.f));
}
void PopTableStyle() {
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(5);
}

enum class Align { Left, Right, Center };

// Text placed in a table cell: numbers read best right-aligned, a verdict centred, words left.
void CellText(Align a, const std::string& text, const ImVec4* color = nullptr) {
  const float w = ImGui::CalcTextSize(text.c_str()).x, avail = ImGui::GetContentRegionAvail().x;
  const float off = a == Align::Right ? avail - w : a == Align::Center ? (avail - w) * 0.5f : 0.f;
  if (off > 0.f)
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
  if (color != nullptr)
    ImGui::TextColored(*color, "%s", text.c_str());
  else
    ImGui::TextUnformatted(text.c_str());
}

// Each column as wide as the wider of its title and its widest cell, plus the cell padding either side, so
// nothing is cut off even when a column's cells are empty.
std::vector<float> ColumnWidths(const std::vector<std::pair<const char*, Align>>& cols, const std::vector<std::vector<std::string>>& rows) {
  std::vector<float> w;
  for (size_t i = 0; i < cols.size(); ++i) {
    float m = ImGui::CalcTextSize(cols[i].first).x;
    for (const auto& r : rows)
      if (i < r.size())
        m = std::max(m, ImGui::CalcTextSize(r[i].c_str()).x);
    w.push_back(m + 24.f); // 2 x the 10 px cell padding, and a little air
  }
  return w;
}

// The header row, each title aligned like the column beneath it.
void TableHeader(const std::vector<std::pair<const char*, Align>>& cols, const std::vector<float>& widths) {
  for (size_t i = 0; i < cols.size(); ++i)
    ImGui::TableSetupColumn(cols[i].first, ImGuiTableColumnFlags_WidthFixed, widths[i]);
  ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
  for (size_t i = 0; i < cols.size(); ++i) {
    ImGui::TableSetColumnIndex(static_cast<int>(i));
    const ImVec4 white(1.f, 1.f, 1.f, 1.f);
    CellText(cols[i].second, cols[i].first, &white);
  }
}

// The "Check a dimension" popup that follows the Check tool's two clicks.
void DrawCheckPopup(Viewer& v, std::vector<std::string>& log) {
  AnnotUi& u = v.ann;
  char id[64];
  std::snprintf(id, sizeof(id), "Check a dimension###pdfcheck%d", v.id);
  if (u.checkPopup) {
    u.checkPopup = false;
    ImGui::OpenPopup(id);
  }
  PushDialogStyle();
  if (!BeginDialog(id)) {
    PopDialogStyle();
    return;
  }
  const PageScale* scale = EffectiveScale(v, u.checkPage);
  const double pts = u.checkPts.size() == 2 ? std::hypot(static_cast<double>(u.checkPts[1].first - u.checkPts[0].first),
                                                        static_cast<double>(u.checkPts[1].second - u.checkPts[0].second))
                                            : 0.0;
  ImGui::Text("The two points are %.3f points (%.4f in) apart on the sheet.", pts, pts / 72.0);
  ImGui::TextUnformatted("The drawing states:");
  ImGui::SameLine();
  if (ImGui::IsWindowAppearing())
    ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(220.f);
  ImGui::InputTextWithHint("##checkval", "43'-0 3/4\"   10'6\"   12.5 m   850 mm", u.checkText, sizeof(u.checkText));
  ScaleCheck made;
  std::string why;
  const bool typed = u.checkText[0] != 0;
  const bool ok = u.checkPts.size() == 2 && MakeCheck(u, scale, made, why);
  if (typed && !ok)
    ImGui::TextColored(ImVec4(1.f, 0.45f, 0.4f, 1.f), "%s", why.c_str());
  if (ok && u.checkTarget == 0 && scale != nullptr) {
    const CheckResult r = EvaluateCheck(made, *scale, u.limits);
    ImGui::TextColored(VerdictColor(r.verdict), "Reads %s %s, the drawing says %s %s: difference %s %s (%s %%) - %s",
                       FormatValue(r.measured, 4).c_str(), UnitLabel(made.unit), FormatValue(r.stated, 4).c_str(), UnitLabel(made.unit),
                       Signed(r.diff, 4).c_str(), UnitLabel(made.unit), Signed(r.pct, 2).c_str(), VerdictName(r.verdict));
  } else if (ok) {
    ImGui::TextDisabled("Added to the robust calibration list; the fit is shown there.");
  }
  ImGui::BeginDisabled(!ok);
  if (ImGui::Button("OK")) {
    if (u.checkTarget == 0) {
      u.session.AddCheck(made);
      u.status = "Check added.";
      log.push_back("PDF scale check: " + FormatValue(made.stated, 4) + " " + UnitLabel(made.unit) + " stated on page " +
                    std::to_string(made.page + 1) + " of " + v.title);
    } else {
      u.robustPicks.push_back({made, true});
      u.checksDialog = true;
      u.checksTab = 1;
      u.tool = Tool::Select;
      u.status.clear();
    }
    u.checkPts.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    u.checkPts.clear();
    if (u.checkTarget == 1) {
      u.checksDialog = true;
      u.checksTab = 1;
      u.tool = Tool::Select;
    }
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
  PopDialogStyle();
}

// REQ-395: reads the page's text and strokes and pairs them, on a worker, with Cancel.
void StartAudit(Viewer& v, int page) {
  AuditUi& a = v.ann.audit;
  if (v.doc == nullptr || a.running.valid())
    return;
  a.cancel = std::make_shared<std::atomic<bool>>(false);
  a.stage = std::make_shared<std::atomic<int>>(0);
  a.page = page;
  a.failed = a.cancelled = a.have = a.judged = false;
  a.matches = {};
  a.audit = {};
  a.selected = -1;
  a.startedAt = std::chrono::steady_clock::now();
  PdfDocument* doc = v.doc.get();
  const auto cancel = a.cancel;
  const auto stage = a.stage;
  a.running = std::async(std::launch::async, [doc, page, cancel, stage]() -> std::shared_ptr<DimMatchSet> {
    const auto stop = [cancel] { return cancel->load(); };
    std::vector<DimText> texts;
    std::vector<DimSeg> segs;
    if (!doc->AuditPageData(page, texts, segs, stop))
      return nullptr;
    stage->store(1);
    auto set = std::make_shared<DimMatchSet>();
    if (!MatchDimensions(texts, segs, DimMatchParams{}, stop, *set))
      return nullptr;
    return set;
  });
}

// The Audit scale dialog: what the page's own dimension text says about its scale. Suggestions only - the scale
// changes only when the user presses Use consensus scale.
void DrawAuditDialog(Viewer& v, std::vector<std::string>& log) {
  AnnotUi& u = v.ann;
  AuditUi& a = u.audit;
  char id[64];
  std::snprintf(id, sizeof(id), "Audit scale###pdfaudit%d", v.id);
  if (a.running.valid() && a.running.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    a.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a.startedAt).count();
    const std::shared_ptr<DimMatchSet> set = a.running.get();
    if (set != nullptr) {
      a.matches = std::move(*set);
      a.have = true;
      log.push_back("PDF scale audit: page " + std::to_string(a.page + 1) + " of " + v.title + " - " + std::to_string(a.matches.matches.size()) +
                    " dimensions matched in " + FormatValue(a.elapsedMs / 1000.0, 2) + " s");
    } else if (a.cancel->load()) {
      a.cancelled = true;
    } else {
      a.failed = true;
      log.push_back("PDF scale audit: could not read page " + std::to_string(a.page + 1) + " of " + v.title);
    }
  }
  if (a.openRequest) {
    a.openRequest = false;
    ImGui::OpenPopup(id);
  }
  ImGui::SetNextWindowSize(ImVec2(820.f, 0.f), ImGuiCond_Appearing);
  PushDialogStyle();
  if (!BeginDialog(id)) {
    PopDialogStyle();
    return;
  }
  const int page = a.page;
  const PageScale* scale = page >= 0 ? EffectiveScale(v, page) : nullptr;
  ImGui::TextWrapped("Reads this page's own dimension text, finds the dimension line each one labels and checks the scale against them. "
                     "These are suggestions: nothing changes unless you press Use consensus scale.");
  ImGui::Separator();
  const bool running = a.running.valid();
  if (running) {
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - a.startedAt).count();
    ImGui::Text("%s page %d... %.1f s", a.stage->load() == 0 ? "Reading" : "Matching dimensions on", page + 1, secs);
    ImGui::ProgressBar(-1.f * static_cast<float>(ImGui::GetTime()), ImVec2(-1.f, 0.f), a.stage->load() == 0 ? "reading text and lines" : "matching");
    if (ImGui::Button("Cancel"))
      a.cancel->store(true);
  } else if (a.cancelled) {
    ImGui::TextDisabled("Cancelled. No result was kept.");
  } else if (a.failed) {
    ImGui::TextColored(ImVec4(1.f, 0.45f, 0.4f, 1.f), "Page %d could not be read.", page + 1);
  } else if (a.have && scale == nullptr) {
    ImGui::TextColored(ImVec4(1.f, 0.45f, 0.4f, 1.f), "Page %d has no scale to judge. Set one (Set scale...), then audit again.", page + 1);
  } else if (a.have) {
    if (!a.judged || !(a.judgedScale == *scale) || a.judgedLimits.goodPct != u.limits.goodPct || a.judgedLimits.checkPct != u.limits.checkPct) {
      a.audit = AuditDimensions(a.matches, *scale, u.limits);
      a.judgedScale = *scale;
      a.judgedLimits = u.limits;
      a.judged = true;
    }
    const DimAudit& au = a.audit;
    const auto& ms = a.matches.matches;
    ImGui::Text("Page %d: %s%s%s%s", page + 1, scale->RatioText().c_str(), scale->note.empty() ? "" : "  (", scale->note.c_str(),
                scale->note.empty() ? "" : ")");
    ImGui::Text("%zu dimensions matched, %d text%s with no dimension line, %d dimension line%s with no text.  (%.1f s)", ms.size(),
                a.matches.unmatchedText, a.matches.unmatchedText == 1 ? "" : "s", a.matches.unmatchedLines,
                a.matches.unmatchedLines == 1 ? "" : "s", a.elapsedMs / 1000.0);
    if (!ms.empty())
      ImGui::Text("Agree with the current scale: %d Good, %d Check, %d Blunder", au.good, au.check, au.blunder);
    if (!au.message.empty())
      ImGui::TextColored(VerdictColor(Verdict::Check), "%s", au.message.c_str());

    PageScale consensus;
    if (au.consensusValid) {
      consensus = ScaleFromConsensus(au.consensusMetresPerPt, static_cast<int>(ms.size()), *scale);
      consensus.note.clear();
      ImGui::Text("Consensus scale: %s", consensus.RatioText().c_str());
      ImGui::SameLine();
      const double cur = MetresPerPt(*scale);
      ImGui::TextDisabled("(%s %s %% from the page's scale)", au.consensusMetresPerPt >= cur ? "+" : "-",
                          FormatValue(std::fabs(au.consensusMetresPerPt / std::max(1e-12, cur) - 1.0) * 100.0, 3).c_str());
    }
    ImGui::BeginDisabled(!au.consensusValid);
    if (ImGui::Button("Use consensus scale")) {
      const PageScale next = ScaleFromConsensus(au.consensusMetresPerPt, static_cast<int>(ms.size()), *scale);
      std::map<int, PageScale> change;
      change[page] = next;
      u.session.SetScales(change);
      log.push_back("PDF scale: page " + std::to_string(page + 1) + " of " + v.title + " set from the audit - " + next.note);
      u.status = "Scale set from the audit: " + next.RatioText() + ".";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Show offenders on the sheet", &a.showOnSheet);
    ImGui::SameLine();
    ImGui::Checkbox("List every matched dimension", &a.listAll);

    std::vector<int> rows = a.listAll ? std::vector<int>() : au.offenders;
    if (a.listAll)
      for (size_t i = 0; i < ms.size(); ++i)
        rows.push_back(static_cast<int>(i));
    if (rows.empty()) {
      if (!ms.empty())
        ImGui::TextDisabled("No dimension disagrees with the page's scale by more than the limits.");
    } else {
      ImGui::SeparatorText(a.listAll ? "Every matched dimension (click one to centre it)" : "Worst offenders (click one to centre it)");
      const std::vector<std::pair<const char*, Align>> cols = {{"#", Align::Center}, {"Drawing says", Align::Right}, {"Reads", Align::Right},
                                                               {"%", Align::Right},  {"Implies", Align::Right},     {"Verdict", Align::Center}};
      std::vector<std::vector<std::string>> cells;
      const size_t shown = std::min<size_t>(rows.size(), 400);
      for (size_t k = 0; k < shown; ++k) {
        const size_t i = static_cast<size_t>(rows[k]);
        const CheckResult& r = au.results[i];
        PageScale implied = *scale;
        implied.label.clear();
        implied.note.clear();
        implied.realValue = ms[i].metresPerPt * 72.0 / UnitInMetres(scale->realUnit);
        cells.push_back({std::to_string(k + 1), ms[i].text, FormatValue(r.measured, 3) + " " + UnitLabel(ms[i].unit), Signed(r.pct, 2),
                         implied.RatioText(), VerdictName(r.verdict)});
      }
      const std::vector<float> widths = ColumnWidths(cols, cells);
      PushTableStyle();
      if (ImGui::BeginTable("##auditrows", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
                            ImVec2(0.f, 260.f))) {
        TableHeader(cols, widths);
        for (size_t k = 0; k < shown; ++k) {
          const int i = rows[k];
          const ImVec4 vc = VerdictColor(au.results[static_cast<size_t>(i)].verdict);
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          char lab[32];
          std::snprintf(lab, sizeof(lab), "%zu##arow%d", k + 1, i);
          if (ImGui::Selectable(lab, a.selected == i, ImGuiSelectableFlags_SpanAllColumns)) {
            a.selected = i;
            CentreOnPagePoint(v, page, (ms[static_cast<size_t>(i)].x0 + ms[static_cast<size_t>(i)].x1) * 0.5f,
                              (ms[static_cast<size_t>(i)].y0 + ms[static_cast<size_t>(i)].y1) * 0.5f);
          }
          for (size_t col = 1; col < cols.size(); ++col) {
            ImGui::TableNextColumn();
            CellText(cols[col].second, cells[k][col], col == 5 ? &vc : nullptr);
          }
        }
        ImGui::EndTable();
      }
      PopTableStyle();
      if (rows.size() > shown)
        ImGui::TextDisabled("Showing the first %zu of %zu.", shown, rows.size());
    }
    ImGui::TextDisabled("Matching can be wrong: a text may pair with a neighbouring line. Judge each by its text and the line highlighted on the sheet.");
  }
  ImGui::Separator();
  ImGui::BeginDisabled(running);
  if (ImGui::Button("Run again"))
    StartAudit(v, std::clamp(v.curPage, 0, v.layout.PageCount() - 1));
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Close")) {
    if (running)
      a.cancel->store(true);
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
  PopDialogStyle();
}

// The Scale checks dialog: the page's checks with verdicts, the best fit and the correction the user may choose
// (tab 1), and the opt-in robust calibration (tab 2).
void DrawChecksDialog(Viewer& v, std::vector<std::string>& log) {
  AnnotUi& u = v.ann;
  char id[64];
  std::snprintf(id, sizeof(id), "Scale checks###pdfchecks%d", v.id);
  if (u.checksDialog) {
    u.checksDialog = false;
    ImGui::OpenPopup(id);
  }
  ImGui::SetNextWindowSize(ImVec2(860.f, 0.f), ImGuiCond_Appearing);
  PushDialogStyle();
  if (!BeginDialog(id)) {
    PopDialogStyle();
    return;
  }
  const int page = std::clamp(v.curPage, 0, v.layout.PageCount() - 1);
  const PageScale* scale = EffectiveScale(v, page);
  const auto& all = u.session.Checks();
  const std::vector<int> idx = PageChecks(u, page);
  int deleteCheck = -1;

  if (ImGui::BeginTabBar("##chktabs")) {
    // ---- Tab 1: the checks ------------------------------------------------------------------------
    if (ImGui::BeginTabItem("Checks", nullptr, u.checksTab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
      if (scale == nullptr) {
        ImGui::TextWrapped("Page %d has no scale yet. Set one (Set scale...), then check it against dimensions the drawing states.", page + 1);
      } else {
        ImGui::Text("Page %d: %s%s%s%s", page + 1, scale->RatioText().c_str(), scale->note.empty() ? "" : "  (", scale->note.c_str(),
                    scale->note.empty() ? "" : ")");
        ImGui::SetNextItemWidth(90.f);
        ImGui::InputDouble("Good up to (%)", &u.limits.goodPct, 0.0, 0.0, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.f);
        ImGui::InputDouble("Check up to (%)", &u.limits.checkPct, 0.0, 0.0, "%.2f");
        ImGui::SameLine();
        ImGui::Checkbox("Show checks on the sheet", &u.showChecks);
        u.limits.goodPct = std::clamp(u.limits.goodPct, 0.0, 100.0);
        u.limits.checkPct = std::clamp(u.limits.checkPct, u.limits.goodPct, 100.0);
        if (idx.empty()) {
          ImGui::TextDisabled("No checks on this page yet. Use the Check tool: click the two ends of a dimension, then type its printed value.");
        }
        std::vector<FitObs> fit;
        for (int i : idx)
          fit.push_back({all[static_cast<size_t>(i)].MeasuredPt(), all[static_cast<size_t>(i)].StatedMetres()});
        const BestFit bf = BestFitScale(fit);

        if (!idx.empty()) {
          const std::vector<std::pair<const char*, Align>> cols = {{"#", Align::Center}, {"Kind", Align::Left}, {"Drawing says", Align::Right},
                                                                   {"Reads", Align::Right}, {"Difference", Align::Right}, {"%", Align::Right},
                                                                   {"Verdict", Align::Center}, {"Flag", Align::Center}, {"", Align::Center}};
          std::vector<std::vector<std::string>> cells;
          std::vector<CheckResult> results;
          for (size_t k = 0; k < idx.size(); ++k) {
            const ScaleCheck& c = all[static_cast<size_t>(idx[k])];
            const CheckResult r = EvaluateCheck(c, *scale, u.limits);
            results.push_back(r);
            cells.push_back({std::to_string(k + 1), c.calibration ? "calibration" : "check",
                             FormatValue(r.stated, 4) + " " + UnitLabel(c.unit), FormatValue(r.measured, 4) + " " + UnitLabel(c.unit),
                             Signed(r.diff, 4) + " " + UnitLabel(c.unit), Signed(r.pct, 2), VerdictName(r.verdict),
                             (k < bf.outlier.size() && bf.outlier[k]) ? "Outlier" : "", "Delete"});
          }
          const std::vector<float> widths = ColumnWidths(cols, cells);
          PushTableStyle();
          if (ImGui::BeginTable("##checks", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            TableHeader(cols, widths);
            for (size_t k = 0; k < idx.size(); ++k) {
              const int i = idx[k];
              const ImVec4 vc = VerdictColor(results[k].verdict);
              ImGui::TableNextRow();
              ImGui::TableNextColumn();
              char lab[32];
              std::snprintf(lab, sizeof(lab), "%zu##row%d", k + 1, i);
              if (ImGui::Selectable(lab, u.selectedCheck == i, ImGuiSelectableFlags_SpanAllColumns))
                u.selectedCheck = i;
              for (size_t col = 1; col < 7; ++col) {
                ImGui::TableNextColumn();
                CellText(cols[col].second, cells[k][col], col == 6 ? &vc : nullptr);
              }
              ImGui::TableNextColumn();
              if (!cells[k][7].empty()) {
                const ImVec4 red(0.95f, 0.30f, 0.26f, 1.f);
                CellText(Align::Center, cells[k][7], &red);
              }
              ImGui::TableNextColumn();
              std::snprintf(lab, sizeof(lab), "Delete##del%d", i);
              if (ImGui::SmallButton(lab))
                deleteCheck = i;
            }
            ImGui::EndTable();
          }
          PopTableStyle();
        }

        if (bf.valid) {
          PageScale fitted = *scale;
          fitted.label.clear();
          fitted.note.clear();
          fitted.realValue = bf.metresPerPt * 72.0 / UnitInMetres(scale->realUnit);
          ImGui::Text("Best fit of %zu checks (longer ones count for more): %s", idx.size(), fitted.RatioText().c_str());
          if (bf.anyOutlier)
            ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.26f, 1.f),
                               "One or more checks disagree with the rest (marked Outlier): a wrong pick, a mistyped value or a wrong unit.");
          else if (idx.size() == 2 && std::fabs(fit[0].metres / fit[0].pts / (fit[1].metres / fit[1].pts) - 1.0) > 0.0025)
            ImGui::TextColored(VerdictColor(Verdict::Check), "The two checks disagree: add a third to see which one is off.");
        }
        ImGui::TextDisabled("A check shows the picked distances agree, not that the drawing is to scale: drawn lines can differ from printed values by a fraction of a percent.");

        // ---- The correction the user may choose ----------------------------------------------------
        if (!idx.empty()) {
          ImGui::SeparatorText("Correct the scale (your choice; nothing is changed until you apply)");
          u.matchIdx = std::clamp(u.matchIdx, 0, static_cast<int>(idx.size()) - 1);
          ImGui::RadioButton("Match this check", &u.correctionMode, 0);
          ImGui::SameLine();
          ImGui::SetNextItemWidth(70.f);
          if (ImGui::BeginCombo("##matchsel", std::to_string(u.matchIdx + 1).c_str())) {
            for (size_t k = 0; k < idx.size(); ++k)
              if (ImGui::Selectable(std::to_string(k + 1).c_str(), static_cast<int>(k) == u.matchIdx))
                u.matchIdx = static_cast<int>(k);
            ImGui::EndCombo();
          }
          ImGui::BeginDisabled(!bf.valid);
          ImGui::RadioButton("Best fit", &u.correctionMode, 1);
          ImGui::EndDisabled();
          ImGui::BeginDisabled(!bf.withoutValid);
          ImGui::SameLine();
          ImGui::RadioButton("Best fit without the outlier", &u.correctionMode, 2);
          ImGui::EndDisabled();
          ImGui::RadioButton("A percentage", &u.correctionMode, 3);
          ImGui::SameLine();
          ImGui::SetNextItemWidth(90.f);
          ImGui::InputDouble("##pct", &u.typedPct, 0.0, 0.0, "%+.3f");
          ImGui::SameLine();
          ImGui::TextUnformatted("% (+ makes every reading larger)");
          ImGui::RadioButton("Leave the scale as it is", &u.correctionMode, 4);
          if ((u.correctionMode == 1 && !bf.valid) || (u.correctionMode == 2 && !bf.withoutValid))
            u.correctionMode = 0;

          const double cur = MetresPerPt(*scale);
          double factor = 1.0;
          if (u.correctionMode == 0)
            factor = FactorMatching(all[static_cast<size_t>(idx[static_cast<size_t>(u.matchIdx)])], *scale);
          else if (u.correctionMode == 1 && bf.valid && cur > 0.0)
            factor = bf.metresPerPt / cur;
          else if (u.correctionMode == 2 && bf.withoutValid && cur > 0.0)
            factor = bf.withoutMetresPerPt / cur;
          else if (u.correctionMode == 3)
            factor = FactorFromPercent(u.typedPct);
          std::vector<ScaleCheck> pageChecks;
          for (int i : idx)
            pageChecks.push_back(all[static_cast<size_t>(i)]);
          const std::vector<CheckResult> after = PreviewCorrection(pageChecks, *scale, factor, u.limits);
          const PageScale next = ApplyFactor(*scale, factor);
          ImGui::Text("After this correction: %s%s%s%s", next.RatioText().c_str(), next.note.empty() ? "" : "  (", next.note.c_str(),
                      next.note.empty() ? "" : ")");
          const std::vector<std::pair<const char*, Align>> cols = {{"#", Align::Center}, {"Would read", Align::Right}, {"Difference", Align::Right},
                                                                   {"%", Align::Right}, {"Verdict", Align::Center}};
          std::vector<std::vector<std::string>> cells;
          for (size_t k = 0; k < after.size(); ++k)
            cells.push_back({std::to_string(k + 1), FormatValue(after[k].measured, 4) + " " + UnitLabel(pageChecks[k].unit),
                             Signed(after[k].diff, 4) + " " + UnitLabel(pageChecks[k].unit), Signed(after[k].pct, 2), VerdictName(after[k].verdict)});
          const std::vector<float> widths = ColumnWidths(cols, cells);
          PushTableStyle();
          if (ImGui::BeginTable("##after", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            TableHeader(cols, widths);
            for (size_t k = 0; k < after.size(); ++k) {
              const ImVec4 vc = VerdictColor(after[k].verdict);
              ImGui::TableNextRow();
              for (size_t col = 0; col < cols.size(); ++col) {
                ImGui::TableNextColumn();
                CellText(cols[col].second, cells[k][col], col == 4 ? &vc : nullptr);
              }
            }
            ImGui::EndTable();
          }
          PopTableStyle();
          ImGui::TextDisabled("Correcting to one check moves the error onto the others: compare the table above with the one before.");
          ImGui::BeginDisabled(factor == 1.0 || u.correctionMode == 4);
          if (ImGui::Button("Apply correction")) {
            std::map<int, PageScale> change;
            change[page] = next;
            u.session.SetScales(change);
            log.push_back("PDF scale: corrected page " + std::to_string(page + 1) + " of " + v.title + " - " + next.note);
            u.status = "Scale " + next.note + ".";
          }
          ImGui::EndDisabled();
          ImGui::SameLine();
        }
        if (ImGui::Button("Copy report")) {
          std::vector<ScaleCheck> pageChecks;
          for (int i : idx)
            pageChecks.push_back(all[static_cast<size_t>(i)]);
          const std::string rep = ScaleReport(v.title, page, *scale, pageChecks, u.limits);
          ImGui::SetClipboardText(rep.c_str());
          log.push_back(rep);
          u.status = "Scale report copied.";
        }
      }
      ImGui::EndTabItem();
    }
    // ---- Tab 2: robust calibration -----------------------------------------------------------------
    if (ImGui::BeginTabItem("Robust calibration", nullptr, u.checksTab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
      ImGui::TextWrapped("For a more exact scale: add at least %d dimensions the drawing states. One scale is solved by least squares; "
                         "long dimensions count for more, because one pick is a small part of them. Nothing changes until you Apply.",
                         u.robustParams.minDimensions);
      ImGui::SetNextItemWidth(80.f);
      ImGui::InputDouble("Pick error (pt)", &u.robustParams.pickPt, 0.0, 0.0, "%.2f");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(80.f);
      double drawPct = u.robustParams.drawingFraction * 100.0;
      if (ImGui::InputDouble("Drawing error (% of length)", &drawPct, 0.0, 0.0, "%.3f"))
        u.robustParams.drawingFraction = std::max(0.0, drawPct) / 100.0;
      ImGui::SameLine();
      ImGui::SetNextItemWidth(70.f);
      ImGui::InputInt("Minimum", &u.robustParams.minDimensions, 0, 0);
      u.robustParams.pickPt = std::max(0.01, u.robustParams.pickPt);
      u.robustParams.minDimensions = std::clamp(u.robustParams.minDimensions, 2, 50);

      if (ImGui::Button("Add dimension...")) {
        u.tool = Tool::Check;
        u.checkTarget = 1;
        u.checkPts.clear();
        u.status = "Robust calibration: click the two ends of a dimension the drawing states.";
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Clear list"))
        u.robustPicks.clear();

      std::vector<RobustObs> obs;
      for (const auto& [c, use] : u.robustPicks)
        obs.push_back({c.MeasuredPt(), c.StatedMetres(), use});
      const RobustResult res = SolveRobust(obs, u.robustParams);
      int removeRow = -1;
      if (!u.robustPicks.empty()) {
        const std::vector<std::pair<const char*, Align>> cols = {{"Use", Align::Center}, {"#", Align::Center}, {"Drawing says", Align::Right},
                                                                 {"Picked (pt)", Align::Right}, {"Residual", Align::Right}, {"%", Align::Right},
                                                                 {"Std. residual", Align::Right}, {"", Align::Center}};
        std::vector<std::vector<std::string>> cells;
        for (size_t i = 0; i < u.robustPicks.size(); ++i) {
          const ScaleCheck& c = u.robustPicks[i].first;
          const bool have = res.ok && u.robustPicks[i].second && i < res.residualMetres.size();
          cells.push_back({"Use", std::to_string(i + 1), FormatValue(c.stated, 4) + " " + UnitLabel(c.unit), FormatValue(c.MeasuredPt(), 3),
                           have ? Signed(res.residualMetres[i] / UnitInMetres(c.unit), 4) + " " + UnitLabel(c.unit) : "",
                           have ? Signed(res.residualPct[i], 2) : "",
                           have ? Signed(res.z[i], 1) + (res.suspect[i] ? "  Suspect" : "") : "", "Remove"});
        }
        const std::vector<float> widths = ColumnWidths(cols, cells);
        PushTableStyle();
        if (ImGui::BeginTable("##robust", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
          TableHeader(cols, widths);
          for (size_t i = 0; i < u.robustPicks.size(); ++i) {
            auto& [c, use] = u.robustPicks[i];
            (void)c;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char lab[32];
            std::snprintf(lab, sizeof(lab), "##use%zu", i);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight()) * 0.5f);
            ImGui::Checkbox(lab, &use);
            for (size_t col = 1; col < 7; ++col) {
              ImGui::TableNextColumn();
              const bool suspect = col == 6 && res.ok && use && i < res.suspect.size() && res.suspect[i];
              const ImVec4 red = VerdictColor(Verdict::Blunder);
              CellText(cols[col].second, cells[i][col], suspect ? &red : nullptr);
            }
            ImGui::TableNextColumn();
            std::snprintf(lab, sizeof(lab), "Remove##rm%zu", i);
            if (ImGui::SmallButton(lab))
              removeRow = static_cast<int>(i);
          }
          ImGui::EndTable();
        }
        PopTableStyle();
      }
      if (removeRow >= 0)
        u.robustPicks.erase(u.robustPicks.begin() + removeRow);

      if (res.ok) {
        const Unit unit = scale != nullptr ? scale->realUnit : u.robustPicks.front().first.unit;
        const PageScale made = ScaleFromRobust(res, unit, scale);
        ImGui::Text("Adjusted scale: %s +/- %s %%  (%d dimensions; RMS residual %s %s)", made.RatioText().c_str(),
                    FormatValue(res.relSigma * 100.0, 3).c_str(), res.used, FormatValue(res.rmsMetres / UnitInMetres(unit), 4).c_str(),
                    UnitLabel(unit));
        bool anySuspect = false;
        for (bool s : res.suspect)
          anySuspect = anySuspect || s;
        if (anySuspect)
          ImGui::TextColored(VerdictColor(Verdict::Blunder),
                             "A dimension is Suspect (a wrong pick or a mistyped value). Untick it to re-solve, or keep it.");
        if (ImGui::Button("Apply")) {
          const int rpage = u.robustPicks.empty() ? page : u.robustPicks.front().first.page;
          std::map<int, PageScale> change;
          change[rpage] = made;
          u.session.SetScales(change);
          log.push_back("PDF scale: robust calibration of page " + std::to_string(rpage + 1) + " of " + v.title + " - " + made.note);
          u.status = "Scale " + made.note + ".";
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
      } else {
        ImGui::TextColored(VerdictColor(Verdict::Check), "%s", res.why.c_str());
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  u.checksTab = -1;
  if (deleteCheck >= 0) {
    u.session.RemoveCheck(deleteCheck);
    u.selectedCheck = -1;
  }
  if (ImGui::Button("Close"))
    ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
  PopDialogStyle();
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
    const Icon ico = t == Tool::Select ? Icon::Select : t == Tool::Text ? Icon::Text : t == Tool::Line ? Icon::Line : t == Tool::Rect ? Icon::Rect
                     : t == Tool::Ellipse ? Icon::Ellipse : t == Tool::Leader ? Icon::Leader : t == Tool::Calibrate ? Icon::Calibrate
                     : t == Tool::Length ? Icon::Length : t == Tool::PolyLength ? Icon::PolyLength : t == Tool::Area ? Icon::Area
                     : t == Tool::Angle ? Icon::Angle : Icon::Check;
    if (IconButton(ico, label)) {
      u.tool = t;
      u.measurePts.clear();
      u.status.clear();
      if (t != Tool::Select)
        u.selected = -1;
      if (IsMeasureTool(t) || t == Tool::Check)
        u.scalesRequested = true; // the page's scale is needed: read what the file has
      if (t == Tool::Check) {
        u.checkTarget = 0;
        u.checkPts.clear();
        u.status = "Check: click the two ends of a dimension the drawing states.";
      }
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
  toolButton("Leader", Tool::Leader);

  bool changed = false;
  changed |= ImGui::ColorEdit3("##annotcol", u.color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
  ImGui::SameLine();
  const Annot* sel = u.selected >= 0 ? &items[static_cast<size_t>(u.selected)] : nullptr;
  const bool leaderStyle = u.tool == Tool::Leader || (sel != nullptr && sel->kind == Annot::Kind::Leader);
  const bool textStyle = u.tool == Tool::Text || leaderStyle || (sel != nullptr && sel->kind == Annot::Kind::Text);
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
  }
  if (!textStyle || leaderStyle) { // a Leader has both: its text and the thickness of its box and arrow
    if (leaderStyle)
      ImGui::SameLine();
    ImGui::SetNextItemWidth(110.f);
    changed |= ImGui::SliderFloat("##annotw", &u.thickness, 0.5f, 20.f, "%.1f pt");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Line thickness");
    if (!leaderStyle && u.tool != Tool::Line && !IsMeasureTool(u.tool) &&
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
  if (IconButton(Icon::Undo, "Undo")) {
    u.session.Undo();
    u.selected = -1;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!u.session.CanRedo() || saving);
  if (IconButton(Icon::Redo, "Redo")) {
    u.session.Redo();
    u.selected = -1;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(u.selected < 0 || saving);
  if (IconButton(Icon::Delete, "Delete")) {
    u.session.Remove(u.selected);
    u.selected = -1;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!u.session.Dirty() || saving);
  if (IconButton(Icon::SaveAs, "Save As...", "Save a copy of this PDF with your markups"))
    StartSave(v, log);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextUnformatted("|");
  ImGui::SameLine();
  if (IconButton(Icon::Scale, "Set scale...", "Set the drawing scale of this page"))
    u.scaleDialog = true;
  ImGui::SameLine();
  {
    const PageScale* cur = EffectiveScale(v, v.curPage);
    const auto unusable = u.fileScales.unusable.find(v.curPage);
    if (cur != nullptr) {
      ImGui::Text("Scale: %s%s%s%s", cur->RatioText().c_str(), cur->note.empty() ? "" : " (", cur->note.c_str(), cur->note.empty() ? "" : ")");
      ImGui::SameLine();
      // A real button, coloured by the worst verdict among this page's checks (blue when there are none yet), so
      // it is obvious that the checks exist and where to open them.
      const std::vector<int> mine = PageChecks(u, v.curPage);
      bool haveVerdict = false;
      Verdict worst = Verdict::Good;
      for (int i : mine) {
        const Verdict vd = EvaluateCheck(u.session.Checks()[static_cast<size_t>(i)], *cur, u.limits).verdict;
        if (!haveVerdict || static_cast<int>(vd) > static_cast<int>(worst))
          worst = vd;
        haveVerdict = true;
      }
      const ImVec4 base = !haveVerdict ? ImVec4(0.20f, 0.45f, 0.78f, 1.f)
                          : worst == Verdict::Good ? ImVec4(0.17f, 0.55f, 0.28f, 1.f)
                          : worst == Verdict::Check ? ImVec4(0.74f, 0.50f, 0.08f, 1.f)
                                                    : ImVec4(0.74f, 0.22f, 0.20f, 1.f);
      ImGui::PushStyleColor(ImGuiCol_Button, base);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(std::min(1.f, base.x + 0.10f), std::min(1.f, base.y + 0.10f), std::min(1.f, base.z + 0.10f), 1.f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(base.x * 0.8f, base.y * 0.8f, base.z * 0.8f, 1.f));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.f, 1.f, 1.f, 0.55f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
      char lab[64];
      std::snprintf(lab, sizeof(lab), "Scale checks (%zu)", mine.size());
      if (ImGui::Button(lab)) {
        u.checksDialog = true;
        u.checksTab = 0;
      }
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(4);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("See how this page's scale agrees with the dimensions you checked,\ncorrect it, or run a robust calibration");
      ImGui::SameLine();
      ImGui::Checkbox("Show on sheet", &u.showChecks); // hide the check lines and labels to cut the noise
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show or hide the check lines and labels drawn on the sheet");
      ImGui::SameLine();
      if (ImGui::Button("Audit scale")) { // REQ-395
        u.audit.openRequest = true;
        StartAudit(v, std::clamp(v.curPage, 0, v.layout.PageCount() - 1));
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Read this page's own dimension text and test the scale against it\n(suggestions only; the scale changes when you say so)");
    }
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
  toolButton("Check", Tool::Check);
  ImGui::TextUnformatted("|");
  ImGui::SameLine();
  { // Snap: end points and corners of the page's own line work (F3)
    if (u.snapOn) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.28f, 1.f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.62f, 0.34f, 1.f));
    }
    if (IconButton(Icon::Snap, u.snapOn ? "Snap: on" : "Snap: off"))
      u.snapOn = !u.snapOn;
    if (u.snapOn)
      ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Snap to the ends and corners of the drawing (F3)");
    if (u.snapOn && v.doc != nullptr) { // read the page's points as soon as Snap is on, so the first click can snap
      SnapPage& sp = u.snapPages[v.curPage];
      if (!sp.started)
        StartSnapRead(v, v.curPage);
      if (sp.reading.valid() && sp.reading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        sp.index = sp.reading.get();
        sp.failed = sp.index == nullptr;
      }
    }
    const auto cur = u.snapPages.find(v.curPage);
    if (u.snapOn && cur != u.snapPages.end() && cur->second.reading.valid()) {
      ImGui::SameLine();
      ImGui::TextDisabled("reading this page...");
    } else if (u.snapOn && cur != u.snapPages.end() && cur->second.failed) {
      ImGui::SameLine();
      ImGui::TextDisabled("this page has nothing to snap to");
    }
  }
  {
    const Annot* ds = u.selected >= 0 ? &items[static_cast<size_t>(u.selected)] : nullptr;
    if (IsMeasureTool(u.tool) || (ds != nullptr && ds->IsDimension())) {
      ImGui::SameLine(); // only here: a SameLine left dangling puts the next panel on this row
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
      ImGui::SameLine();
      ImGui::SetNextItemWidth(130.f);
      const auto& fonts = FontChoices();
      u.fontIdx = std::clamp(u.fontIdx, 0, static_cast<int>(fonts.size()) - 1);
      if (ImGui::BeginCombo("##dimfont", fonts[static_cast<size_t>(u.fontIdx)].family.c_str())) {
        for (size_t i = 0; i < fonts.size(); ++i)
          if (ImGui::Selectable(fonts[i].family.c_str(), static_cast<int>(i) == u.fontIdx)) {
            u.fontIdx = static_cast<int>(i);
            if (ds != nullptr && ds->IsDimension()) {
              Annot a = *ds;
              ApplyStyle(u, a);
              u.session.Replace(u.selected, a);
            }
          }
        ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Label font");
    }
  }
  ImGui::PopStyleVar(3);
  DrawScaleDialogs(v, log);
  DrawCheckPopup(v, log);
  DrawChecksDialog(v, log);
  DrawAuditDialog(v, log);

  // The note text dialog.
  char id[64];
  std::snprintf(id, sizeof(id), "Text note###pdftext%d", v.id);
  if (u.textPopup) {
    u.textPopup = false;
    ImGui::OpenPopup(id);
  }
  ImGui::SetNextWindowSize(ImVec2(420.f, 0.f), ImGuiCond_Appearing);
  if (BeginDialog(id)) {
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
        a.kind = u.textIsLeader ? Annot::Kind::Leader : Annot::Kind::Text;
        a.page = u.textPage;
        a.x0 = a.x1 = u.textX; // the box's top-left corner is the clicked point; FitTextBox sizes it from there
        a.y0 = a.y1 = u.textY;
        if (u.textIsLeader)
          a.pts = {u.leaderTip};
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
  if (BeginDialog(id)) {
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

// Reads a page's ends and corners in the background (REQ-391 clause 5). One at a time per page; a few pages are
// kept and the farthest finished one is dropped to make room.
void StartSnapRead(Viewer& v, int page) {
  AnnotUi& u = v.ann;
  SnapPage& sp = u.snapPages[page];
  if (sp.started || v.doc == nullptr)
    return;
  while (u.snapPages.size() > 4) {
    int farthest = -1;
    for (const auto& [p, other] : u.snapPages)
      if (p != page && !other.reading.valid() && (farthest < 0 || std::abs(p - page) > std::abs(farthest - page)))
        farthest = p;
    if (farthest < 0)
      break;
    u.snapPages.erase(farthest);
  }
  SnapPage& cur = u.snapPages[page];
  cur.started = true;
  PdfDocument* doc = v.doc.get();
  const auto cancel = cur.cancel;
  cur.reading = std::async(std::launch::async, [doc, page, cancel]() -> std::shared_ptr<SnapIndex> {
    std::vector<SnapPoint> pts;
    if (!doc->SnapPoints(page, pts, [cancel] { return cancel->load(); }))
      return nullptr;
    auto idx = std::make_shared<SnapIndex>();
    idx->Build(std::move(pts));
    return idx;
  });
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
  // The pointer in page points. With `snap`, the nearest end or corner of the page's own line work wins when the
  // Snap toggle is on and one is close (found just below).
  const auto toPt = [&](const PageRect& r, float& x, float& y, bool clamp, bool snap = false) {
    x = (io.MousePos.x - r.tl.x) / k;
    y = r.hPt - (io.MousePos.y - r.tl.y) / k;
    if (clamp) {
      x = std::clamp(x, 0.f, r.wPt);
      y = std::clamp(y, 0.f, r.hPt);
    }
    if (snap && u.snapHit && u.snapPage == r.page) {
      x = u.snapX;
      y = u.snapY;
    }
  };

  // Snap: the nearest end or corner of the page under the pointer, while a tool is placing points. The page's
  // points are read in the background the first time; the Length offset click is free (never snapped).
  u.snapHit = false;
  {
    const bool placing = u.tool != Tool::Select || u.drag == AnnotUi::Drag::Handle || u.drag == AnnotUi::Drag::Create;
    const bool offsetClick = (u.tool == Tool::Length && u.measurePts.size() == 2) ||
                             (u.drag == AnnotUi::Drag::Handle && u.handle == 2 && u.preview.kind == Annot::Kind::Length);
    if (u.snapOn && placing && !offsetClick && hovered && !panning) {
      for (const PageRect& r : rects) {
        const ImVec2 m = io.MousePos;
        if (m.x < r.tl.x || m.y < r.tl.y || m.x > r.tl.x + r.wPt * k || m.y > r.tl.y + r.hPt * k)
          continue;
        SnapPage& sp = u.snapPages[r.page];
        if (!sp.started)
          StartSnapRead(v, r.page);
        if (sp.reading.valid() && sp.reading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
          sp.index = sp.reading.get();
          sp.failed = sp.index == nullptr;
        }
        if (sp.index != nullptr) {
          float px, py;
          toPt(r, px, py, false);
          SnapIndex::Pt hit;
          const float radius = 10.f / k;
          bool found = sp.index->Nearest(px, py, radius, hit, 6.f / k); // points within 6 screen pixels merge into the strongest
          // Stay on the last point while the pointer is still near it, unless another is clearly closer (5 screen
          // pixels): without this the marker flips between neighbouring points as the pointer drifts.
          if (found && u.snapPrevValid && u.snapPrevPage == r.page) {
            const float dPrev = std::hypot(u.snapPrevX - px, u.snapPrevY - py), dNew = std::hypot(hit.first - px, hit.second - py);
            if (dPrev <= radius && dNew > dPrev - 5.f / k)
              hit = {u.snapPrevX, u.snapPrevY};
          }
          if (found) {
            u.snapHit = true;
            u.snapPage = r.page;
            u.snapX = hit.first;
            u.snapY = hit.second;
            u.snapPrevValid = true;
            u.snapPrevPage = r.page;
            u.snapPrevX = hit.first;
            u.snapPrevY = hit.second;
          }
        }
        break;
      }
    }
    if (!u.snapHit)
      u.snapPrevValid = false;
  }

  // A dimension is complete at its point count; a polyline or area is ended with Enter, a double-click, or (area)
  // a click on its first point. A Length takes a third click for its dimension line (Enter: on the points).
  const auto finishMeasure = [&] {
    Annot a;
    a.kind = KindOfMeasureTool(u.tool);
    a.page = u.measurePage;
    a.pts = u.measurePts;
    a.offset = a.kind == Annot::Kind::Length ? u.measureOffset : 0.f;
    u.measurePts.clear();
    u.measureOffset = 0.f;
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
    if (ImGui::IsKeyPressed(ImGuiKey_F3))
      u.snapOn = !u.snapOn;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      const bool wasMidway = !u.measurePts.empty() || !u.checkPts.empty();
      u.selected = -1;
      u.measurePts.clear();
      u.checkPts.clear();
      u.measureOffset = 0.f;
      if (u.tool == Tool::Calibrate) {
        u.tool = Tool::Select;
        u.calCount = 0;
        u.status.clear();
      } else if (!wasMidway && u.tool != Tool::Select) { // a second Esc leaves the tool: back to selecting
        u.tool = Tool::Select;
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
      toPt(r, x, y, false, u.tool == Tool::Text); // only a note's position snaps; picking an existing mark never does
      const float tol = 5.f / k;
      if (u.tool == Tool::Select) {
        if (u.selected >= 0 && items[static_cast<size_t>(u.selected)].page == r.page) {
          const auto grips = HandlePoints(items[static_cast<size_t>(u.selected)]);
          for (size_t i = 0; i < grips.size(); ++i)
            if (std::fabs(x - grips[i].first) <= 7.f / k && std::fabs(y - grips[i].second) <= 7.f / k) {
              u.drag = AnnotUi::Drag::Handle;
              u.pressMouse = io.MousePos;
              u.dragMoved = false;
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
          if ((a.kind == Annot::Kind::Text || a.kind == Annot::Kind::Leader) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            std::snprintf(u.textBuf, sizeof(u.textBuf), "%s", a.text.c_str());
            u.textEdit = hit;
            u.textPopup = true;
          } else {
            u.drag = AnnotUi::Drag::Move;
            u.pressMouse = io.MousePos;
            u.dragMoved = false;
            u.dragPage = r.page;
            u.px0 = x;
            u.py0 = y;
            u.original = u.preview = a;
          }
        }
      } else if (IsMeasureTool(u.tool)) {
        float cx, cy;
        toPt(r, cx, cy, true, true);
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
        if (u.tool == Tool::Length && u.measurePts.size() == 2) { // the third click places the dimension line
          Annot tmp;
          tmp.kind = Annot::Kind::Length;
          tmp.pts = u.measurePts;
          const DimLine dl = LengthDimLine(tmp);
          u.measureOffset = (cx - u.measurePts[0].first) * dl.nx + (cy - u.measurePts[0].second) * dl.ny;
          finishMeasure();
          return;
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !u.measurePts.empty() && u.tool != Tool::Length) {
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
                   : u.tool == Tool::Length   ? (u.measurePts.size() == 1 ? "Click the second point."
                                                                          : "Click where the dimension line goes (Enter: on the points).")
                                              : "";
        if (u.tool == Tool::Angle && u.measurePts.size() == 3)
          finishMeasure();
      } else if (u.tool == Tool::Check) {
        float cx, cy;
        toPt(r, cx, cy, true, true);
        if (!u.checkPts.empty() && r.page != u.checkPage) {
          u.status = "Keep both points on one page.";
          return;
        }
        if (u.checkPts.empty()) {
          if (u.checkTarget == 0 && EffectiveScale(v, r.page) == nullptr) { // a check tests a scale, so there must be one
            if (!u.scalesRead) {
              u.scalesRequested = true;
              u.status = "Reading this page's scale... click again in a moment.";
            } else {
              u.status = "This page has no scale. Use Set scale... first.";
            }
            return;
          }
          u.checkPage = r.page;
        }
        u.checkPts.push_back({cx, cy});
        if (u.checkPts.size() == 2) {
          u.checkPopup = true;
          u.checkText[0] = 0;
          u.status.clear();
        } else {
          u.status = "Click the other end of the dimension.";
        }
      } else if (u.tool == Tool::Calibrate) {
        float cx, cy;
        toPt(r, cx, cy, true, true);
        if (u.calCount == 0 || r.page == u.calPage) {
          u.calPage = r.page;
          u.calX[u.calCount] = cx;
          u.calY[u.calCount] = cy;
          u.calSnapped[u.calCount] = u.snapHit && u.snapPage == r.page;
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
        u.textIsLeader = false;
        u.textPage = r.page;
        u.textX = x;
        u.textY = y;
        u.textPopup = true;
      } else {
        float cx, cy;
        toPt(r, cx, cy, true, true);
        Annot a;
        a.kind = u.tool == Tool::Line     ? Annot::Kind::Line
                 : u.tool == Tool::Rect   ? Annot::Kind::Rect
                 : u.tool == Tool::Leader ? Annot::Kind::Leader
                                          : Annot::Kind::Ellipse;
        a.page = r.page;
        a.x0 = a.x1 = cx;
        a.y0 = a.y1 = cy;
        if (a.kind == Annot::Kind::Leader) { // the first point is the arrow tip; the box follows the pointer
          a.pts = {{cx, cy}};
          a.text = "Note";
        }
        ApplyStyle(u, a);
        u.drag = AnnotUi::Drag::Create;
        u.createSticky = false;
        u.pressMouse = io.MousePos;
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
    u.createSticky = false;
    return;
  }
  // A click is not a drag: nothing moves (and nothing snaps) until the pointer has travelled a few pixels, so
  // selecting a mark, or clicking one of its grips, leaves it exactly where it was.
  if (!u.dragMoved && std::hypot(io.MousePos.x - u.pressMouse.x, io.MousePos.y - u.pressMouse.y) >= 4.f)
    u.dragMoved = true;
  if (r != nullptr && (u.dragMoved || u.drag == AnnotUi::Drag::Create)) {
    float x, y;
    toPt(*r, x, y, u.drag != AnnotUi::Drag::Move, u.drag != AnnotUi::Drag::Move);
    if (u.drag == AnnotUi::Drag::Create && u.preview.kind == Annot::Kind::Leader) {
      u.preview.x0 = u.preview.x1 = x; // the box's top-left follows the pointer; FitTextBox sizes it
      u.preview.y0 = u.preview.y1 = y;
      FitTextBox(u.preview);
    } else if (u.drag == AnnotUi::Drag::Create) {
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
  // A shape is made by press-drag-release, or by click, move, click: a release that has not travelled leaves the
  // shape waiting (with its preview following the pointer) for the second click.
  const auto finishCreate = [&] {
    const Annot& p = u.preview;
    const bool leader = p.kind == Annot::Kind::Leader && !p.pts.empty();
    const float dx = leader ? p.x0 - p.pts[0].first : p.x1 - p.x0, dy = leader ? p.y1 - p.pts[0].second : p.y1 - p.y0;
    if (std::hypot(dx, dy) * k < 4.f)
      return; // both clicks on the same spot: nothing to make
    if (leader) { // the text is typed next, in the note dialog
      u.textBuf[0] = 0;
      u.textEdit = -1;
      u.textIsLeader = true;
      u.leaderTip = p.pts[0];
      u.textPage = p.page;
      u.textX = p.x0;
      u.textY = p.y1;
      u.textPopup = true;
    } else {
      u.session.Add(p);
    }
  };
  if (u.drag == AnnotUi::Drag::Create && u.createSticky) {
    if (!(u.tool == Tool::Line || u.tool == Tool::Rect || u.tool == Tool::Ellipse || u.tool == Tool::Leader)) {
      u.drag = AnnotUi::Drag::None; // another tool was chosen while waiting for the second click
      u.createSticky = false;
    } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive()) {
      finishCreate();
      u.drag = AnnotUi::Drag::None;
      u.createSticky = false;
    }
    return;
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    if (u.drag == AnnotUi::Drag::Create) {
      if (std::hypot(io.MousePos.x - u.pressMouse.x, io.MousePos.y - u.pressMouse.y) < 4.f) {
        u.createSticky = true; // a click, not a drag: wait for the second click
        return;
      }
      finishCreate();
    } else if (u.dragMoved && u.selected >= 0 && u.preview != u.original) {
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
  if (v.bench.active && v.bench.scrolling)
    ImGui::SetScrollY(v.bench.scrollPx);

  // The scroll position that is really wanted: a change asked for this frame or last (ImGui applies a
  // SetScroll one frame late, so its own GetScroll is stale right after a zoom). Zooming twice before ImGui
  // caught up used to start the second zoom from that stale value and land several pages away.
  const auto wantedY = [&] { return v.pendingScrollY >= 0.f ? v.pendingScrollY : ImGui::GetScrollY(); };
  const auto wantedX = [&] { return v.pendingScrollX >= 0.f ? v.pendingScrollX : ImGui::GetScrollX(); };

  const bool hovered = ImGui::IsWindowHovered();
  const ImGuiIO& io = ImGui::GetIO();
  if (hovered && io.KeyCtrl && io.MouseWheel != 0.f) {
    // Zoom about the pointer: the point under it stays under it.
    const ImVec2 wp = ImGui::GetWindowPos();
    const float mx = io.MousePos.x - wp.x, my = io.MousePos.y - wp.y;
    const float ptY = (wantedY() + my - kMarginPx) / v.pxPerPt;
    const float ptX = (wantedX() + mx - kMarginPx) / v.pxPerPt;
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
    v.pendingScrollY = std::max(0.f, wantedY() - io.MouseDelta.y);
    v.pendingScrollX = std::max(0.f, wantedX() - io.MouseDelta.x);
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

  // Take the wanted scroll position NOW, for drawing and for picking: ImGui moves its own scroll next frame, so
  // this frame is drawn as if it already had (the pages are shifted by the difference). Without this, the frame
  // after a zoom or a page jump showed the new layout at the old scroll offset: a flash of some other page.
  float scrollY = ImGui::GetScrollY(), scrollX = ImGui::GetScrollX();
  if (v.pendingScrollY >= 0.f) {
    const float maxY = std::max(0.f, ContentHeightPx(v) - v.viewH);
    scrollY = std::clamp(v.pendingScrollY, 0.f, maxY);
    ImGui::SetScrollY(scrollY);
    v.pendingScrollY = -1.f;
  }
  if (v.pendingScrollX >= 0.f) {
    const float maxX = std::max(0.f, std::max(avail.x, v.layout.maxWidth * v.pxPerPt + 2 * kMarginPx) - v.viewW);
    scrollX = std::clamp(v.pendingScrollX, 0.f, maxX);
    ImGui::SetScrollX(scrollX);
    v.pendingScrollX = -1.f;
  }
  const float shiftY = ImGui::GetScrollY() - scrollY, shiftX = ImGui::GetScrollX() - scrollX;
  v.lastScrollX = scrollX;
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
  ImVec2 origin = ImGui::GetCursorScreenPos(); // scrolls with the content
  origin.x += shiftX;                           // ... as if ImGui's scroll were already at the wanted position
  origin.y += shiftY;
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
        if (u.tool == Tool::Length && cur.pts.size() == 2) { // both points placed: the line follows the pointer
          const DimLine d = LengthDimLine(cur);
          const float mx = (mouse.x - r.tl.x) / v.pxPerPt, my = r.hPt - (mouse.y - r.tl.y) / v.pxPerPt;
          cur.offset = (mx - cur.pts[0].first) * d.nx + (my - cur.pts[0].second) * d.ny;
          DrawAnnot(dl, cur, r.tl, r.hPt, v.pxPerPt, false, EffectiveScale(v, r.page));
        } else {
          DrawAnnot(dl, cur, r.tl, r.hPt, v.pxPerPt, false, nullptr, &mouse);
        }
      }
      if (u.snapHit && u.snapPage == r.page) { // the end or corner the next click will take
        const ImVec2 c(r.tl.x + u.snapX * v.pxPerPt, r.tl.y + (r.hPt - u.snapY) * v.pxPerPt);
        dl->AddRect(ImVec2(c.x - 6.f, c.y - 6.f), ImVec2(c.x + 6.f, c.y + 6.f), IM_COL32(0, 0, 0, 220), 0.f, 0, 3.5f);
        dl->AddRect(ImVec2(c.x - 6.f, c.y - 6.f), ImVec2(c.x + 6.f, c.y + 6.f), IM_COL32(60, 255, 90, 255), 0.f, 0, 1.8f);
      }
      {
        // REQ-394: the checks on this page, in their verdict colour, with what they read against what the drawing says;
        // and the robust calibration's dimensions; and the Check tool's line while it is being picked.
        const PageScale* sc = EffectiveScale(v, r.page);
        const auto S = [&](float x, float y) { return ImVec2(r.tl.x + x * v.pxPerPt, r.tl.y + (r.hPt - y) * v.pxPerPt); };
        // A label as a solid chip in the check's colour with dark text, so it reads on any drawing.
        const auto label = [&](ImVec2 at, const std::string& text, ImU32 col) {
          const float fsPx = 16.f;
          ImFont* font = ImGui::GetFont();
          const ImVec2 ts = font->CalcTextSizeA(fsPx, 1e9f, 0.f, text.c_str());
          dl->AddRectFilled(ImVec2(at.x - 5.f, at.y - 3.f), ImVec2(at.x + ts.x + 5.f, at.y + ts.y + 3.f), col, 4.f);
          dl->AddRect(ImVec2(at.x - 5.f, at.y - 3.f), ImVec2(at.x + ts.x + 5.f, at.y + ts.y + 3.f), IM_COL32(0, 0, 0, 200), 4.f, 0, 1.5f);
          dl->AddText(font, fsPx, at, IM_COL32(10, 10, 10, 255), text.c_str());
        };
        const auto& checks = u.session.Checks();
        for (size_t i = 0; u.showChecks && i < checks.size(); ++i) {
          const ScaleCheck& c = checks[i];
          if (c.page != r.page)
            continue;
          ImU32 col = IM_COL32(150, 150, 150, 255);
          std::string text = "no scale";
          if (sc != nullptr) {
            const CheckResult cr = EvaluateCheck(c, *sc, u.limits);
            const ImVec4 vc = VerdictColor(cr.verdict);
            col = IM_COL32(static_cast<int>(vc.x * 255), static_cast<int>(vc.y * 255), static_cast<int>(vc.z * 255), 255);
            text = FormatValue(cr.measured, 3) + " vs " + FormatValue(cr.stated, 3) + " " + UnitLabel(c.unit) + "  (" + Signed(cr.pct, 2) + " %)";
          }
          const bool sel = static_cast<int>(i) == u.selectedCheck;
          dl->AddLine(S(c.x0, c.y0), S(c.x1, c.y1), col, sel ? 4.f : 2.f);
          dl->AddCircleFilled(S(c.x0, c.y0), 3.5f, col);
          dl->AddCircleFilled(S(c.x1, c.y1), 3.5f, col);
          const ImVec2 mid = S((c.x0 + c.x1) * 0.5f, (c.y0 + c.y1) * 0.5f);
          label(ImVec2(mid.x + 6.f, mid.y + 4.f), text, col);
        }
        for (size_t i = 0; u.showChecks && i < u.robustPicks.size(); ++i) {
          const ScaleCheck& c = u.robustPicks[i].first;
          if (c.page != r.page)
            continue;
          const ImU32 col = u.robustPicks[i].second ? IM_COL32(60, 140, 255, 255) : IM_COL32(150, 150, 150, 255);
          dl->AddLine(S(c.x0, c.y0), S(c.x1, c.y1), col, 2.f);
          dl->AddCircleFilled(S(c.x0, c.y0), 3.5f, col);
          dl->AddCircleFilled(S(c.x1, c.y1), 3.5f, col);
          const ImVec2 mid = S((c.x0 + c.x1) * 0.5f, (c.y0 + c.y1) * 0.5f);
          label(ImVec2(mid.x + 6.f, mid.y + 4.f), "#" + std::to_string(i + 1) + "  " + FormatValue(c.stated, 3) + " " + UnitLabel(c.unit), col);
        }
        if (u.audit.showOnSheet && u.audit.judged && u.audit.page == r.page) { // REQ-395: the audit's offenders
          const AuditUi& au = u.audit;
          const bool all = au.listAll;
          const auto& ms = au.matches.matches;
          for (size_t i = 0; i < ms.size() && i < au.audit.results.size(); ++i) {
            const Verdict vd = au.audit.results[i].verdict;
            const bool sel = static_cast<int>(i) == au.selected;
            if (vd == Verdict::Good && !all && !sel)
              continue;
            const ImVec4 vc = VerdictColor(vd);
            const ImU32 col = IM_COL32(static_cast<int>(vc.x * 255), static_cast<int>(vc.y * 255), static_cast<int>(vc.z * 255), 255);
            const ImVec2 a0 = S(ms[i].x0, ms[i].y0), a1 = S(ms[i].x1, ms[i].y1);
            dl->AddLine(a0, a1, col, sel ? 5.f : 3.f);
            dl->AddCircleFilled(a0, 3.5f, col);
            dl->AddCircleFilled(a1, 3.5f, col);
            if (sel || vd != Verdict::Good)
              label(ImVec2((a0.x + a1.x) * 0.5f + 6.f, (a0.y + a1.y) * 0.5f + 4.f),
                    ms[i].text + "  (" + Signed(au.audit.results[i].pct, 2) + " %)", col);
          }
        }
        if (u.tool == Tool::Check && !u.checkPts.empty() && u.checkPage == r.page) {
          const ImVec2 p0 = S(u.checkPts[0].first, u.checkPts[0].second);
          const ImVec2 p1 = u.checkPts.size() == 2 ? S(u.checkPts[1].first, u.checkPts[1].second) : ImGui::GetIO().MousePos;
          dl->AddLine(p0, p1, IM_COL32(60, 140, 255, 255), 2.f);
          dl->AddCircleFilled(p0, 4.f, IM_COL32(60, 140, 255, 255));
        }
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

  if (cmd.pdfCompareBenchPages > 0) {  // BENCH PDFCOMPARE [pages]: two generated files, the second a little different
    const int pages = cmd.pdfCompareBenchPages;
    cmd.pdfCompareBenchPages = 0;
    static int compareSerial = 0;
    const int serial = ++compareSerial;
    const auto write = [&](const char* tag, int lines) {
      const std::filesystem::path file =
          std::filesystem::temp_directory_path() / ("gosurvey_pdfcompare_" + std::string(tag) + "_" + std::to_string(serial) + ".pdf");
      const std::string bytes = MakeSyntheticPdf(pages, lines, 50);
      std::ofstream f(file, std::ios::binary);
      f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      return file;
    };
    const std::filesystem::path baseFile = write("base", 400), revFile = write("rev", 380);
    OpenPdfInViewer(baseFile.u8string());
    Viewer& v = *g_viewers.back();
    v.bench.file = baseFile;
    v.benchRev = revFile;
    v.benchRevPages = pages;
  }

  if (cmd.pdfDiffBench) {  // BENCH PDFDIFF: two generated 36 x 24 in sheets, the second a later revision of the first
    cmd.pdfDiffBench = false;
    static int diffSerial = 0;
    const int serial = ++diffSerial;
    const auto write = [&](const char* tag, int variant) {
      const std::filesystem::path file =
          std::filesystem::temp_directory_path() / ("gosurvey_pdfdiff_" + std::string(tag) + "_" + std::to_string(serial) + ".pdf");
      const std::string bytes = MakeLineWorkPdf(2592.0, 1728.0, 60000, 12345u, variant);
      std::ofstream f(file, std::ios::binary);
      f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      return file;
    };
    const std::filesystem::path baseFile = write("base", 0), revFile = write("rev", 1);
    OpenPdfInViewer(baseFile.u8string());
    Viewer& v = *g_viewers.back();
    v.bench.file = baseFile;
    v.benchRev = revFile;
    v.benchDiff = true;
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
    if (v.syncApply) { // ImGui's window was out of step with the OS window: take the OS's position and size
      ImGui::SetNextWindowPos(v.syncPos, ImGuiCond_Always);
      ImGui::SetNextWindowSize(v.syncSize, ImGuiCond_Always);
      v.syncApply = false;
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
      // REQ-397: a new viewer opens maximized. A window maximized only once, straight after ImGui created it, was
      // left with the mouse offset from the buttons (a screenshot showed the pointer over Line with "<" lit) until
      // the user moved it and maximized it again. So do what the user did: maximize, restore, maximize again,
      // a few frames apart, each time making ImGui re-read the window's position and size from the OS.
      if (!v.maximized && v.osFramed && v.framedHwnd != nullptr) {
        const int step = ++v.framedFrames;
        const int cmd = step == 4 ? SW_MAXIMIZE : step == 7 ? SW_RESTORE : step == 10 ? SW_MAXIMIZE : -1;
        if (cmd >= 0) {
          ShowWindow(static_cast<HWND>(v.framedHwnd), cmd);
          self->Viewport->PlatformRequestMove = true;
          self->Viewport->PlatformRequestResize = true;
        }
        if (step >= 10)
          v.maximized = true;
      }
      // Self-check (REQ-397): ImGui must think the window is exactly where the OS has it, or the mouse lands on
      // the wrong button (seen after the opening maximize). If the two stay apart for a few frames, say so in the
      // log and set ImGui's window to the OS's position and size.
      if (v.osFramed && v.framedHwnd != nullptr && !IsIconic(static_cast<HWND>(v.framedHwnd))) {
        POINT origin{0, 0};
        RECT client{};
        ClientToScreen(static_cast<HWND>(v.framedHwnd), &origin);
        GetClientRect(static_cast<HWND>(v.framedHwnd), &client);
        const ImVec2 vp = self->Viewport->Pos, vs = self->Viewport->Size;
        const bool apart = std::fabs(vp.x - static_cast<float>(origin.x)) > 1.5f || std::fabs(vp.y - static_cast<float>(origin.y)) > 1.5f ||
                           std::fabs(vs.x - static_cast<float>(client.right)) > 1.5f || std::fabs(vs.y - static_cast<float>(client.bottom)) > 1.5f;
        v.syncBad = apart && !ImGui::IsMouseDown(ImGuiMouseButton_Left) ? v.syncBad + 1 : 0;
        if (v.syncBad >= 3 && !v.syncApply) {
          v.syncPos = ImVec2(static_cast<float>(origin.x), static_cast<float>(origin.y));
          v.syncSize = ImVec2(static_cast<float>(client.right), static_cast<float>(client.bottom));
          v.syncApply = true;
          v.syncBad = 0;
          if (v.syncLogged++ < 5) {
            char msg[200];
            std::snprintf(msg, sizeof(msg), "PDF viewer: window out of step (ImGui %.0f,%.0f %.0fx%.0f; OS %ld,%ld %ldx%ld) - corrected.",
                          vp.x, vp.y, vs.x, vs.y, origin.x, origin.y, client.right, client.bottom);
            log.push_back(msg);
          }
        }
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
        if (v.comparePickRequest) {  // REQ-392 clause 1: Compare... asks for the revision
          v.comparePickRequest = false;
          char tmp[1024] = {};
          if (BrowseOpenFilePdfUtf8(tmp, sizeof(tmp)) && tmp[0] != '\0')
            v.compare = std::make_unique<PdfCompare>(v.doc.get(), v.title, v.curPage, std::filesystem::u8path(tmp));
        }
        if (!v.benchRev.empty()) {  // BENCH PDFCOMPARE
          v.compare = std::make_unique<PdfCompare>(v.doc.get(), v.title, 0, v.benchRev);
          if (v.benchDiff)
            v.compare->StartDiffBench(true);
          else
            v.compare->StartBench(v.benchRevPages, true);
          v.benchRev.clear();
        }
        if (v.compare != nullptr) {
          if (!v.compare->Draw(log))
            v.compare.reset();
          else if (v.compare->BenchFinished())
            v.bench.finished = true;
        } else {
        PushPanelStyle(); // the tool bars sit in a raised panel, like the comparison bar
        ImGui::BeginChild("##pdftools", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
        DrawToolbar(v);
        DrawAnnotBar(v, log);
        ImGui::EndChild();
        PopPanelStyle();
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
