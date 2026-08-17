#include "SurveyPoints.hpp"

#include "CadCommands.hpp"
#include "MtextRichFormat.hpp"
#include "NumFormat.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

float SurveyPointCrossHalfWorldFromPaper(float crossSpanPlottedInches, float modelUnitsPerPlottedInch) {
  const float mup = std::max(modelUnitsPerPlottedInch, 1.e-6f);
  const float span = std::max(crossSpanPlottedInches, 1.e-6f);
  return 0.5f * span * mup;
}

void AppendSurveyPointCrossVertices(float easting, float northing, float elevationZ, float halfExtentWorld,
                                    std::vector<float>* outLines, const MarkerBillboardBasis& basis) {
  if (!outLines || halfExtentWorld <= 0.f)
    return;
  const float s = halfExtentWorld;
  // Offsets are in the marker's own plane (u across, v up), mapped into world through the basis.
  // With the default basis this reduces to the previous world-XY arithmetic exactly.
  auto emit = [&](float u, float v) {
    outLines->push_back(easting + basis.rightX * u + basis.upX * v);
    outLines->push_back(northing + basis.rightY * u + basis.upY * v);
    outLines->push_back(elevationZ + basis.rightZ * u + basis.upZ * v);
  };
  emit(-s, -s);
  emit(s, s);
  emit(-s, s);
  emit(s, -s);
}

void AppendAllSurveyPointMarkers(float crossHalfWorld, const std::vector<SurveyPoint>& pts,
                                 std::vector<float>* outLines, const MarkerBillboardBasis& basis) {
  if (!outLines || pts.empty())
    return;
  // The point's OWN elevation (REQ-057/058). This used to be `0.055f + elevation * 1e-6f` — a
  // draw-ORDER bias from the flat renderer, with a microscopic elevation term to keep coincident
  // markers stably ordered. Depth testing is off, so Z never ordered anything (draw order does);
  // once the view could tilt, that constant simply pinned every marker to the datum while its own
  // snap, hover ring and ID label had moved to the real elevation.
  const float h = std::max(crossHalfWorld, 1.e-8f);
  for (const auto& p : pts)
    AppendSurveyPointCrossVertices(p.easting, p.northing, p.elevation, h, outLines, basis);
}

namespace {

int FindSurveyPointIndexById(const std::vector<SurveyPoint>& pts, int id) {
  for (size_t i = 0; i < pts.size(); ++i)
    if (pts[i].id == id)
      return static_cast<int>(i);
  return -1;
}

bool SurveyIdExists(const std::vector<SurveyPoint>& pts, int id) {
  return FindSurveyPointIndexById(pts, id) >= 0;
}

int NextFreeId(const std::vector<SurveyPoint>& pts, int start, int step) {
  const int inc = step != 0 ? std::abs(step) : 1;
  const int dir = step >= 0 ? 1 : -1;
  int id = start;
  int guard = 0;
  while (SurveyIdExists(pts, id) && guard < 1000000) {
    id += dir * inc;
    ++guard;
  }
  return id;
}

std::string JsonEscape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char uc : s) {
    const char c = static_cast<char>(uc);
    switch (c) {
    case '"':
      o += "\\\"";
      break;
    case '\\':
      o += "\\\\";
      break;
    case '\n':
      o += "\\n";
      break;
    case '\r':
      o += "\\r";
      break;
    case '\t':
      o += "\\t";
      break;
    default:
      o += c;
      break;
    }
  }
  return o;
}

bool ExtractJsonStringField(const std::string& obj, const char* key, std::string* out) {
  const std::string pat = std::string("\"") + key + "\":\"";
  size_t a = obj.find(pat);
  if (a == std::string::npos)
    return false;
  a += pat.size();
  std::string accum;
  for (size_t i = a; i < obj.size(); ++i) {
    const char c = obj[i];
    if (c == '\\' && i + 1 < obj.size()) {
      const char n = obj[i + 1];
      if (n == 'n')
        accum.push_back('\n');
      else if (n == 'r')
        accum.push_back('\r');
      else if (n == 't')
        accum.push_back('\t');
      else if (n == '"' || n == '\\')
        accum.push_back(n);
      else {
        accum.push_back(c);
        accum.push_back(n);
      }
      ++i;
      continue;
    }
    if (c == '"')
      break;
    accum.push_back(c);
  }
  *out = accum;
  return true;
}

} // namespace

