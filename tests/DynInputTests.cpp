// REQ-354 / D-2026-09-28-j (GitHub issue #564 section 5) — the point-entry dynamic input shows which
// mode it is in. The model (src/commands/CadDynInput.*) is what the viewport's boxes draw from and
// what the headless DYN verbs type into; these pin its rules, and the parser half: a typed Z is read
// at every point prompt, and PIPERUN takes `@dx,dy,dz` from its last vertex.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "CadDynInput.hpp"

using Catch::Approx;
using dyninput::Mode;

namespace {

const std::string kDelta = "\xce\x94";  // U+0394

void Submit(AppCommandState& st, std::vector<std::string>& log, const std::string& text) {
  char buf[256];
  std::snprintf(buf, sizeof buf, "%s", text.c_str());
  ProcessCommandLineSubmit(buf, static_cast<int>(sizeof buf), st, log);
}

dyninput::Group Fresh(Mode initial, bool showZ = false) {
  dyninput::Group g;
  dyninput::Reset(g, initial, showZ);
  return g;
}

}  // namespace

TEST_CASE("dyninput: the labels follow what is typed", "[req354][dyninput]") {
  dyninput::Group g = Fresh(Mode::Absolute);
  CHECK(dyninput::LabelLine(g) == "X Y");
  dyninput::TypeText(g, "@");
  CHECK(g.mode == Mode::Relative);
  CHECK(dyninput::LabelLine(g) == "@ " + kDelta + "X " + kDelta + "Y");

  g = Fresh(Mode::Absolute, /*showZ=*/true);
  CHECK(dyninput::LabelLine(g) == "X Y Z");
  dyninput::TypeText(g, "@");
  CHECK(dyninput::LabelLine(g) == "@ " + kDelta + "X " + kDelta + "Y " + kDelta + "Z");

  g = Fresh(Mode::Absolute, true);
  dyninput::TypeText(g, "12<");
  CHECK(g.mode == Mode::Polar);
  CHECK(dyninput::LabelLine(g) == "Distance < Angle");  // no Z in the polar pair
  CHECK(g.f[0].text == "12");
  CHECK(g.focus == 1);

  g = Fresh(Mode::Absolute);
  dyninput::TypeText(g, "@12<");
  CHECK(g.mode == Mode::RelativePolar);
  CHECK(dyninput::LabelLine(g) == "@ Distance < Angle");
}

TEST_CASE("dyninput: the mode character is consumed, never left in a number", "[req354][dyninput]") {
  dyninput::Group g = Fresh(Mode::Absolute);
  dyninput::TypeText(g, "@5,3");
  CHECK(g.f[0].text == "5");
  CHECK(g.f[1].text == "3");
  CHECK(g.f[0].locked);
  CHECK(g.f[1].locked);

  // A bare `@` leaves the box tracking the cursor (unlocked), ready for the number.
  g = Fresh(Mode::Absolute);
  dyninput::TypeText(g, "@");
  CHECK(g.f[0].text.empty());
  CHECK_FALSE(g.f[0].locked);

  // A comma moves on; a second one asks for a Z even in plan view rather than dropping it.
  g = Fresh(Mode::Absolute);
  dyninput::TypeText(g, "1,2,3");
  CHECK(g.zRevealed);
  CHECK(dyninput::FieldCount(g) == 3);
  CHECK(g.f[2].text == "3");
}

TEST_CASE("dyninput: Backspace on the mode character reverts, keeping what was typed", "[req354][dyninput]") {
  dyninput::Group g = Fresh(Mode::Absolute);
  dyninput::TypeText(g, "@");
  REQUIRE(dyninput::BackspaceAtEmpty(g, 0));
  CHECK(g.mode == Mode::Absolute);
  CHECK(dyninput::LabelLine(g) == "X Y");

  // `<` after a distance: Backspace in the empty Angle box goes back to X / Y with the 12 kept.
  g = Fresh(Mode::Absolute);
  dyninput::TypeText(g, "12<");
  REQUIRE(dyninput::BackspaceAtEmpty(g, 1));
  CHECK(g.mode == Mode::Absolute);
  CHECK(g.f[0].text == "12");
  CHECK(g.f[0].locked);
  CHECK_FALSE(g.f[1].locked);
  CHECK(g.focus == 0);

  // A value typed after `@` (reached by Tab) survives the revert: `@` changes no layout.
  g = Fresh(Mode::Absolute);
  dyninput::TypeText(g, "@");
  g.focus = 1;
  dyninput::TypeText(g, "7");
  REQUIRE(dyninput::BackspaceAtEmpty(g, 0));
  CHECK(g.f[1].text == "7");

  // Nothing to take back: Backspace changes nothing.
  g = Fresh(Mode::Absolute);
  CHECK_FALSE(dyninput::BackspaceAtEmpty(g, 0));
  CHECK(g.mode == Mode::Absolute);
}

