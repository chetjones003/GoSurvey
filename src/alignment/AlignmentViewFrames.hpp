#pragma once

#include "alignment/Alignment.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace alignment {

/// Where the alignment sits across a view frame (REQ-517.4). Persisted as its integer value; never reorder.
enum class ViewFramePlacement : unsigned char { Centred = 0, Left = 1, Right = 2 };

/// The inputs a view frame group is made from (REQ-517). Stored with the group so that it can regenerate whenever
/// the alignment changes (REQ-517.8). Stations are INTERNAL stations (REQ-483), so station equations do not move frames.
struct ViewFrameSettings {
  double startStation = 0.0;
  double endStation = 0.0;
  double sheetWidthIn = 0.0;   ///< Paper width in inches; the frame's length along the alignment is this times the scale.
  double sheetHeightIn = 0.0;  ///< Paper height in inches; the frame's width across the alignment is this times the scale.
  double groundPerInch = 0.0;  ///< Drawing units per paper inch, from the named scale (REQ-517.3).
  double overlap = 0.0;        ///< Drawing units shared by consecutive frames, measured along the alignment (REQ-517.5).
  ViewFramePlacement placement = ViewFramePlacement::Centred;
  std::string sheetName;       ///< Paper size name, for reports and the layout created later (REQ-518).
  std::string scaleName;       ///< Named scale, for reports and the layout created later (REQ-518).
};

/// One generated frame (REQ-517.7). Corners are in plan, drawing-local, in counter-clockwise order.
struct ViewFrame {
  std::string name;            ///< `VF-1`, `VF-2`, ... within its group.
  double startStation = 0.0;   ///< Internal station where the frame begins along the alignment.
  double endStation = 0.0;     ///< Internal station where it ends. The last frame may pass the alignment's end.
  double centreStation = 0.0;  ///< Station the frame is oriented on (REQ-517.4).
  double length = 0.0;         ///< Along the alignment: sheet width x scale.
  double width = 0.0;          ///< Across the alignment: sheet height x scale.
  Point2 centre;
  Point2 corner[4];
};

/// A stored view frame group (REQ-517.7). Frames are derived from `settings`, so they are rebuilt, never edited.
struct ViewFrameGroup {
  std::uint64_t id = 0;
  std::uint64_t alignmentId = 0;  ///< The alignment the frames follow.
  std::string name;               ///< Named after the alignment.
  ViewFrameSettings settings;
  std::vector<ViewFrame> frames;
};

/// Frames per group at most. A guard against a tiny overlap or a huge range producing an unusable number of sheets.
inline constexpr int kMaxViewFramesPerGroup = 2000;

/// Largest frame length or width accepted, in drawing units. Keeps derived geometry finite and well inside double range.
inline constexpr double kMaxFrameExtent = 1.0e9;

/// Why these settings cannot make frames on this alignment, or an empty string when they can (REQ-517.6). The
/// message is written to the command log as it stands, so it names the value that failed.
[[nodiscard]] std::string ViewFrameSettingsRefusal(const Alignment& a, const ViewFrameSettings& s);

/// REQ-517.6a: the internal station a DISPLAYED station names on `a`. Exactly one match gives true and `internal`. A
/// station in a repeated range (a backward equation) or in a gap (a forward equation) gives false and `why`.
[[nodiscard]] bool ResolveDisplayedStation(const Alignment& a, double displayed, double* internal, std::string* why);

/// The frames for `s` on `a` (REQ-517.5). Returns an empty vector when ViewFrameSettingsRefusal is not empty.
[[nodiscard]] std::vector<ViewFrame> ComputeViewFrames(const Alignment& a, const ViewFrameSettings& s);

/// Rebuilds `group.frames` from its settings on the alignment `a` (REQ-517.8). Returns the refusal text when the
/// stored settings no longer fit the alignment; the group then has no frames until they fit again.
[[nodiscard]] std::string RegenerateViewFrameGroup(const Alignment& a, ViewFrameGroup& group);

/// Keeps `groups` in step with `alignments` (REQ-517.8, REQ-517.9). A group whose alignment is gone is removed. Every
/// other group regenerates from its settings. A group whose settings no longer fit its alignment shows no frames and
/// gets a line in `notes` naming the group and the reason. Called after every alignment change and on load.
void SyncViewFrameGroups(const std::vector<Alignment>& alignments, std::vector<ViewFrameGroup>& groups,
                         std::vector<std::string>* notes);

}  // namespace alignment