std::vector<int> ResolvePointGroup(const AppCommandState& st, const PointGroup& group,
                                   std::vector<std::string>* log) {
  std::vector<std::string> badTokens;
  const std::vector<PointIdRange> ranges = ParseIdRanges(group.rule.idRangesText, &badTokens);
  if (log && !badTokens.empty()) {
    std::string msg = "Point group \"" + group.name + "\": ignored unparseable id range(s):";
    for (const std::string& t : badTokens)
      msg += " \"" + t + "\"";
    log->push_back(msg);
  }

  std::vector<int> out;
  for (size_t i = 0; i < st.surveyPoints.size(); ++i) {
    const SurveyPoint& p = st.surveyPoints[i];
    if (PointMatchesRule(group.rule, ranges, p.id, p.description, p.rawDescription))
      out.push_back(static_cast<int>(i));
  }

  // A configured rule that finds nothing is a legitimate result, not an error — but it is also the
  // single most confusing outcome to meet in silence, so it is said out loud (REQ-201). An *empty*
  // rule is not reported here: it resolves to nothing by definition, and the editor shows that.
  if (log && out.empty() && !group.rule.empty())
    log->push_back("Point group \"" + group.name + "\" matches no points.");
  return out;
}

int FindPointGroupIndex(const AppCommandState& st, const std::string& name) {
  auto eqCI = [](const std::string& a, const std::string& b) {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i < a.size(); ++i)
      if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
        return false;
    return true;
  };
  for (size_t i = 0; i < st.pointGroups.size(); ++i)
    if (eqCI(st.pointGroups[i].name, name))
      return static_cast<int>(i);
  return -1;
}

void ResetCreatePointsNextIdFromSettings(AppCommandState& st) {
  st.createPointsNextId = st.createPointsOpts.startNumber;
}

bool TryPlaceSurveyPoint(AppCommandState& st, float easting, float northing, float elevation,
                         std::vector<std::string>& log) {
  auto& opts = st.createPointsOpts;
  const int idTry = st.createPointsNextId;
  const int step = opts.pointNumberOffset != 0 ? opts.pointNumberOffset : 1;

  auto advanceSequential = [&](int placedId) {
    if (opts.sequentialNumbering)
      st.createPointsNextId = placedId + step;
  };

  auto commitNew = [&](int id) {
    SurveyPoint p{};
    p.id = id;
    p.easting = easting;
    p.northing = northing;
    p.elevation = elevation;
    p.layer = opts.layer;
    p.description = opts.defaultDescription;
    // A point created here carries what the user typed, so the raw code is the same string
    // (REQ-066). They diverge only when the description is later edited.
    p.rawDescription = opts.defaultDescription;
    st.surveyPoints.push_back(std::move(p));
    log.push_back("Survey point " + std::to_string(id) + " — E " + std::to_string(easting) + " N " +
                  std::to_string(northing) + " Z " + std::to_string(elevation));
    advanceSequential(id);
    EnsureSurveyPointLabelMtext(st, st.surveyPoints.size() - 1, &log);
  };

  if (!SurveyIdExists(st.surveyPoints, idTry)) {
    commitNew(idTry);
    return true;
  }

  switch (opts.duplicatePolicy) {
  case SurveyDuplicatePolicy::Notify:
    log.push_back("Survey point ID " + std::to_string(idTry) + " already exists — not placed (Notify).");
    return false;

  case SurveyDuplicatePolicy::Renumber: {
    const int nid = NextFreeId(st.surveyPoints, idTry, step);
    commitNew(nid);
    log.push_back("ID " + std::to_string(idTry) + " taken — stored as " + std::to_string(nid) + ".");
    return true;
  }

  case SurveyDuplicatePolicy::Merge: {
    const int ix = FindSurveyPointIndexById(st.surveyPoints, idTry);
    SurveyPoint& p = st.surveyPoints[static_cast<size_t>(ix)];
    p.easting = easting;
    p.northing = northing;
    p.elevation = elevation;
    if (!opts.defaultDescription.empty()) {
      if (!p.description.empty())
        p.description += "; ";
      p.description += opts.defaultDescription;
    }
    log.push_back("Survey point " + std::to_string(idTry) + " merged (coordinates/description updated).");
    advanceSequential(idTry);
    EnsureSurveyPointLabelMtext(st, static_cast<size_t>(ix), &log);
    return true;
  }

  case SurveyDuplicatePolicy::Overwrite: {
    const int ix = FindSurveyPointIndexById(st.surveyPoints, idTry);
    SurveyPoint& p = st.surveyPoints[static_cast<size_t>(ix)];
    p.easting = easting;
    p.northing = northing;
    p.elevation = elevation;
    p.layer = opts.layer;
    p.description = opts.defaultDescription;
    // Overwrite replaces the point, so the raw code is replaced with it. Merge (above) deliberately
    // does NOT touch rawDescription: it appends to the description of a point that keeps its
    // original field code, and REQ-066 says the raw code is not rewritten by description edits.
    p.rawDescription = opts.defaultDescription;
    log.push_back("Survey point " + std::to_string(idTry) + " overwritten.");
    advanceSequential(idTry);
    EnsureSurveyPointLabelMtext(st, static_cast<size_t>(ix), &log);
    return true;
  }
  }
  return false;
}