TEST_CASE("dyninput: an anchored prompt (Distance < Angle) re-lays on @ and on a comma", "[req354][dyninput]") {
  dyninput::Group g = Fresh(Mode::RelativePolar);
  CHECK(dyninput::LabelLine(g) == "@ Distance < Angle");

  dyninput::TypeText(g, "@");
  CHECK(g.mode == Mode::Relative);  // issue #564 section 5: `@` in LINE shows ΔX / ΔY
  REQUIRE(dyninput::BackspaceAtEmpty(g, 0));
  CHECK(g.mode == Mode::RelativePolar);

  // D-2026-09-28-j: `5,5` in the Distance box is the coordinate 5,5, as on the command line.
  g = Fresh(Mode::RelativePolar);
  dyninput::TypeText(g, "5,5");
  CHECK(g.mode == Mode::Absolute);
  CHECK(g.f[0].text == "5");
  CHECK(g.f[1].text == "5");
  // ... and after `@`, the relative pair.
  g = Fresh(Mode::Absolute);
  dyninput::TypeText(g, "@4<");
  dyninput::BackspaceAtEmpty(g, 1);
  CHECK(g.mode == Mode::Relative);
}

TEST_CASE("dyninput: Compose submits what the labels say", "[req354][dyninput]") {
  dyninput::Live live;
  live.v[0] = 100.0;
  live.v[1] = 200.0;
  live.v[2] = 0.0;

  dyninput::Group g = Fresh(Mode::Absolute);
  CHECK(dyninput::Compose(g, live).empty());  // nothing typed: Enter answers the prompt's default

  dyninput::TypeText(g, "5,6");
  CHECK(dyninput::Compose(g, live) == "5,6");

  g = Fresh(Mode::Absolute);
  dyninput::TypeText(g, "5");  // Y left live
  CHECK(dyninput::Compose(g, live) == "5,200.000000");

  g = Fresh(Mode::Absolute, true);
  dyninput::TypeText(g, "@5,6,7");
  CHECK(dyninput::Compose(g, live) == "@5,6,7");

  g = Fresh(Mode::Absolute, true);
  dyninput::TypeText(g, "5,6");  // Z untouched: not sent, the command's work plane stands
  CHECK(dyninput::Compose(g, live) == "5,6");

  // Polar: the angle box is a bearing (clockwise from north), resolved here — the command line
  // has no polar grammar. 90 = east.
  g = Fresh(Mode::Absolute);
  dyninput::TypeText(g, "10<90");
  CHECK(dyninput::Compose(g, live) == "10.000000,0.000000");
  g = Fresh(Mode::Absolute);
  dyninput::TypeText(g, "@10<0");
  CHECK(dyninput::Compose(g, live) == "@0.000000,10.000000");

  // A keyword in the first box is an answer to the prompt.
  g = Fresh(Mode::RelativePolar);
  dyninput::TypeText(g, "C");
  CHECK(dyninput::Compose(g, live) == "C");

  // PIPERUN's compass owns the direction: a distance alone goes bare.
  g = Fresh(Mode::RelativePolar);
  dyninput::TypeText(g, "12");
  CHECK(dyninput::Compose(g, live, /*directDistance=*/true) == "12");

  // An unreadable box is sent as typed, for the command to refuse by name.
  g = Fresh(Mode::Absolute);
  dyninput::TypeText(g, "5<x9");
  CHECK(dyninput::Compose(g, live) == "5<x9");
}

