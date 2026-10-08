#include "AcisSatParser.hpp"

#include "nurbs.hpp"
#include "ray3d.hpp"
#include "ucs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

/// See AcisSatParser.hpp for scope. This file also documents, in one place, the exact SAT field
/// layout this parser understands (a fixed subset of the real ACIS standard schema, chosen because
/// this parser has no real-world SAT corpus to test against and every layout below is exercised by a
/// hand-authored fixture in tests/AcisSatParserTests.cpp — ADR-051's explicitly sanctioned approach).
///
///   body            attrib lump wire transform
///   lump            attrib next shell owner
///   shell           attrib next subshell face wire owner
///   face            attrib next loop owner surface sense("forward"/"reversed") sides("single"/"double")
///   loop            attrib next coedge owner
///   coedge          attrib next previous partner edge sense("forward"/"reversed") owner
///   edge            attrib start-vertex end-vertex curve sense("forward"/"reversed"); curve may be $-1
///   vertex          attrib edge point
///   point           attrib x y z
///   straight-curve  attrib origin.xyz direction.xyz
///   ellipse-curve   attrib centre.xyz normal.xyz major-axis.xyz radius-ratio
///   intcurve-curve  attrib { int_int_cur ... sample.xyz* torus centre axis major minor refdir plane origin normal refdir ... }
///   plane-surface   attrib origin.xyz normal.xyz refdir.xyz
///   cone-surface    attrib origin.xyz axis.xyz refdir.xyz sin-angle cos-angle major-radius radius-ratio
///   sphere-surface  attrib centre.xyz axis.xyz refdir.xyz radius
///   torus-surface   attrib centre.xyz axis.xyz major-radius minor-radius refdir.xyz
///   spline-surface  attrib degU degV nu nv rational knotsU[nu+degU+1] knotsV[nv+degV+1]
///                          ctrlpt[nu*nv](x y z [w if rational])
///   blend-surface   attrib underlying-surface
///   sweep-surface   attrib underlying-surface
///
/// `spline-surface`, `blend-surface` and `sweep-surface` are this project's own invented schema for
/// GitHub issue #300 (ADR-051 fast-follow), not real ACIS record syntax — like every other record
/// above, ADR-051's rationale for choosing a hand-authored layout over the real one applies. A
/// `spline-surface` carries a `nurbs::Patch` directly (degree/knot/control-point/weight data,
/// row-major control points exactly as `nurbs::Patch::ctrl` documents) rather than ACIS's actual
/// fit-point/approximation encoding — this importer maps it straight onto `SurfaceKind::Nurbs`
/// (REQ-315/ADR-048), so it is naturally limited to ADR-048 (b)'s degree-3-or-less, untrimmed
/// rectangular patch. A `blend-surface`/`sweep-surface` record names the one surface it "reduces to"
/// when representable (a `$-1` pointer means it does not reduce to anything this importer can
/// represent, e.g. a genuinely variable-radius fillet) — real ACIS never says this so plainly, but the
/// alternative is re-deriving a blend/sweep surface's true math from its defining curves, which is out
/// of scope (see AcisSatParser.hpp).
///
/// A record's leading "$" pointer fields resolve to another record's 0-based position in the file
/// (the modern, non-indexed ACIS SAT convention); "$-1" is the null pointer.
///
/// **Real ACIS/ASM SAT** (what Civil 3D's `ACISOUT` and every real `.dwg`/`.dxf` `3DSOLID` write,
/// GitHub #473) uses a richer record layout than the simplified schema above — a leading
/// `$attrib -1 $pattern` triple on every record, edge parameter ranges, `I` bound markers, `@n`
/// strings, interleaved `color-adesk-attrib` records, a `body` `transform`, and a full cone/cylinder
/// wall listed as two single-edge rim loops rather than one two-edge loop. `NormalizeRealAcisSchema`
/// rewrites such a stream's records into the simplified layout above (and `Build` applies the
/// `transform`), so everything below this comment only ever sees the simplified form. The
/// hand-authored fixtures remain in the simplified schema and are detected as such (unchanged).
///
/// No exceptions (this project builds with them disabled): every step below returns `bool` and
/// writes a specific message to `error_` on the first failure, exactly like `brep::Make*`'s
/// `Problem* outWhy` pattern — a refusal the caller can show, not a crash.

namespace acissat {

namespace {

using ray3d::Vec3;

struct SatRecord {
  std::string type;
  std::vector<std::string> fields;
};

std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos)
    return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

std::vector<std::string> SplitWhitespace(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream iss(s);
  std::string tok;
  while (iss >> tok)
    out.push_back(tok);
  return out;
}

/// Splits the SAT text into records: skips the 3 mandated ACIS header lines, then splits the
/// remainder on '#' (every record, including the last, ends with one).
std::vector<SatRecord> Tokenize(const std::string& sat) {
  std::vector<std::string> lines;
  {
    std::istringstream iss(sat);
    std::string line;
    while (std::getline(iss, line))
      lines.push_back(line);
  }
  size_t headerLinesSeen = 0;
  size_t bodyStart = 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (Trim(lines[i]).empty())
      continue;
    ++headerLinesSeen;
    if (headerLinesSeen == 3) {
      bodyStart = i + 1;
      break;
    }
  }
  std::string body;
  for (size_t i = bodyStart; i < lines.size(); ++i) {
    body += lines[i];
    body += ' ';
  }
  std::vector<SatRecord> records;
  size_t pos = 0;
  while (pos < body.size()) {
    size_t hash = body.find('#', pos);
    std::string chunk = Trim(hash == std::string::npos ? body.substr(pos) : body.substr(pos, hash - pos));
    pos = (hash == std::string::npos) ? body.size() : hash + 1;
    if (chunk.empty())
      continue;
    if (chunk.rfind("End-of-ACIS", 0) == 0)
      continue;
    std::vector<std::string> toks = SplitWhitespace(chunk);
    if (toks.empty())
      continue;
    SatRecord rec;
    rec.type = toks.front();
    rec.fields.assign(toks.begin() + 1, toks.end());
    records.push_back(std::move(rec));
  }
  return records;
}

constexpr double kTol = 1e-6;
constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] bool DetectPlantAsmSatStream(const std::vector<SatRecord>& recs) {
  bool plantFaceLayout = false;
  bool plantSolidHistory = false;
  for (const SatRecord& r : recs) {
    if (r.type == "persubent-acadSolidHistory-attrib")
      plantSolidHistory = true;
    if (r.type == "face" && r.fields.size() >= 9 && !r.fields[0].empty() && r.fields[0][0] == '$' &&
        r.fields[0] != "$-1" && r.fields[1] == "-1" && r.fields[2] == "$-1")
      plantFaceLayout = true;
  }
  // Civil 3D ACISOUT shares the face token layout but not Plant's solid-history attribs (issue #473).
  return plantFaceLayout && plantSolidHistory;
}

/// The ACIS header's third line is `<mm-per-unit> <resabs> <resnor>`; a standalone `.sat` file
/// carries no other unit hint. Returns 0 when the line cannot be read.
double HeaderMmPerUnit(const std::string& sat) {
  std::istringstream iss(sat);
  std::string line;
  int seen = 0;
  while (std::getline(iss, line)) {
    if (Trim(line).empty())
      continue;
    if (++seen < 3)
      continue;
    const std::string t = Trim(line);
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    return (end != t.c_str() && std::isfinite(v) && v > 0.0) ? v : 0.0;
  }
  return 0.0;
}

/// Real ACIS/ASM writes each entity record with a leading `$attribute -1 $pattern` triple and, on
/// several records, extra pointer/parameter fields the hand-authored fixture schema this parser was
/// first built against (ADR-051, GitHub #299) omits. Rather than fork every field accessor, this
/// rewrites a real-format record's `fields` in place to that simplified layout — dropping the
/// attribute/pattern/id fields, curve/surface bound markers, and coedge back-pointers, while
/// preserving edge parameter ranges needed for partial rim arcs (Plant ASM, issue #786). Unknown
/// record types (`color-adesk-attrib`, and `transform`, which `Build` reads with its real layout)
/// are left untouched so `$n` record numbering stays intact (GitHub #473).
///
/// LibreDWG's ASM→SAT conversion (AcDs / ShapeManager, issue #786) can emit `shell`/`face`/`transform`
/// records while the `body`/`lump` pair stays inside the binary `asmheader` wrapper. Append a
/// fixture-schema body/lump that points at the first shell (and optional transform) so `Build` can run.
/// Plant ASM loop records carry a bare record-id `-1` field and put `next/coedge/owner` at [3..5].
/// Rewrite to the hand-authored fixture layout (`loop $-1 $next $coedge $owner`) \ref WalkLoop expects.
/// Plant `body` records place the lump pointer at field [3] (`body $-1 -1 $-1 $lump $-1 $transform`).
void AdaptPlantAsmBodyRecord(std::vector<SatRecord>& recs) {
  for (SatRecord& r : recs) {
    if (r.type != "body" || r.fields.size() < 4)
      continue;
    if (r.fields[1] != "-1" || r.fields[2] != "$-1")
      continue;
    if (r.fields[3].empty() || r.fields[3][0] != '$')
      continue;
    const std::string lump = r.fields[3];
    const std::string transform =
        (r.fields.size() > 5 && !r.fields[5].empty() && r.fields[5][0] == '$') ? r.fields[5] : std::string("$-1");
    r.fields = {"$-1", lump, "$-1", transform};
  }
}

void AdaptPlantAsmLumpShellRecords(std::vector<SatRecord>& recs) {
  for (SatRecord& r : recs) {
    if (r.type == "lump" && r.fields.size() >= 5 && r.fields[1] == "-1" && r.fields[4][0] == '$') {
      const std::string owner = (r.fields.size() > 5 && r.fields[5][0] == '$') ? r.fields[5] : std::string("$-1");
      r.fields = {"$-1", "$-1", r.fields[4], owner};
      continue;
    }
    if (r.type == "shell" && r.fields.size() >= 6 && r.fields[1] == "-1") {
      std::string facePtr = "$-1";
      std::string ownerPtr = "$-1";
      for (size_t i = 3; i < r.fields.size(); ++i) {
        if (r.fields[i].empty() || r.fields[i][0] != '$' || r.fields[i] == "$-1")
          continue;
        const int id = static_cast<int>(std::strtol(r.fields[i].c_str() + 1, nullptr, 10));
        if (id < 0 || static_cast<size_t>(id) >= recs.size())
          continue;
        if (recs[static_cast<size_t>(id)].type == "face" && facePtr == "$-1")
          facePtr = r.fields[i];
        else if (facePtr != "$-1")
          ownerPtr = r.fields[i];
      }
      r.fields = {"$-1", "$-1", "$-1", facePtr, "$-1", ownerPtr};
    }
  }
}

void AdaptPlantAsmEdgeRecords(std::vector<SatRecord>& recs) {
  auto pick = [](const SatRecord& r, std::initializer_list<size_t> idx) {
    std::vector<std::string> out;
    for (size_t i : idx)
      out.push_back(i < r.fields.size() ? r.fields[i] : std::string("$-1"));
    return out;
  };
  auto edgeHasParamRange = [](const std::vector<std::string>& f) {
    if (f.size() < 8)
      return false;
    auto isNum = [](const std::string& tok) {
      if (tok.empty() || tok[0] == '$')
        return false;
      char* end = nullptr;
      const double v = std::strtod(tok.c_str(), &end);
      return end != tok.c_str() && std::isfinite(v);
    };
    return f[3][0] == '$' && isNum(f[4]) && f[5][0] == '$' && isNum(f[6]);
  };
  for (SatRecord& r : recs) {
    if (r.type != "edge" || r.fields.size() < 8 || r.fields[1] != "-1")
      continue;
    // Real Plant ASM: curve at [8] ($-1 for null-curve seams); [7] is a coedge back-pointer.
    if (edgeHasParamRange(r.fields)) {
      std::vector<std::string> nf = pick(r, {0, 3, 5, 8});
      nf.push_back(r.fields[4]);
      nf.push_back(r.fields[6]);
      r.fields = std::move(nf);
    } else if (r.fields.size() <= 5) {
      r.fields = pick(r, {0, 1, 2, 3});
    } else {
      r.fields = pick(r, {0, 3, 5, 8});
    }
  }
}