void DuplicateSelectedSurveyPointsTranslated(AppCommandState& st, float dx, float dy, SurveyDuplicatePolicy policy,
                                             std::vector<std::string>& log) {
  auto& pts = st.surveyPoints;
  auto& buffers = st.surveyPointIdBuffers;
  std::vector<int> ix = st.selectedSurveyPointIndices;
  std::sort(ix.begin(), ix.end());
  ix.erase(std::unique(ix.begin(), ix.end()), ix.end());

  const int step = st.createPointsOpts.pointNumberOffset != 0 ? st.createPointsOpts.pointNumberOffset : 1;
  int added = 0;
  int skipped = 0;
  int merged = 0;
  int overwrote = 0;

  for (int i : ix) {
    if (i < 0 || static_cast<size_t>(i) >= pts.size())
      continue;

    SurveyPoint copy = pts[static_cast<size_t>(i)];
    copy.easting += dx;
    copy.northing += dy;
    const int srcId = copy.id;

    auto findOtherWithId = [&](int id) -> int {
      for (size_t j = 0; j < pts.size(); ++j)
        if (static_cast<int>(j) != i && pts[j].id == id)
          return static_cast<int>(j);
      return -1;
    };

    switch (policy) {
    case SurveyDuplicatePolicy::Notify: {
      if (findOtherWithId(srcId) >= 0) {
        ++skipped;
        continue;
      }
      const int nid = NextFreeId(pts, srcId, step);
      copy.id = nid;
      copy.labelMtextAnnId = 0;   // a copied point is a new point: it owns no label yet (REQ-076)
      pts.push_back(std::move(copy));
      buffers.push_back(std::to_string(nid));
      ++added;
      EnsureSurveyPointLabelMtext(st, pts.size() - 1, &log);
      break;
    }
    case SurveyDuplicatePolicy::Renumber: {
      const int nid = NextFreeId(pts, srcId, step);
      copy.id = nid;
      copy.labelMtextAnnId = 0;   // a copied point is a new point: it owns no label yet (REQ-076)
      pts.push_back(std::move(copy));
      buffers.push_back(std::to_string(nid));
      ++added;
      EnsureSurveyPointLabelMtext(st, pts.size() - 1, &log);
      break;
    }
    case SurveyDuplicatePolicy::Merge: {
      const int other = findOtherWithId(srcId);
      if (other >= 0) {
        SurveyPoint& tgt = pts[static_cast<size_t>(other)];
        tgt.easting = copy.easting;
        tgt.northing = copy.northing;
        tgt.elevation = copy.elevation;
        if (!copy.description.empty()) {
          if (!tgt.description.empty())
            tgt.description += "; ";
          tgt.description += copy.description;
        }
        if (!copy.layer.empty())
          tgt.layer = copy.layer;
        ++merged;
        EnsureSurveyPointLabelMtext(st, static_cast<size_t>(other), &log);
      } else {
        const int nid = NextFreeId(pts, srcId, step);
        copy.id = nid;
        copy.labelMtextAnnId = 0;   // a copied point is a new point: it owns no label yet (REQ-076)
        pts.push_back(std::move(copy));
        buffers.push_back(std::to_string(nid));
        ++added;
        EnsureSurveyPointLabelMtext(st, pts.size() - 1, &log);
      }
      break;
    }
    case SurveyDuplicatePolicy::Overwrite: {
      const int other = findOtherWithId(srcId);
      if (other >= 0) {
        pts[static_cast<size_t>(other)] = copy;
        if (static_cast<size_t>(other) < buffers.size())
          buffers[static_cast<size_t>(other)] = std::to_string(copy.id);
        ++overwrote;
        EnsureSurveyPointLabelMtext(st, static_cast<size_t>(other), &log);
      } else {
        const int nid = NextFreeId(pts, srcId, step);
        copy.id = nid;
        copy.labelMtextAnnId = 0;   // a copied point is a new point: it owns no label yet (REQ-076)
        pts.push_back(std::move(copy));
        buffers.push_back(std::to_string(nid));
        ++added;
        EnsureSurveyPointLabelMtext(st, pts.size() - 1, &log);
      }
      break;
    }
    }
  }

  if (added || skipped || merged || overwrote) {
    std::string msg = "COPY survey — ";
    if (added)
      msg += std::to_string(added) + " added";
    if (merged) {
      if (added)
        msg += ", ";
      msg += std::to_string(merged) + " merged";
    }
    if (overwrote) {
      if (added || merged)
        msg += ", ";
      msg += std::to_string(overwrote) + " overwritten";
    }
    if (skipped) {
      if (added || merged || overwrote)
        msg += ", ";
      msg += std::to_string(skipped) + " skipped (duplicate ID)";
    }
    msg += ".";
    log.push_back(msg);
  }
}