TEST_CASE("dyninput: the prompt decides the opening layout, base and Z box", "[req354][dyninput]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartLineCommand(st, log);
  CadDynInputPrompt p = CadDynInputPromptFor(st);
  CHECK(p.pointEntry);
  CHECK(p.initialMode == Mode::Absolute);
  CHECK_FALSE(p.haveBase);
  CHECK_FALSE(p.showZ);  // plan view: X / Y only (issue #564 Q3)

  st.viewportAzimuthDeg = -45.f;
  st.viewportElevationDeg = 35.f;
  CHECK_FALSE(CadViewIsPlanToActiveUcs(st));
  CHECK(CadDynInputPromptFor(st).showZ);
  st.viewportAzimuthDeg = 0.f;
  st.viewportElevationDeg = 90.f;
  CHECK(CadViewIsPlanToActiveUcs(st));

  Submit(st, log, "1,2");
  p = CadDynInputPromptFor(st);
  CHECK(p.initialMode == Mode::RelativePolar);
  REQUIRE(p.haveBase);
  CHECK(p.baseWorld.x == Approx(1.0));
  CHECK(p.baseWorld.y == Approx(2.0));
  const dyninput::Live live = CadDynInputLive(st, p, Mode::Relative, 4.0, 6.0, 0.0);
  CHECK(live.v[0] == Approx(3.0));
  CHECK(live.v[1] == Approx(4.0));
  const dyninput::Live pl = CadDynInputLive(st, p, Mode::RelativePolar, 4.0, 6.0, 0.0);
  CHECK(pl.v[0] == Approx(5.0));

  AppCommandState pr;
  StartPipeRunCommand(pr, log);
  CHECK_FALSE(CadDynInputPromptFor(pr).pointEntry);  // the size prompt is not a point
  REQUIRE(HandlePipeRunTextInput("4in", pr, log));
  REQUIRE(HandlePipeRunTextInput("", pr, log));
  CHECK(CadDynInputPromptFor(pr).pointEntry);
  REQUIRE(HandlePipeRunTextInput("0,0,0", pr, log));
  p = CadDynInputPromptFor(pr);
  CHECK(p.initialMode == Mode::RelativePolar);
  CHECK(p.directDistance == pr.pipeRunCompassOn);
}

TEST_CASE("typed Z: every point prompt reads x,y,z and @dx,dy,dz", "[req354][parser]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartLineCommand(st, log);
  Submit(st, log, "0,0,5");
  Submit(st, log, "10,0,5");
  REQUIRE(st.userLinesFlat.size() == 6);
  CHECK(st.userLinesFlat[2] == Approx(5.0));
  CHECK(st.userLinesFlat[5] == Approx(5.0));

  // Relative dz is measured from the previous point, not from the work plane.
  Submit(st, log, "@0,10,2");
  REQUIRE(st.userLinesFlat.size() == 12);
  CHECK(st.userLinesFlat[9] == Approx(10.0));
  CHECK(st.userLinesFlat[10] == Approx(10.0));
  CHECK(st.userLinesFlat[11] == Approx(7.0));

  // A point with no Z after a typed one is back on the work plane — the typed Z does not linger.
  Submit(st, log, "20,10");
  REQUIRE(st.userLinesFlat.size() == 18);
  CHECK(st.userLinesFlat[17] == Approx(0.0));

  float lx = 0.f, ly = 0.f;
  CHECK_FALSE(ParseStoragePoint(st, "1,2,3,4", &lx, &ly, false, 0.f, 0.f));  // too many
  CHECK_FALSE(ParseStoragePoint(st, "1,2,z", &lx, &ly, false, 0.f, 0.f));
}

TEST_CASE("PIPERUN takes @dx,dy,dz from its last vertex", "[req354][piperun]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));

  // A first point has nothing to be relative to: refused by name, the prompt stands.
  REQUIRE(HandlePipeRunTextInput("@1,2,3", st, log));
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitFirstPoint);
  CHECK(log.back().find("needs a previous point") != std::string::npos);

  REQUIRE(HandlePipeRunTextInput("1,2,3", st, log));
  REQUIRE(HandlePipeRunTextInput("@10,0,4", st, log));
  REQUIRE(st.pipeRunDraftVerts.size() == 6);
  CHECK(st.pipeRunDraftVerts[3] == Approx(11.0));
  CHECK(st.pipeRunDraftVerts[4] == Approx(2.0));
  CHECK(st.pipeRunDraftVerts[5] == Approx(7.0));
}