/// Plant ASM skips \ref NormalizeRealAcisSchema (face records must stay on the Plant layout). Rewrite
/// vertex/point/curve/surface numeric fields to the fixture layout the importer already expects.
void AdaptPlantAsmVertexPointAndGeometryRecords(std::vector<SatRecord>& recs) {
  auto pick = [](const SatRecord& r, std::initializer_list<size_t> idx) {
    std::vector<std::string> out;
    for (size_t i : idx)
      out.push_back(i < r.fields.size() ? r.fields[i] : std::string("$-1"));
    return out;
  };
  for (SatRecord& r : recs) {
    if (r.type == "vertex") {
      size_t pt = 4;
      if (r.fields.size() > 4 && !r.fields[4].empty() && r.fields[4][0] != '$')
        pt = 5;
      r.fields = pick(r, {0, 3, pt});
    } else if (r.type == "point") {
      r.fields = pick(r, {0, 3, 4, 5});
    } else if (r.type == "ellipse-curve") {
      r.fields = pick(r, {0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
    } else if (r.type == "straight-curve") {
      r.fields = pick(r, {0, 3, 4, 5, 6, 7, 8});
    } else if (r.type == "plane-surface") {
      r.fields = pick(r, {0, 3, 4, 5, 6, 7, 8, 9, 10, 11});
    } else if (r.type == "cone-surface") {
      r.fields = pick(r, {0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 15, 16, 17, 12});
      // Plant often leaves sin/cos as 0/1 placeholders; the ref-direction length is the section radius.
      if (r.fields.size() > 12) {
        double sinA = 0.0;
        double cosA = 1.0;
        char* end = nullptr;
        sinA = std::strtod(r.fields[10].c_str(), &end);
        if (end == r.fields[10].c_str())
          sinA = 0.0;
        end = nullptr;
        cosA = std::strtod(r.fields[11].c_str(), &end);
        if (end == r.fields[11].c_str())
          cosA = 1.0;
        if (std::fabs(sinA) < 1e-12 && std::fabs(cosA - 1.0) < 1e-12) {
          double rx = 0.0;
          double ry = 0.0;
          double rz = 0.0;
          end = nullptr;
          rx = std::strtod(r.fields[7].c_str(), &end);
          if (end != r.fields[7].c_str()) {
            end = nullptr;
            ry = std::strtod(r.fields[8].c_str(), &end);
            end = nullptr;
            rz = std::strtod(r.fields[9].c_str(), &end);
            const double refLen = std::hypot(rx, std::hypot(ry, rz));
            if (refLen > kTol)
              r.fields[12] = std::to_string(refLen);
          }
        }
      }
    } else if (r.type == "torus-surface") {
      r.fields = pick(r, {0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13});
    } else if (r.type == "sphere-surface") {
      r.fields = pick(r, {0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
    }
  }
}

void AdaptPlantAsmCoedgeRecords(std::vector<SatRecord>& recs) {
  for (SatRecord& r : recs) {
    if (r.type != "coedge" || r.fields.size() < 8)
      continue;
    if (r.fields[1] != "-1")
      continue;
    if (r.fields.size() < 8 || r.fields[3].empty() || r.fields[3][0] != '$' || r.fields[6].empty() ||
        r.fields[6][0] != '$')
      continue;
    std::string sense = r.fields[7];
    if (sense == "I")
      sense = "reversed";
    else if (sense == "F")
      sense = "forward";
    const std::string owner =
        (r.fields.size() > 8 && !r.fields[8].empty() && r.fields[8][0] == '$') ? r.fields[8] : std::string("$-1");
    const std::string partner =
        (r.fields[5].empty() || r.fields[5][0] != '$') ? std::string("$-1") : r.fields[5];
    r.fields = {"$-1", r.fields[3], r.fields[4], partner, r.fields[6], sense, owner};
  }
}

void AdaptPlantAsmLoopRecords(std::vector<SatRecord>& recs) {
  for (SatRecord& r : recs) {
    if (r.type != "loop" || r.fields.size() < 6)
      continue;
    if (r.fields[1] != "-1" || r.fields[2] != "$-1")
      continue;
    if (r.fields[3].empty() || r.fields[3][0] != '$' || r.fields[4].empty() || r.fields[4][0] != '$')
      continue;
    const std::string owner = (r.fields.size() > 5 && !r.fields[5].empty()) ? r.fields[5] : std::string("$-1");
    r.fields = {"$-1", r.fields[3], r.fields[4], owner};
  }
}

void SynthesizeBodyLumpIfMissing(std::vector<SatRecord>& recs) {
  for (const SatRecord& r : recs)
    if (r.type == "body")
      return;
  int shellIdx = -1;
  for (size_t i = 0; i < recs.size(); ++i) {
    if (recs[i].type == "shell") {
      shellIdx = static_cast<int>(i);
      break;
    }
  }
  if (shellIdx < 0)
    return;
  int transformIdx = -1;
  for (size_t i = 0; i < recs.size(); ++i) {
    if (recs[i].type == "transform") {
      transformIdx = static_cast<int>(i);
      break;
    }
  }
  const int lumpIdx = static_cast<int>(recs.size());
  SatRecord lump;
  lump.type = "lump";
  // Real ACIS layout (pre-`NormalizeRealAcisSchema`); field 4 is the shell pointer.
  lump.fields = {"$-1", "-1", "$-1", "$-1", "$" + std::to_string(shellIdx), "$-1"};
  recs.push_back(std::move(lump));
  SatRecord body;
  body.type = "body";
  body.fields = {"$-1",
                 "-1",
                 "$-1",
                 "$" + std::to_string(lumpIdx),
                 "$-1",
                 transformIdx >= 0 ? "$" + std::to_string(transformIdx) : std::string("$-1")};
  recs.push_back(std::move(body));
}

/// Detection: a real-format `body` record's second field is the bare id `-1`; the fixture schema's
/// is the `$lump` pointer. Returns true when the stream was real-format (and was rewritten).
bool NormalizeRealAcisSchema(std::vector<SatRecord>& recs) {
  const SatRecord* body = nullptr;
  for (const SatRecord& r : recs)
    if (r.type == "body") {
      body = &r;
      break;
    }
  if (body == nullptr || body->fields.size() < 2 || body->fields[1].empty() || body->fields[1][0] == '$')
    return false;  // fixture schema, or no body — leave it to the existing path / error
  // LibreDWG Plant ASM (issue #786): same face token layout as some Civil exports, but Plant also carries
  // `persubent-acadSolidHistory-attrib`. Skip real-ACIS rewrite only for that stream — Civil flange (#473)
  // shares the face layout and still needs `NormalizeRealAcisSchema`.
  if (DetectPlantAsmSatStream(recs))
    return false;

  auto edgeHasParamRange = [](const std::vector<std::string>& f) {
    if (f.size() < 8)
      return false;
    auto isNum = [](const std::string& tok) {
      if (tok.empty() || tok[0] == '$')
        return false;
      char* end = nullptr;
      const double v = std::strtod(tok.c_str(), &end);
      return end != tok.c_str() && std::isfinite(v);
    };
    return f[3][0] == '$' && isNum(f[4]) && f[5][0] == '$' && isNum(f[6]) && f[7][0] == '$';
  };

  auto pick = [](const SatRecord& r, std::initializer_list<size_t> idx) {
    std::vector<std::string> out;
    for (size_t i : idx)
      out.push_back(i < r.fields.size() ? r.fields[i] : std::string("$-1"));
    return out;
  };
  for (SatRecord& r : recs) {
    // Field indices below are into the real record, AFTER the record type name:
    //   [0]=$attrib [1]=id(-1) [2]=$pattern, then the record-specific fields.
    if (r.type == "body")            r.fields = pick(r, {0, 3, 4, 5});                 // attrib lump wire transform
    else if (r.type == "lump")       r.fields = pick(r, {0, 3, 4});                    // attrib next shell
    else if (r.type == "shell")      r.fields = pick(r, {0, 3, 4, 5});                 // attrib next subshell face
    else if (r.type == "face")       r.fields = pick(r, {0, 3, 4, 5, 7, 8, 9});        // attrib next loop owner surface sense sides
    else if (r.type == "loop")       r.fields = pick(r, {0, 3, 4});                    // attrib next coedge
    else if (r.type == "coedge")     r.fields = pick(r, {0, 3, 4, 5, 6, 7});           // attrib next prev partner edge sense
    else if (r.type == "edge") {
      // Real ASM: start-vtx, t0, end-vtx, t1, coedge, curve — curve is at [8] ($-1 for null-curve seams).
      if (edgeHasParamRange(r.fields)) {
        std::vector<std::string> nf = pick(r, {0, 3, 5, 8});
        nf.push_back(r.fields[4]);
        nf.push_back(r.fields[6]);
        r.fields = std::move(nf);
      } else if (r.fields.size() <= 5) {
        r.fields = pick(r, {0, 1, 2, 3});  // hand-authored fixture: attrib start end curve
      } else {
        r.fields = pick(r, {0, 3, 5, 8});
      }
    }
    else if (r.type == "vertex") {
      // Real Civil 3D SAT: point at [4]. Plant ASM convert inserts a flag int at [4], point at [5].
      size_t pt = 4;
      if (r.fields.size() > 4 && !r.fields[4].empty() && r.fields[4][0] != '$')
        pt = 5;
      r.fields = pick(r, {0, 3, pt}); // attrib edge point
    }
    else if (r.type == "point")      r.fields = pick(r, {0, 3, 4, 5});                 // attrib x y z
    else if (r.type == "ellipse-curve")
      r.fields = pick(r, {0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});                        // attrib centre[3] normal[3] major[3] ratio
    else if (r.type == "straight-curve")
      r.fields = pick(r, {0, 3, 4, 5, 6, 7, 8});                                       // attrib root[3] dir[3] (unread; type name is enough)
    else if (r.type == "plane-surface")
      r.fields = pick(r, {0, 3, 4, 5, 6, 7, 8, 9, 10, 11});                            // attrib origin[3] normal[3] refdir[3]
    else if (r.type == "cone-surface")
      r.fields = pick(r, {0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 15, 16, 17, 12});            // attrib origin[3] axis[3] refdir[3] sin cos radius ratio
    else if (r.type == "torus-surface")
      r.fields = pick(r, {0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13});                    // attrib centre[3] axis[3] refdir[3] major minor
    else if (r.type == "sphere-surface")
      r.fields = pick(r, {0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});                       // attrib centre[3] radius axis[3] refdir[3]
    // else: attribute records, transform, or an unknown type — untouched.
  }
  return true;
}

bool IsFinite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

bool TryFieldDouble(const std::string& tok, double* out) {
  if (tok.empty() || tok[0] == '$')
    return false;
  char* end = nullptr;
  const double v = std::strtod(tok.c_str(), &end);
  if (end == tok.c_str() || !std::isfinite(v))
    return false;
  *out = v;
  return true;
}

bool TryFieldVec3(const std::vector<std::string>& fields, size_t field0, Vec3* out) {
  if (field0 + 2 >= fields.size())
    return false;
  return TryFieldDouble(fields[field0], &out->x) && TryFieldDouble(fields[field0 + 1], &out->y) &&
         TryFieldDouble(fields[field0 + 2], &out->z);
}

size_t FindFieldWord(const std::vector<std::string>& fields, const char* word) {
  for (size_t i = 0; i < fields.size(); ++i) {
    if (fields[i] == word)
      return i;
  }
  return fields.size();
}

size_t FindFieldWordAfter(const std::vector<std::string>& fields, const char* word, size_t afterIdx) {
  for (size_t i = afterIdx + 1; i < fields.size(); ++i) {
    if (fields[i] == word)
      return i;
  }
  return fields.size();
}

double SignedAreaPoly(const std::vector<curveisect::Vec2>& poly) {
  double a = 0.0;
  for (size_t i = 0, n = poly.size(); i < n; ++i)
    a += poly[i].x * poly[(i + 1) % n].y - poly[(i + 1) % n].x * poly[i].y;
  return 0.5 * a;
}

struct BuiltEdge {
  int edgeIndex = -1;
};

/// Everything gathered while walking one ACIS face's loop, before it is turned into a brep::Face.
struct LoopWalk {
  std::vector<brep::EdgeUse> uses;
};

void RotateFrameInPlace(ucs::Ucs& f, const Vec3& axisPoint, const Vec3& axisUnit, double angleRad) {
  f.origin = ray3d::RotatePointAboutAxis(f.origin, axisPoint, axisUnit, angleRad);
  f.xAxis = ray3d::RotateVectorAboutAxis(f.xAxis, axisUnit, angleRad);
  f.yAxis = ray3d::RotateVectorAboutAxis(f.yAxis, axisUnit, angleRad);
  f.zAxis = ray3d::RotateVectorAboutAxis(f.zAxis, axisUnit, angleRad);
}

void RotateSurfaceInPlace(brep::Surface& sf, const Vec3& axisPoint, const Vec3& axisUnit, double angleRad) {
  RotateFrameInPlace(sf.frame, axisPoint, axisUnit, angleRad);
  if (sf.kind == brep::SurfaceKind::Nurbs)
    sf.patch = nurbs::Rotate(sf.patch, axisPoint, axisUnit, angleRad);
}

// ---------------------------------------------------------------------------------------------
// The importer. One instance per body; every method returns false and sets `error_` on the first
// unsupported or malformed thing it finds (see file header — no exceptions in this codebase).
// ---------------------------------------------------------------------------------------------

class Importer {
 public:
  Importer(std::vector<SatRecord> recs, std::string label, bool plantAsm)
      : recs_(std::move(recs)), label_(std::move(label)), plantAsm_(plantAsm) {}

  bool Run(brep::Solid* out, bool validateSolid) {
    if (!Build(out, validateSolid)) {
      error_ = (label_.empty() ? std::string() : (label_ + ": ")) + error_;
      return false;
    }
    return true;
  }

  bool Run(brep::Solid* out) { return Run(out, true); }

  const std::string& Error() const { return error_; }

 private:
  const SatRecord* At(int id) const {
    if (id < 0 || static_cast<size_t>(id) >= recs_.size())
      return nullptr;
    return &recs_[static_cast<size_t>(id)];
  }

  int FindFirst(const std::string& type) const {
    for (size_t i = 0; i < recs_.size(); ++i)
      if (recs_[i].type == type)
        return static_cast<int>(i);
    return -1;
  }

  bool Fail(const std::string& msg) {
    error_ = msg;
    return false;
  }

  bool Req(int id, const char* what, const SatRecord** out) {
    const SatRecord* r = At(id);
    if (r == nullptr)
      return Fail(std::string("missing ") + what + " record");
    *out = r;
    return true;
  }

  bool Ptr(const SatRecord& r, size_t field, const char* what, int* out) {
    if (field >= r.fields.size() || r.fields[field].empty() || r.fields[field][0] != '$')
      return Fail(std::string("malformed ") + what + " pointer in a '" + r.type + "' record");
    const char* digits = r.fields[field].c_str() + 1;
    char* end = nullptr;
    const long v = std::strtol(digits, &end, 10);
    if (end == digits || *end != '\0')
      return Fail(std::string("malformed ") + what + " pointer in a '" + r.type + "' record");
    *out = static_cast<int>(v);
    return true;
  }

  [[nodiscard]] bool TryPtr(const SatRecord& r, size_t field, int* out) const {
    if (field >= r.fields.size() || r.fields[field].empty() || r.fields[field][0] != '$')
      return false;
    const char* digits = r.fields[field].c_str() + 1;
    char* end = nullptr;
    const long v = std::strtol(digits, &end, 10);
    if (end == digits || *end != '\0')
      return false;
    *out = static_cast<int>(v);
    return true;
  }

  [[nodiscard]] bool IsPlantAsmFaceRecord(const SatRecord& face) const {
    return plantAsm_ && face.type == "face" && face.fields.size() >= 9 && !face.fields[0].empty() &&
           face.fields[0][0] == '$' && face.fields[0] != "$-1" && face.fields[1] == "-1" &&
           face.fields[2] == "$-1";
  }

  [[nodiscard]] bool IsSurfaceRecord(int recId) const {
    const SatRecord* r = At(recId);
    if (r == nullptr)
      return false;
    return r->type == "plane-surface" || r->type == "cone-surface" || r->type == "sphere-surface" ||
           r->type == "torus-surface" || r->type == "spline-surface" || r->type == "blend-surface" ||
           r->type == "sweep-surface";
  }

  /// Plant face layout: `$attrib -1 $-1 $next $loop $shell $subshell $surface sense sides`. The
  /// surface pointer is explicit — record order is not (the next face often precedes this face's
  /// surface record), so never infer it from position.
  [[nodiscard]] int ResolvePlantFaceSurfaceId(const SatRecord& face) const {
    int surfaceId = -1;
    if (!TryPtr(face, 7, &surfaceId) || !IsSurfaceRecord(surfaceId))
      return -1;
    return surfaceId;
  }

  [[nodiscard]] bool LoopOwnerMatchesFace(int loopRecIdx, int faceRecIdx) const {
    const SatRecord* loop = At(loopRecIdx);
    if (loop == nullptr || loop->type != "loop" || loop->fields.size() < 4)
      return false;
    int ownerId = 0;
    if (!TryPtr(*loop, 3, &ownerId))
      return false;
    return ownerId == faceRecIdx;
  }

  static void DedupeIdenticalLoopWalks(std::vector<LoopWalk>* loops) {
    if (loops == nullptr || loops->size() < 2)
      return;
    auto signature = [](const LoopWalk& lw) {
      std::vector<int> sig;
      sig.reserve(lw.uses.size() * 2);
      for (const brep::EdgeUse& u : lw.uses) {
        sig.push_back(u.edge);
        sig.push_back(u.reversed ? 1 : 0);
      }
      return sig;
    };
    std::vector<LoopWalk> unique;
    unique.reserve(loops->size());
    for (LoopWalk& lw : *loops) {
      const std::vector<int> sig = signature(lw);
      bool seen = false;
      for (const LoopWalk& prior : unique) {
        if (signature(prior) == sig) {
          seen = true;
          break;
        }
      }
      if (!seen)
        unique.push_back(std::move(lw));
    }
    *loops = std::move(unique);
  }

  bool AppendLoopChain(int startLoopId, brep::Solid* out, std::unordered_set<int>* seenLoopRec,
                       std::vector<LoopWalk>* loops) {
    int lId = startLoopId;
    int guardLoops = 0;
    while (lId >= 0) {
      if (seenLoopRec->count(lId) != 0)
        break;
      if (++guardLoops > 64)
        return Fail("face has an implausible number of loops (possible ACIS record corruption)");
      seenLoopRec->insert(lId);
      LoopWalk lw;
      if (!WalkLoop(lId, out, &lw))
        return false;
      if (!lw.uses.empty())
        loops->push_back(std::move(lw));
      const SatRecord* lr = nullptr;
      if (!Req(lId, "loop", &lr))
        return false;
      int lNext = -1;
      if (!TryPtr(*lr, 1, &lNext))
        break;
      lId = lNext;
    }
    return true;
  }

  bool CollectLoopsForFace(int faceRecIdx, const SatRecord& face, brep::Solid* out, std::vector<LoopWalk>* loops) {
    loops->clear();
    std::unordered_set<int> seenLoopRec;
    if (IsPlantAsmFaceRecord(face)) {
      for (size_t i = 0; i < recs_.size(); ++i) {
        if (recs_[i].type != "loop")
          continue;
        if (!LoopOwnerMatchesFace(static_cast<int>(i), faceRecIdx))
          continue;
        if (!AppendLoopChain(static_cast<int>(i), out, &seenLoopRec, loops))
          return false;
      }
      DedupeIdenticalLoopWalks(loops);
      return true;
    }
    int loopId = 0;
    if (!Ptr(face, 2, "face.loop", &loopId))
      return false;
    return AppendLoopChain(loopId, out, &seenLoopRec, loops);
  }

  bool Num(const SatRecord& r, size_t field, const char* what, double* out) {
    if (field >= r.fields.size())
      return Fail(std::string("missing ") + what + " in a '" + r.type + "' record");
    char* end = nullptr;
    const double v = std::strtod(r.fields[field].c_str(), &end);
    if (end == r.fields[field].c_str())
      return Fail(std::string("malformed ") + what + " in a '" + r.type + "' record");
    *out = v;
    return true;
  }

  bool Vec(const SatRecord& r, size_t field0, const char* what, Vec3* out) {
    return Num(r, field0, what, &out->x) && Num(r, field0 + 1, what, &out->y) &&
           Num(r, field0 + 2, what, &out->z);
  }

  /// A non-negative integer field (a spline-surface's degree/count fields), stored as an ordinary SAT
  /// number token but required to be an exact whole number.
  bool Int(const SatRecord& r, size_t field, const char* what, int* out) {
    double v = 0.0;
    if (!Num(r, field, what, &v))
      return false;
    const double rounded = std::round(v);
    if (std::fabs(v - rounded) > 1e-9 || rounded < 0.0)
      return Fail(std::string("malformed ") + what + " in a '" + r.type + "' record");
    *out = static_cast<int>(rounded);
    return true;
  }

  bool Word(const SatRecord& r, size_t field, const char* what, std::string* out) {
    if (field >= r.fields.size())
      return Fail(std::string("missing ") + what + " in a '" + r.type + "' record");
    *out = r.fields[field];
    return true;
  }

  bool VertexOf(int vertexRecId, brep::Solid* out, int* outIdx) {
    auto it = vertexIndex_.find(vertexRecId);
    if (it != vertexIndex_.end()) {
      *outIdx = it->second;
      return true;
    }
    const SatRecord* v = nullptr;
    if (!Req(vertexRecId, "vertex", &v))
      return false;
    int pointId = 0;
    if (!Ptr(*v, 2, "vertex.point", &pointId))
      return false;
    const SatRecord* p = nullptr;
    if (!Req(pointId, "point", &p))
      return false;
    Vec3 pos{};
    if (!Vec(*p, 1, "point.xyz", &pos))
      return false;
    if (!IsFinite(pos))
      return Fail("vertex has a non-finite coordinate");
    const int idx = static_cast<int>(out->vertices.size());
    out->vertices.push_back(brep::Vertex{pos});
    vertexIndex_[vertexRecId] = idx;
    *outIdx = idx;
    return true;
  }

  bool BuildCircularArc(const SatRecord& c, const Vec3& v0Pos, const Vec3& v1Pos, bool full,
                         brep::Edge* out, bool haveEdgeParams = false, double edgeT0 = 0.0,
                         double edgeT1 = 0.0) {
    Vec3 centre{}, normalRaw{}, majorAxis{};
    double ratio = 0.0;
    if (!Vec(c, 1, "ellipse-curve.centre", &centre) || !Vec(c, 4, "ellipse-curve.normal", &normalRaw) ||
        !Vec(c, 7, "ellipse-curve.major-axis", &majorAxis) ||
        !Num(c, 10, "ellipse-curve.radius-ratio", &ratio))
      return false;
    const Vec3 normal = ray3d::Normalize(normalRaw);
    const double semiMajor = ray3d::Length(majorAxis);
    if (!(semiMajor > kTol) || !(ratio > kTol))
      return Fail("elliptical edge has a non-positive radius");
    if (ray3d::Length(normal) < 0.5)
      return Fail("elliptical edge has a degenerate plane normal");
    const bool isCircle = std::fabs(ratio - 1.0) <= 1e-4;
    ucs::Ucs f2;
    f2.origin = centre;
    f2.zAxis = normal;
    if (isCircle) {
      // Re-align frame X toward v0 so `sweep` measures the arc from the start vertex. A circle is
      // unchanged by that rotation; an ellipse is not, so a true ellipse keeps the SAT major axis.
      ucs::Ucs frame;
      if (!ucs::FromNormal(centre, normal, &frame))
        return Fail("circular edge has a degenerate plane normal");
      const Vec3 r0 = ray3d::Sub(v0Pos, centre);
      const double a0 = std::atan2(ray3d::Dot(r0, frame.yAxis), ray3d::Dot(r0, frame.xAxis));
      f2.xAxis = ray3d::Normalize(
          ray3d::Add(ray3d::Scale(frame.xAxis, std::cos(a0)), ray3d::Scale(frame.yAxis, std::sin(a0))));
      out->kind = brep::CurveKind::Arc;
      out->radius = semiMajor;
    } else {
      const Vec3 majorDir = ray3d::Normalize(majorAxis);
      const Vec3 xRaw = ray3d::Sub(majorDir, ray3d::Scale(normal, ray3d::Dot(majorDir, normal)));
      if (ray3d::Length(xRaw) < 0.5)
        return Fail("elliptical edge major axis is parallel to the plane normal");
      f2.xAxis = ray3d::Normalize(xRaw);
      out->kind = brep::CurveKind::Ellipse;
      out->radius = semiMajor;
      out->radius2 = semiMajor * ratio;
    }
    f2.yAxis = ray3d::Normalize(ray3d::Cross(f2.zAxis, f2.xAxis));
    out->frame = f2;
    if (haveEdgeParams) {
      out->sweep = edgeT1 - edgeT0;
      if (out->sweep <= 1e-12)
        out->sweep = 2.0 * kPi;
    } else if (full) {
      out->sweep = 2.0 * kPi;
    } else {
      const Vec3 r1 = ray3d::Sub(v1Pos, centre);
      double sweep = std::atan2(ray3d::Dot(r1, f2.yAxis), ray3d::Dot(r1, f2.xAxis));
      while (sweep <= 0.0)
        sweep += 2.0 * kPi;
      out->sweep = sweep;
    }
    return true;
  }

  /// Plant ASM `intcurve-curve` records embed the two analytic surfaces whose intersection is the
  /// edge (torus + plane fillets on piping). NURBS knot/control data is ignored; marching uses
  /// \ref brep::CurveKind::Intersection like the kernel's B-rep tests.
  bool SurfaceFromIntcurveEmbeddedTorus(const SatRecord& c, size_t torusWord, brep::Surface* out) {
    if (torusWord + 11 >= c.fields.size())
      return Fail("intcurve-curve embedded torus parameters are truncated");
    Vec3 centre{}, axisRaw{}, refRaw{};
    double majorR = 0.0;
    double minorR = 0.0;
    if (!TryFieldVec3(c.fields, torusWord + 1, &centre) || !TryFieldVec3(c.fields, torusWord + 4, &axisRaw) ||
        !TryFieldDouble(c.fields[torusWord + 7], &majorR) || !TryFieldDouble(c.fields[torusWord + 8], &minorR) ||
        !TryFieldVec3(c.fields, torusWord + 9, &refRaw))
      return Fail("intcurve-curve embedded torus has malformed numeric fields");
    if (!IsFinite(centre) || !IsFinite(axisRaw) || !IsFinite(refRaw))
      return Fail("intcurve-curve embedded torus has a non-finite placement vector");
    if (!(majorR > kTol) || !(minorR > kTol))
      return Fail("intcurve-curve embedded torus has a non-positive major or minor radius");
    if (std::fabs(majorR - minorR) < kTol)
      return Fail("intcurve-curve embedded torus is degenerate (equal major and minor radii)");
    ucs::Ucs frame;
    if (!TorusFrameFromAcis(centre, axisRaw, refRaw, &frame))
      return Fail("intcurve-curve embedded torus has a degenerate axis or ref direction");
    out->kind = brep::SurfaceKind::Torus;
    out->frame = frame;
    out->radius = majorR;
    out->radius2 = minorR;
    out->inward = false;
    return true;
  }

  bool SurfaceFromIntcurveEmbeddedCone(const SatRecord& c, size_t coneWord, const Vec3& v0Pos, const Vec3& v1Pos,
                                       brep::Surface* out) {
    if (coneWord + 10 >= c.fields.size())
      return Fail("intcurve-curve embedded cone parameters are truncated");
    Vec3 axisOrigin{}, axisRaw{};
    if (!TryFieldVec3(c.fields, coneWord + 1, &axisOrigin) || !TryFieldVec3(c.fields, coneWord + 4, &axisRaw))
      return Fail("intcurve-curve embedded cone has malformed origin or axis");
    axisRaw = ray3d::Normalize(axisRaw);
    if (!IsFinite(axisOrigin) || !IsFinite(axisRaw) || ray3d::Length(axisRaw) < 0.5)
      return Fail("intcurve-curve embedded cone has a degenerate origin or axis");
    // Plant ASM matches raw `cone-surface`: refdir occupies [7..9]; sin/cos/major follow the `F F` flags.
    double sinA = 0.0;
    double cosA = 1.0;
    double majorRadius = 0.0;
    bool haveMajor = false;
    for (size_t i = coneWord + 10; i + 2 < c.fields.size(); ++i) {
      if (c.fields[i] == "F" || c.fields[i] == "forward_v" || c.fields[i] == "nullbs")
        continue;
      double a = 0.0;
      double b = 0.0;
      double m = 0.0;
      if (TryFieldDouble(c.fields[i], &a) && TryFieldDouble(c.fields[i + 1], &b) &&
          TryFieldDouble(c.fields[i + 2], &m) && m > kTol) {
        sinA = a;
        cosA = b;
        majorRadius = m;
        haveMajor = true;
        break;
      }
    }
    if (!haveMajor) {
      if (!TryFieldDouble(c.fields[coneWord + 7], &majorRadius) || !(majorRadius > kTol))
        return Fail("intcurve-curve embedded cone has a non-positive major radius");
      sinA = 0.0;
      cosA = 1.0;
    }
    double sectionRatio = 1.0;
    for (size_t i = coneWord + 10; i + 1 < c.fields.size(); ++i) {
      if (c.fields[i + 1] != "F")
        continue;
      double ratio = 0.0;
      if (TryFieldDouble(c.fields[i], &ratio) && ratio > kTol && ratio < 1.0 - 1e-6) {
        sectionRatio = ratio;
        break;
      }
    }
    if (std::fabs(sinA) < 1e-9 && sectionRatio < 1.0 - 1e-6) {
      // Circular in name only: the cross-section is an ellipse, major axis along the ref direction,
      // minor = major * ratio. A round cylinder misses the fillet by the difference of those radii.
      Vec3 ref{};
      if (!TryFieldVec3(c.fields, coneWord + 7, &ref))
        return Fail("intcurve-curve embedded cone has a malformed ref direction");
      const Vec3 z = axisRaw;
      Vec3 x = ray3d::Sub(ref, ray3d::Scale(z, ray3d::Dot(ref, z)));
      if (!(ray3d::Length(x) > kTol))
        return Fail("intcurve-curve embedded cone major axis is parallel to its axis");
      x = ray3d::Normalize(x);
      const Vec3 y = ray3d::Normalize(ray3d::Cross(z, x));
      const double major = majorRadius > kTol ? majorRadius : ray3d::Length(ref);
      out->kind = brep::SurfaceKind::Cylinder;
      out->frame.origin = axisOrigin;
      out->frame.zAxis = z;
      out->frame.xAxis = x;
      out->frame.yAxis = y;
      out->radius = major;
      out->radius2 = major * sectionRatio;
      out->height = 0.0;
      out->inward = false;
      return true;
    }
    if (std::fabs(cosA) < 1e-12)
      return Fail("intcurve-curve embedded cone has a degenerate half-angle");
    double tMin = 0.0;
    double tMax = 0.0;
    bool first = true;
    for (const Vec3& v : {v0Pos, v1Pos}) {
      const double t = ray3d::Dot(ray3d::Sub(v, axisOrigin), axisRaw);
      if (first || t < tMin)
        tMin = t;
      if (first || t > tMax)
        tMax = t;
      first = false;
    }
    const double pad = std::max(0.05 * (tMax - tMin), 0.01);
    tMin -= pad;
    tMax += pad;
    const double height = tMax - tMin;
    if (!(height > kTol))
      return Fail("intcurve-curve embedded cone has zero axial extent along the edge");
    const Vec3 base = ray3d::Add(axisOrigin, ray3d::Scale(axisRaw, tMin));
    ucs::Ucs frame;
    if (!ucs::FromNormal(base, axisRaw, &frame))
      return Fail("intcurve-curve embedded cone has a degenerate axis");
    const bool isCylinder = std::fabs(sinA) < 1e-9 && cosA > 0.0;
    out->kind = isCylinder ? brep::SurfaceKind::Cylinder : brep::SurfaceKind::Cone;
    out->frame = frame;
    out->height = height;
    out->inward = false;
    if (isCylinder) {
      out->radius = majorRadius;
    } else {
      const double tanA = sinA / cosA;
      const double rBase = majorRadius + tMin * tanA;
      const double rTop = majorRadius + tMax * tanA;
      if (!(rBase > kTol) || !(rTop >= 0.0))
        return Fail("intcurve-curve embedded cone produces a non-positive radius over the edge extent");
      out->radius = rBase;
      out->radius2 = rTop;
    }
    return true;
  }

  bool SurfaceFromIntcurveEmbeddedPlane(const SatRecord& c, size_t planeWord, brep::Surface* out) {
    if (planeWord + 9 >= c.fields.size())
      return Fail("intcurve-curve embedded plane parameters are truncated");
    Vec3 origin{}, normalRaw{};
    if (!TryFieldVec3(c.fields, planeWord + 1, &origin) || !TryFieldVec3(c.fields, planeWord + 4, &normalRaw))
      return Fail("intcurve-curve embedded plane has malformed numeric fields");
    const Vec3 normalN = ray3d::Normalize(normalRaw);
    if (!IsFinite(origin) || !IsFinite(normalN) || ray3d::Length(normalN) < 0.5)
      return Fail("intcurve-curve embedded plane has a degenerate origin or normal");
    ucs::Ucs frame;
    if (!ucs::FromNormal(origin, normalN, &frame))
      return Fail("intcurve-curve embedded plane has a degenerate normal");
    out->kind = brep::SurfaceKind::Plane;
    out->frame = frame;
    out->inward = false;
    return true;
  }

  bool SurfaceFromIntcurveEmbeddedSphere(const SatRecord& c, size_t sphereWord, brep::Surface* out) {
    if (sphereWord + 10 >= c.fields.size())
      return Fail("intcurve-curve embedded sphere parameters are truncated");
    Vec3 centre{}, axisRaw{}, refRaw{};
    double radius = 0.0;
    if (!TryFieldVec3(c.fields, sphereWord + 1, &centre) ||
        !TryFieldDouble(c.fields[sphereWord + 4], &radius) ||
        !TryFieldVec3(c.fields, sphereWord + 5, &axisRaw) ||
        !TryFieldVec3(c.fields, sphereWord + 8, &refRaw))
      return Fail("intcurve-curve embedded sphere has malformed numeric fields");
    if (!IsFinite(centre) || !IsFinite(axisRaw) || !IsFinite(refRaw) || !(radius > kTol))
      return Fail("intcurve-curve embedded sphere has a degenerate centre, axis, or radius");
    ucs::Ucs frame;
    if (!TorusFrameFromAcis(centre, axisRaw, refRaw, &frame))
      return Fail("intcurve-curve embedded sphere has a degenerate axis or ref direction");
    out->kind = brep::SurfaceKind::Sphere;
    out->frame = frame;
    out->radius = radius;
    out->inward = false;
    return true;
  }

  bool IntcurveMarchWitness(const SatRecord& c, size_t beforeSurfaces, const Vec3& v0Pos, const Vec3& v1Pos,
                            const brep::Surface& surfA, const brep::Surface& surfB, Vec3* witness) {
    const auto onBoth = [&](const Vec3& p) {
      const double tol = 1e-3 * (1.0 + ray3d::Length(p));
      return ray3d::Length(ray3d::Sub(p, brep::ClosestPointOnSurface(surfA, p))) <= tol &&
             ray3d::Length(ray3d::Sub(p, brep::ClosestPointOnSurface(surfB, p))) <= tol;
    };
    // A closed fillet's only "3" marker is the vertex itself. The control points sit just before
    // the surface block; one of them, settled onto both surfaces, is the direction around the loop.
    const double closeEps = 1e-8 * (1.0 + ray3d::Length(v0Pos));
    if (ray3d::Length(ray3d::Sub(v0Pos, v1Pos)) <= closeEps) {
      auto settle = [&](Vec3 p) {
        for (int n = 0; n < 24; ++n) {
          const Vec3 onA = brep::ClosestPointOnSurface(surfA, p);
          const Vec3 onB = brep::ClosestPointOnSurface(surfB, onA);
          if (ray3d::Length(ray3d::Sub(onB, p)) <= 1e-13 * (1.0 + ray3d::Length(onB)))
            return onB;
          p = onB;
        }
        return p;
      };
      Vec3 best{};
      double bestDist = 0.0;
      bool have = false;
      for (size_t i = 0; i + 2 < beforeSurfaces; ++i) {
        Vec3 raw{};
        if (!TryFieldVec3(c.fields, i, &raw) || !IsFinite(raw))
          continue;
        if (ray3d::Length(ray3d::Sub(raw, v0Pos)) > 20.0 || !onBoth(raw))
          continue;
        const Vec3 p = settle(raw);
        if (!onBoth(p))
          continue;
        const double dist = ray3d::Length(ray3d::Sub(p, v0Pos));
        if (dist < 1e-4)
          continue;
        if (dist > bestDist) {
          bestDist = dist;
          best = p;
          have = true;
        }
      }
      if (!have)
        return Fail("intcurve-curve has no sample point on both surfaces");
      *witness = best;
      return true;
    }
    // Fit points are marked by a leading "3" (dimension). Knot values and the spline degree are
    // also numeric, so a stride from the start of the record locks onto those and picks a witness
    // that is not on the curve — a closed fillet then looks like a point.
    std::vector<Vec3> samples;
    for (size_t i = 0; i + 3 < beforeSurfaces; ++i) {
      if (c.fields[i] != "3")
        continue;
      Vec3 p{};
      if (!TryFieldVec3(c.fields, i + 1, &p) || !IsFinite(p))
        continue;
      if (ray3d::Length(ray3d::Sub(p, v0Pos)) > 20.0 || !onBoth(p))
        continue;
      if (ray3d::Length(ray3d::Sub(p, v0Pos)) < 1e-4 && ray3d::Length(ray3d::Sub(p, v1Pos)) < 1e-4)
        continue;
      samples.push_back(p);
    }
    if (samples.empty()) {
      // No dimension-3 fit point sat on both surfaces. Fall back to any finite triple before the
      // surface block (cone-cone fillets are packed this way) and keep the one farthest from the
      // chord, which is the old witness rule.
      for (size_t i = 5; i + 2 < beforeSurfaces; ++i) {
        Vec3 p{};
        if (!TryFieldVec3(c.fields, i, &p) || !IsFinite(p))
          continue;
        if (ray3d::Length(ray3d::Sub(p, v0Pos)) > 50.0)
          continue;
        samples.push_back(p);
        i += 2;
      }
    }
    if (samples.empty())
      return Fail("intcurve-curve has no sample point on both surfaces");
    const Vec3 mid = ray3d::Scale(ray3d::Add(v0Pos, v1Pos), 0.5);
    size_t best = 0;
    double bestScore = -1.0;
    for (size_t k = 0; k < samples.size(); ++k) {
      const double score = ray3d::Length(ray3d::Sub(samples[k], mid));
      if (score > bestScore) {
        bestScore = score;
        best = k;
      }
    }
    *witness = samples[best];
    return true;
  }

  bool BuildIntcurveIntersectionEdge(const SatRecord& c, const Vec3& v0Pos, const Vec3& v1Pos, brep::Edge* out) {
    const size_t torusWord = FindFieldWord(c.fields, "torus");
    brep::Surface surfA;
    brep::Surface surfB;
    size_t witnessBefore = c.fields.size();
    if (torusWord < c.fields.size()) {
      const size_t planeWord = FindFieldWordAfter(c.fields, "plane", torusWord);
      const size_t coneWord = FindFieldWordAfter(c.fields, "cone", torusWord);
      if (!SurfaceFromIntcurveEmbeddedTorus(c, torusWord, &surfA))
        return false;
      if (planeWord < c.fields.size()) {
        if (!SurfaceFromIntcurveEmbeddedPlane(c, planeWord, &surfB))
          return false;
      } else if (coneWord < c.fields.size()) {
        if (!SurfaceFromIntcurveEmbeddedCone(c, coneWord, v0Pos, v1Pos, &surfB))
          return false;
      } else {
        const size_t sphereWord = FindFieldWordAfter(c.fields, "sphere", torusWord);
        if (sphereWord >= c.fields.size() || !SurfaceFromIntcurveEmbeddedSphere(c, sphereWord, &surfB))
          return Fail("intcurve-curve does not embed plane, cone, or sphere with the torus");
      }
      witnessBefore = torusWord;
    } else {
      const size_t cone0 = FindFieldWord(c.fields, "cone");
      const size_t cone1 = FindFieldWordAfter(c.fields, "cone", cone0);
      const size_t sphere0 = FindFieldWord(c.fields, "sphere");
      if (cone0 < c.fields.size() && cone1 < c.fields.size()) {
        if (!SurfaceFromIntcurveEmbeddedCone(c, cone0, v0Pos, v1Pos, &surfA) ||
            !SurfaceFromIntcurveEmbeddedCone(c, cone1, v0Pos, v1Pos, &surfB))
          return false;
        witnessBefore = cone1;
      } else if (sphere0 < c.fields.size()) {
        const size_t sphere1 = FindFieldWordAfter(c.fields, "sphere", sphere0);
        const size_t mateCone = FindFieldWord(c.fields, "cone");
        const size_t matePlane = FindFieldWord(c.fields, "plane");
        if (!SurfaceFromIntcurveEmbeddedSphere(c, sphere0, &surfA))
          return false;
        if (sphere1 < c.fields.size()) {
          if (!SurfaceFromIntcurveEmbeddedSphere(c, sphere1, &surfB))
            return false;
          witnessBefore = sphere1;
        } else if (mateCone < c.fields.size()) {
          if (!SurfaceFromIntcurveEmbeddedCone(c, mateCone, v0Pos, v1Pos, &surfB))
            return false;
          witnessBefore = std::min(sphere0, mateCone);
        } else if (matePlane < c.fields.size()) {
          if (!SurfaceFromIntcurveEmbeddedPlane(c, matePlane, &surfB))
            return false;
          witnessBefore = std::min(sphere0, matePlane);
        } else {
          return Fail("intcurve-curve does not embed torus+mate or two cones (Plant intersection expected)");
        }
      } else {
        return Fail("intcurve-curve does not embed torus+mate or two cones (Plant intersection expected)");
      }
    }
    Vec3 witness{};
    if (!IntcurveMarchWitness(c, witnessBefore, v0Pos, v1Pos, surfA, surfB, &witness))
      return false;
    out->kind = brep::CurveKind::Intersection;
    out->isectSurfaces = {surfA, surfB};
    out->frame.origin = witness;
    return true;
  }

  bool EdgeOf(int edgeRecId, brep::Solid* out, BuiltEdge* outEdge) {
    auto it = edgeCache_.find(edgeRecId);
    if (it != edgeCache_.end()) {
      *outEdge = it->second;
      return true;
    }
    const SatRecord* e = nullptr;
    if (!Req(edgeRecId, "edge", &e))
      return false;
    int v0Id = 0, v1Id = 0, curveId = 0;
    if (!Ptr(*e, 1, "edge.start-vertex", &v0Id) || !Ptr(*e, 2, "edge.end-vertex", &v1Id) ||
        !Ptr(*e, 3, "edge.curve", &curveId))
      return false;
    if (curveId < 0 && v0Id == v1Id) {
      // Null-curve seam on one vertex — no geometric edge; do not instantiate orphan vertices.
      outEdge->edgeIndex = -1;
      edgeCache_[edgeRecId] = *outEdge;
      return true;
    }
    int vi0 = 0, vi1 = 0;
    if (!VertexOf(v0Id, out, &vi0) || !VertexOf(v1Id, out, &vi1))
      return false;
    brep::Edge built;
    built.v0 = vi0;
    built.v1 = vi1;
    if (curveId < 0) {
      built.kind = brep::CurveKind::Line;
      const int idx = static_cast<int>(out->edges.size());
      out->edges.push_back(built);
      outEdge->edgeIndex = idx;
      edgeCache_[edgeRecId] = *outEdge;
      return true;
    }
    double edgeT0 = 0.0;
    double edgeT1 = 0.0;
    const bool haveEdgeParams =
        e->fields.size() >= 6 && TryFieldDouble(e->fields[4], &edgeT0) && TryFieldDouble(e->fields[5], &edgeT1);
    const SatRecord* curve = nullptr;
    if (!Req(curveId, "curve", &curve))
      return false;
    if (curve->type == "straight-curve") {
      built.kind = brep::CurveKind::Line;
    } else if (curve->type == "ellipse-curve") {
      const bool full = (v0Id == v1Id) && !haveEdgeParams;
      if (!BuildCircularArc(*curve, out->vertices[static_cast<size_t>(vi0)].p,
                             out->vertices[static_cast<size_t>(vi1)].p, full, &built, haveEdgeParams,
                             edgeT0, edgeT1))
        return false;
    } else if (curve->type == "intcurve-curve") {
      if (!BuildIntcurveIntersectionEdge(*curve, out->vertices[static_cast<size_t>(vi0)].p,
                                         out->vertices[static_cast<size_t>(vi1)].p, &built))
        return false;
    } else {
      return Fail("edge curve kind '" + curve->type + "' is not supported by this importer (see issue #300)");
    }
    const int idx = static_cast<int>(out->edges.size());
    out->edges.push_back(built);
    outEdge->edgeIndex = idx;
    edgeCache_[edgeRecId] = *outEdge;
    return true;
  }

  bool WalkLoop(int loopId, brep::Solid* out, LoopWalk* outLw) {
    const SatRecord* loop = nullptr;
    if (!Req(loopId, "loop", &loop))
      return false;
    int coId = 0;
    if (!Ptr(*loop, 2, "loop.coedge", &coId))
      return false;
    const int firstCoedgeId = coId;
    int guard = 0;
    while (coId >= 0) {
      if (++guard > 4096)
        return Fail("loop's coedge chain never closes (possible ACIS record corruption)");
      const SatRecord* co = nullptr;
      if (!Req(coId, "coedge", &co))
        return false;
      int edgeRecId = 0;
      std::string sense;
      if (!Ptr(*co, 4, "coedge.edge", &edgeRecId) || !Word(*co, 5, "coedge.sense", &sense))
        return false;
      // Real ASM SAT (Plant 3D) encodes coedge sense as I/F, not the Civil 3D text "forward"/"reversed".
      // I/F are flipped relative to edge direction compared to the text tokens (issue #786 torus loop).
      if (sense == "I")
        sense = "reversed";
      else if (sense == "F")
        sense = "forward";
      if (sense != "forward" && sense != "reversed")
        return Fail("coedge sense '" + sense + "' is not 'forward'/'reversed'");
      BuiltEdge be;
      if (!EdgeOf(edgeRecId, out, &be))
        return false;
      if (be.edgeIndex < 0 && plantAsm_) {
        int partnerCoIdx = -1;
        if (TryPtr(*co, 3, &partnerCoIdx) && partnerCoIdx >= 0) {
          const SatRecord* pco = nullptr;
          if (Req(partnerCoIdx, "coedge", &pco)) {
            int pEdgeRecId = 0;
            std::string pSense;
            if (Ptr(*pco, 4, "coedge.edge", &pEdgeRecId) && Word(*pco, 5, "coedge.sense", &pSense)) {
              if (pSense == "I")
                pSense = "reversed";
              else if (pSense == "F")
                pSense = "forward";
              BuiltEdge pbe;
              if (EdgeOf(pEdgeRecId, out, &pbe) && pbe.edgeIndex >= 0) {
                be = pbe;
                sense = (sense == "reversed") ? "forward" : "reversed";
              }
            }
          }
        }
      }
      if (be.edgeIndex >= 0) {
        brep::EdgeUse use;
        use.edge = be.edgeIndex;
        use.reversed = (sense == "reversed");
        outLw->uses.push_back(use);
      }
      int nextCo = 0;
      if (!Ptr(*co, 1, "coedge.next", &nextCo))
        return false;
      if (nextCo == firstCoedgeId)
        break;
      coId = nextCo;
    }
    if (outLw->uses.empty())
      return true;  // degenerate loop (apex seam of null-curve edges only) — caller may skip
    return true;
  }

  /// A plane face's boundary has no rectangle restriction — the kernel already accepts an arbitrary
  /// simple polygon of Line/Arc edges. `Face::uStart/uEnd/vStart/vEnd` are unused for
  /// `SurfaceKind::Plane` (brep.hpp).
  bool BuildPlaneFace(const SatRecord& surface, const std::string& faceSense, brep::Face* outFace) {
    Vec3 origin{}, normalRaw{};
    if (!Vec(surface, 1, "plane-surface.origin", &origin) || !Vec(surface, 4, "plane-surface.normal", &normalRaw))
      return false;
    const Vec3 normalN = ray3d::Normalize(normalRaw);
    if (!IsFinite(origin) || !IsFinite(normalN) || ray3d::Length(normalN) < 0.5)
      return Fail("plane-surface has a degenerate origin or normal");
    const Vec3 normal = (faceSense == "reversed") ? ray3d::Scale(normalN, -1.0) : normalN;
    ucs::Ucs frame;
    if (!ucs::FromNormal(origin, normal, &frame))
      return Fail("plane-surface has a degenerate normal");
    outFace->surface.kind = brep::SurfaceKind::Plane;
    outFace->surface.frame = frame;
    return true;
  }

  /// A planar face with more than one hole — a flange's flat face pierced by a bolt circle, say —
  /// cannot be the kernel's "simple polygon, at most one hole" plane face. It goes in as a general
  /// trim loop (`Face::paramLoops`, ADR-052 / issue #306) instead, exactly as a partial cone revolve
  /// does via \ref BuildConeGeneralTrim: every loop is projected into the plane's own (x, y) frame,
  /// curved edges sampled, the largest-area loop taken as the outer boundary and wound
  /// counter-clockwise with every hole wound the other way (what `brep::Validate` requires of
  /// `paramLoops`). \p loops is reordered in place so `loops[0]` is that outer boundary — the caller
  /// builds `Face::loops` from it in order, and the two must stay index-aligned.
  bool BuildPlaneGeneralTrim(const SatRecord& surface, const std::string& faceSense,
                             const brep::Solid& out, std::vector<LoopWalk>* loops, brep::Face* outFace) {
    if (!BuildPlaneFace(surface, faceSense, outFace))
      return false;
    const ucs::Ucs frame = outFace->surface.frame;
    constexpr int kArcSamples = 32;  // a bolt hole near the rim must still nest inside the outer polygon

    auto project = [&](const LoopWalk& lw) {
      std::vector<curveisect::Vec2> poly;
      for (const brep::EdgeUse& u : lw.uses) {
        const brep::Edge& e = out.edges[static_cast<size_t>(u.edge)];
        const int samples = (e.kind == brep::CurveKind::Line) ? 1 : kArcSamples;
        for (int k = 0; k < samples; ++k) {
          const double tTraverse = static_cast<double>(k) / samples;
          const double s = u.reversed ? (1.0 - tTraverse) : tTraverse;
          const Vec3 w = brep::EdgePointAt(out, e, s);
          const Vec3 local = ucs::WorldToUcs(frame, w);
          poly.push_back(curveisect::Vec2{local.x, local.y});
        }
      }
      return poly;
    };
    auto signedArea = [](const std::vector<curveisect::Vec2>& p) {
      double a = 0.0;
      for (size_t i = 0, n = p.size(); i < n; ++i) {
        const curveisect::Vec2& q0 = p[i];
        const curveisect::Vec2& q1 = p[(i + 1) % n];
        a += q0.x * q1.y - q1.x * q0.y;
      }
      return 0.5 * a;
    };

    std::vector<std::vector<curveisect::Vec2>> polys;
    polys.reserve(loops->size());
    size_t outerIdx = 0;
    double outerArea = -1.0;
    for (size_t i = 0; i < loops->size(); ++i) {
      polys.push_back(project((*loops)[i]));
      if (polys.back().size() < 3)
        return Fail("planar face's general trim loop has too few points to enclose an area");
      const double a = std::fabs(signedArea(polys.back()));
      if (a > outerArea) {
        outerArea = a;
        outerIdx = i;
      }
    }

    std::vector<size_t> order;
    order.push_back(outerIdx);
    for (size_t i = 0; i < loops->size(); ++i)
      if (i != outerIdx)
        order.push_back(i);

    std::vector<LoopWalk> reLoops;
    std::vector<std::vector<curveisect::Vec2>> reParam;
    double xLo = 0, xHi = 0, yLo = 0, yHi = 0;
    bool first = true;
    for (size_t j = 0; j < order.size(); ++j) {
      std::vector<curveisect::Vec2> poly = std::move(polys[order[j]]);
      const bool wantCcw = (j == 0);
      if ((wantCcw && signedArea(poly) < 0.0) || (!wantCcw && signedArea(poly) > 0.0))
        std::reverse(poly.begin(), poly.end());
      for (const curveisect::Vec2& p : poly) {
        if (first) {
          xLo = xHi = p.x;
          yLo = yHi = p.y;
          first = false;
        } else {
          xLo = std::min(xLo, p.x);
          xHi = std::max(xHi, p.x);
          yLo = std::min(yLo, p.y);
          yHi = std::max(yHi, p.y);
        }
      }
      reParam.push_back(std::move(poly));
      reLoops.push_back(std::move((*loops)[order[j]]));
    }
    *loops = std::move(reLoops);
    outFace->paramLoops = std::move(reParam);
    outFace->uStart = xLo;
    outFace->uEnd = xHi;
    outFace->vStart = yLo;
    outFace->vEnd = yHi;
    return true;
  }

  /// Eccentric angle of an elliptical cylinder (major \p radius along frame X, minor \p radius2 along Y).
  /// A circular section keeps `atan2(y, x)`.
  double RevolutionSectionAngle(const Vec3& local, double radius, double radius2) const {
    const bool ellipse =
        radius > kTol && radius2 > kTol && std::fabs(radius - radius2) > 1e-8 * radius;
    if (!ellipse)
      return std::atan2(local.y, local.x);
    return std::atan2(local.y / radius2, local.x / radius);
  }

  /// A cone-surface with sin≈0 and a radius ratio other than 1 is an elliptical cylinder. The ref
  /// direction is the major axis; the minor axis is that ratio times the major.
  bool ApplyEllipticalCylinderSection(const SatRecord& surface, brep::Surface* sf) {
    if (sf == nullptr || sf->kind != brep::SurfaceKind::Cylinder)
      return true;
    double ratio = 1.0;
    if (surface.fields.size() <= 13 || !TryFieldDouble(surface.fields[13], &ratio))
      return true;
    if (!(ratio > kTol) || !(ratio < 1.0 - 1e-6))
      return true;
    Vec3 ref{};
    if (!TryFieldVec3(surface.fields, 7, &ref))
      return Fail("cone-surface elliptical section has a malformed ref direction");
    const Vec3 z = sf->frame.zAxis;
    Vec3 x = ray3d::Sub(ref, ray3d::Scale(z, ray3d::Dot(ref, z)));
    if (!(ray3d::Length(x) > kTol))
      return Fail("cone-surface elliptical major axis is parallel to its axis");
    x = ray3d::Normalize(x);
    sf->frame.xAxis = x;
    sf->frame.yAxis = ray3d::Normalize(ray3d::Cross(z, x));
    sf->radius2 = sf->radius * ratio;
    return true;
  }

  /// Maps a cone/cylinder face's loop into a general parametric trim loop (`Face::paramLoops`,
  /// ADR-052, issue #306) for the non-full-revolve case `BuildConeFace` cannot reduce to its
  /// rectangular `uStart..uEnd` span — issue #310's concrete case for stopping the outright refusal.
  /// \p frame is the face's own base/axis frame, already computed by the caller from the same axial
  /// extent as the rectangle path. `u` is `atan2(y, x)` in that frame, continuously unwrapped as the
  /// loop is walked (so a loop that happens to cross the frame's own angular seam does not read as a
  /// spurious ~2*pi jump); `v` is the frame-local Z, already zero-based at the face's own axial start
  /// — exactly `LocalSurfaceDerivs`' `SurfaceKind::Cylinder`/`Cone` convention (brep.cpp), so no
  /// rescaling is needed for area/volume/tessellation to agree with it. A curved edge is sampled at
  /// `kArcSamples` points along its own span, per \ref brep::Face::paramLoops' "a curved edge
  /// contributes several polyline vertices" contract; a straight edge contributes just its start
  /// point (its end point is the next edge's start).
  ///
  /// This is exact, and the resulting solid passes `Validate`, only when the loop's real 3D shape
  /// reduces to the `uStart..uEnd x [0, height]` rectangle this function's own bounding box reports
  /// (a partial revolve: two constant-`v` rim edges — arcs or straight generatrix chords whose
  /// endpoints share a height — joined by two constant-`u` generatrix edges). `Validate`'s own
  /// closure probe integrates the face by that reported rectangle, not by `paramLoops` (ADR-052 (c):
  /// the closure probe distrusts `paramLoops` as a possible placeholder), so a loop that is
  /// genuinely non-rectangular in (u, v) — e.g. an oblique planar cut through only part of a
  /// revolve, without an `Ellipse` edge `CylinderCutZExtent` (brep.cpp) can recognize — will build
  /// here but then fail `Validate` with `NotClosed`, refused rather than silently misimported.
  bool BuildConeGeneralTrim(const brep::Solid& out, const LoopWalk& loop, const ucs::Ucs& frame,
                             brep::Face* outFace) {
    if (ApplyConeRectangularTrimFromConstantVRim(out, loop, outFace->surface.height, frame, outFace))
      return true;
    constexpr int kArcSamples = 8;
    std::vector<curveisect::Vec2> poly;
    bool haveRaw = false;
    double prevRaw = 0.0;
    double contU = 0.0;
    auto addPoint = [&](const Vec3& p3) {
      const Vec3 local = ucs::WorldToUcs(frame, p3);
      const double rawU = RevolutionSectionAngle(local, outFace->surface.radius, outFace->surface.radius2);
      if (!haveRaw) {
        contU = rawU;
        haveRaw = true;
      } else {
        double delta = rawU - prevRaw;
        while (delta > kPi)
          delta -= 2.0 * kPi;
        while (delta <= -kPi)
          delta += 2.0 * kPi;
        contU += delta;
      }
      prevRaw = rawU;
      poly.push_back(curveisect::Vec2{contU, local.z});
    };
    for (size_t ui = 0; ui < loop.uses.size(); ++ui) {
      const brep::EdgeUse& u = loop.uses[ui];
      const brep::Edge& e = out.edges[static_cast<size_t>(u.edge)];
      const int samples = (e.kind == brep::CurveKind::Line) ? 1 : kArcSamples;
      const int count = (ui + 1 == loop.uses.size()) ? samples + 1 : samples;
      for (int k = 0; k < count; ++k) {
        const double tTraverse = static_cast<double>(k) / samples;
        const double s = u.reversed ? (1.0 - tTraverse) : tTraverse;
        addPoint(brep::EdgePointAt(out, e, s));
      }
    }
    if (poly.size() < 3)
      return Fail("cylindrical/conical face's general trim loop has too few points to enclose an area");
    if (SignedAreaPoly(poly) < 0.0)
      std::reverse(poly.begin(), poly.end());
    double uLo = poly[0].x, uHi = poly[0].x, vLo = poly[0].y, vHi = poly[0].y;
    for (const curveisect::Vec2& p : poly) {
      uLo = std::min(uLo, p.x);
      uHi = std::max(uHi, p.x);
      vLo = std::min(vLo, p.y);
      vHi = std::max(vHi, p.y);
    }
    outFace->uStart = uLo;
    outFace->uEnd = uHi;
    outFace->vStart = vLo;
    outFace->vEnd = vHi;
    outFace->paramLoops.assign(1, std::move(poly));
    return true;
  }

  std::vector<curveisect::Vec2> ProjectConeLoopToParam(const brep::Solid& solid, const LoopWalk& loop,
                                                         const ucs::Ucs& frame, double radius, double radius2) {
    constexpr int kArcSamples = 8;
    std::vector<curveisect::Vec2> poly;
    bool haveRaw = false;
    double prevRaw = 0.0;
    double contU = 0.0;
    for (size_t ui = 0; ui < loop.uses.size(); ++ui) {
      const brep::EdgeUse& u = loop.uses[ui];
      const brep::Edge& e = solid.edges[static_cast<size_t>(u.edge)];
      const int samples = (e.kind == brep::CurveKind::Line) ? 1 : kArcSamples;
      const int count = (ui + 1 == loop.uses.size()) ? samples + 1 : samples;
      for (int k = 0; k < count; ++k) {
        const double tTraverse = static_cast<double>(k) / samples;
        const double s = u.reversed ? (1.0 - tTraverse) : tTraverse;
        const Vec3 local = ucs::WorldToUcs(frame, brep::EdgePointAt(solid, e, s));
        const double rawU = RevolutionSectionAngle(local, radius, radius2);
        if (!haveRaw) {
          contU = rawU;
          haveRaw = true;
        } else {
          double delta = rawU - prevRaw;
          while (delta > kPi)
            delta -= 2.0 * kPi;
          while (delta <= -kPi)
            delta += 2.0 * kPi;
          contU += delta;
        }
        prevRaw = rawU;
        poly.push_back(curveisect::Vec2{contU, local.z});
      }
    }
    return poly;
  }

  /// `ConicalFaceIntegrals` uses `surface.height`, not `paramLoops`. Shrink the analytic patch when trim
  /// `(vStart,vEnd)` spans less than the vertex-derived height from \ref BuildConeGeneralTrimMulti.
  void ShrinkRevolutionFaceToParamHeight(brep::Face* f) {
    if (f->surface.kind != brep::SurfaceKind::Cone && f->surface.kind != brep::SurfaceKind::Cylinder)
      return;
    if (f->paramLoops.empty())
      return;
    const double vSpan = f->vEnd - f->vStart;
    if (!(vSpan > kTol))
      return;
    if (f->surface.height <= vSpan + std::max(kTol, 1e-4 * vSpan))
      return;
    const ucs::Ucs& frame = f->surface.frame;
    const double oldH = std::max(f->surface.height, kTol);
    const double v0 = f->vStart;
    const auto radiusAtOldZ = [&](double zOld) {
      if (f->surface.kind == brep::SurfaceKind::Cylinder)
        return f->surface.radius;
      return f->surface.radius + (f->surface.radius2 - f->surface.radius) * (zOld / oldH);
    };
    const double rBase = radiusAtOldZ(v0);
    const double rTop = radiusAtOldZ(v0 + vSpan);
    f->surface.frame.origin = ucs::UcsToWorld(frame, Vec3{0.0, 0.0, v0});
    f->surface.height = vSpan;
    if (f->surface.kind == brep::SurfaceKind::Cone) {
      f->surface.radius = std::max(rBase, kTol);
      f->surface.radius2 = std::max(rTop, 0.0);
    }
    const double uSpan = f->uEnd - f->uStart;
    const double uShift = f->uStart;
    for (std::vector<curveisect::Vec2>& poly : f->paramLoops)
      for (curveisect::Vec2& p : poly) {
        p.y -= v0;
        p.x -= uShift;
      }
    f->vStart = 0.0;
    f->vEnd = vSpan;
    f->uStart = 0.0;
    f->uEnd = uSpan;
  }

  bool RepackConeParamLoops(const brep::Solid& solid, std::vector<LoopWalk>* loops, const ucs::Ucs& frame,
                            brep::Face* outFace) {
    std::vector<LoopWalk> validLoops;
    std::vector<std::vector<curveisect::Vec2>> polys;
    validLoops.reserve(loops->size());
    polys.reserve(loops->size());
    for (LoopWalk& lw : *loops) {
      std::vector<curveisect::Vec2> poly = ProjectConeLoopToParam(solid, lw, frame, outFace->surface.radius,
                                                                 outFace->surface.radius2);
      if (poly.size() < 3)
        continue;  // apex seam loop of null-curve edges — no area in (u,v)
      validLoops.push_back(std::move(lw));
      polys.push_back(std::move(poly));
    }
    if (polys.empty())
      return Fail("cylindrical/conical face's general trim loop has too few points to enclose an area");
    *loops = std::move(validLoops);
    if (loops->size() == 1) {
      bool useRectangularTrim = true;
      if (plantAsm_) {
        const brep::Edge& e = solid.edges[static_cast<size_t>((*loops)[0].uses[0].edge)];
        useRectangularTrim =
            (e.kind == brep::CurveKind::Arc && e.v0 == e.v1 && std::fabs(e.sweep) + 0.05 >= 2.0 * kPi);
      }
      if (useRectangularTrim &&
          ApplyConeRectangularTrimFromConstantVRim(solid, (*loops)[0], outFace->surface.height, frame, outFace))
        return true;
      brep::Face scratch;
      scratch.surface = outFace->surface;
      if (BuildConeGeneralTrim(solid, (*loops)[0], frame, &scratch)) {
        outFace->uStart = scratch.uStart;
        outFace->uEnd = scratch.uEnd;
        outFace->vStart = scratch.vStart;
        outFace->vEnd = scratch.vEnd;
        outFace->paramLoops = std::move(scratch.paramLoops);
        return true;
      }
    }
    if (!polys.empty()) {
      double ref = 0.0;
      for (const curveisect::Vec2& p : polys[0])
        ref += p.x;
      ref /= static_cast<double>(polys[0].size());
      for (std::vector<curveisect::Vec2>& poly : polys) {
        double mean = 0.0;
        for (const curveisect::Vec2& p : poly)
          mean += p.x;
        mean /= static_cast<double>(poly.size());
        while (mean - ref > kPi) {
          for (curveisect::Vec2& p : poly)
            p.x -= 2.0 * kPi;
          mean -= 2.0 * kPi;
        }
        while (ref - mean > kPi) {
          for (curveisect::Vec2& p : poly)
            p.x += 2.0 * kPi;
          mean += 2.0 * kPi;
        }
      }
    }
    size_t outerIdx = 0;
    double outerArea = -1.0;
    for (size_t i = 0; i < polys.size(); ++i) {
      const double a = std::fabs(SignedAreaPoly(polys[i]));
      if (a > outerArea) {
        outerArea = a;
        outerIdx = i;
      }
    }
    std::vector<size_t> order;
    order.push_back(outerIdx);
    for (size_t i = 0; i < polys.size(); ++i)
      if (i != outerIdx)
        order.push_back(i);
    std::vector<LoopWalk> reLoops;
    std::vector<std::vector<curveisect::Vec2>> reParam;
    double uLo = 0.0;
    double uHi = 0.0;
    double vLo = 0.0;
    double vHi = 0.0;
    bool first = true;
    for (size_t j = 0; j < order.size(); ++j) {
      std::vector<curveisect::Vec2> poly = std::move(polys[order[j]]);
      const bool wantCcw = (j == 0);
      if ((wantCcw && SignedAreaPoly(poly) < 0.0) || (!wantCcw && SignedAreaPoly(poly) > 0.0))
        std::reverse(poly.begin(), poly.end());
      for (const curveisect::Vec2& p : poly) {
        if (first) {
          uLo = uHi = p.x;
          vLo = vHi = p.y;
          first = false;
        } else {
          uLo = std::min(uLo, p.x);
          uHi = std::max(uHi, p.x);
          vLo = std::min(vLo, p.y);
          vHi = std::max(vHi, p.y);
        }
      }
      reParam.push_back(std::move(poly));
      reLoops.push_back(std::move((*loops)[order[j]]));
    }
    *loops = std::move(reLoops);
    outFace->paramLoops = std::move(reParam);
    outFace->uStart = uLo;
    outFace->uEnd = uHi;
    outFace->vStart = vLo;
    outFace->vEnd = vHi;
    ShrinkRevolutionFaceToParamHeight(outFace);
    return true;
  }

  [[nodiscard]] static double RadialDistanceFromAxis(const Vec3& p, const Vec3& axisOrigin, const Vec3& axisUnit) {
    const Vec3 d = ray3d::Sub(p, axisOrigin);
    const Vec3 perp = ray3d::Sub(d, ray3d::Scale(axisUnit, ray3d::Dot(d, axisUnit)));
    return ray3d::Length(perp);
  }

  /// A conical face's end radii from the surface itself: `r(t) = r0 + t tan(alpha)` at the face's
  /// axial extent [tMin, tMax]. A face whose base reaches the apex is refused rather than reshaped.
  bool ConeEndRadii(double majorRadius, double sinA, double cosA, double tMin, double tMax, double* rBase,
                    double* rTop) {
    if (std::fabs(cosA) < 1e-12)
      return Fail("cone-surface has a degenerate half-angle");
    const double tanA = sinA / cosA;
    *rBase = majorRadius + tMin * tanA;
    *rTop = majorRadius + tMax * tanA;
    if (!(*rBase > kTol) || *rTop < -kTol)
      return Fail("conical face reaches its apex at its base — not supported by this importer");
    if (*rTop < 0.0)
      *rTop = 0.0;
    return true;
  }

  void ClipConeAxialExtentToApex(double majorRadius, double sinA, double cosA, double* tMin, double* tMax) {
    if (std::fabs(sinA) < 1e-9 && cosA > 0.0)
      return;
    if (std::fabs(cosA) < 1e-12)
      return;
    const double tanA = sinA / cosA;
    if (std::fabs(tanA) < 1e-12)
      return;
    const double tApex = -majorRadius / tanA;
    if (*tMax <= tApex + kTol)
      return;  // face lies entirely on the apex-tip side — leave extent unchanged
    if (*tMin < tApex)
      *tMin = tApex;
  }

  /// Plant ASM often gives a cone face a single circular rim loop (constant axial height). Expand to a
  /// thin axial sliver so the analytic cone patch and general trim can be built.
  /// Plant ASM often bounds a cone/cylinder wall with one circular rim at a single axial height. Sampling
  /// that loop into (u, v) yields a near-zero-height strip that closes with a 2π jump in u and fails
  /// `Validate` as self-intersecting. Use the analytic rectangle span instead (same as a full revolve).
  bool ApplyConeRectangularTrimFromConstantVRim(const brep::Solid& solid, const LoopWalk& loop,
                                                double height, const ucs::Ucs& frame, brep::Face* outFace) {
    if (!(height > kTol))
      return false;
    constexpr int kSamples = 16;
    double vLo = 0.0;
    double vHi = 0.0;
    bool firstV = true;
    bool haveU = false;
    double prevRaw = 0.0;
    double contU = 0.0;
    double uAtStart = 0.0;
    double uAtEnd = 0.0;
    for (const brep::EdgeUse& u : loop.uses) {
      const brep::Edge& e = solid.edges[static_cast<size_t>(u.edge)];
      const int samples = (e.kind == brep::CurveKind::Line) ? 2 : kSamples;
      for (int k = 0; k < samples; ++k) {
        const double tTraverse = (samples <= 1) ? 0.0 : static_cast<double>(k) / static_cast<double>(samples - 1);
        const double s = u.reversed ? (1.0 - tTraverse) : tTraverse;
        const Vec3 local = ucs::WorldToUcs(frame, brep::EdgePointAt(solid, e, s));
        if (firstV) {
          vLo = vHi = local.z;
          firstV = false;
        } else {
          vLo = std::min(vLo, local.z);
          vHi = std::max(vHi, local.z);
        }
        const double rawU = std::atan2(local.y, local.x);
        if (!haveU) {
          contU = rawU;
          uAtStart = contU;
          haveU = true;
        } else {
          double delta = rawU - prevRaw;
          while (delta > kPi)
            delta -= 2.0 * kPi;
          while (delta <= -kPi)
            delta += 2.0 * kPi;
          contU += delta;
        }
        prevRaw = rawU;
        uAtEnd = contU;
      }
    }
    if (!haveU)
      return false;
    const double axialTol = std::max(kTol, 1e-4 * height);
    if (vHi - vLo > axialTol)
      return false;
    double uSpan = uAtEnd - uAtStart;
    if (loop.uses.size() == 1) {
      const brep::Edge& e = solid.edges[static_cast<size_t>(loop.uses[0].edge)];
      if (e.kind == brep::CurveKind::Arc && e.v0 == e.v1) {
        if (plantAsm_ && std::fabs(e.sweep) + 0.05 < 2.0 * kPi)
          return false;
        uSpan = 2.0 * kPi;
      }
    }
    if (!(uSpan > 0.25 * kPi))
      return false;
    outFace->uStart = 0.0;
    outFace->uEnd = (uSpan >= 2.0 * kPi - 1e-3) ? 2.0 * kPi : uSpan;
    outFace->vStart = 0.0;
    outFace->vEnd = height;
    outFace->paramLoops.clear();
    return true;
  }

  /// Two full-turn rims do not share a vertex, so each (u, v) loop closes with a chord that cuts the
  /// curve. A ruling of the cone joins one point on each rim; walked down and back it is one loop.
  void SeamFullTurnRims(brep::Solid* solid, std::vector<LoopWalk>* loops, const ucs::Ucs& frame) {
    if (solid == nullptr || loops == nullptr || loops->size() < 2)
      return;
    const auto uSpan = [&](const LoopWalk& lw) {
      const std::vector<curveisect::Vec2> poly = ProjectConeLoopToParam(*solid, lw, frame, 0.0, 0.0);
      if (poly.size() < 3)
        return 0.0;
      double lo = poly[0].x;
      double hi = poly[0].x;
      for (const curveisect::Vec2& p : poly) {
        lo = std::min(lo, p.x);
        hi = std::max(hi, p.x);
      }
      return hi - lo;
    };
    std::vector<int> full;
    for (int i = 0; i < static_cast<int>(loops->size()); ++i) {
      if (uSpan((*loops)[static_cast<size_t>(i)]) > 5.0)
        full.push_back(i);
    }
    if (full.size() != 2)
      return;
    int circleIdx = -1;
    for (int i : full) {
      if ((*loops)[static_cast<size_t>(i)].uses.size() != 1)
        continue;
      const brep::Edge& e = solid->edges[static_cast<size_t>((*loops)[static_cast<size_t>(i)].uses[0].edge)];
      if ((e.kind == brep::CurveKind::Arc || e.kind == brep::CurveKind::Ellipse) && e.v0 == e.v1)
        circleIdx = i;
    }
    if (circleIdx < 0)
      return;
    const int rimIdx = full[0] == circleIdx ? full[1] : full[0];
    const auto startOf = [&](const brep::EdgeUse& u) {
      const brep::Edge& e = solid->edges[static_cast<size_t>(u.edge)];
      return u.reversed ? e.v1 : e.v0;
    };
    LoopWalk rim = (*loops)[static_cast<size_t>(rimIdx)];
    if (rim.uses.empty())
      return;
    const int seamFrom = startOf(rim.uses[0]);
    const brep::EdgeUse circleUse = (*loops)[static_cast<size_t>(circleIdx)].uses[0];
    brep::Edge& circle = solid->edges[static_cast<size_t>(circleUse.edge)];
    const int circleVert = circle.v0;
    int owners = 0;
    for (const brep::Edge& e : solid->edges) {
      if (e.v0 == circleVert || e.v1 == circleVert)
        ++owners;
    }
    if (owners != 1)
      return;
    const Vec3 fromLocal = ucs::WorldToUcs(frame, solid->vertices[static_cast<size_t>(seamFrom)].p);
    const double uTarget = std::atan2(fromLocal.y, fromLocal.x);
    Vec3 best = solid->vertices[static_cast<size_t>(circleVert)].p;
    double bestGap = 1e9;
    for (int k = 0; k <= 64; ++k) {
      const Vec3 p = brep::EdgePointAt(*solid, circle, static_cast<double>(k) / 64.0);
      const Vec3 loc = ucs::WorldToUcs(frame, p);
      double gap = std::atan2(loc.y, loc.x) - uTarget;
      while (gap > kPi)
        gap -= 2.0 * kPi;
      while (gap < -kPi)
        gap += 2.0 * kPi;
      if (std::fabs(gap) < bestGap) {
        bestGap = std::fabs(gap);
        best = p;
      }
    }
    Vec3 radial = ray3d::Sub(best, circle.frame.origin);
    radial = ray3d::Sub(radial, ray3d::Scale(circle.frame.zAxis, ray3d::Dot(radial, circle.frame.zAxis)));
    if (ray3d::Length(radial) < kTol)
      return;
    circle.frame.xAxis = ray3d::Normalize(radial);
    circle.frame.yAxis = ray3d::Normalize(ray3d::Cross(circle.frame.zAxis, circle.frame.xAxis));
    solid->vertices[static_cast<size_t>(circleVert)].p = best;
    brep::Edge seam;
    seam.kind = brep::CurveKind::Line;
    seam.v0 = seamFrom;
    seam.v1 = circleVert;
    solid->edges.push_back(seam);
    const int seamIdx = static_cast<int>(solid->edges.size()) - 1;
    LoopWalk merged;
    merged.uses = rim.uses;
    merged.uses.push_back(brep::EdgeUse{seamIdx, false});
    merged.uses.push_back(circleUse);
    merged.uses.push_back(brep::EdgeUse{seamIdx, true});
    std::vector<LoopWalk> kept;
    kept.push_back(std::move(merged));
    for (int i = 0; i < static_cast<int>(loops->size()); ++i) {
      if (i != circleIdx && i != rimIdx)
        kept.push_back((*loops)[static_cast<size_t>(i)]);
    }
    *loops = std::move(kept);
  }

  bool BuildConeGeneralTrimMulti(const SatRecord& surface, const std::string& faceSense, brep::Solid* out,
                                 std::vector<LoopWalk>* loops, brep::Face* outFace, int /*plantOwnedLoopRecords*/) {
    Vec3 axisOrigin{}, axisRaw{};
    double sinA = 0.0;
    double cosA = 0.0;
    double majorRadius = 0.0;
    if (!Vec(surface, 1, "cone-surface.origin", &axisOrigin) || !Vec(surface, 4, "cone-surface.axis", &axisRaw) ||
        !Num(surface, 10, "cone-surface.sin-angle", &sinA) || !Num(surface, 11, "cone-surface.cos-angle", &cosA) ||
        !Num(surface, 12, "cone-surface.major-radius", &majorRadius))
      return false;
    axisRaw = ray3d::Normalize(axisRaw);
    if (!IsFinite(axisOrigin) || !IsFinite(axisRaw) || ray3d::Length(axisRaw) < 0.5)
      return Fail("cone-surface has a degenerate origin or axis");
    if (!(majorRadius > kTol))
      return Fail("cone-surface has a non-positive major radius");
    double tMin = 0.0;
    double tMax = 0.0;
    bool first = true;
    for (const LoopWalk& lw : *loops) {
      for (const brep::EdgeUse& u : lw.uses) {
        const brep::Edge& e = out->edges[static_cast<size_t>(u.edge)];
        for (int vi : {e.v0, e.v1}) {
          const double t = ray3d::Dot(ray3d::Sub(out->vertices[static_cast<size_t>(vi)].p, axisOrigin), axisRaw);
          if (first || t < tMin)
            tMin = t;
          if (first || t > tMax)
            tMax = t;
          first = false;
        }
      }
    }
    ClipConeAxialExtentToApex(majorRadius, sinA, cosA, &tMin, &tMax);
    const double height = tMax - tMin;
    if (!(height > kTol))
      return Fail("cylindrical/conical face has zero axial extent");
    const Vec3 base = ray3d::Add(axisOrigin, ray3d::Scale(axisRaw, tMin));
    ucs::Ucs frame;
    if (!ucs::FromNormal(base, axisRaw, &frame))
      return Fail("cone-surface has a degenerate axis");
    const bool isCylinder = std::fabs(sinA) < 1e-9 && cosA > 0.0;
    outFace->surface.kind = isCylinder ? brep::SurfaceKind::Cylinder : brep::SurfaceKind::Cone;
    outFace->surface.frame = frame;
    outFace->surface.height = height;
    if (isCylinder) {
      outFace->surface.radius = majorRadius;
      if (!ApplyEllipticalCylinderSection(surface, &outFace->surface))
        return false;
    } else {
      if (!ConeEndRadii(majorRadius, sinA, cosA, tMin, tMax, &outFace->surface.radius, &outFace->surface.radius2))
        return false;
    }
    outFace->surface.inward = (faceSense == "reversed");
    SeamFullTurnRims(out, loops, frame);
    return RepackConeParamLoops(*out, loops, frame, outFace);
  }

  /// Builds a right-handed torus frame: Z along the axis of revolution, X from the ACIS ref direction
  /// projected into the equatorial plane (u = 0).
  bool TorusFrameFromAcis(const Vec3& centre, const Vec3& axisRaw, const Vec3& refRaw, ucs::Ucs* frame) {
    const Vec3 zAxis = ray3d::Normalize(axisRaw);
    if (!IsFinite(zAxis) || ray3d::Length(zAxis) < 0.5)
      return false;
    Vec3 xRaw = ray3d::Sub(refRaw, ray3d::Scale(zAxis, ray3d::Dot(refRaw, zAxis)));
    if (!IsFinite(xRaw) || ray3d::Length(xRaw) < kTol)
      return false;
    const Vec3 xAxis = ray3d::Normalize(xRaw);
    const Vec3 yAxis = ray3d::Normalize(ray3d::Cross(zAxis, xAxis));
    frame->origin = centre;
    frame->xAxis = xAxis;
    frame->yAxis = yAxis;
    frame->zAxis = zAxis;
    return ucs::IsRightHandedOrthonormal(*frame);
  }

  /// An elbow's tube outline is stored as two full turns: down one end, across the seam, down the
  /// other end (another turn), and back on the seam one period away. Reflecting the second turn
  /// across that seam makes one simple patch. A loop that already comes back is left alone.
  void FoldDoubleWoundTorusLoop(std::vector<curveisect::Vec2>* poly) {
    if (poly == nullptr || poly->size() < 8)
      return;
    double vMin = poly->front().y;
    double vMax = vMin;
    for (const curveisect::Vec2& p : *poly) {
      vMin = std::min(vMin, p.y);
      vMax = std::max(vMax, p.y);
    }
    if (vMax - vMin <= 3.0 * kPi)
      return;
    constexpr double kVTol = 1e-3;
    size_t seamEnd = 0;
    bool found = false;
    size_t i = 1;
    while (i + 1 < poly->size()) {
      if (std::fabs((*poly)[i].y - (*poly)[i - 1].y) > kVTol) {
        ++i;
        continue;
      }
      size_t j = i;
      while (j + 1 < poly->size() && std::fabs((*poly)[j + 1].y - (*poly)[i].y) <= kVTol)
        ++j;
      if (j >= i + 1) {
        seamEnd = j;
        found = true;
        break;
      }
      i = j + 1;
    }
    if (!found || seamEnd + 1 >= poly->size())
      return;
    const double vSeam = (*poly)[seamEnd].y;
    const double away = vSeam - poly->front().y;
    if (!(std::fabs(away) > 1.0))
      return;
    if (((*poly)[seamEnd + 1].y - vSeam) * away <= 0.0)
      return;
    for (size_t k = seamEnd + 1; k < poly->size(); ++k)
      (*poly)[k].y = 2.0 * vSeam - (*poly)[k].y;
  }

  /// Samples a torus face loop into (u, v) parameter space matching `brep::SurfaceParamDeriv` for
  /// `SurfaceKind::Torus` (u = angle around axis, v = angle around the tube).
  bool BuildTorusGeneralTrim(const brep::Solid& solid, const LoopWalk& loop, const ucs::Ucs& frame,
                               double majorR, double minorR, brep::Face* outFace) {
    // Eight samples per turn leave a chord that cuts about 0.2% off an elbow. Thirty-two stays
    // inside the 0.1% volume band AutoCAD's solid is checked against.
    constexpr int kArcSamples = 32;
    std::vector<curveisect::Vec2> poly;
    bool haveU = false;
    bool haveV = false;
    double prevRawU = 0.0;
    double prevRawV = 0.0;
    double contU = 0.0;
    double contV = 0.0;
    auto unwrap = [](double raw, double prevRaw, bool* have, double* cont) {
      if (!*have) {
        *cont = raw;
        *have = true;
      } else {
        double delta = raw - prevRaw;
        while (delta > kPi)
          delta -= 2.0 * kPi;
        while (delta <= -kPi)
          delta += 2.0 * kPi;
        *cont += delta;
      }
    };
    auto addPoint = [&](const Vec3& p3) {
      const Vec3 local = ucs::WorldToUcs(frame, p3);
      const double x = local.x;
      const double y = local.y;
      const double z = local.z;
      const double rho = std::hypot(x, y);
      const double rawU = std::atan2(y, x);
      unwrap(rawU, prevRawU, &haveU, &contU);
      prevRawU = rawU;
      const double tubeRadial = rho - majorR;
      const double rawV = std::atan2(z, tubeRadial);
      unwrap(rawV, prevRawV, &haveV, &contV);
      prevRawV = rawV;
      poly.push_back(curveisect::Vec2{contU, contV});
    };
    for (const brep::EdgeUse& u : loop.uses) {
      const brep::Edge& e = solid.edges[static_cast<size_t>(u.edge)];
      const int samples = (e.kind == brep::CurveKind::Line) ? 1 : kArcSamples;
      for (int k = 0; k < samples; ++k) {
        const double tTraverse = static_cast<double>(k) / samples;
        const double s = u.reversed ? (1.0 - tTraverse) : tTraverse;
        addPoint(brep::EdgePointAt(solid, e, s));
      }
    }
    if (poly.size() < 3)
      return Fail("torus face's trim loop has too few points to enclose an area");
    FoldDoubleWoundTorusLoop(&poly);
    {
      double a = 0.0;
      for (size_t i = 0, n = poly.size(); i < n; ++i)
        a += poly[i].x * poly[(i + 1) % n].y - poly[(i + 1) % n].x * poly[i].y;
      if (a < 0.0)
        std::reverse(poly.begin(), poly.end());
    }
    double uLo = poly[0].x, uHi = poly[0].x, vLo = poly[0].y, vHi = poly[0].y;
    for (const curveisect::Vec2& p : poly) {
      uLo = std::min(uLo, p.x);
      uHi = std::max(uHi, p.x);
      vLo = std::min(vLo, p.y);
      vHi = std::max(vHi, p.y);
    }
    outFace->uStart = uLo;
    outFace->uEnd = uHi;
    outFace->vStart = vLo;
    outFace->vEnd = vHi;
    outFace->paramLoops.assign(1, std::move(poly));
    (void)minorR;
    return true;
  }

  std::vector<curveisect::Vec2> ProjectTorusLoopToParam(const brep::Solid& solid, const LoopWalk& loop,
                                                         const ucs::Ucs& frame, double majorR) {
    constexpr int kArcSamples = 32;
    std::vector<curveisect::Vec2> poly;
    bool haveU = false;
    bool haveV = false;
    double prevRawU = 0.0;
    double prevRawV = 0.0;
    double contU = 0.0;
    double contV = 0.0;
    auto unwrap = [](double raw, double prevRaw, bool* have, double* cont) {
      if (!*have) {
        *cont = raw;
        *have = true;
      } else {
        double delta = raw - prevRaw;
        while (delta > kPi)
          delta -= 2.0 * kPi;
        while (delta <= -kPi)
          delta += 2.0 * kPi;
        *cont += delta;
      }
    };
    for (const brep::EdgeUse& u : loop.uses) {
      const brep::Edge& e = solid.edges[static_cast<size_t>(u.edge)];
      const int samples = (e.kind == brep::CurveKind::Line) ? 1 : kArcSamples;
      for (int k = 0; k < samples; ++k) {
        const double tTraverse = static_cast<double>(k) / samples;
        const double s = u.reversed ? (1.0 - tTraverse) : tTraverse;
        const Vec3 local = ucs::WorldToUcs(frame, brep::EdgePointAt(solid, e, s));
        const double rho = std::hypot(local.x, local.y);
        const double rawU = std::atan2(local.y, local.x);
        unwrap(rawU, prevRawU, &haveU, &contU);
        prevRawU = rawU;
        const double tubeRadial = rho - majorR;
        const double rawV = std::atan2(local.z, tubeRadial);
        unwrap(rawV, prevRawV, &haveV, &contV);
        prevRawV = rawV;
        poly.push_back(curveisect::Vec2{contU, contV});
      }
    }
    return poly;
  }

  bool RepackTorusParamLoops(const brep::Solid& solid, std::vector<LoopWalk>* loops, const ucs::Ucs& frame,
                             double majorR, brep::Face* outFace) {
    std::vector<LoopWalk> validLoops;
    std::vector<std::vector<curveisect::Vec2>> polys;
    validLoops.reserve(loops->size());
    polys.reserve(loops->size());
    for (LoopWalk& lw : *loops) {
      std::vector<curveisect::Vec2> poly = ProjectTorusLoopToParam(solid, lw, frame, majorR);
      if (poly.size() < 3)
        continue;
      validLoops.push_back(std::move(lw));
      polys.push_back(std::move(poly));
    }
    if (polys.empty())
      return Fail("torus face's trim loop has too few points to enclose an area");
    *loops = std::move(validLoops);
    size_t outerIdx = 0;
    double outerArea = -1.0;
    for (size_t i = 0; i < polys.size(); ++i) {
      const double a = std::fabs(SignedAreaPoly(polys[i]));
      if (a > outerArea) {
        outerArea = a;
        outerIdx = i;
      }
    }
    std::vector<size_t> order;
    order.push_back(outerIdx);
    for (size_t i = 0; i < polys.size(); ++i)
      if (i != outerIdx)
        order.push_back(i);
    std::vector<LoopWalk> reLoops;
    std::vector<std::vector<curveisect::Vec2>> reParam;
    double uLo = 0.0;
    double uHi = 0.0;
    double vLo = 0.0;
    double vHi = 0.0;
    bool first = true;
    for (size_t j = 0; j < order.size(); ++j) {
      std::vector<curveisect::Vec2> poly = std::move(polys[order[j]]);
      const bool wantCcw = (j == 0);
      if ((wantCcw && SignedAreaPoly(poly) < 0.0) || (!wantCcw && SignedAreaPoly(poly) > 0.0))
        std::reverse(poly.begin(), poly.end());
      for (const curveisect::Vec2& p : poly) {
        if (first) {
          uLo = uHi = p.x;
          vLo = vHi = p.y;
          first = false;
        } else {
          uLo = std::min(uLo, p.x);
          uHi = std::max(uHi, p.x);
          vLo = std::min(vLo, p.y);
          vHi = std::max(vHi, p.y);
        }
      }
      reParam.push_back(std::move(poly));
      reLoops.push_back(std::move((*loops)[order[j]]));
    }
    *loops = std::move(reLoops);
    outFace->paramLoops = std::move(reParam);
    outFace->uStart = uLo;
    outFace->uEnd = uHi;
    outFace->vStart = vLo;
    outFace->vEnd = vHi;
    return true;
  }

  bool BuildTorusGeneralTrimMulti(const SatRecord& surface, const std::string& faceSense,
                                  const brep::Solid& solid, std::vector<LoopWalk>* loops,
                                  brep::Face* outFace) {
    Vec3 centre{}, axisRaw{}, refRaw{};
    double majorR = 0.0;
    double minorR = 0.0;
    if (!Vec(surface, 1, "torus-surface.centre", &centre) || !Vec(surface, 4, "torus-surface.axis", &axisRaw) ||
        !Num(surface, 7, "torus-surface.major-radius", &majorR) ||
        !Num(surface, 8, "torus-surface.minor-radius", &minorR) || !Vec(surface, 9, "torus-surface.refdir", &refRaw))
      return false;
    if (!IsFinite(centre) || !IsFinite(axisRaw) || !IsFinite(refRaw))
      return Fail("torus-surface has a non-finite placement vector");
    if (!(majorR > kTol) || !(minorR > kTol))
      return Fail("torus-surface has a non-positive major or minor radius");
    if (std::fabs(majorR - minorR) < kTol)
      return Fail("torus-surface has equal major and minor radii — degenerate for this kernel");
    ucs::Ucs frame;
    if (!TorusFrameFromAcis(centre, axisRaw, refRaw, &frame))
      return Fail("torus-surface has a degenerate axis or ref direction");
    outFace->surface.kind = brep::SurfaceKind::Torus;
    outFace->surface.frame = frame;
    outFace->surface.radius = majorR;
    outFace->surface.radius2 = minorR;
    outFace->surface.inward = (faceSense == "reversed");
    return RepackTorusParamLoops(solid, loops, frame, majorR, outFace);
  }

  /// Sphere (u, v): u = longitude about +Z, v = latitude (`brep::SurfaceParamDeriv`).
  bool BuildSphereGeneralTrim(const brep::Solid& solid, const LoopWalk& loop, const ucs::Ucs& frame,
                              double radius, brep::Face* outFace) {
    constexpr int kArcSamples = 8;
    std::vector<curveisect::Vec2> poly;
    bool haveU = false;
    bool haveV = false;
    double prevRawU = 0.0;
    double prevRawV = 0.0;
    double contU = 0.0;
    double contV = 0.0;
    auto unwrap = [](double raw, double prevRaw, bool* have, double* cont) {
      if (!*have) {
        *cont = raw;
        *have = true;
      } else {
        double delta = raw - prevRaw;
        while (delta > kPi)
          delta -= 2.0 * kPi;
        while (delta <= -kPi)
          delta += 2.0 * kPi;
        *cont += delta;
      }
    };
    auto addPoint = [&](const Vec3& p3) {
      const Vec3 local = ucs::WorldToUcs(frame, p3);
      const double rho = std::hypot(local.x, local.y);
      const double rawU = std::atan2(local.y, local.x);
      unwrap(rawU, prevRawU, &haveU, &contU);
      prevRawU = rawU;
      const double rawV = std::atan2(local.z, rho);
      unwrap(rawV, prevRawV, &haveV, &contV);
      prevRawV = rawV;
      poly.push_back(curveisect::Vec2{contU, contV});
    };
    for (const brep::EdgeUse& u : loop.uses) {
      const brep::Edge& e = solid.edges[static_cast<size_t>(u.edge)];
      const int samples = (e.kind == brep::CurveKind::Line) ? 1 : kArcSamples;
      for (int k = 0; k < samples; ++k) {
        const double tTraverse = static_cast<double>(k) / samples;
        const double s = u.reversed ? (1.0 - tTraverse) : tTraverse;
        addPoint(brep::EdgePointAt(solid, e, s));
      }
    }
    if (poly.size() < 3)
      return Fail("sphere face's trim loop has too few points to enclose an area");
    {
      double a = 0.0;
      for (size_t i = 0, n = poly.size(); i < n; ++i)
        a += poly[i].x * poly[(i + 1) % n].y - poly[(i + 1) % n].x * poly[i].y;
      if (a < 0.0)
        std::reverse(poly.begin(), poly.end());
    }
    double uLo = poly[0].x, uHi = poly[0].x, vLo = poly[0].y, vHi = poly[0].y;
    for (const curveisect::Vec2& p : poly) {
      uLo = std::min(uLo, p.x);
      uHi = std::max(uHi, p.x);
      vLo = std::min(vLo, p.y);
      vHi = std::max(vHi, p.y);
    }
    outFace->uStart = uLo;
    outFace->uEnd = uHi;
    outFace->vStart = vLo;
    outFace->vEnd = vHi;
    outFace->paramLoops.assign(1, std::move(poly));
    (void)radius;
    return true;
  }

  bool BuildSphereGeneralTrimMulti(const SatRecord& surface, const std::string& faceSense,
                                   const brep::Solid& solid, std::vector<LoopWalk>* loops,
                                   brep::Face* outFace) {
    Vec3 centre{}, axisRaw{}, refRaw{};
    double radius = 0.0;
    size_t radiusField = 10;
    size_t axisField = 4;
    size_t refField = 7;
    if (surface.fields.size() >= 11 && !surface.fields[4].empty() && surface.fields[4][0] != '$') {
      radiusField = 4;
      axisField = 5;
      refField = 8;
    }
    if (!Vec(surface, 1, "sphere-surface.centre", &centre) ||
        !Num(surface, radiusField, "sphere-surface.radius", &radius) ||
        !Vec(surface, axisField, "sphere-surface.axis", &axisRaw) ||
        !Vec(surface, refField, "sphere-surface.refdir", &refRaw))
      return false;
    ucs::Ucs frame;
    if (!TorusFrameFromAcis(centre, axisRaw, refRaw, &frame))
      return Fail("sphere-surface has a degenerate axis or ref direction");
    outFace->surface.kind = brep::SurfaceKind::Sphere;
    outFace->surface.frame = frame;
    outFace->surface.radius = radius;
    outFace->surface.inward = (faceSense == "reversed");

    constexpr int kArcSamples = 8;
    auto project = [&](const LoopWalk& lw) {
      std::vector<curveisect::Vec2> poly;
      bool haveU = false;
      bool haveV = false;
      double prevRawU = 0.0;
      double prevRawV = 0.0;
      double contU = 0.0;
      double contV = 0.0;
      auto unwrap = [](double raw, double prevRaw, bool* have, double* cont) {
        if (!*have) {
          *cont = raw;
          *have = true;
        } else {
          double delta = raw - prevRaw;
          while (delta > kPi)
            delta -= 2.0 * kPi;
          while (delta <= -kPi)
            delta += 2.0 * kPi;
          *cont += delta;
        }
      };
      for (const brep::EdgeUse& u : lw.uses) {
        const brep::Edge& e = solid.edges[static_cast<size_t>(u.edge)];
        const int samples = (e.kind == brep::CurveKind::Line) ? 1 : kArcSamples;
        for (int k = 0; k < samples; ++k) {
          const double tTraverse = static_cast<double>(k) / samples;
          const double s = u.reversed ? (1.0 - tTraverse) : tTraverse;
          const Vec3 local = ucs::WorldToUcs(frame, brep::EdgePointAt(solid, e, s));
          const double rho = std::hypot(local.x, local.y);
          const double rawU = std::atan2(local.y, local.x);
          unwrap(rawU, prevRawU, &haveU, &contU);
          prevRawU = rawU;
          const double rawV = std::atan2(local.z, rho);
          unwrap(rawV, prevRawV, &haveV, &contV);
          prevRawV = rawV;
          poly.push_back(curveisect::Vec2{contU, contV});
        }
      }
      return poly;
    };
    auto signedArea = [](const std::vector<curveisect::Vec2>& p) {
      double a = 0.0;
      for (size_t i = 0, n = p.size(); i < n; ++i)
        a += p[i].x * p[(i + 1) % n].x - p[(i + 1) % n].x * p[i].y;
      return 0.5 * a;
    };

    std::vector<std::vector<curveisect::Vec2>> polys;
    polys.reserve(loops->size());
    size_t outerIdx = 0;
    double outerArea = -1.0;
    for (size_t i = 0; i < loops->size(); ++i) {
      polys.push_back(project((*loops)[i]));
      if (polys.back().size() < 3)
        return Fail("sphere face's general trim loop has too few points to enclose an area");
      const double a = std::fabs(signedArea(polys.back()));
      if (a > outerArea) {
        outerArea = a;
        outerIdx = i;
      }
    }

    std::vector<size_t> order;
    order.push_back(outerIdx);
    for (size_t i = 0; i < loops->size(); ++i)
      if (i != outerIdx)
        order.push_back(i);

    std::vector<LoopWalk> reLoops;
    std::vector<std::vector<curveisect::Vec2>> reParam;
    double uLo = 0, uHi = 0, vLo = 0, vHi = 0;
    bool first = true;
    for (size_t j = 0; j < order.size(); ++j) {
      std::vector<curveisect::Vec2> poly = std::move(polys[order[j]]);
      const bool wantCcw = (j == 0);
      if ((wantCcw && signedArea(poly) < 0.0) || (!wantCcw && signedArea(poly) > 0.0))
        std::reverse(poly.begin(), poly.end());
      for (const curveisect::Vec2& p : poly) {
        if (first) {
          uLo = uHi = p.x;
          vLo = vHi = p.y;
          first = false;
        } else {
          uLo = std::min(uLo, p.x);
          uHi = std::max(uHi, p.x);
          vLo = std::min(vLo, p.y);
          vHi = std::max(vHi, p.y);
        }
      }
      reParam.push_back(std::move(poly));
      reLoops.push_back(std::move((*loops)[order[j]]));
    }
    *loops = std::move(reLoops);
    outFace->paramLoops = std::move(reParam);
    outFace->uStart = uLo;
    outFace->uEnd = uHi;
    outFace->vStart = vLo;
    outFace->vEnd = vHi;
    return true;
  }

  bool BuildSphereFace(const SatRecord& surface, const std::string& faceSense, LoopWalk* loop,
                       const brep::Solid& solid, brep::Face* outFace) {
    Vec3 centre{}, axisRaw{}, refRaw{};
    double radius = 0.0;
    size_t radiusField = 10;
    size_t axisField = 4;
    size_t refField = 7;
    // Real ASM (after NormalizeRealAcisSchema): centre[1..3] radius[4] axis[5..7] ref[8..10].
    if (surface.fields.size() >= 11 && surface.fields[4].empty() == false &&
        surface.fields[4][0] != '$') {
      radiusField = 4;
      axisField = 5;
      refField = 8;
    }
    if (!Vec(surface, 1, "sphere-surface.centre", &centre) ||
        !Num(surface, radiusField, "sphere-surface.radius", &radius) ||
        !Vec(surface, axisField, "sphere-surface.axis", &axisRaw) ||
        !Vec(surface, refField, "sphere-surface.refdir", &refRaw))
      return false;
    if (!IsFinite(centre) || !IsFinite(axisRaw) || !IsFinite(refRaw))
      return Fail("sphere-surface has a non-finite placement vector");
    if (!(radius > kTol))
      return Fail("sphere-surface has a non-positive radius");
    ucs::Ucs frame;
    if (!TorusFrameFromAcis(centre, axisRaw, refRaw, &frame))
      return Fail("sphere-surface has a degenerate axis or ref direction");
    outFace->surface.kind = brep::SurfaceKind::Sphere;
    outFace->surface.frame = frame;
    outFace->surface.radius = radius;
    outFace->surface.inward = (faceSense == "reversed");
    return BuildSphereGeneralTrim(solid, *loop, frame, radius, outFace);
  }

  bool BuildTorusFace(const SatRecord& surface, const std::string& faceSense, LoopWalk* loop,
                      const brep::Solid& solid, brep::Face* outFace) {
    Vec3 centre{}, axisRaw{}, refRaw{};
    double majorR = 0.0;
    double minorR = 0.0;
    if (!Vec(surface, 1, "torus-surface.centre", &centre) || !Vec(surface, 4, "torus-surface.axis", &axisRaw) ||
        !Num(surface, 7, "torus-surface.major-radius", &majorR) ||
        !Num(surface, 8, "torus-surface.minor-radius", &minorR) || !Vec(surface, 9, "torus-surface.refdir", &refRaw))
      return false;
    if (!IsFinite(centre) || !IsFinite(axisRaw) || !IsFinite(refRaw))
      return Fail("torus-surface has a non-finite placement vector");
    if (!(majorR > kTol) || !(minorR > kTol))
      return Fail("torus-surface has a non-positive major or minor radius");
    if (std::fabs(majorR - minorR) < kTol)
      return Fail("torus-surface has equal major and minor radii — degenerate for this kernel");
    ucs::Ucs frame;
    if (!TorusFrameFromAcis(centre, axisRaw, refRaw, &frame))
      return Fail("torus-surface has a degenerate axis or ref direction");
    outFace->surface.kind = brep::SurfaceKind::Torus;
    outFace->surface.frame = frame;
    outFace->surface.radius = majorR;
    outFace->surface.radius2 = minorR;
    outFace->surface.inward = (faceSense == "reversed");
    return BuildTorusGeneralTrim(solid, *loop, frame, majorR, minorR, outFace);
  }

  /// Recognizes the two cylinder/cone loop shapes this importer maps straight to `Face`'s
  /// rectangular `uStart..uEnd` span (see AcisSatParser.hpp) — any other single-loop shape falls
  /// through to \ref BuildConeGeneralTrim instead. Derives the rectangular span directly from the
  /// recognized shape rather than from a generic vertex scan — see ADR-051 (b-1) for why.
  /// A conical face bounded by a single full-circle rim (any other loop is only the degenerate apex
  /// point) is the whole cone from that rim to its tip — Plant valve bodies are two of these meeting
  /// tip to tip. The axial extent is analytic (`t_apex = -r0 / tan(alpha)`), not read from
  /// neighbouring faces. Topology mirrors `brep::MakeCone`'s pointed form: rim, then a seam line to
  /// the apex vertex walked out and back.
  bool BuildPointedConeFace(const Vec3& axisOrigin, const Vec3& axisUnit, double sinA, double cosA,
                            double majorRadius, const std::string& faceSense, LoopWalk* loop,
                            brep::Solid* out, brep::Face* outFace) {
    if (std::fabs(cosA) < 1e-12)
      return Fail("cone-surface has a degenerate half-angle");
    const brep::EdgeUse rimUse = loop->uses[0];
    const brep::Edge& rim = out->edges[static_cast<size_t>(rimUse.edge)];
    const Vec3 rimP = out->vertices[static_cast<size_t>(rim.v0)].p;
    const double tRim = ray3d::Dot(ray3d::Sub(rimP, axisOrigin), axisUnit);
    const double tApex = -majorRadius * cosA / sinA;
    const double height = std::fabs(tApex - tRim);
    if (!(height > kTol))
      return Fail("conical face's rim lies at its apex — degenerate");
    const Vec3 apexP = ray3d::Add(axisOrigin, ray3d::Scale(axisUnit, tApex));
    int apexV = -1;
    for (size_t i = 0; i < out->vertices.size(); ++i)
      if (ray3d::Length(ray3d::Sub(out->vertices[i].p, apexP)) <= 1e-6 * (1.0 + height)) {
        apexV = static_cast<int>(i);
        break;
      }
    if (apexV < 0) {
      apexV = static_cast<int>(out->vertices.size());
      out->vertices.push_back(brep::Vertex{apexP});
    }
    brep::Edge seam;
    seam.kind = brep::CurveKind::Line;
    seam.v0 = rim.v0;
    seam.v1 = apexV;
    const int seamIdx = static_cast<int>(out->edges.size());
    out->edges.push_back(seam);
    loop->uses = {rimUse, {seamIdx, false}, {seamIdx, true}};

    // Frame Z runs from the rim toward the apex, so the base is the rim and the top radius is 0.
    const Vec3 toApex = (tApex > tRim) ? axisUnit : ray3d::Scale(axisUnit, -1.0);
    const Vec3 base = ray3d::Add(axisOrigin, ray3d::Scale(axisUnit, tRim));
    ucs::Ucs frame;
    if (!ucs::FromNormal(base, toApex, &frame))
      return Fail("cone-surface has a degenerate axis");
    outFace->surface.kind = brep::SurfaceKind::Cone;
    outFace->surface.frame = frame;
    outFace->surface.height = height;
    outFace->surface.radius = RadialDistanceFromAxis(rimP, axisOrigin, axisUnit);
    outFace->surface.radius2 = 0.0;
    outFace->surface.inward = (faceSense == "reversed");
    outFace->uStart = 0.0;
    outFace->uEnd = 2.0 * kPi;
    outFace->vStart = 0.0;
    outFace->vEnd = height;
    return true;
  }

  bool BuildConeFace(const SatRecord& surface, const std::string& faceSense, LoopWalk* loop,
                      brep::Solid* out, brep::Face* outFace) {
    Vec3 axisOrigin{}, axisRaw{};
    double sinA = 0.0, cosA = 0.0, majorRadius = 0.0;
    if (!Vec(surface, 1, "cone-surface.origin", &axisOrigin) || !Vec(surface, 4, "cone-surface.axis", &axisRaw) ||
        !Num(surface, 10, "cone-surface.sin-angle", &sinA) || !Num(surface, 11, "cone-surface.cos-angle", &cosA) ||
        !Num(surface, 12, "cone-surface.major-radius", &majorRadius))
      return false;
    axisRaw = ray3d::Normalize(axisRaw);
    if (!IsFinite(axisOrigin) || !IsFinite(axisRaw) || ray3d::Length(axisRaw) < 0.5)
      return Fail("cone-surface has a degenerate origin or axis");
    if (!(majorRadius > kTol))
      return Fail("cone-surface has a non-positive major radius");

    const size_t n = loop->uses.size();
    auto edgeKind = [&](size_t i) { return out->edges[static_cast<size_t>(loop->uses[i].edge)].kind; };
    bool full = false;
    if (n == 2 && edgeKind(0) == brep::CurveKind::Arc && edgeKind(1) == brep::CurveKind::Arc) {
      const brep::Edge& e0 = out->edges[static_cast<size_t>(loop->uses[0].edge)];
      const brep::Edge& e1 = out->edges[static_cast<size_t>(loop->uses[1].edge)];
      if (e0.v0 == e0.v1 && e1.v0 == e1.v1)
        full = true;
    }
    // A loop that is not the two-full-circle-rim shape above (most commonly a partial revolve — a
    // seam/arc/seam/arc quadrilateral) used to be refused outright (issue #302's original ADR-051
    // (c) narrowing) because deriving its u-span from the seam edges' angular position, while still
    // reporting the rectangle form, was a materially different and untested derivation. Issue #306's
    // `Face::paramLoops` general trim loop removes the need for that derivation entirely: the loop's
    // own edges are sampled directly into a (u,v) polygon (`BuildConeGeneralTrim` below), so the
    // face's true boundary is what area/volume/tessellation/picking (#307-#309) actually see — this
    // is issue #310's concrete case for this surface kind. See `BuildConeGeneralTrim`'s own comment
    // for why a loop that is genuinely non-rectangular in (u, v) still gets refused, via `Validate`.

    // A full revolve's two rim edges (each a full circle, v0 == v1) share no vertex with each other,
    // so brep::Validate's "consecutive edge uses share a vertex" ring-closure check cannot see them as
    // one closed loop on its own. ACIS's periodic-surface loop has no such connecting edge — the
    // surface's own periodicity closes it — so this importer adds one, exactly like the kernel's own
    // MakeCylinder does with its two seam lines: a synthetic Line edge between the two rims' vertices,
    // traversed once each direction. It has no effect on the analytic area/volume (those integrate
    // the surface in closed form from uStart/uEnd, not from the loop's shape) — it exists only to
    // satisfy the topology check.
    const bool isCylinder = std::fabs(sinA) < 1e-9 && cosA > 0.0;
    if (!isCylinder && n == 1 && edgeKind(0) == brep::CurveKind::Arc) {
      const brep::Edge& rim = out->edges[static_cast<size_t>(loop->uses[0].edge)];
      if (rim.v0 == rim.v1 && std::fabs(rim.sweep) + 1e-3 >= 2.0 * kPi)
        return BuildPointedConeFace(axisOrigin, axisRaw, sinA, cosA, majorRadius, faceSense, loop, out, outFace);
    }
    if (full) {
      const brep::Edge& e0 = out->edges[static_cast<size_t>(loop->uses[0].edge)];
      const brep::Edge& e1 = out->edges[static_cast<size_t>(loop->uses[1].edge)];
      brep::Edge seam;
      seam.kind = brep::CurveKind::Line;
      seam.v0 = e0.v0;
      seam.v1 = e1.v0;
      if (ray3d::Length(ray3d::Sub(out->vertices[static_cast<size_t>(seam.v1)].p,
                                    out->vertices[static_cast<size_t>(seam.v0)].p)) <= kTol)
        return Fail("cylindrical/conical face's two rims meet at the same point — degenerate");
      const int seamIdx = static_cast<int>(out->edges.size());
      out->edges.push_back(seam);
      LoopWalk synthesized;
      synthesized.uses = {loop->uses[0], {seamIdx, false}, loop->uses[1], {seamIdx, true}};
      *loop = std::move(synthesized);
    }

    // Axial extent, from the vertices the loop actually uses (works for both shapes).
    double tMin = 0.0, tMax = 0.0;
    bool first = true;
    for (const brep::EdgeUse& u : loop->uses) {
      const brep::Edge& e = out->edges[static_cast<size_t>(u.edge)];
      for (int vi : {e.v0, e.v1}) {
        const double t = ray3d::Dot(ray3d::Sub(out->vertices[static_cast<size_t>(vi)].p, axisOrigin), axisRaw);
        if (first || t < tMin)
          tMin = t;
        if (first || t > tMax)
          tMax = t;
        first = false;
      }
    }
    ClipConeAxialExtentToApex(majorRadius, sinA, cosA, &tMin, &tMax);
    const double height = tMax - tMin;
    if (!(height > kTol))
      return Fail("cylindrical/conical face has zero axial extent");
    const Vec3 base = ray3d::Add(axisOrigin, ray3d::Scale(axisRaw, tMin));

    ucs::Ucs frame;
    if (!ucs::FromNormal(base, axisRaw, &frame))
      return Fail("cone-surface has a degenerate axis");

    outFace->surface.kind = isCylinder ? brep::SurfaceKind::Cylinder : brep::SurfaceKind::Cone;
    outFace->surface.frame = frame;
    outFace->surface.height = height;
    if (isCylinder) {
      outFace->surface.radius = majorRadius;
      if (!ApplyEllipticalCylinderSection(surface, &outFace->surface))
        return false;
    } else {
      double rBase = 0.0;
      double rTop = 0.0;
      if (!ConeEndRadii(majorRadius, sinA, cosA, tMin, tMax, &rBase, &rTop))
        return false;
      outFace->surface.radius = rBase;
      outFace->surface.radius2 = rTop;
    }
    outFace->surface.inward = (faceSense == "reversed");
    if (full) {
      outFace->uStart = 0.0;
      outFace->uEnd = 2.0 * kPi;
      return true;
    }
    if (ApplyConeRectangularTrimFromConstantVRim(*out, *loop, height, frame, outFace))
      return true;
    return BuildConeGeneralTrim(*out, *loop, frame, outFace);
  }

  /// Reverses a patch's U parametrisation in place (reflects `knotsU`, reverses each row of `ctrl` /
  /// `wts`) — what a 'reversed' face sense means for a NURBS patch: it flips the sign of `du`, and so
  /// of the outward normal `du x dv`, the same job \ref BuildPlaneFace does by negating the normal and
  /// \ref BuildConeFace does by setting `surface.inward`.
  static void ReversePatchU(nurbs::Patch* p) {
    const double lo = p->knotsU.front();
    const double hi = p->knotsU.back();
    std::vector<double> nk(p->knotsU.size());
    for (size_t i = 0; i < nk.size(); ++i)
      nk[i] = lo + hi - p->knotsU[p->knotsU.size() - 1 - i];
    p->knotsU = std::move(nk);
    std::vector<Vec3> nc(p->ctrl.size());
    std::vector<double> nw(p->wts.size());
    for (int j = 0; j < p->nv; ++j)
      for (int i = 0; i < p->nu; ++i) {
        const size_t src = static_cast<size_t>(j) * static_cast<size_t>(p->nu) +
                           static_cast<size_t>(p->nu - 1 - i);
        const size_t dst = static_cast<size_t>(j) * static_cast<size_t>(p->nu) + static_cast<size_t>(i);
        nc[dst] = p->ctrl[src];
        nw[dst] = p->wts[src];
      }
    p->ctrl = std::move(nc);
    p->wts = std::move(nw);
  }

  /// Parses a `spline-surface` record's degree/knot/control-point/weight fields (see the field-layout
  /// comment at the top of this file) straight onto a `nurbs::Patch` and maps it to
  /// `SurfaceKind::Nurbs` (REQ-315/ADR-048). ADR-048 (b)'s patch is always the **whole, untrimmed**
  /// parameter rectangle, so — unlike `BuildConeFace` — the face's `uStart..vEnd` span is simply the
  /// patch's own domain; it is `VerifySplineLoopIsFullBoundary` below, not this function, that confirms
  /// the loop actually bounds that whole rectangle rather than a proper trim this importer cannot
  /// represent.
  bool BuildSplineFace(const SatRecord& surface, const std::string& faceSense, brep::Face* outFace) {
    int degU = 0, degV = 0, nu = 0, nv = 0, rational = 0;
    if (!Int(surface, 1, "spline-surface.degU", &degU) || !Int(surface, 2, "spline-surface.degV", &degV) ||
        !Int(surface, 3, "spline-surface.nu", &nu) || !Int(surface, 4, "spline-surface.nv", &nv) ||
        !Int(surface, 5, "spline-surface.rational", &rational))
      return false;
    if (degU < 1 || degU > nurbs::kMaxDegree || degV < 1 || degV > nurbs::kMaxDegree)
      return Fail("spline-surface has a degree outside the [1, " + std::to_string(nurbs::kMaxDegree) +
                  "] range this importer's NURBS patch representation supports (ADR-048 (b))");

    nurbs::Patch patch;
    patch.degU = degU;
    patch.degV = degV;
    patch.nu = nu;
    patch.nv = nv;
    size_t field = 6;
    const int knotUCount = nu + degU + 1;
    const int knotVCount = nv + degV + 1;
    if (knotUCount < 0 || knotVCount < 0)
      return Fail("spline-surface has a non-positive control-point count");
    patch.knotsU.resize(static_cast<size_t>(knotUCount));
    for (int i = 0; i < knotUCount; ++i, ++field)
      if (!Num(surface, field, "spline-surface.knotsU", &patch.knotsU[static_cast<size_t>(i)]))
        return false;
    patch.knotsV.resize(static_cast<size_t>(knotVCount));
    for (int i = 0; i < knotVCount; ++i, ++field)
      if (!Num(surface, field, "spline-surface.knotsV", &patch.knotsV[static_cast<size_t>(i)]))
        return false;

    const long long ctrlCount = static_cast<long long>(nu) * static_cast<long long>(nv);
    if (ctrlCount <= 0)
      return Fail("spline-surface has no control points");
    patch.ctrl.resize(static_cast<size_t>(ctrlCount));
    patch.wts.assign(static_cast<size_t>(ctrlCount), 1.0);
    for (long long i = 0; i < ctrlCount; ++i) {
      Vec3 p{};
      if (!Vec(surface, field, "spline-surface.ctrlpt", &p))
        return false;
      field += 3;
      if (rational != 0) {
        double w = 1.0;
        if (!Num(surface, field, "spline-surface.ctrlpt-weight", &w))
          return false;
        ++field;
        if (!(w > 0.0))
          return Fail("spline-surface control point has a non-positive weight");
        patch.wts[static_cast<size_t>(i)] = w;
      }
      patch.ctrl[static_cast<size_t>(i)] = p;
    }

    const nurbs::PatchProblem prob = nurbs::ValidatePatch(patch);
    if (prob != nurbs::PatchProblem::Ok)
      return Fail(std::string("spline-surface patch is invalid: ") + nurbs::PatchProblemText(prob));
    if (faceSense == "reversed")
      ReversePatchU(&patch);

    outFace->surface.kind = brep::SurfaceKind::Nurbs;
    outFace->uStart = nurbs::UMin(patch);
    outFace->uEnd = nurbs::UMax(patch);
    outFace->vStart = nurbs::VMin(patch);
    outFace->vEnd = nurbs::VMax(patch);
    outFace->surface.patch = std::move(patch);
    return true;
  }

  /// A `spline-surface` face's loop must bound the **entire** patch rectangle, corner to corner — this
  /// importer has no way to represent a proper trim (ADR-048 (b) patches are always the full untrimmed
  /// rectangle). Requires exactly 4 edges whose 4 vertices are, as an unordered set within tolerance,
  /// the patch's 4 corner control points (`ctrl[0]`, `ctrl[nu-1]`, `ctrl[(nv-1)*nu]`,
  /// `ctrl[nu*nv-1]`) — the same corners `nurbs::RuledLinear`/`ArcRibbon` place at a Loft/Sweep side
  /// face's own 4-edge loop, so a genuinely untrimmed patch built by this importer's own kernel would
  /// pass this same check.
  bool VerifySplineLoopIsFullBoundary(const brep::Solid& out, const nurbs::Patch& patch,
                                       const LoopWalk& lw) {
    if (lw.uses.size() != 4)
      return false;
    const Vec3 corners[4] = {patch.ctrl[0], patch.ctrl[static_cast<size_t>(patch.nu - 1)],
                              patch.ctrl[static_cast<size_t>((patch.nv - 1) * patch.nu)],
                              patch.ctrl[static_cast<size_t>(patch.nu * patch.nv - 1)]};
    bool used[4] = {false, false, false, false};
    for (const brep::EdgeUse& u : lw.uses) {
      const brep::Edge& e = out.edges[static_cast<size_t>(u.edge)];
      const Vec3& p = out.vertices[static_cast<size_t>(u.reversed ? e.v1 : e.v0)].p;
      bool matched = false;
      for (int c = 0; c < 4; ++c) {
        if (used[c])
          continue;
        if (ray3d::Length(ray3d::Sub(p, corners[c])) <= kTol) {
          used[c] = true;
          matched = true;
          break;
        }
      }
      if (!matched)
        return false;
    }
    return used[0] && used[1] && used[2] && used[3];
  }

  /// Builds one `brep::Face` for the surface at \p surfaceId, dispatching on its record type.
  /// `blend-surface` and `sweep-surface` records recurse through the surface they name as their
  /// representable reduction (a `$-1` "underlying-surface" pointer means they have none) — see the
  /// field-layout comment at the top of this file.
  bool BuildFaceForSurface(int faceRecIdx, int surfaceId, const std::string& faceSense, std::vector<LoopWalk>* loops,
                            brep::Solid* out, brep::Face* outFace, int depth) {
    if (depth > 8)
      return Fail("surface reduction chain is implausibly deep (possible ACIS record corruption)");
    const SatRecord* surface = nullptr;
    if (!Req(surfaceId, "surface", &surface))
      return false;
    if (surface->type == "plane-surface") {
      if (loops->size() >= 2)
        return BuildPlaneGeneralTrim(*surface, faceSense, *out, loops, outFace);
      return BuildPlaneFace(*surface, faceSense, outFace);
    }
    if (surface->type == "cone-surface") {
      // Real ACIS lists a full cone/cylinder wall's two rim circles as two separate single-edge
      // loops; `BuildConeFace` wants them as one loop of two rim edges (its "full revolve" shape).
      auto isFullRim = [&](const LoopWalk& lw) {
        if (lw.uses.size() != 1)
          return false;
        const brep::Edge& e = out->edges[static_cast<size_t>(lw.uses[0].edge)];
        return e.kind == brep::CurveKind::Arc && e.v0 == e.v1;
      };
      if (loops->size() == 2 && isFullRim((*loops)[0]) && isFullRim((*loops)[1])) {
        LoopWalk merged;
        merged.uses = {(*loops)[0].uses[0], (*loops)[1].uses[0]};
        *loops = std::vector<LoopWalk>{std::move(merged)};
      }
      if (loops->size() >= 2)
        return BuildConeGeneralTrimMulti(*surface, faceSense, out, loops, outFace, 0);
      if (loops->size() != 1)
        return Fail("cylindrical/conical face has no boundary loop");
      return BuildConeFace(*surface, faceSense, &loops->front(), out, outFace);
    }
    if (surface->type == "spline-surface") {
      if (loops->size() != 1)
        return Fail("spline-surface face has more than one loop — a trimmed NURBS boundary with holes "
                    "is not supported by this importer (issue #300)");
      if (!BuildSplineFace(*surface, faceSense, outFace))
        return false;
      if (!VerifySplineLoopIsFullBoundary(*out, outFace->surface.patch, loops->front()))
        return Fail(
            "spline-surface face's loop does not bound the whole parametric patch — a proper trim is "
            "not representable by this importer's untrimmed NURBS patch (ADR-048 (b), issue #300)");
      return true;
    }
    if (surface->type == "blend-surface" || surface->type == "sweep-surface") {
      int underlyingId = 0;
      const std::string what = surface->type + ".underlying-surface";
      if (!Ptr(*surface, 1, what.c_str(), &underlyingId))
        return false;
      if (underlyingId < 0)
        return Fail("'" + surface->type +
                    "' does not reduce to a surface this importer can represent — not supported "
                    "(issue #300)");
      return BuildFaceForSurface(faceRecIdx, underlyingId, faceSense, loops, out, outFace, depth + 1);
    }
    if (surface->type == "torus-surface") {
      if (loops->size() >= 2)
        return BuildTorusGeneralTrimMulti(*surface, faceSense, *out, loops, outFace);
      if (loops->size() != 1)
        return Fail("torus face has no boundary loop");
      return BuildTorusFace(*surface, faceSense, &loops->front(), *out, outFace);
    }
    if (surface->type == "sphere-surface") {
      if (loops->size() >= 2)
        return BuildSphereGeneralTrimMulti(*surface, faceSense, *out, loops, outFace);
      if (loops->size() != 1)
        return Fail("sphere face has no boundary loop");
      return BuildSphereFace(*surface, faceSense, &loops->front(), *out, outFace);
    }
    return Fail("surface kind '" + surface->type + "' is not supported by this importer (see issue #300)");
  }

  bool Build(brep::Solid* out, bool validateSolid) {
    const int bodyId = FindFirst("body");
    if (bodyId < 0)
      return Fail("no ACIS 'body' record found");
    const SatRecord* body = At(bodyId);
    int wireId = 0, lumpId = 0;
    if (!Ptr(*body, 2, "body.wire", &wireId) || !Ptr(*body, 1, "body.lump", &lumpId))
      return false;
    int transformId = -1;
    if (body->fields.size() > 3 && !body->fields[3].empty() && body->fields[3][0] == '$')
      (void)Ptr(*body, 3, "body.transform", &transformId);
    if (lumpId < 0) {
      if (wireId >= 0)
        return Fail("body is a wire (curves only, no faces) — not a solid; wire import is out of scope (#299)");
      return Fail("body has no lump — nothing to import");
    }
    const SatRecord* lump = nullptr;
    if (!Req(lumpId, "lump", &lump))
      return false;
    int lumpNext = 0;
    if (!Ptr(*lump, 1, "lump.next", &lumpNext))
      return false;
    if (lumpNext >= 0)
      return Fail("body has more than one lump — multi-lump bodies are out of scope for this increment (#299)");
    int shellId = 0;
    if (!Ptr(*lump, 2, "lump.shell", &shellId))
      return false;
    const SatRecord* shell = nullptr;
    if (!Req(shellId, "shell", &shell))
      return false;
    int subshell = 0;
    if (!Ptr(*shell, 2, "shell.subshell", &subshell))
      return false;
    if (subshell >= 0)
      return Fail("shell has a sub-shell — void/nested shells are out of scope for this increment (#299)");

    int faceId = 0;
    if (!Ptr(*shell, 3, "shell.face", &faceId))
      return false;
    int guardFaces = 0;
    while (faceId >= 0) {
      if (++guardFaces > 100000)
        return Fail("face chain never terminates (possible ACIS record corruption)");
      const SatRecord* face = nullptr;
      if (!Req(faceId, "face", &face))
        return false;
      int surfaceId = 0;
      std::string faceSense;
      if (IsPlantAsmFaceRecord(*face)) {
        if (!Word(*face, 8, "face.sense", &faceSense))
          return false;
        surfaceId = ResolvePlantFaceSurfaceId(*face);
        if (surfaceId < 0)
          return Fail("Plant ASM face's surface pointer does not name a surface record");
      } else {
        if (!Ptr(*face, 4, "face.surface", &surfaceId) || !Word(*face, 5, "face.sense", &faceSense))
          return false;
      }
      if (faceSense != "forward" && faceSense != "reversed")
        return Fail("face sense '" + faceSense + "' is not 'forward'/'reversed'");

      std::vector<LoopWalk> loops;
      if (!CollectLoopsForFace(faceId, *face, out, &loops))
        return false;

      brep::Face outFace;
      if (!BuildFaceForSurface(faceId, surfaceId, faceSense, &loops, out, &outFace, 0))
        return false;
      for (LoopWalk& lw : loops)
        outFace.loops.push_back(brep::Loop{std::move(lw.uses)});
      out->faces.push_back(std::move(outFace));

      int faceNext = 0;
      if (IsPlantAsmFaceRecord(*face)) {
        if (!TryPtr(*face, 3, &faceNext))
          return Fail("malformed face.next pointer in a 'face' record");
      } else if (!Ptr(*face, 1, "face.next", &faceNext)) {
        return false;
      }
      faceId = faceNext;
    }

    if (out->faces.empty())
      return Fail("shell has no faces");
    brep::Shell sh;
    sh.faces.resize(out->faces.size());
    for (size_t i = 0; i < out->faces.size(); ++i)
      sh.faces[i] = static_cast<int>(i);
    out->shells.push_back(std::move(sh));

    if (transformId >= 0 && !ApplyBodyTransform(transformId, out, validateSolid))
      return false;

    if (validateSolid) {
      const brep::Problem why = brep::Validate(*out);
      if (why != brep::Problem::Ok)
        return Fail(std::string("imported topology failed validation: ") + brep::ProblemText(why));
    }
    return true;
  }

  /// `brep::Rotate` turns surface frames and vertices but leaves `Face::paramLoops` in the pre-motion
  /// (u, v) plane; re-sample from the moved topology so `Validate` still sees a consistent trim.
  void RebuildGeneralTrimLoops(brep::Solid* solid) {
    for (brep::Face& f : solid->faces) {
      if (f.paramLoops.empty())
        continue;
      std::vector<LoopWalk> walks;
      walks.reserve(f.loops.size());
      for (const brep::Loop& lp : f.loops) {
        LoopWalk lw;
        lw.uses = lp.uses;
        walks.push_back(std::move(lw));
      }
      if (walks.empty())
        continue;
      brep::Face scratch;
      scratch.surface = f.surface;
      bool ok = false;
      if (walks.size() == 1) {
        if (f.surface.kind == brep::SurfaceKind::Torus)
          ok = BuildTorusGeneralTrim(*solid, walks[0], f.surface.frame, f.surface.radius, f.surface.radius2,
                                     &scratch);
        else if (f.surface.kind == brep::SurfaceKind::Sphere)
          ok = BuildSphereGeneralTrim(*solid, walks[0], f.surface.frame, f.surface.radius, &scratch);
        else if (f.surface.kind == brep::SurfaceKind::Cylinder || f.surface.kind == brep::SurfaceKind::Cone)
          ok = BuildConeGeneralTrim(*solid, walks[0], f.surface.frame, &scratch);
      } else if (f.surface.kind == brep::SurfaceKind::Torus) {
        ok = RepackTorusParamLoops(*solid, &walks, f.surface.frame, f.surface.radius, &scratch);
      } else if (f.surface.kind == brep::SurfaceKind::Cylinder || f.surface.kind == brep::SurfaceKind::Cone) {
        ok = RepackConeParamLoops(*solid, &walks, f.surface.frame, &scratch);
      }
      if (ok) {
        f.uStart = scratch.uStart;
        f.uEnd = scratch.uEnd;
        f.vStart = scratch.vStart;
        f.vEnd = scratch.vEnd;
        f.paramLoops = std::move(scratch.paramLoops);
        if (walks.size() == f.loops.size()) {
          for (size_t i = 0; i < walks.size(); ++i)
            f.loops[i].uses = std::move(walks[i].uses);
        }
      }
    }
  }

  /// Applies the `body`'s `transform` record — a 3x3 rotation, a translation, and a uniform scale —
  /// to the fully built solid, in the order ACIS composes them (`p' = scale * R * p + t`). Real ASM
  /// bodies keep their placement here rather than baked into the geometry; the fixture schema has no
  /// transform record so this is never reached for those. Reflection or shear is refused (out of
  /// scope, and neither maps onto `brep`'s rigid transforms).
  bool ApplyBodyTransform(int transformId, brep::Solid* out, bool validateSolid) {
    const SatRecord* t = nullptr;
    if (!Req(transformId, "transform", &t))
      return false;
    // ACIS stores the 3x3 row-major and applies it to ROW vectors (`p' = p * A`); `brep::Rotate`
    // and `RotationMatrixToAxisAngle` below work on column vectors (`p' = M * p`), so transpose:
    // M = A^T.
    double a[9] = {};
    for (int i = 0; i < 9; ++i)
      if (!Num(*t, static_cast<size_t>(2 + i), "transform.matrix", &a[i]))
        return false;
    const double m[9] = {a[0], a[3], a[6], a[1], a[4], a[7], a[2], a[5], a[8]};
    Vec3 trans{};
    double scale = 1.0;
    if (!Vec(*t, 11, "transform.translation", &trans) || !Num(*t, 14, "transform.scale", &scale))
      return false;
    std::string reflect, shear;
    if (Word(*t, 16, "transform.reflect", &reflect) && reflect == "reflect")
      return Fail("body transform includes a reflection — not supported by this importer");
    if (Word(*t, 17, "transform.shear", &shear) && shear == "shear")
      return Fail("body transform includes a shear — not supported by this importer");
    if (!(scale > 0.0) || !std::isfinite(scale))
      return Fail("body transform has a non-positive scale");

    brep::Solid work = std::move(*out);
    if (std::fabs(scale - 1.0) > 1e-12) {
      brep::Solid scaled;
      brep::Problem why = brep::Problem::Ok;
      if (!brep::Scale(work, Vec3{0, 0, 0}, scale, &scaled, &why))
        return Fail(std::string("body transform scale rejected: ") + brep::ProblemText(why));
      work = std::move(scaled);
    }
    Vec3 axis{};
    double angle = 0.0;
    if (!ray3d::RotationMatrixToAxisAngle(m, &axis, &angle))
      return Fail("body transform's matrix is not a rotation (reflection or non-orthogonal)");
    if (angle > 1e-9) {
      for (brep::Vertex& v : work.vertices)
        v.p = ray3d::RotatePointAboutAxis(v.p, Vec3{0, 0, 0}, axis, angle);
      for (brep::Edge& e : work.edges) {
        if (e.kind != brep::CurveKind::Line)
          RotateFrameInPlace(e.frame, Vec3{0, 0, 0}, axis, angle);
        for (brep::Surface& sf : e.isectSurfaces)
          RotateSurfaceInPlace(sf, Vec3{0, 0, 0}, axis, angle);
      }
      for (brep::Face& f : work.faces)
        RotateSurfaceInPlace(f.surface, Vec3{0, 0, 0}, axis, angle);
      RotateFrameInPlace(work.recipe.frame, Vec3{0, 0, 0}, axis, angle);
      RebuildGeneralTrimLoops(&work);
    }
    work = brep::Translate(work, trans);
    RebuildGeneralTrimLoops(&work);
    *out = std::move(work);
    if (validateSolid) {
      const brep::Problem why = brep::Validate(*out);
      if (why != brep::Problem::Ok)
        return Fail(std::string("body transform left invalid topology: ") + brep::ProblemText(why));
    }
    return true;
  }

  std::vector<SatRecord> recs_;
  std::string label_;
  bool plantAsm_ = false;  // a Plant 3D AcDs ASM stream (DetectPlantAsmSatStream)
  std::string error_;
  std::unordered_map<int, int> vertexIndex_;   // ACIS vertex record id -> out.vertices index
  std::unordered_map<int, BuiltEdge> edgeCache_;  // ACIS edge record id -> already-built edge
};

/// Builds the solid without the final `brep::Validate` (ImportSatSolid adds it).
ImportResult ImportSatSolidBuildOnly(const std::string& sat, const std::string& entityLabel) {
  ImportResult result;
  result.mmPerUnit = HeaderMmPerUnit(sat);
  std::vector<SatRecord> recs = Tokenize(sat);
  if (recs.empty()) {
    result.error = (entityLabel.empty() ? std::string() : entityLabel + ": ") +
                    "ACIS SAT stream is empty or has no records";
    return result;
  }
  const bool plantAsm = DetectPlantAsmSatStream(recs);
  SynthesizeBodyLumpIfMissing(recs);
  if (plantAsm) {
    AdaptPlantAsmBodyRecord(recs);
    AdaptPlantAsmLumpShellRecords(recs);
    AdaptPlantAsmEdgeRecords(recs);
    AdaptPlantAsmCoedgeRecords(recs);
    AdaptPlantAsmLoopRecords(recs);
    AdaptPlantAsmVertexPointAndGeometryRecords(recs);
  }
  NormalizeRealAcisSchema(recs);
  Importer importer(std::move(recs), entityLabel, plantAsm);
  brep::Solid solid;
  if (!importer.Run(&solid, false)) {
    result.ok = false;
    result.error = importer.Error();
    return result;
  }
  result.ok = true;
  result.solid = std::move(solid);
  return result;
}

}  // namespace

ImportResult ImportSatSolid(const std::string& sat, const std::string& entityLabel) {
  ImportResult result = ImportSatSolidBuildOnly(sat, entityLabel);
  if (!result.ok)
    return result;
  const brep::Problem why = brep::Validate(result.solid);
  if (why != brep::Problem::Ok) {
    result.ok = false;
    result.error = (entityLabel.empty() ? std::string() : entityLabel + ": ") +
                   std::string("imported topology failed validation: ") + brep::ProblemText(why);
  }
  return result;
}

}  // namespace acissat