namespace {

void RotateSurveyCoords(float bx, float by, float rad, float* x, float* y) {
  const float c = std::cos(rad);
  const float s = std::sin(rad);
  float dx = *x - bx;
  float dy = *y - by;
  *x = bx + c * dx - s * dy;
  *y = by + s * dx + c * dy;
}

} // namespace

void DuplicateSelectedSurveyPointsRotated(AppCommandState& st, float bx, float by, float rad,
                                            SurveyDuplicatePolicy policy, std::vector<std::string>& log) {
  auto& pts = st.surveyPoints;
  auto& buffers = st.surveyPointIdBuffers;
  std::vector<int> ix = st.selectedSurveyPointIndices;
  std::sort(ix.begin(), ix.end());
  ix.erase(std::unique(ix.begin(), ix.end()), ix.end());

  const int step = st.createPointsOpts.pointNumberOffset != 0 ? st.createPointsOpts.pointNumberOffset : 1;
  int added = 0;
  int skipped = 0;
  int merged = 0;
  int overwrote = 0;

  for (int i : ix) {
    if (i < 0 || static_cast<size_t>(i) >= pts.size())
      continue;

    SurveyPoint copy = pts[static_cast<size_t>(i)];
    RotateSurveyCoords(bx, by, rad, &copy.easting, &copy.northing);
    const int srcId = copy.id;

    auto findOtherWithId = [&](int id) -> int {
      for (size_t j = 0; j < pts.size(); ++j)
        if (static_cast<int>(j) != i && pts[j].id == id)
          return static_cast<int>(j);
      return -1;
    };

    switch (policy) {
    case SurveyDuplicatePolicy::Notify: {
      if (findOtherWithId(srcId) >= 0) {
        ++skipped;
        continue;
      }
      const int nid = NextFreeId(pts, srcId, step);
      copy.id = nid;
      copy.labelMtextAnnId = 0;   // a copied point is a new point: it owns no label yet (REQ-076)
      pts.push_back(std::move(copy));
      buffers.push_back(std::to_string(nid));
      ++added;
      EnsureSurveyPointLabelMtext(st, pts.size() - 1, &log);
      break;
    }
    case SurveyDuplicatePolicy::Renumber: {
      const int nid = NextFreeId(pts, srcId, step);
      copy.id = nid;
      copy.labelMtextAnnId = 0;   // a copied point is a new point: it owns no label yet (REQ-076)
      pts.push_back(std::move(copy));
      buffers.push_back(std::to_string(nid));
      ++added;
      EnsureSurveyPointLabelMtext(st, pts.size() - 1, &log);
      break;
    }
    case SurveyDuplicatePolicy::Merge: {
      const int other = findOtherWithId(srcId);
      if (other >= 0) {
        SurveyPoint& tgt = pts[static_cast<size_t>(other)];
        tgt.easting = copy.easting;
        tgt.northing = copy.northing;
        tgt.elevation = copy.elevation;
        if (!copy.description.empty()) {
          if (!tgt.description.empty())
            tgt.description += "; ";
          tgt.description += copy.description;
        }
        if (!copy.layer.empty())
          tgt.layer = copy.layer;
        ++merged;
        EnsureSurveyPointLabelMtext(st, static_cast<size_t>(other), &log);
      } else {
        const int nid = NextFreeId(pts, srcId, step);
        copy.id = nid;
        copy.labelMtextAnnId = 0;   // a copied point is a new point: it owns no label yet (REQ-076)
        pts.push_back(std::move(copy));
        buffers.push_back(std::to_string(nid));
        ++added;
        EnsureSurveyPointLabelMtext(st, pts.size() - 1, &log);
      }
      break;
    }
    case SurveyDuplicatePolicy::Overwrite: {
      const int other = findOtherWithId(srcId);
      if (other >= 0) {
        pts[static_cast<size_t>(other)] = copy;
        if (static_cast<size_t>(other) < buffers.size())
          buffers[static_cast<size_t>(other)] = std::to_string(copy.id);
        ++overwrote;
        EnsureSurveyPointLabelMtext(st, static_cast<size_t>(other), &log);
      } else {
        const int nid = NextFreeId(pts, srcId, step);
        copy.id = nid;
        copy.labelMtextAnnId = 0;   // a copied point is a new point: it owns no label yet (REQ-076)
        pts.push_back(std::move(copy));
        buffers.push_back(std::to_string(nid));
        ++added;
        EnsureSurveyPointLabelMtext(st, pts.size() - 1, &log);
      }
      break;
    }
    }
  }

  if (added || skipped || merged || overwrote) {
    std::string msg = "ROTATE COPY survey — ";
    if (added)
      msg += std::to_string(added) + " added";
    if (merged) {
      if (added)
        msg += ", ";
      msg += std::to_string(merged) + " merged";
    }
    if (overwrote) {
      if (added || merged)
        msg += ", ";
      msg += std::to_string(overwrote) + " overwritten";
    }
    if (skipped) {
      if (added || merged || overwrote)
        msg += ", ";
      msg += std::to_string(skipped) + " skipped (duplicate ID)";
    }
    msg += ".";
    log.push_back(msg);
  }
}

