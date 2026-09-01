// CadCommands_Bench.cpp — REQ-100 frame-budget benchmark (BENCH command),
// split out of CadCommands.cpp (TASK-150 Phase 2, GitHub issue #142).
//
// StartFrameBudgetBench installs the bench scene + scripted orbit; the frame
// loop drives it and calls FinishFrameBudgetBench, which restores the drawing
// and writes the report. Both declared in CadCommands.hpp.

#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"
#include "util/benchscene.hpp"
#include "util/tinbuild.hpp"
#include "SurfaceStyle.hpp"
#include "AppPaths.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

/// Install the bench scene, saving the user's polylines so the run cannot cost them their drawing.
///
/// The camera is saved too and forced to a fixed starting orientation: a benchmark that began from
/// whatever the user was looking at would measure a different amount of geometry every run, and the
/// number would not be comparable to the one in the requirement.
bool StartFrameBudgetBench(AppCommandState& st, int segments, int frames, std::vector<std::string>& log) {
  if (st.bench.active) {
    log.push_back("BENCH — a run is already in progress.");
    return false;
  }
  if (st.activeSpaceIndex != kModelSpaceIndex) {
    log.push_back("BENCH — switch to model space first (REQ-100 measures the model viewport).");
    return false;
  }
  if (segments < 1 || frames < 1) {
    log.push_back("BENCH — segment count and frame count must both be positive.");
    return false;
  }

  AppCommandState::BenchRun& b = st.bench;
  b.savedPolyVerts = st.userPolylineVerts;
  b.savedPolyOffsets = st.userPolylineOffsets;
  b.savedPolyClosed = st.userPolylineClosed;
  b.savedPolyAttrs = st.userPolylineAttrs;
  b.savedSurfaces = st.cadSurfaces;
  b.savedSurfaceAttrs = st.cadSurfaceAttrs;
  b.savedMeshes = st.cadMeshes;
  b.savedMeshAttrs = st.cadMeshAttrs;
  b.savedSolids = st.cadSolids;
  b.savedSolidAttrs = st.cadSolidAttrs;
  b.savedVisualStyle = st.viewportVisualStyle;
  b.savedAzimuthDeg = st.viewportAzimuthDeg;
  b.savedElevationDeg = st.viewportElevationDeg;
  b.savedRollDeg = st.viewportRollDeg;  // #153
  b.savedZoom = st.viewportZoom;
  b.savedPanX = st.viewportPanX;
  b.savedPanY = st.viewportPanY;
  b.savedPanZ = st.viewportPanZ;

  if (b.solidCount > 0) {
    // B-rep solid profile (REQ-313 / REQ-100). The line, surface and mesh stores are emptied for the
    // same reason the other large profiles empty them: the number has to be the solids' cost and
    // nothing else.
    //
    // The scene is MANY solids rather than one big one, and that is the measurement's whole point. A
    // solid's per-frame cost is not one large indexed upload — it is a cache lookup, a stream upload
    // and a draw call per solid for the faces, and another for the edges. Ten thousand triangles in
    // one solid and in a hundred solids are completely different frames, and the second is what a
    // real model looks like. This is also the profile that can catch the failure #120 names
    // directly: if the tessellation were being regenerated per frame, it would show up here and
    // nowhere else.
    st.userPolylineVerts.clear();
    st.userPolylineOffsets.clear();  // empty, not {0} — see ErasePolylineByIndex / issue #60
    st.userPolylineClosed.clear();
    st.userPolylineAttrs.clear();
    st.cadSurfaces.clear();
    st.cadSurfaceAttrs.clear();
    st.cadMeshes.clear();
    st.cadMeshAttrs.clear();
    st.cadSolids.clear();
    st.cadSolidAttrs.clear();
    st.solidDisplayCache.clear();
    st.solidDisplayGeometry.solids.clear();

    // A grid of alternating cylinders and spheres — the two curved primitives, so the tessellation
    // is real work rather than a box's twelve triangles.
    const int side = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(b.solidCount)))));
    int made = 0;
    for (int gy = 0; gy < side && made < b.solidCount; ++gy) {
      for (int gx = 0; gx < side && made < b.solidCount; ++gx) {
        ucs::Ucs frame;
        frame.origin = {static_cast<double>(gx) * 30.0, static_cast<double>(gy) * 30.0, 0.0};
        brep::Solid s;
        brep::Problem why = brep::Problem::Ok;
        const bool ok = (made % 2 == 0) ? brep::MakeCylinder(frame, 8.0, 20.0, &s, &why)
                                        : brep::MakeSphere(frame, 9.0, &s, &why);
        if (!ok) {
          log.push_back(std::string("BENCH — solid scene failed to build: ") + brep::ProblemText(why));
          return false;
        }
        st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(s)));
        st.cadSolidAttrs.push_back(MakeNewEntityAttrs(st));
        ++made;
      }
    }
    b.solidCount = made;

    // Count the triangles the profile will actually draw, so the report states a DENSITY rather than
    // an object count nobody can compare against REQ-100's other profiles.
    b.solidTriangleCount = 0;
    for (const CadSolidPtr& sp : st.cadSolids) {
      brep::Tessellation t;
      brep::Problem tw = brep::Problem::Ok;
      if (brep::Tessellate(*sp, kSolidChordToleranceFt, &t, &tw))
        b.solidTriangleCount += t.triangleCount();
    }

    // Shaded, for the reason the mesh profile forces it: that is the style REQ-064's budget
    // condition is stated in, and it is the only style in which a solid's FACES are drawn at all.
    st.viewportVisualStyle = VisualStyle::Shaded;
    b.segmentCount = 0;
  } else if (b.meshTriangleCount > 0) {
    // Shaded-mesh profile (REQ-100 (b), density decided 2026-08-15). The line stores are emptied
    // for the same reason the surface profile empties them: the number has to be the mesh's cost
    // and nothing else. Surfaces are cleared too, so the two large profiles can never overlap.
    st.userPolylineVerts.clear();
    st.userPolylineOffsets.clear();  // empty, not {0} — see ErasePolylineByIndex / issue #60
    st.userPolylineClosed.clear();
    st.userPolylineAttrs.clear();
    st.cadSurfaces.clear();
    st.cadSurfaceAttrs.clear();

    auto mesh = std::make_shared<CadMesh>();
    b.meshTriangleCount =
        benchscene::BuildMeshScene(b.meshTriangleCount, &mesh->vertsXyz, &mesh->normalsXyz, &mesh->indices);
    mesh->sourceName = "BENCH mesh";
    // One part, deliberately: this is the shape TASK-041 measured the mesh path against, so the two
    // numbers are comparable. A real import has hundreds of parts and therefore hundreds of draw
    // calls — that per-part cost is NOT in this profile, and saying so is better than inventing a
    // part count nobody chose.
    CadMeshPart part;
    part.name = "terrain";
    part.indexBegin = 0;
    part.indexCount = static_cast<int>(mesh->indices.size());
    mesh->parts.push_back(part);
    st.cadMeshes.assign(1, std::move(mesh));
    st.cadMeshAttrs.assign(1, MakeNewEntityAttrs(st));

    // The profile is *shaded* meshes. In 2D Wireframe the mesh is not drawn at all (TASK-041 §9),
    // so measuring in the user's current style would measure whatever they happened to be in.
    st.viewportVisualStyle = VisualStyle::Shaded;
    b.segmentCount = 0;  // no line segments in this profile; the report prints triangles instead
  } else if (b.surfacePointCount > 0) {
    // Surface profile (REQ-100 as amended / ADR-028): the scene is ONE surface, and the line stores
    // are emptied so the measurement is the surface's cost and nothing else. Triangle edges are
    // regenerated display geometry, which is exactly why this profile is not implied by the other
    // two and has to be measured on its own.
    st.userPolylineVerts.clear();
    st.userPolylineOffsets.clear();  // empty, not {0} — see ErasePolylineByIndex / issue #60
    st.userPolylineClosed.clear();
    st.userPolylineAttrs.clear();

    std::vector<float> ptsXyz;
    const int n = benchscene::BuildSurfacePointScene(b.surfacePointCount, &ptsXyz);
    std::vector<TinInputPoint> pts;
    pts.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
      pts.push_back({static_cast<double>(ptsXyz[static_cast<size_t>(i) * 3 + 0]),
                     static_cast<double>(ptsXyz[static_cast<size_t>(i) * 3 + 1]),
                     ptsXyz[static_cast<size_t>(i) * 3 + 2]});
    const TinBuildResult tr = BuildTin(pts);
    if (!tr.ok()) {
      log.push_back(std::string("BENCH — surface scene failed to triangulate: ") + tr.message);
      return false;
    }
    auto tin = std::make_shared<CadTin>();
    tin->vertsXyz = tr.vertsXyz;
    tin->indices = tr.indices;
    CadSurface bs;
    bs.name = "BENCH surface";
    bs.tin = std::move(tin);
    b.surfaceTriangleCount = bs.triangleCount();
    st.cadSurfaces.assign(1, std::move(bs));
    st.cadSurfaceAttrs.assign(1, MakeNewEntityAttrs(st));
    b.segmentCount = b.surfaceTriangleCount * 3;  // triangulation edges, for the report

    // REQ-100 profile (c) is defined as a **contoured** surface, and until REQ-070 landed this case
    // measured an uncontoured one because contours did not exist. The bench surface carries no
    // styleName, so it resolves to "Standard" — which draws contours and the border — and the record
    // below states the interval, because a contour count is meaningless without it.
    SurfaceStyles::EnsureStandard(st.surfaceStyles);
    if (const SurfaceStyle* bstyle = SurfaceStyles::Resolve(st.surfaceStyles, std::string())) {
      b.surfaceMinorIntervalFt = bstyle->minorIntervalFt;
      b.surfaceMajorIntervalFt = bstyle->majorIntervalFt;
    }
    b.regenBaselineTaken = false;
    b.regenDuringRun = 0;
  } else {
    b.segmentCount = benchscene::BuildContourScene(segments, &st.userPolylineVerts, &st.userPolylineOffsets,
                                                   &st.userPolylineClosed);
    st.userPolylineAttrs.assign(st.userPolylineClosed.size(), MakeNewEntityAttrs(st));
  }

  // Frame the whole scene from a tilted view. The framing is computed from the geometry rather than
  // hard-coded: if any of the scene falls outside the viewport the GPU stops rasterising it and the
  // benchmark measures less than the density the requirement names. Sizing to the scene's bounding
  // SPHERE means that stays true through the whole orbit, at every azimuth.
  double mnX = 1e300, mxX = -1e300, mnY = 1e300, mxY = -1e300, mnZ = 1e300, mxZ = -1e300;
  // Whichever store the profile filled: the contour scene lives in the polylines, the surface
  // profile in the TIN, the mesh profile in the mesh. Framing from the wrong one would put the
  // scene off screen and measure a viewport with nothing in it.
  const std::vector<float>& frameVerts =
      (b.meshTriangleCount > 0 && !st.cadMeshes.empty() && st.cadMeshes[0])
          ? st.cadMeshes[0]->vertsXyz
          : ((b.surfacePointCount > 0 && !st.cadSurfaces.empty() && st.cadSurfaces[0].tin)
                 ? st.cadSurfaces[0].tin->vertsXyz
                 : st.userPolylineVerts);
  // The solid profile frames from the solids' ANALYTIC bounds instead — there is no vertex array to
  // walk, and a sphere's two stored vertices would frame a line segment rather than a scene, putting
  // most of the geometry off screen and measuring a viewport with nothing in it.
  if (b.solidCount > 0) {
    for (const CadSolidPtr& sp : st.cadSolids) {
      if (!sp)
        continue;
      const brep::Bounds bb = brep::ComputeBounds(*sp);
      if (!bb.valid)
        continue;
      mnX = std::min(mnX, bb.mn.x);
      mxX = std::max(mxX, bb.mx.x);
      mnY = std::min(mnY, bb.mn.y);
      mxY = std::max(mxY, bb.mx.y);
      mnZ = std::min(mnZ, bb.mn.z);
      mxZ = std::max(mxZ, bb.mx.z);
    }
  }
  for (size_t i = 0; i + 2 < frameVerts.size(); i += 3) {
    mnX = std::min(mnX, static_cast<double>(frameVerts[i]));
    mxX = std::max(mxX, static_cast<double>(frameVerts[i]));
    mnY = std::min(mnY, static_cast<double>(frameVerts[i + 1]));
    mxY = std::max(mxY, static_cast<double>(frameVerts[i + 1]));
    mnZ = std::min(mnZ, static_cast<double>(frameVerts[i + 2]));
    mxZ = std::max(mxZ, static_cast<double>(frameVerts[i + 2]));
  }
  const double cx = 0.5 * (mnX + mxX);
  const double cy = 0.5 * (mnY + mxY);
  const double cz = 0.5 * (mnZ + mxZ);
  const double radius =
      0.5 * std::sqrt((mxX - mnX) * (mxX - mnX) + (mxY - mnY) * (mxY - mnY) + (mxZ - mnZ) * (mxZ - mnZ));
  st.viewportPanX = cx;
  st.viewportPanY = cy;
  st.viewportPanZ = cz;
  const double halfH = std::max(radius * 1.05, 1.0);  // 5% margin so nothing clips at the edge
  st.viewportZoom = static_cast<float>(50.0 / halfH);
  st.viewportAzimuthDeg = 0.f;
  st.viewportElevationDeg = 55.f;
  st.viewportRollDeg = 0.f;  // #153

  b.frameMs.clear();
  b.frameMs.reserve(static_cast<size_t>(frames));
  b.framesTotal = frames;
  b.warmupFrames = 60;
  b.frameIndex = 0;
  b.orbitDegPerFrame = 0.5;  // a full turn every 720 frames — continuous, and never repeats a frame
  b.sceneInstalled = true;
  b.active = true;
  BumpCadGpuCache(st);

  char scene[64];
  if (b.meshTriangleCount > 0)
    std::snprintf(scene, sizeof(scene), "%d triangles, Shaded", b.meshTriangleCount);
  else if (b.surfacePointCount > 0)
    std::snprintf(scene, sizeof(scene), "%d points", b.surfacePointCount);
  else
    std::snprintf(scene, sizeof(scene), "%d segments", b.segmentCount);

  char msg[256];
  std::snprintf(msg, sizeof(msg),
                "BENCH — REQ-100: %s, %d frames (%d warm-up), continuous orbit. Vsync is disabled for the "
                "run; the drawing%s restored when it finishes.",
                scene, frames, b.warmupFrames,
                b.meshTriangleCount > 0 ? " and the visual style are" : " is");
  log.push_back(msg);
  return true;
}

