#pragma once
// The point-entry dynamic input's MODEL (REQ-024, REQ-354 / GitHub issue #564 §5): which boxes a
// point prompt shows, what each is labelled, what typing `@` / `<` / `,` does to them, and the one
// command-line string they submit.
//
// It lives in the command layer, with no ImGui, for one reason: the UI and the headless driver must
// drive the SAME code. The labels on screen and the text the command parses are both derived from
// one `dyninput::Group`, so a transcript that types `@5,3` and checks both the labels and the point
// that lands is checking what the user gets — the two cannot drift apart.

#include "util/ray3d.hpp"

#include <array>
#include <string>
#include <vector>

struct AppCommandState;

namespace dyninput {

/// What the boxes are collecting. The issue's table, one row each.
enum class Mode : unsigned char {
  Absolute,       ///< X  Y  (Z)
  Relative,       ///< @  ΔX  ΔY  (ΔZ) — from the prompt's base point
  Polar,          ///< Distance < Angle — from the frame origin
  RelativePolar,  ///< @  Distance < Angle — from the prompt's base point
};

[[nodiscard]] inline bool IsRelative(Mode m) { return m == Mode::Relative || m == Mode::RelativePolar; }
[[nodiscard]] inline bool IsPolar(Mode m) { return m == Mode::Polar || m == Mode::RelativePolar; }

struct Field {
  std::string text;
  /// Typed by the user (REQ-024 lock-on-edit). An unlocked field shows, and commits, the live
  /// cursor reading.
  bool locked = false;
};

/// A mode change the user can take back with Backspace (the issue: "backspacing the mode character
/// reverts the fields to the previous mode without losing what was already typed").
struct Transition {
  Mode prev = Mode::Absolute;
  std::array<Field, 3> prevFields{};
  /// The field the keystroke left the user in; Backspace there, with nothing typed, undoes it.
  int landSlot = 0;
  /// Whether `@` had been typed before this change (restored with the mode).
  bool prevAtTyped = false;
  /// The boxes changed meaning (X/Y ↔ Distance/Angle). Fields from the landing slot on are restored
  /// from before the change on revert; without a layout change every field is kept as it is.
  bool layoutChanged = false;
};

struct Group {
  Mode initial = Mode::Absolute;
  Mode mode = Mode::Absolute;
  bool showZ = false;
  /// A second comma typed into Y asks for a Z the view did not offer; the Z field then stays up for
  /// the rest of this prompt rather than the value being dropped.
  bool zRevealed = false;
  /// The user typed `@`. Distinguishes LINE's own Distance < Angle (relative by construction, where
  /// `@` means "switch to ΔX / ΔY" and a comma means absolute X / Y) from one the user asked for.
  bool atTyped = false;
  std::array<Field, 3> f{};
  std::vector<Transition> history;
  /// The field the user is typing into.
  int focus = 0;
  /// One-shot request for the UI: move keyboard focus to this field (-1 = none).
  int requestFocus = -1;
};

void Reset(Group& g, Mode initial, bool showZ);

/// 2 in the polar modes; 2 or 3 in the cartesian ones, as `showZ` says.
[[nodiscard]] int FieldCount(const Group& g);

/// The label a field carries in the current mode: X / Y / Z, ΔX / ΔY / ΔZ (UTF-8), Distance / Angle.
[[nodiscard]] std::string Label(const Group& g, int slot);

/// Labels of every field shown, space-separated, with `@` in front in a relative mode and `<`
/// between the polar pair — what the transcript `EXPECT DYNLABELS` compares.
[[nodiscard]] std::string LabelLine(const Group& g);

/// The user's text for \p slot after an edit. A leading `@` switches to the relative form, a `<`
/// to Distance < Angle, and a `,` moves on to the next field (or, in the Distance box, to X / Y —
/// D-2026-09-28-j); the mode character is consumed, never left inside a number. Anything else
/// locks the field to the text; an emptied field goes back to tracking the cursor.
void EditText(Group& g, int slot, const std::string& text);

/// Typing \p text into the focused field one character at a time, exactly as the UI would — what
/// type-to-start and the headless driver use. A live (unlocked) field is replaced by the first
/// character, as its select-all makes it be on screen.
void TypeText(Group& g, const std::string& text);

/// Backspace in \p slot while it holds nothing typed. Undoes the mode change that landed the user
/// there, if any; returns false (and changes nothing) otherwise.
bool BackspaceAtEmpty(Group& g, int slot);

/// Live cursor readings for the current mode, in field order: x, y, z (absolute, in the frame a
/// typed point is read in); dx, dy, dz (relative, along that frame's axes); distance and the
/// internal math angle in degrees (polar). \p haveBase false leaves the relative readings at 0.
struct Live {
  double v[3] = {0.0, 0.0, 0.0};
};

/// The text the group submits: `x,y[,z]`, `@dx,dy[,dz]`, or a polar pair resolved to one of those
/// (the command line has no polar grammar). Empty when nothing is typed — Enter then answers the
/// prompt's default (D-2026-09-24-b). A non-numeric first field (a keyword: `C`, `U`, `END`, `2P`)
/// is submitted as typed. \p directDistance: a distance typed with the angle left live is sent
/// bare, for a prompt whose command owns the direction (PIPERUN's compass).
[[nodiscard]] std::string Compose(const Group& g, const Live& live, bool directDistance = false);

}  // namespace dyninput

/// What the dynamic input shows at the active prompt.
struct CadDynInputPrompt {
  /// The prompt asks for a point (REQ-024's coordinate field group).
  bool pointEntry = false;
  /// REQ-154's UCS directional prompts: their own distance/angle pair, unchanged by REQ-354.
  bool ucsPolar = false;
  ray3d::Vec3 ucsPolarBase{};
  /// The layout the prompt opens in: Distance < Angle after an anchor (LINE's second point and
  /// on), X / Y otherwise.
  dyninput::Mode initialMode = dyninput::Mode::Absolute;
  /// The point a relative entry is measured from, in TRUE world coordinates, when the command has
  /// one (a first point has none, and its command refuses `@`).
  bool haveBase = false;
  ray3d::Vec3 baseWorld{};
  /// Issue #564 Q3: a Z field whenever the view is not plan to the current UCS (model space only).
  bool showZ = false;
  /// A bare distance is the command's own direct-distance entry (PIPERUN's compass, REQ-346).
  bool directDistance = false;
};

/// True when the active prompt takes a coordinate point. The second of the two lists a
/// point-picking command has to appear in (ViewportClickRouteFor is the first).
[[nodiscard]] bool CadCommandExpectsPointEntry(const AppCommandState& cmd);

/// REQ-154: the UCS directional prompts measure from a base (the new frame's origin, or the first
/// pick of a two-point angle). False for every other prompt.
[[nodiscard]] bool CadUcsPolarPromptBase(const AppCommandState& cmd, ray3d::Vec3* baseWorld);

/// True when the view looks straight down the active UCS's Z axis — plan to the current UCS.
[[nodiscard]] bool CadViewIsPlanToActiveUcs(const AppCommandState& cmd);

[[nodiscard]] CadDynInputPrompt CadDynInputPromptFor(const AppCommandState& cmd);

/// The live readings for \p mode with the cursor at TRUE-world (\p wx, \p wy, \p wz).
[[nodiscard]] dyninput::Live CadDynInputLive(const AppCommandState& cmd, const CadDynInputPrompt& prompt,
                                             dyninput::Mode mode, double wx, double wy, double wz);