int FindSurveyLabelAnnIndex(const AppCommandState& st, const SurveyPoint& p) {
  if (p.labelMtextAnnId == 0)
    return -1;
  const EntityRef r = FindEntityById(st, p.labelMtextAnnId);
  // An id that resolves to some other entity kind means the annotation was erased and the id was
  // never issued again — so this is "no label", not "look somewhere else" (REQ-076).
  if (!r.valid() || r.kind != EntityKind::Annotation)
    return -1;
  return r.index;
}

int SurveyPointIndexForId(const AppCommandState& st, int surveyPointId) {
  if (surveyPointId < 0)
    return -1;
  return FindSurveyPointIndexById(st.surveyPoints, surveyPointId);
}

void RemoveSurveyPointAt(AppCommandState& st, size_t index) {
  if (index >= st.surveyPoints.size())
    return;
  const int doomedPointId = st.surveyPoints[index].id;
  int annIx = FindSurveyLabelAnnIndex(st, st.surveyPoints[index]);
  // Also honour a label that claims this point but is not claimed back — the two halves can
  // disagree after an import, and dropping such a label is what keeps an orphan off the screen.
  for (size_t ai = 0; ai < st.cadAnnotations.size(); ++ai) {
    if (st.cadAnnotations[ai].kind == CadAnnotation::Kind::Mtext &&
        st.cadAnnotations[ai].surveyPointLabelForId == doomedPointId)
      annIx = static_cast<int>(ai);
  }
  if (annIx >= 0 && static_cast<size_t>(annIx) < st.cadAnnotations.size())
    EraseCadAnnotationAtIndex(st, static_cast<size_t>(annIx));

  // Clear only the links that named *this* point. No renumbering pass: the back-link is a point id
  // (REQ-076), and erasing an earlier point no longer changes what any other link means — which is
  // the entire reason this is an id and not the index it used to be.
  for (CadAnnotation& a : st.cadAnnotations)
    if (a.surveyPointLabelForId == doomedPointId)
      a.surveyPointLabelForId = -1;

  st.surveyPoints.erase(st.surveyPoints.begin() + static_cast<std::ptrdiff_t>(index));
  if (index < st.surveyPointIdBuffers.size())
    st.surveyPointIdBuffers.erase(st.surveyPointIdBuffers.begin() + static_cast<std::ptrdiff_t>(index));
  auto& sv = st.selectedSurveyPointIndices;
  sv.erase(std::remove_if(sv.begin(), sv.end(), [&](int i) { return i == static_cast<int>(index); }), sv.end());
  for (int& i : sv) {
    if (i > static_cast<int>(index))
      --i;
  }
  BumpCadGpuCache(st);
}

namespace {

void ReplaceAll(std::string* s, const std::string& from, const std::string& to) {
  if (from.empty())
    return;
  size_t pos = 0;
  while ((pos = s->find(from, pos)) != std::string::npos) {
    s->replace(pos, from.size(), to);
    pos += to.size();
  }
}

} // namespace