/// Restore the drawing and camera, and report. Called from the frame loop when the run completes.
void FinishFrameBudgetBench(AppCommandState& st, std::vector<std::string>& log) {
  AppCommandState::BenchRun& b = st.bench;
  if (!b.active)
    return;
  if (b.sceneInstalled) {
    st.userPolylineVerts = std::move(b.savedPolyVerts);
    st.userPolylineOffsets = std::move(b.savedPolyOffsets);
    st.userPolylineClosed = std::move(b.savedPolyClosed);
    st.userPolylineAttrs = std::move(b.savedPolyAttrs);
    st.cadSurfaces = std::move(b.savedSurfaces);
    st.cadSurfaceAttrs = std::move(b.savedSurfaceAttrs);
    st.cadMeshes = std::move(b.savedMeshes);
    st.cadMeshAttrs = std::move(b.savedMeshAttrs);
    st.cadSolids = std::move(b.savedSolids);
    st.cadSolidAttrs = std::move(b.savedSolidAttrs);
    st.viewportVisualStyle = b.savedVisualStyle;
    b.savedMeshes.clear();
    b.savedMeshAttrs.clear();
    b.savedSolids.clear();
    b.savedSolidAttrs.clear();
    b.savedPolyVerts.clear();
    b.savedPolyOffsets.clear();
    b.savedPolyClosed.clear();
    b.savedPolyAttrs.clear();
    b.savedSurfaces.clear();
    b.savedSurfaceAttrs.clear();
    st.viewportAzimuthDeg = b.savedAzimuthDeg;
    st.viewportElevationDeg = b.savedElevationDeg;
    st.viewportRollDeg = b.savedRollDeg;  // #153
    st.viewportZoom = b.savedZoom;
    st.viewportPanX = b.savedPanX;
    st.viewportPanY = b.savedPanY;
    st.viewportPanZ = b.savedPanZ;
    b.sceneInstalled = false;
    BumpCadGpuCache(st);
  }
  b.active = false;

  const benchscene::FrameStats s = benchscene::Summarize(b.frameMs);
  if (s.frames < 1) {
    log.push_back("BENCH — no frames were timed; nothing to report.");
    return;
  }
  constexpr double kBudgetMs = 16.0;  // REQ-100
  const bool pass = s.p95Ms <= kBudgetMs;
  // Name the profile, and describe the scene that produced the number: REQ-100 has three profiles,
  // and a p95 quoted without saying which one it measured is as unreproducible as one quoted
  // without the reference machine. Built once and used by BOTH the console line and the file
  // record below — the record is the half that outlives the session, and it is the half that used
  // to omit this (a surface run was preserved as "segments 599898", indistinguishable from a
  // 600k-segment line scene).
  const char* profileName = "line segments";
  char scene[128];
  std::snprintf(scene, sizeof(scene), "%d segments", b.segmentCount);
  if (b.solidCount > 0) {
    profileName = "B-rep solids";
    // Both numbers, deliberately: the triangle count is what makes this comparable with the mesh
    // profile, and the SOLID count is what the per-object cost scales with. A p95 quoted for
    // "40,000 triangles" alone would not say whether it came from one solid or four hundred.
    std::snprintf(scene, sizeof(scene), "%d solids, %d triangles, Shaded", b.solidCount,
                  b.solidTriangleCount);
  } else if (b.meshTriangleCount > 0) {
    profileName = "shaded meshes";
    std::snprintf(scene, sizeof(scene), "%d triangles, Shaded", b.meshTriangleCount);
  } else if (b.surfacePointCount > 0) {
    profileName = "surface (contoured)";
    std::snprintf(scene, sizeof(scene), "%d points, %d triangles, contoured at %s/%s ft (%d contour segs)",
                  b.surfacePointCount, b.surfaceTriangleCount,
                  SurfaceStyles::FormatFt(b.surfaceMinorIntervalFt).c_str(),
                  SurfaceStyles::FormatFt(b.surfaceMajorIntervalFt).c_str(), b.surfaceContourSegs);
  }

  char msg[320];
  std::snprintf(msg, sizeof(msg), "BENCH (%s) — %s, %d timed frames: p95 %.2f ms (budget %.0f ms) — %s.",
                profileName, scene, s.frames, s.p95Ms, kBudgetMs, pass ? "PASS" : "FAIL");
  log.push_back(msg);
  std::snprintf(msg, sizeof(msg), "BENCH — min %.2f  median %.2f  mean %.2f  p95 %.2f  p99 %.2f  max %.2f ms.",
                s.minMs, s.medianMs, s.meanMs, s.p95Ms, s.p99Ms, s.maxMs);
  log.push_back(msg);

  // ADR-036 (e)'s separate obligation, reported separately: the timing above would look identical
  // whether the cache held or not on a fast enough machine, so "held" has to be its own claim.
  const bool cacheHeld = b.regenDuringRun == 0;
  if (b.surfacePointCount > 0) {
    std::snprintf(msg, sizeof(msg),
                  "BENCH — surface display cache regenerated %llu time(s) across %d timed frames "
                  "(expected 0) — %s.",
                  static_cast<unsigned long long>(b.regenDuringRun), s.frames,
                  cacheHeld ? "HELD" : "NOT HELD, contours are being regenerated per frame");
    log.push_back(msg);
  } else if (b.solidCount > 0) {
    // ADR-036 (e)'s obligation for the solid profile: #120 asks that a solid's render mesh not be
    // regenerated every frame, and on a fast machine the p95 above cannot tell a held cache from one
    // silently rebuilding. Reported as its own claim, exactly as the surface line is.
    std::snprintf(msg, sizeof(msg),
                  "BENCH — solid tessellation cache regenerated %llu time(s) across %d timed frames "
                  "(expected 0) — %s.",
                  static_cast<unsigned long long>(b.regenDuringRun), s.frames,
                  cacheHeld ? "HELD" : "NOT HELD, solids are being retessellated per frame");
    log.push_back(msg);
  }

  // Also written to a file: a benchmark's value is in the record, and reading six figures off a
  // fading command line is how a number gets transcribed wrong into a completion report.
  const std::filesystem::path dir = UserDataDirectory();
  if (!dir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream f(dir / "bench-req100.txt", std::ios::app);
    if (f) {
      const std::time_t t = std::time(nullptr);
      char timeBuf[32];
      struct tm tmInfo{};
#ifdef _WIN32
      localtime_s(&tmInfo, &t);
#else
      localtime_r(&t, &tmInfo);
#endif
      std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tmInfo);
      f << timeBuf << "  REQ-100 frame budget\n"
        << "  profile       " << profileName << "\n"
        << "  scene         " << scene << "\n"
        << "  timed frames  " << s.frames << " (after " << b.warmupFrames << " warm-up)\n"
        << "  orbit         " << b.orbitDegPerFrame << " deg/frame, vsync off\n"
        << "  min           " << s.minMs << " ms\n"
        << "  median        " << s.medianMs << " ms\n"
        << "  mean          " << s.meanMs << " ms\n"
        << "  p95           " << s.p95Ms << " ms   <-- REQ-100 is judged on this\n"
        << "  p99           " << s.p99Ms << " ms\n"
        << "  max           " << s.maxMs << " ms\n"
        << "  budget        " << kBudgetMs << " ms  => " << (pass ? "PASS" : "FAIL") << "\n";
      if (b.surfacePointCount > 0) {
        // The record's own half of ADR-036 (e). TASK-053's fix (b) applies here too: the permanent
        // record is the half that gets missed, and a p95 with no statement of whether the cache held
        // cannot be told apart from one measured with the defect present.
        f << "  contour interval  minor " << SurfaceStyles::FormatFt(b.surfaceMinorIntervalFt)
          << " ft, major " << SurfaceStyles::FormatFt(b.surfaceMajorIntervalFt) << " ft\n"
          << "  contour segs      " << b.surfaceContourSegs << "\n"
          << "  cache regens      " << b.regenDuringRun << " during the timed frames (expected 0)  => "
          << (cacheHeld ? "HELD" : "NOT HELD") << "\n";
      }
      if (b.solidCount > 0) {
        f << "  cache regens      " << b.regenDuringRun << " during the timed frames (expected 0)  => "
          << (cacheHeld ? "HELD" : "NOT HELD") << "\n";
      }
      f << "\n";
    }
  }
  b.frameMs.clear();
}