TEST_CASE("dyninput: PIPERUN driven through the boxes lands where the labels say", "[req354][piperun]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));
  REQUIRE(HandlePipeRunTextInput("0,50,0", st, log));

  const CadDynInputPrompt p = CadDynInputPromptFor(st);
  dyninput::Group g;
  dyninput::Reset(g, p.initialMode, p.showZ);
  dyninput::TypeText(g, "@20,0");
  REQUIRE(dyninput::LabelLine(g) == "@ \xce\x94X \xce\x94Y");
  const std::string text = dyninput::Compose(g, CadDynInputLive(st, p, g.mode, 0.0, 0.0, 0.0), p.directDistance);
  CHECK(text == "@20,0");
  REQUIRE(HandlePipeRunTextInput(text, st, log));
  REQUIRE(st.pipeRunDraftVerts.size() == 6);
  CHECK(st.pipeRunDraftVerts[3] == Approx(20.0));
  CHECK(st.pipeRunDraftVerts[4] == Approx(50.0));
}

TEST_CASE("dyninput: under a rotated UCS an untouched box commits the number it shows", "[req354][dyninput][ucs]") {
  AppCommandState st;
  std::vector<std::string> log;
  ucs::Ucs u;
  u.origin = {100.0, 50.0, 0.0};
  u = ucs::RotatedAboutZ(u, 30.0);
  st.activeUcs = u;
  StartLineCommand(st, log);
  const CadDynInputPrompt p = CadDynInputPromptFor(st);
  // The cursor at world (110, 60): the X / Y boxes read it in the UCS, the frame a typed point is
  // read in (REQ-154) — so typing the X alone and leaving Y live puts the point under the cursor.
  const dyninput::Live live = CadDynInputLive(st, p, Mode::Absolute, 110.0, 60.0, 0.0);
  const ray3d::Vec3 inUcs = ucs::WorldToUcs(u, {110.0, 60.0, 0.0});
  CHECK(live.v[0] == Approx(inUcs.x));
  CHECK(live.v[1] == Approx(inUcs.y));

  dyninput::Group g;
  dyninput::Reset(g, p.initialMode, p.showZ);
  char xText[32];
  std::snprintf(xText, sizeof xText, "%.6f", inUcs.x);
  dyninput::TypeText(g, xText);
  float lx = 0.f, ly = 0.f;
  REQUIRE(ParseStoragePoint(st, dyninput::Compose(g, live), &lx, &ly, false, 0.f, 0.f));
  CHECK(lx == Approx(110.0).margin(1e-3));
  CHECK(ly == Approx(60.0).margin(1e-3));
}

TEST_CASE("typed Z: an earlier typed Z is not the base of a later @dx,dy,dz", "[req354][parser]") {
  // Code review on #578: with no caller base Z, `@` measures dz from the work plane — not from a Z
  // typed for the previous point, which the headless driver (no frames) would otherwise keep.
  AppCommandState st;
  float lx = 0.f, ly = 0.f;
  double wz = 0.0;
  REQUIRE(ParseStoragePointZ(st, "0,0,10", &lx, &ly, &wz, false, 0.f, 0.f));
  CHECK(wz == Approx(10.0));
  REQUIRE(ParseStoragePointZ(st, "@5,0,2", &lx, &ly, &wz, true, 0.f, 0.f));
  CHECK(wz == Approx(2.0));
  CHECK(CadWorkPlaneElevation(st) == Approx(2.f));
}

TEST_CASE("PIPERUN reads a typed x,y,z wholly in the active UCS", "[req354][piperun][ucs]") {
  // Code review on #578: the Z box shows a UCS Z, so the Z typed into it is one. Under a Front-style
  // UCS (UCS Y = world Z) the old world-elevation reading dropped the typed Y.
  AppCommandState st;
  std::vector<std::string> log;
  st.activeUcs = ucs::RotatedAboutX(ucs::Ucs{}, 90.0);
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));
  REQUIRE(HandlePipeRunTextInput("1,2,3", st, log));
  REQUIRE(st.pipeRunDraftVerts.size() == 3);
  const ray3d::Vec3 want = ucs::UcsToWorld(st.activeUcs, {1.0, 2.0, 3.0});
  CHECK(st.pipeRunDraftVerts[0] == Approx(want.x).margin(1e-4));
  CHECK(st.pipeRunDraftVerts[1] == Approx(want.y).margin(1e-4));
  CHECK(st.pipeRunDraftVerts[2] == Approx(want.z).margin(1e-4));
}