std::string FormatSurveyPointLabelPlain(const SurveyPoint& p, SurveyPointLabelStyle style,
                                       const SurveyLabelStyleTemplates& templates, int precision,
                                       double worldOriginX, double worldOriginY) {
  if (style == SurveyPointLabelStyle::None)
    return {};
  const std::string* tpl = &templates.numberDesc;
  switch (style) {
  case SurveyPointLabelStyle::NumberDesc:
    tpl = &templates.numberDesc;
    break;
  case SurveyPointLabelStyle::NumberOnly:
    tpl = &templates.numberOnly;
    break;
  case SurveyPointLabelStyle::DescOnly:
    tpl = &templates.descOnly;
    break;
  case SurveyPointLabelStyle::NumberElev:
    tpl = &templates.numberElev;
    break;
  case SurveyPointLabelStyle::NumberElevDesc:
    tpl = &templates.numberElevDesc;
    break;
  case SurveyPointLabelStyle::NumberNorthEast:
    tpl = &templates.numberNorthEast;
    break;
  case SurveyPointLabelStyle::NorthEast:
    tpl = &templates.northEast;
    break;
  case SurveyPointLabelStyle::NumberNorthEastElev:
    tpl = &templates.numberNorthEastElev;
    break;
  default:
    tpl = &templates.numberDesc;
    break;
  }
  std::string out = *tpl;
  ReplaceAll(&out, "{id}", std::to_string(p.id));
  ReplaceAll(&out, "{desc}", p.description);
  ReplaceAll(&out, "{elev}", FormatLinear(static_cast<double>(p.elevation), precision));
  ReplaceAll(&out, "{north}", FormatLinear(static_cast<double>(p.northing) + worldOriginY, precision));
  ReplaceAll(&out, "{east}", FormatLinear(static_cast<double>(p.easting) + worldOriginX, precision));
  return out;
}

void RepositionSurveyLabelMtextForPoint(AppCommandState& st, size_t pointIndex) {
  if (pointIndex >= st.surveyPoints.size())
    return;
  const SurveyPoint& p = st.surveyPoints[pointIndex];
  const int aix = FindSurveyLabelAnnIndex(st, p);
  if (aix < 0)
    return;
  CadAnnotation& a = st.cadAnnotations[static_cast<size_t>(aix)];
  if (a.kind != CadAnnotation::Kind::Mtext)
    return;

  const float mup = std::max(st.modelUnitsPerPlottedInch, 1.e-6f);
  const float hWorld = CadAnnotationHeightWorld(a, st.modelUnitsPerPlottedInch);
  const float vpH = std::max(st.viewportLastSurveyLayoutHeightPx, 64.f);
  const float halfH = std::max(st.viewportLastSurveyLayoutOrthoHalfH, 1.e-4f);
  const float worldPerPxY = (2.f * halfH) / vpH;

  ImFont* font = ImGui::GetFont();
  const float fontPx =
      std::clamp(hWorld / std::max(worldPerPxY, 1.e-6f), st.viewportMtextMinPx, st.viewportMtextMaxPx);

  const std::string norm = MtextRichNormalize(a.text);
  float pw = 8.f;
  float ph = fontPx * 1.22f;
  MtextRichNaturalContentPx(font, fontPx, norm, &pw, &ph);

  constexpr float kPadPx = 8.f;
  const float bw = (pw + 2.f * kPadPx) * worldPerPxY;
  const float bh = (ph + 2.f * kPadPx) * worldPerPxY;

  const float minDim = std::max(0.04f * hWorld, 1.e-4f * mup);
  const float bwClamped = std::max(bw, minDim);
  const float bhClamped = std::max(bh, minDim);

  const float offsetE = a.surveyLabelHasUserOffset ? a.surveyLabelUserOffsetEast
                                                    : st.surveyLabelOffsetEastPlottedIn * mup;
  const float offsetN = a.surveyLabelHasUserOffset ? a.surveyLabelUserOffsetNorth
                                                    : st.surveyLabelOffsetNorthPlottedIn * mup;
  const float cx = p.easting + offsetE;
  const float cy = p.northing + offsetN;
  a.boxMinX = cx - bwClamped * 0.5f;
  a.boxMaxX = cx + bwClamped * 0.5f;
  a.boxMinY = cy - bhClamped * 0.5f;
  a.boxMaxY = cy + bhClamped * 0.5f;
  a.insX = a.boxMinX;
  a.insY = a.boxMinY;
  // The label sits at its POINT's elevation (REQ-057/058). Without this, insZ keeps its 0 default —
  // and insZ is absolute (ADR-025 D2), so every label was pinned to the datum while its point moved
  // to real elevation, which is exactly what an orbit made visible. The draw path already reads
  // insZ everywhere; this assignment was the only thing missing.
  a.insZ = p.elevation;
}

void RepositionAllSurveyPointLabels(AppCommandState& st) {
  for (size_t i = 0; i < st.surveyPoints.size(); ++i)
    RepositionSurveyLabelMtextForPoint(st, i);
}

void RegenerateAllSurveyPointLabels(AppCommandState& st) {
  // Rebuilds label text (so {north}/{east} reflect the current document origin) and repositions.
  for (size_t i = 0; i < st.surveyPoints.size(); ++i)
    EnsureSurveyPointLabelMtext(st, i, nullptr);
}

void ResolveConflictingWorldSurveyPoints(AppCommandState& st, const std::vector<SurveyPoint>& conflictsWorld,
                                         bool overwrite, int offset, std::vector<std::string>* log) {
  for (const SurveyPoint& w : conflictsWorld) {
    int id = w.id;
    if (overwrite) {
      const int idx = FindSurveyPointIndexById(st.surveyPoints, id);
      if (idx >= 0)
        RemoveSurveyPointAt(st, static_cast<size_t>(idx));
    } else {
      id = w.id + offset;
      int guard = 0;
      while (SurveyIdExists(st.surveyPoints, id) && guard++ < 1000000)
        ++id;
    }
    SurveyPoint sp = w;
    sp.id = id;
    sp.easting = static_cast<float>(static_cast<double>(w.easting) - st.worldDocumentOriginX);
    sp.northing = static_cast<float>(static_cast<double>(w.northing) - st.worldDocumentOriginY);
    sp.labelMtextAnnId = 0;
    st.surveyPoints.push_back(sp);
    EnsureSurveyPointLabelMtext(st, st.surveyPoints.size() - 1, log);
  }
}

void EnsureSurveyPointLabelMtext(AppCommandState& st, size_t pointIndex, std::vector<std::string>* log) {
  if (pointIndex >= st.surveyPoints.size())
    return;
  SurveyPoint& p = st.surveyPoints[pointIndex];
  if (p.labelStyle == SurveyPointLabelStyle::None) {
    const int existing = FindSurveyLabelAnnIndex(st, p);
    if (existing >= 0)
      EraseCadAnnotationAtIndex(st, static_cast<size_t>(existing));
    p.labelMtextAnnId = 0;
    BumpCadGpuCache(st);
    return;
  }

  const std::string body =
      FormatSurveyPointLabelPlain(p, p.labelStyle, st.surveyLabelTemplates, st.surveyPointDisplayPrecision,
                                  st.worldDocumentOriginX, st.worldDocumentOriginY);
  const int existing = FindSurveyLabelAnnIndex(st, p);
  if (existing >= 0) {
    CadAnnotation& a = st.cadAnnotations[static_cast<size_t>(existing)];
    if (a.kind == CadAnnotation::Kind::Mtext) {
      a.text = body;
      a.surveyPointLabelForId = p.id;
      a.plottedHeightInches = std::max(st.surveyPointLabelPlottedHeightInches, 0.04f);
      RepositionSurveyLabelMtextForPoint(st, pointIndex);
      BumpCadGpuCache(st);
      return;
    }
    p.labelMtextAnnId = 0;
  }

  CadAnnotation ann{};
  ann.kind = CadAnnotation::Kind::Mtext;
  ann.text = body;
  ann.plottedHeightInches = std::max(st.surveyPointLabelPlottedHeightInches, 0.04f);
  ann.rotationRad = 0.f;
  ann.surveyPointLabelForId = p.id;
  st.cadAnnotations.push_back(std::move(ann));
  while (st.cadAnnotationAttrs.size() < st.cadAnnotations.size())
    st.cadAnnotationAttrs.emplace_back();
  // Allocate eagerly rather than waiting for the sweep: the point records the id in the next line,
  // and a reference to id 0 would be a reference to nothing (REQ-076).
  const std::uint64_t labelId = AllocEntityId(st);
  st.cadAnnotationAttrs.back().id = labelId;
  p.labelMtextAnnId = labelId;
  RepositionSurveyLabelMtextForPoint(st, pointIndex);
  if (log)
    log->push_back("Survey label MTEXT created for point " + std::to_string(p.id) + ".");
  BumpCadGpuCache(st);
}

bool SaveSurveyPointsToJsonFile(const AppCommandState& st, const char* path, std::vector<std::string>& log) {
  std::ofstream f(path);
  if (!f) {
    log.push_back("Could not open file for saving survey points.");
    return false;
  }
  f << "{\"points\":[";
  for (size_t i = 0; i < st.surveyPoints.size(); ++i) {
    const SurveyPoint& p = st.surveyPoints[i];
    if (i != 0)
      f << ',';
    f << "{\"id\":" << p.id << ",\"easting\":" << p.easting << ",\"northing\":" << p.northing
      << ",\"elevation\":" << p.elevation << ",\"labelStyle\":" << static_cast<int>(p.labelStyle)
      << ",\"layer\":\"" << JsonEscape(p.layer) << "\",\"description\":\"" << JsonEscape(p.description) << "\"}";
  }
  f << "]}\n";
  log.push_back("Saved " + std::to_string(st.surveyPoints.size()) + " survey point(s) to " +
                std::string(path));
  return true;
}

bool LoadSurveyPointsFromJsonFile(AppCommandState& st, const char* path, std::vector<std::string>& log) {
  std::ifstream f(path);
  if (!f) {
    log.push_back("Could not open survey points file.");
    return false;
  }
  std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  st.surveyPoints.clear();
  st.surveyPointIdBuffers.clear();
  st.selectedSurveyPointIndices.clear();

  auto parseIntField = [](const std::string& o, const char* key, int* out) -> bool {
    const std::string pat = std::string("\"") + key + "\":";
    const size_t p0 = o.find(pat);
    if (p0 == std::string::npos)
      return false;
    const char* str = o.c_str() + p0 + pat.size();
    char* end = nullptr;
    const long v = std::strtol(str, &end, 10);
    if (end == str)
      return false;
    *out = static_cast<int>(v);
    return true;
  };
  auto parseFloatField = [](const std::string& o, const char* key, float* out) -> bool {
    const std::string pat = std::string("\"") + key + "\":";
    const size_t p0 = o.find(pat);
    if (p0 == std::string::npos)
      return false;
    const char* str = o.c_str() + p0 + pat.size();
    char* end = nullptr;
    *out = std::strtof(str, &end);
    return end != str;
  };

  size_t search = 0;
  while (true) {
    const size_t idPos = s.find("\"id\":", search);
    if (idPos == std::string::npos)
      break;
    const size_t objStart = s.rfind('{', idPos);
    const size_t objEnd = s.find('}', idPos);
    if (objStart == std::string::npos || objEnd == std::string::npos || objEnd <= objStart) {
      search = idPos + 5;
      continue;
    }
    const std::string obj = s.substr(objStart, objEnd - objStart + 1);
    SurveyPoint p{};
    if (!parseIntField(obj, "id", &p.id) || !parseFloatField(obj, "easting", &p.easting) ||
        !parseFloatField(obj, "northing", &p.northing) || !parseFloatField(obj, "elevation", &p.elevation)) {
      search = objEnd + 1;
      continue;
    }
    if (!ExtractJsonStringField(obj, "layer", &p.layer))
      p.layer = "0";
    if (!ExtractJsonStringField(obj, "description", &p.description))
      p.description.clear();
    int ls = static_cast<int>(SurveyPointLabelStyle::NumberDesc);
    if (parseIntField(obj, "labelStyle", &ls)) {
      if (ls < 0 || ls > static_cast<int>(SurveyPointLabelStyle::NumberNorthEastElev))
        ls = static_cast<int>(SurveyPointLabelStyle::NumberDesc);
      p.labelStyle = static_cast<SurveyPointLabelStyle>(ls);
    }
    st.surveyPoints.push_back(std::move(p));
    search = objEnd + 1;
  }

  // BUG-023: `surveyPointIdBuffers` is cleared above and MUST be refilled here.
  // It is a parallel array the Viewpoints grid indexes with the point index, and
  // that grid draws its Load button and its rows in the SAME frame — so a load
  // that left the buffers empty had the table read `surveyPointIdBuffers[i]` on
  // an empty vector for every row it had just gained. Refilled at the owner
  // rather than patched at the reader: anything else that loads points inherits
  // the same invariant for free.
  st.surveyPointIdBuffers.resize(st.surveyPoints.size());
  for (size_t i = 0; i < st.surveyPoints.size(); ++i)
    st.surveyPointIdBuffers[i] = std::to_string(st.surveyPoints[i].id);

  for (size_t i = 0; i < st.surveyPoints.size(); ++i)
    EnsureSurveyPointLabelMtext(st, i, &log);

  log.push_back("Loaded " + std::to_string(st.surveyPoints.size()) + " survey point(s) from " +
                std::string(path));
  return true;
}

void StartCreatePointsCommand(AppCommandState& st, std::vector<std::string>& log) {
  st.showCreatePointsWindow = true;
  st.selBoxWaitingSecond = false;
  ResetCreatePointsNextIdFromSettings(st);
  log.push_back("CREATEPOINTS — click in the drawing to place points. ESC closes. VIEWPOINTS for table.");
}

void StartViewPointsCommand(AppCommandState& st, std::vector<std::string>& log) {
  st.showViewPointsWindow = true;
  log.push_back("VIEWPOINTS — survey points table opened.");
}

void StartImportPointsCommand(AppCommandState& st, std::vector<std::string>& log) {
  st.showImportPointsWindow = true;
  st.surveyImportPreviewDirty = true;
  // REQ-083: the importer accepts .csv and .txt alike, so the hint names both.
  log.push_back("IMPORTPOINTS — choose CSV or TXT file, column order, and preview.");
}

void StartExportPointsCommand(AppCommandState& st, std::vector<std::string>& log) {
  st.showExportPointsWindow = true;
  log.push_back("EXPORTPOINTS — choose save path and column order.");
}
