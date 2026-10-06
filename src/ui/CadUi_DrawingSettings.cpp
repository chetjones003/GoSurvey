// Drawing Settings window (REQ-357, GitHub issue #582 increment 1): the drawing's units, scale and
// settings. The window edits a staged copy; only OK and Apply write it to the drawing, through
// ApplyDrawingSettings (one undo step). The Zone group (REQ-358) picks the drawing's coordinate
// system from the CS-MAP catalogue through src/geo/. The Transformation tab (REQ-360) relates local
// and grid coordinates; its pick buttons hide the window while a DRAWINGSETTINGS pick runs in the
// drawing and bring it back with the staged values. The Object Layers tab (REQ-361) sets the layer
// each kind of new object is created on.

#include "CadUi.hpp"
#include "CadUiHelpers.hpp"
#include "ProjectSettings.hpp"
#include "geo/CoordinateSystems.hpp"
#include "util/PlotScales.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

constexpr const char* kPopupId = "###GoSurveyDrawingSettings";
constexpr const char* kNotImplemented = "Not implemented yet.";
constexpr const char* kTagEnforced = "Enforced by project";
constexpr const char* kTagInherited = "Inherited from project";
constexpr const char* kTagOverridden = "Overridden";

/// The read-only details of the staged zone (REQ-358 item 2).
struct ZoneDetails {
  std::string description, projection, datum;
};

ZoneDetails DetailsOf(const std::string& code) {
  if (code.empty())
    return {geo::kNoZoneCategory, "Unknown projection", "Unknown Datum"};
  if (const auto info = geo::FindCoordinateSystem(code))
    return {info->description, info->projection, info->datum};
  // Kept, never dropped (REQ-358 item 5): the drawing is still geolocated.
  return {code + " (unknown in this dictionary)", "Unknown projection", "Unknown Datum"};
}

struct StagedDrawingSettings {
  int             insUnits = 2;
  float           modelUnitsPerPlottedInch = 50.f;
  bool            customScale = false;
  char            customScaleText[32] = {};
  DrawingSettings settings;
  // Zone group (REQ-358): the category shown, the systems it lists (code + description, loaded once
  // per category change, not per frame), the code field and its last refusal.
  std::string              category;  ///< Empty = No Datum, No Projection (or an unknown code).
  std::vector<std::string> systemCodes;
  std::vector<std::string> systemLabels;
  char                     codeText[64] = {};
  std::string              codeError;
  ZoneDetails              details;  ///< Looked up once per selection, not every frame.
  // Transformation tab (REQ-360): the resolved factors, recomputed only when a staged value changes
  // (they call CS-MAP), and a pick that was refused.
  DrawingTransformFactors factors;
  DrawingSettings         factorsFor;
  int                     factorsInsUnits = -1;
  std::string             transformMessage;
  int                     drawingIdx = -1;             ///< The drawing tab this was seeded from.
  bool                    selectTransformation = false;  ///< Come back on the tab after a pick.
  // REQ-375. projectMode: the window edits the project's settings, not a drawing's. proj: the project
  // the drawing being edited belongs to (null for a standalone drawing and in projectMode); refreshed
  // every frame. seedRef holds the values the window was opened with, to tell what the user changed.
  bool                   projectMode = false;
  const ProjectSettings* proj = nullptr;
  std::string            projName;
  ProjectSettings        seedRef;
};

const DrawingTransformFactors& StagedFactors(StagedDrawingSettings& s) {
  if (s.factorsInsUnits != s.insUnits || s.factorsFor != s.settings) {
    s.factors = ResolveDrawingTransform(s.settings, s.insUnits);
    s.factorsFor = s.settings;
    s.factorsInsUnits = s.insUnits;
  }
  return s.factors;
}

void LoadCategory(StagedDrawingSettings& s, const std::string& category) {
  s.category = category;
  s.systemCodes = category.empty() ? std::vector<std::string>{} : geo::CoordinateSystemsIn(category);
  s.systemLabels.clear();
  for (const std::string& code : s.systemCodes) {
    const auto info = geo::FindCoordinateSystem(code);
    s.systemLabels.push_back(info && !info->description.empty() ? info->description : code);
  }
}

/// Show \p code (empty = no zone) in the Zone group: its category, its list and the code field.
void SelectZone(StagedDrawingSettings& s, const std::string& code) {
  s.settings.zoneCode = code;
  s.details = DetailsOf(code);
  s.codeError.clear();
  std::snprintf(s.codeText, sizeof(s.codeText), "%s", code.empty() ? "." : code.c_str());
  // Keep the category being browsed when it lists the code (a code can be in several categories).
  for (const std::string& c : s.systemCodes)
    if (!code.empty() && c == code)
      return;
  LoadCategory(s, code.empty() ? std::string() : geo::CategoryOf(code));
}

void SeedFromDrawing(const AppCommandState& cmd, StagedDrawingSettings& s) {
  s.insUnits = cmd.drawingInsUnits;
  s.modelUnitsPerPlottedInch = cmd.modelUnitsPerPlottedInch;
  s.customScale = PlotScaleChoiceIndex(PlotScaleChoicesFor(s.insUnits), s.modelUnitsPerPlottedInch) < 0;
  std::snprintf(s.customScaleText, sizeof(s.customScaleText), "%g",
                static_cast<double>(s.modelUnitsPerPlottedInch));
  s.settings = cmd.drawingSettings;
  s.projectMode = false;
  s.seedRef = ProjectSettings{};
  s.seedRef.hasDefaults = true;
  s.seedRef.defaults = cmd.drawingSettings;
  s.seedRef.plotScale = cmd.modelUnitsPerPlottedInch;
  s.factorsInsUnits = -1;  // resolve again
  s.transformMessage.clear();
  s.drawingIdx = cmd.activeDrawingIdx;
  // The category list is kept: after Apply the user stays in the category they were browsing
  // (SelectZone reloads it only when it does not list the drawing's zone).
  SelectZone(s, s.settings.zoneCode);
}

/// Show a project's settings (REQ-375): its unit, zone and defaults, or the standard defaults for a
/// project that has none yet.
void SeedFromProject(const AppCommandState& cmd, const ProjectSettings& p, StagedDrawingSettings& s) {
  s.insUnits = p.insUnits >= 0 ? p.insUnits : 2;
  s.modelUnitsPerPlottedInch = p.hasDefaults ? p.plotScale : 50.f;
  s.customScale = PlotScaleChoiceIndex(PlotScaleChoicesFor(s.insUnits), s.modelUnitsPerPlottedInch) < 0;
  std::snprintf(s.customScaleText, sizeof(s.customScaleText), "%g",
                static_cast<double>(s.modelUnitsPerPlottedInch));
  s.settings = p.hasDefaults ? p.defaults : DrawingSettings{};
  s.settings.overridden = 0;
  s.settings.zoneCode = p.zoneCode;
  s.projectMode = true;
  s.proj = nullptr;
  s.factorsInsUnits = -1;
  s.transformMessage.clear();
  s.drawingIdx = cmd.activeDrawingIdx;
  SelectZone(s, s.settings.zoneCode);
}

/// The staged scale, or false when Custom holds something that is not a positive number.
bool StagedScale(const StagedDrawingSettings& s, float* out) {
  if (!s.customScale) {
    *out = s.modelUnitsPerPlottedInch;
    return true;
  }
  char* end = nullptr;
  const double v = std::strtod(s.customScaleText, &end);
  while (end != nullptr && *end == ' ')
    ++end;
  if (end == s.customScaleText || end == nullptr || *end != '\0' || !std::isfinite(v) || v <= 0.0)
    return false;
  *out = static_cast<float>(v);
  return true;
}

constexpr const char* kLabels[] = {"Drawing units:", "Angular units:", "Imperial to Metric conversion:",
                                   "Scale:", "Custom scale:", "Categories:",
                                   "Available coordinate systems:", "Selected coordinate system code:",
                                   "Description:", "Projection:", "Datum:"};
constexpr const char* kAngular[] = {"Degrees", "Radians", "Grads"};
constexpr const char* kFoot[] = {"US Survey Foot (1 m = 39.37 in)", "International Foot (1 ft = 0.3048 m)"};
// The widest description the Zone fields are sized for; a longer one is clipped in its field.
constexpr const char* kZoneWidthSample = "HARN (HPGN datum) Texas State Planes, Central Zone, Meter";
constexpr const char* kCheckScaleInserted = "Scale objects inserted from other drawings";
constexpr const char* kCheckSetVariables = "Set drawing variables to match";
constexpr const char* kCustomScaleError = "The custom scale must be a positive number.";
constexpr const char* kTransformError = "Check the Transformation tab.";

// Object Layers tab (REQ-361), rows in ObjectLayerKind order.
constexpr const char* kObjectLayerNames[kObjectLayerKindCount] = {
    "Survey point", "Survey point label", "TIN surface", "Feature line",
    "Pipe run",     "Pipe fitting",       "Solid",       "Table"};
constexpr const char* kModifiers[] = {"None", "Prefix", "Suffix"};
constexpr const char* kObjectLayersInfo =
    "Enter a single * (asterisk) in the value field to include the object name as the prefix or suffix "
    "value in a layer name.";
constexpr const char* kCheckDisplayComponents = "Immediate and independent layer on/off control of display components";
constexpr const char* kObjectLayersError = "Check the Object Layers tab.";

// Transformation tab (REQ-360).
constexpr const char* kTxLabels[] = {"Zone description:", "Elevation:", "Spheroid radius (m):", "Computation:",
                                     "Grid scale factor:", "Combined factor:", "Angle:", "To north:",
                                     "Local azimuth:", "Grid azimuth:", "Point number:", "Local Northing:",
                                     "Local Easting:", "Grid Northing:", "Grid Easting:"};
constexpr const char* kComputation[] = {"Reference Point", "User Defined"};
constexpr const char* kAngleKinds[] = {"To north", "Azimuth"};
// The widest value a Transformation field shows: a state-plane northing in feet, or an angle.
constexpr const char* kTxFieldSample = "-10,000,000.00000000 grad";
constexpr const char* kCheckApplyTransform = "Apply transform settings";
constexpr const char* kCheckSeaLevel = "Apply sea level scale factor";
constexpr const char* kRadioRotationPoint = "Rotation point";
constexpr const char* kRadioGridAngle = "Specify grid rotation angle";

/// Every size in the window, measured from the current font and style — never fixed pixels, so the
/// window fits its text at any UI font size and does not change size while it is open.
struct DialogLayout {
  float  labelW = 0.f;  ///< Label column, the fields start here.
  float  fieldW = 0.f;  ///< Every combo and the Custom scale field.
  float  btnW = 0.f;
  float  tagX = 0.f;    ///< Where the "inherited / overridden / enforced" tag starts (REQ-375).
  float  tagW = 0.f;
  // Transformation tab: two columns of label + field.
  float  txLabelW = 0.f;
  float  txFieldW = 0.f;
  float  txColW = 0.f;
  float  pickW = 0.f;   ///< The "Pick" button beside a field.
  // Object Layers tab: the fixed columns (Layer stretches).
  float  olObjectW = 0.f;
  float  olModifierW = 0.f;
  float  olValueW = 0.f;
  ImVec2 size;          ///< The whole window, title bar included.
};

float MaxTextWidth(std::initializer_list<const char*> texts) {
  float w = 0.f;
  for (const char* s : texts)
    w = (std::max)(w, ImGui::CalcTextSize(s).x);
  return w;
}

DialogLayout MeasureLayout() {
  const ImGuiStyle& st = ImGui::GetStyle();
  DialogLayout L;

  float label = 0.f;
  for (const char* s : kLabels)
    label = (std::max)(label, ImGui::CalcTextSize(s).x);
  L.labelW = label + st.ItemSpacing.x * 2.f;

  // The widest thing any field can show: unit names, angular units, foot definitions, every scale in
  // both lists, "Custom" and the zone placeholder.
  float option = MaxTextWidth({kFoot[0], kFoot[1], geo::kNoZoneCategory, "Custom", kZoneWidthSample});
  for (int i = 0; i < kDrawingUnitCount; ++i)
    option = (std::max)(option, ImGui::CalcTextSize(kDrawingUnitNames[i]).x);
  for (const char* s : kAngular)
    option = (std::max)(option, ImGui::CalcTextSize(s).x);
  for (int units : {2, 6})
    for (const PlotScaleChoice& c : PlotScaleChoicesFor(units))
      option = (std::max)(option, ImGui::CalcTextSize(c.label.c_str()).x);
  // Text + frame padding on both sides + the arrow button (one frame height square).
  L.fieldW = option + st.FramePadding.x * 2.f + st.ItemInnerSpacing.x + ImGui::GetFrameHeight();

  L.btnW = (std::max)(ImGui::CalcTextSize("Cancel").x + st.FramePadding.x * 4.f, ImGui::GetFontSize() * 5.f);
  const float footerW = MaxTextWidth({kCustomScaleError, kTransformError, kObjectLayersError}) +
                        st.ItemSpacing.x * 2.f +
                        L.btnW * 3.f + st.ItemSpacing.x * 2.f;
  const float checkW = ImGui::GetFrameHeight() + st.ItemInnerSpacing.x +
                       MaxTextWidth({kCheckScaleInserted, kCheckSetVariables});

  // Transformation: label + field per column, the widest of its labels, values and choices.
  float txLabel = 0.f;
  for (const char* s : kTxLabels)
    txLabel = (std::max)(txLabel, ImGui::CalcTextSize(s).x);
  L.txLabelW = txLabel + st.ItemSpacing.x * 2.f;
  L.txFieldW = MaxTextWidth({kTxFieldSample, kComputation[0], kComputation[1]}) + st.FramePadding.x * 2.f +
               st.ItemInnerSpacing.x + ImGui::GetFrameHeight();
  L.pickW = ImGui::CalcTextSize("Pick").x + st.FramePadding.x * 2.f;
  const float radioW = ImGui::GetFrameHeight() + st.ItemInnerSpacing.x +
                       MaxTextWidth({kRadioRotationPoint, kRadioGridAngle, kCheckSeaLevel});
  L.txColW = (std::max)(L.txLabelW + L.txFieldW, radioW);
  const float txW = L.txColW * 2.f + st.ItemSpacing.x * 3.f;

  // Object Layers: icon + the longest object name; the modifier combo; a value like "-*-EXIST".
  float objectName = 0.f;
  for (const char* n : kObjectLayerNames)
    objectName = (std::max)(objectName, ImGui::CalcTextSize(n).x);
  L.olObjectW = ImGui::GetFrameHeight() + st.ItemSpacing.x + objectName + st.CellPadding.x * 2.f;
  L.olModifierW = MaxTextWidth({kModifiers[0], kModifiers[1], kModifiers[2]}) + st.FramePadding.x * 2.f +
                  st.ItemInnerSpacing.x + ImGui::GetFrameHeight();
  L.olValueW = ImGui::CalcTextSize("-*-EXIST").x + st.FramePadding.x * 2.f;

  // REQ-375: a tag after each row of a project drawing — "overridden" and its Reset button, or
  // "inherited", or "Enforced by project". Always reserved so the window never changes size.
  L.tagX = (std::max)(L.labelW + L.fieldW, checkW) + st.ItemSpacing.x * 2.f;
  L.tagW = (std::max)(MaxTextWidth({kTagEnforced, kTagInherited}),
                      ImGui::CalcTextSize(kTagOverridden).x + st.ItemSpacing.x + ImGui::CalcTextSize("Reset").x +
                          st.FramePadding.x * 2.f);
  const float contentW = (std::max)({L.tagX + L.tagW, footerW, txW});

  // Body rows, top to bottom: tab bar, spacing, five field rows, spacing, two checkboxes, spacing,
  // the "Zone" separator, six Zone rows and the Zone message line.
  const float row = ImGui::GetFrameHeightWithSpacing();
  const float gap = st.ItemSpacing.y;
  const float separatorText = ImGui::GetTextLineHeight() + st.SeparatorTextPadding.y * 2.f + gap;
  const float message = ImGui::GetTextLineHeightWithSpacing();
  const float unitsBody = row + gap + 5.f * row + gap + 2.f * row + gap + separatorText + 6.f * row + message;
  // Transformation: tab bar, spacing, zone description, the zone-units line, Apply transform
  // settings, then the taller column (three separators and twelve rows) and the message line.
  const float txBody = row + gap + row + message + row + 3.f * separatorText + 12.f * row + message;
  // Object Layers: tab bar, spacing, header + eight rows, spacing, the info line (two lines at most),
  // the display-components checkbox and the message line.
  const float tableRow = row + st.CellPadding.y * 2.f + 1.f;  // plus the row border
  const float olBody = row + gap + row + 9.f * tableRow + gap + 2.f * message + row + message;
  const float body = (std::max)({unitsBody, txBody, olBody});
  const float footer = gap + 1.f + gap + row;  // separator line, then the buttons
  const float titleBar = ImGui::GetFontSize() + st.FramePadding.y * 2.f;
  L.size = ImVec2(contentW + st.WindowPadding.x * 2.f,
                  titleBar + st.WindowPadding.y * 2.f + body + footer + gap);
  return L;
}

void GreyedWithReason(const char* reason) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", reason);
}

/// The typed code, on Enter or leaving the field: a known code selects its category and system;
/// "." or nothing is No Datum, No Projection; anything else is refused and the selection kept.
void CommitTypedCode(StagedDrawingSettings& s) {
  std::string code = s.codeText;
  const size_t first = code.find_first_not_of(' ');
  code = first == std::string::npos ? std::string() : code.substr(first, code.find_last_not_of(' ') - first + 1);
  if (code.empty() || code == ".") {
    SelectZone(s, {});
    return;
  }
  if (const auto info = geo::FindCoordinateSystem(code)) {
    SelectZone(s, info->code);
    return;
  }
  SelectZone(s, s.settings.zoneCode);  // the field shows the unchanged selection again
  s.codeError = "\"" + code + "\" is not a coordinate system in this dictionary.";
}

float StagedScaleOrCurrent(const StagedDrawingSettings& s) {
  float v = s.modelUnitsPerPlottedInch;
  StagedScale(s, &v);
  return v;
}

/// REQ-375: is \p key an override in this drawing as the window stands? A value the user changed is
/// an override exactly when it differs from the project's; one left alone keeps the drawing's flag
/// (so choosing the project's own value is the same as inheriting it).
bool KeyOverridden(const StagedDrawingSettings& s, ProjectDefaultKey key) {
  if (!s.proj || !s.proj->hasDefaults)
    return false;
  const float v = StagedScaleOrCurrent(s);
  if (DiffersFromProjectDefault(s.seedRef, key, v, s.settings))
    return DiffersFromProjectDefault(*s.proj, key, v, s.settings);
  return s.settings.IsOverridden(key);
}

/// The tag after a row: "Inherited from project", or "Overridden" with Reset to project value. \p x < 0
/// puts it on the current line. Nothing for a standalone drawing or when editing the project itself.
void InheritTag(StagedDrawingSettings& s, ProjectDefaultKey key, float x) {
  if (!s.proj || !s.proj->hasDefaults)
    return;
  if (x < 0.f)
    ImGui::SameLine();
  else
    ImGui::SameLine(x);
  ImGui::AlignTextToFramePadding();
  if (!KeyOverridden(s, key)) {
    ImGui::TextDisabled("%s", kTagInherited);
    return;
  }
  ImGui::TextUnformatted(kTagOverridden);
  ImGui::SameLine();
  ImGui::PushID(static_cast<int>(key));
  if (ImGui::SmallButton("Reset")) {
    float scale = s.modelUnitsPerPlottedInch;
    CopyProjectDefault(*s.proj, key, &scale, &s.settings);
    s.settings.SetOverridden(key, false);
    if (key == ProjectDefaultKey::PlotScale) {
      s.modelUnitsPerPlottedInch = scale;
      s.customScale = PlotScaleChoiceIndex(PlotScaleChoicesFor(s.insUnits), scale) < 0;
      std::snprintf(s.customScaleText, sizeof(s.customScaleText), "%g", static_cast<double>(scale));
    }
  }
  ImGui::PopID();
  ItemHelpTooltip("Reset to the project's value.");
}

void DrawZoneGroup(StagedDrawingSettings& s, const std::function<void(const char*)>& row) {
  const bool loaded = geo::DictionariesLoaded();
  ImGui::BeginDisabled(!loaded);

  // Categories: No Datum, No Projection, then the dictionary's own list (Lat Longs first).
  const std::string catPreview = s.settings.zoneCode.empty() ? std::string(geo::kNoZoneCategory) : s.category;
  row("Categories:");
  if (ImGui::BeginCombo("##ds_category", catPreview.c_str(), ImGuiComboFlags_HeightLarge)) {
    if (ImGui::Selectable(geo::kNoZoneCategory, s.settings.zoneCode.empty()))
      SelectZone(s, {});
    for (const std::string& c : geo::Categories()) {
      if (ImGui::Selectable(c.c_str(), !s.settings.zoneCode.empty() && c == s.category)) {
        LoadCategory(s, c);  // a category is browsed by selecting its first system
        SelectZone(s, s.systemCodes.empty() ? std::string() : s.systemCodes.front());
      }
    }
    ImGui::EndCombo();
  }
  ItemHelpTooltip("The coordinate-system categories of the CS-MAP dictionary.");

  int sysIx = -1;
  for (size_t i = 0; i < s.systemCodes.size(); ++i)
    if (s.systemCodes[i] == s.settings.zoneCode)
      sysIx = static_cast<int>(i);
  const std::string sysPreview = sysIx >= 0 ? s.systemLabels[static_cast<size_t>(sysIx)] : std::string();
  ImGui::BeginDisabled(s.systemCodes.empty());
  row("Available coordinate systems:");
  if (ImGui::BeginCombo("##ds_system", sysPreview.c_str(), ImGuiComboFlags_HeightLarge)) {
    for (size_t i = 0; i < s.systemCodes.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::Selectable(s.systemLabels[i].c_str(), static_cast<int>(i) == sysIx))
        SelectZone(s, s.systemCodes[i]);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", s.systemCodes[i].c_str());
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
  ItemHelpTooltip("Every coordinate system in the selected category.");

  row("Selected coordinate system code:");
  bool commit = ImGui::InputText("##ds_code", s.codeText, sizeof(s.codeText), ImGuiInputTextFlags_EnterReturnsTrue);
  commit = ImGui::IsItemDeactivatedAfterEdit() || commit;
  if (commit)
    CommitTypedCode(s);
  ItemHelpTooltip("The CS-MAP code, e.g. HARN/TX.TX-CF. Type a code and press Enter to select it.");

  const ZoneDetails& d = s.details;
  const auto readOnly = [&](const char* label, const char* id, const std::string& text) {
    row(label);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s", text.c_str());
    ImGui::InputText(id, buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
  };
  readOnly("Description:", "##ds_desc", d.description);
  readOnly("Projection:", "##ds_proj", d.projection);
  readOnly("Datum:", "##ds_datum", d.datum);
  ImGui::EndDisabled();

  // One message line, kept even when empty so the window never changes size.
  const std::string& msg = loaded ? s.codeError : geo::DictionaryError();
  if (msg.empty() && s.projectMode && s.settings.zoneCode.empty())
    ImGui::TextDisabled("No coordinate system: each drawing keeps its own.");  // REQ-375
  else if (msg.empty())
    ImGui::TextUnformatted("");
  else
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.30f, 1.f), "%s", msg.c_str());
}

void DrawUnitsAndZoneTab(StagedDrawingSettings& s, const DialogLayout& L) {
  const std::function<void(const char*)> row = [&](const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(L.labelW);
    ImGui::SetNextItemWidth(L.fieldW);
  };

  int unitSel = 0;
  for (int i = 0; i < kDrawingUnitCount; ++i)
    if (s.insUnits == kDrawingUnitCodes[i])
      unitSel = i;
  // REQ-375: the project fixes the drawing unit and the coordinate system of its drawings.
  const bool lockUnits = s.proj && s.proj->insUnits >= 0;
  const bool lockZone = s.proj && !s.proj->zoneCode.empty();
  row("Drawing units:");
  ImGui::BeginDisabled(lockUnits);
  const bool unitsPicked = ImGui::Combo("##ds_units", &unitSel, kDrawingUnitNames, kDrawingUnitCount);
  ImGui::EndDisabled();
  if (lockUnits) {
    ImGui::SameLine(L.tagX);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", kTagEnforced);
  }
  if (unitsPicked) {
    s.insUnits = kDrawingUnitCodes[std::clamp(unitSel, 0, kDrawingUnitCount - 1)];
    // A relabel: the scale value is kept, and reads as Custom if the new list does not hold it.
    if (!s.customScale &&
        PlotScaleChoiceIndex(PlotScaleChoicesFor(s.insUnits), s.modelUnitsPerPlottedInch) < 0) {
      s.customScale = true;
      std::snprintf(s.customScaleText, sizeof(s.customScaleText), "%g",
                    static_cast<double>(s.modelUnitsPerPlottedInch));
    }
  }
  ItemHelpTooltip("The drawing's unit (INSUNITS). A relabel only: no coordinate changes. "
                  "The same value the UNITS dialog shows.");

  int ang = static_cast<int>(s.settings.angularUnits);
  row("Angular units:");
  if (ImGui::Combo("##ds_ang", &ang, kAngular, IM_ARRAYSIZE(kAngular)))
    s.settings.angularUnits = static_cast<DrawingSettings::AngularUnits>(std::clamp(ang, 0, 2));
  InheritTag(s, ProjectDefaultKey::AngularUnits, L.tagX);
  ItemHelpTooltip("The drawing's angular unit (AUNITS). The app-wide angle display format is set in UNITS.");

  int foot = static_cast<int>(s.settings.footDefinition);
  row("Imperial to Metric conversion:");
  if (ImGui::Combo("##ds_foot", &foot, kFoot, IM_ARRAYSIZE(kFoot)))
    s.settings.footDefinition = static_cast<DrawingSettings::FootDefinition>(std::clamp(foot, 0, 1));
  InheritTag(s, ProjectDefaultKey::FootDefinition, L.tagX);
  ItemHelpTooltip("The foot every feet/meters conversion in this drawing uses. The two differ by "
                  "2 parts per million: about 1 ft at 500,000 ft.");

  const std::vector<PlotScaleChoice> choices = PlotScaleChoicesFor(s.insUnits);
  const int listIx = s.customScale ? -1 : PlotScaleChoiceIndex(choices, s.modelUnitsPerPlottedInch);
  const std::string preview = listIx >= 0 ? choices[static_cast<size_t>(listIx)].label : std::string("Custom");
  row("Scale:");
  if (ImGui::BeginCombo("##ds_scale", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
    for (size_t i = 0; i < choices.size(); ++i) {
      if (ImGui::Selectable(choices[i].label.c_str(), static_cast<int>(i) == listIx)) {
        s.customScale = false;
        s.modelUnitsPerPlottedInch = choices[i].modelUnitsPerPlottedInch;
      }
    }
    if (ImGui::Selectable("Custom", listIx < 0))
      s.customScale = true;
    ImGui::EndCombo();
  }
  ItemHelpTooltip("The drawing's plot scale: the same value as the status-bar scale. It sizes survey-point "
                  "markers, labels and plotted text.");
  InheritTag(s, ProjectDefaultKey::PlotScale, L.tagX);

  ImGui::BeginDisabled(!s.customScale);
  row("Custom scale:");
  ImGui::InputText("##ds_custom", s.customScaleText, sizeof(s.customScaleText));
  ImGui::EndDisabled();
  ItemHelpTooltip("Drawing units per plotted inch (e.g. 50 for 1\" = 50' in a feet drawing).");

  ImGui::Spacing();
  ImGui::Checkbox(kCheckScaleInserted, &s.settings.scaleInsertedObjects);
  InheritTag(s, ProjectDefaultKey::ScaleInsertedObjects, L.tagX);
  ItemHelpTooltip("On: INSERT converts a block drawn in another unit to this drawing's unit. "
                  "Off: blocks are inserted as drawn.");
  ImGui::Checkbox(kCheckSetVariables, &s.settings.setDrawingVariables);
  InheritTag(s, ProjectDefaultKey::SetDrawingVariables, L.tagX);
  ItemHelpTooltip("On: saving writes LUNITS and AUNITS from these settings beside INSUNITS.");

  ImGui::Spacing();
  ImGui::SeparatorText(lockZone ? "Zone (enforced by project)" : "Zone");
  ImGui::BeginDisabled(lockZone);
  DrawZoneGroup(s, row);
  ImGui::EndDisabled();
}

// --- Transformation tab (REQ-360) --------------------------------------------------------------------

using PickTarget = AppCommandState::DrawingSettingsPickState::Target;

/// Degrees → the drawing's Angular units (REQ-357) and back; the stored angles are degrees.
double DegToAngular(double deg, DrawingSettings::AngularUnits u) {
  switch (u) {
    case DrawingSettings::AngularUnits::Radians: return deg * 3.14159265358979323846 / 180.0;
    case DrawingSettings::AngularUnits::Grads:   return deg * 400.0 / 360.0;
    default:                                      return deg;
  }
}
double AngularToDeg(double v, DrawingSettings::AngularUnits u) {
  switch (u) {
    case DrawingSettings::AngularUnits::Radians: return v * 180.0 / 3.14159265358979323846;
    case DrawingSettings::AngularUnits::Grads:   return v * 360.0 / 400.0;
    default:                                      return v;
  }
}
const char* AngularFormat(DrawingSettings::AngularUnits u) {
  switch (u) {
    case DrawingSettings::AngularUnits::Radians: return "%.8f rad";
    case DrawingSettings::AngularUnits::Grads:   return "%.6f grad";
    default:                                      return "%.6f\xC2\xB0";
  }
}

/// A read-only field showing \p text (the Transformation readouts).
void ReadOnlyField(const char* id, const std::string& text) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ImGui::InputText(id, buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
}

std::string Fixed(double v, int decimals) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
  return buf;
}

/// Bring a finished (or cancelled) DRAWINGSETTINGS pick back into the staged values. True when a pick
/// just ended (the window reappears this frame).
bool ConsumePick(AppCommandState& cmd, StagedDrawingSettings& s) {
  const AppCommandState::DrawingSettingsPickState pick = cmd.drawingSettingsPick;
  if (pick.target == PickTarget::None)
    return false;
  cmd.drawingSettingsPick = {};
  s.selectTransformation = true;
  s.transformMessage.clear();
  if (!pick.done)
    return true;  // Esc: nothing changes
  DrawingSettings::Transform& t = s.settings.transform;
  switch (pick.target) {
    case PickTarget::ReferencePoint:
    case PickTarget::RotationPoint: {
      // The grid pair starts as the picked point in zone units (no transformation); the user types
      // the real grid coordinates over it.
      DrawingSettings plain = s.settings;
      plain.transform.apply = false;
      const geo::GeoResult g = DrawingWorldToGrid(plain, s.insUnits, pick.worldX, pick.worldY);
      const bool ref = pick.target == PickTarget::ReferencePoint;
      (ref ? t.refLocalX : t.rotLocalX) = pick.worldX;
      (ref ? t.refLocalY : t.rotLocalY) = pick.worldY;
      (ref ? t.refPointNumber : t.rotPointNumber) = pick.pointNumber;
      if (g.ok) {
        (ref ? t.refGridE : t.rotGridE) = g.x;
        (ref ? t.refGridN : t.rotGridN) = g.y;
      }
      if (std::hypot(t.rotLocalX - t.refLocalX, t.rotLocalY - t.refLocalY) < 1e-9)
        s.transformMessage = "The rotation point is the reference point; pick a different point.";
      break;
    }
    case PickTarget::ToNorth:      t.toNorthDeg = pick.azimuthDeg; break;
    case PickTarget::LocalAzimuth: t.localAzimuthDeg = pick.azimuthDeg; break;
    default: break;
  }
  return true;
}

/// A "Pick" button: starts the pick and asks the window to hide (\p picking).
void PickButton(const char* id, PickTarget target, AppCommandState& cmd, std::vector<std::string>& log,
                bool* picking, float width = 0.f) {
  if (ImGui::Button(id, ImVec2(width, 0.f)) && StartDrawingSettingsPick(cmd, target, log))
    *picking = true;
}

/// Point number, Local Northing / Easting (read-only) and Grid Northing / Easting (typed) of the
/// reference or rotation point, under its pick button.
void DrawPointGroup(const char* title, bool reference, StagedDrawingSettings& s, const DialogLayout& L,
                    AppCommandState& cmd, std::vector<std::string>& log, bool* picking,
                    const std::function<void(const char*)>& row) {
  DrawingSettings::Transform& t = s.settings.transform;
  ImGui::SeparatorText(title);
  ImGui::PushID(title);
  const std::string pickLabel = std::string("Pick ") + (reference ? "Reference Point" : "Rotation Point");
  PickButton(pickLabel.c_str(), reference ? PickTarget::ReferencePoint : PickTarget::RotationPoint, cmd, log,
             picking, L.txColW);
  ItemHelpTooltip("Hides this window: pick a point, or snap to a survey point, in the drawing (or type X,Y). "
                  "Esc comes back with no change.");
  const int number = reference ? t.refPointNumber : t.rotPointNumber;
  row("Point number:");
  ReadOnlyField("##num", number > 0 ? std::to_string(number) : std::string());
  row("Local Northing:");
  ReadOnlyField("##ln", Fixed(reference ? t.refLocalY : t.rotLocalY, 4));
  row("Local Easting:");
  ReadOnlyField("##le", Fixed(reference ? t.refLocalX : t.rotLocalX, 4));
  row("Grid Northing:");
  ImGui::InputDouble("##gn", reference ? &t.refGridN : &t.rotGridN, 0.0, 0.0, "%.4f");
  ItemHelpTooltip("The point's grid northing in the zone's unit.");
  row("Grid Easting:");
  ImGui::InputDouble("##ge", reference ? &t.refGridE : &t.rotGridE, 0.0, 0.0, "%.4f");
  ItemHelpTooltip("The point's grid easting in the zone's unit.");
  ImGui::PopID();
}

/// The left column: sea level factor, grid scale factor and rotation (items 3, 4, 6).
void DrawFactorsAndRotation(StagedDrawingSettings& s, const DrawingTransformFactors& f, const DialogLayout& L,
                            AppCommandState& cmd, std::vector<std::string>& log, bool* picking,
                            const std::function<void(const char*)>& row) {
  DrawingSettings::Transform& t = s.settings.transform;
  const DrawingSettings::AngularUnits ang = s.settings.angularUnits;
  ImGui::PushID("left");
  ImGui::SeparatorText("Sea Level Scale Factor");
  ImGui::Checkbox(kCheckSeaLevel, &t.applySeaLevel);
  ItemHelpTooltip("k_sea = R / (R + h): reduces ground distances at elevation h to the spheroid.");
  ImGui::BeginDisabled(!t.applySeaLevel);
  row("Elevation:");
  ImGui::InputDouble("##elev", &t.elevation, 0.0, 0.0, "%.4f");
  ItemHelpTooltip("The project's mean elevation h, in drawing units.");
  row("Spheroid radius (m):");
  double radius = t.spheroidRadiusM > 0.0 ? t.spheroidRadiusM : f.spheroidRadiusM;
  if (ImGui::InputDouble("##radius", &radius, 0.0, 0.0, "%.3f"))
    t.spheroidRadiusM = radius;
  ItemHelpTooltip("R in meters. Default: the semi-major axis of the zone datum's ellipsoid.");
  ImGui::EndDisabled();

  ImGui::SeparatorText("Grid Scale Factor");
  int computation = static_cast<int>(t.computation);
  row("Computation:");
  if (ImGui::Combo("##comp", &computation, kComputation, IM_ARRAYSIZE(kComputation)))
    t.computation = static_cast<DrawingSettings::Transform::Computation>(std::clamp(computation, 0, 1));
  ItemHelpTooltip("Reference Point: the projection's scale factor at the reference point's grid "
                  "coordinate. User Defined: type it.");
  row("Grid scale factor:");
  if (t.computation == DrawingSettings::Transform::Computation::UserDefined) {
    ImGui::InputDouble("##kgrid", &t.userScaleFactor, 0.0, 0.0, "%.8f");
  } else {
    ReadOnlyField("##kgrid", f.transformOk ? Fixed(f.gridFactor, 8) : std::string());
  }
  row("Combined factor:");
  ReadOnlyField("##kcomb", f.transformOk ? Fixed(f.combined, 8) : std::string());
  ItemHelpTooltip("k = grid scale factor x sea level scale factor.");

  ImGui::SeparatorText("Rotation");
  using R = DrawingSettings::Transform::Rotation;
  if (ImGui::RadioButton(kRadioRotationPoint, t.rotation == R::RotationPoint))
    t.rotation = R::RotationPoint;
  ItemHelpTooltip("The angle between a second point's local and grid bearings from the reference point.");
  if (ImGui::RadioButton(kRadioGridAngle, t.rotation != R::RotationPoint) && t.rotation == R::RotationPoint)
    t.rotation = R::ToNorth;
  ImGui::BeginDisabled(t.rotation == R::RotationPoint);
  int kind = t.rotation == R::Azimuth ? 1 : 0;
  row("Angle:");
  if (ImGui::Combo("##kind", &kind, kAngleKinds, IM_ARRAYSIZE(kAngleKinds)))
    t.rotation = kind == 1 ? R::Azimuth : R::ToNorth;
  const float pickFieldW = L.txFieldW - L.pickW - ImGui::GetStyle().ItemSpacing.x;
  const auto angleField = [&](const char* label, const char* id, double* deg, PickTarget pick, bool enabled,
                              const char* help) {
    ImGui::BeginDisabled(!enabled);
    row(label);
    if (pick != PickTarget::None)
      ImGui::SetNextItemWidth(pickFieldW);
    double v = DegToAngular(*deg, ang);
    if (ImGui::InputDouble(id, &v, 0.0, 0.0, AngularFormat(ang)))
      *deg = AngularToDeg(v, ang);
    ItemHelpTooltip(help);
    if (pick != PickTarget::None) {
      ImGui::SameLine();
      ImGui::PushID(id);
      PickButton("Pick", pick, cmd, log, picking, L.pickW);
      ItemHelpTooltip("Hides this window: pick two points in the drawing along the direction.");
      ImGui::PopID();
    }
    ImGui::EndDisabled();
  };
  angleField("To north:", "##tonorth", &t.toNorthDeg, PickTarget::ToNorth, t.rotation == R::ToNorth,
             "The angle from local north to grid north, clockwise. Pick: the direction of grid north in the drawing.");
  angleField("Local azimuth:", "##locaz", &t.localAzimuthDeg, PickTarget::LocalAzimuth, t.rotation == R::Azimuth,
             "A local azimuth (clockwise from north). Pick: a line in the drawing.");
  angleField("Grid azimuth:", "##gridaz", &t.gridAzimuthDeg, PickTarget::None, t.rotation == R::Azimuth,
             "The grid azimuth the local azimuth becomes.");
  ImGui::EndDisabled();
  ImGui::PopID();
}

// --- Object Layers tab (REQ-361) ---------------------------------------------------------------------

/// A padlock button: closed when \p *locked. Toggles on click.
void PadlockToggle(const char* id, bool* locked) {
  const float h = ImGui::GetFrameHeight();
  const bool clicked = ImGui::InvisibleButton(id, ImVec2(h, h));
  if (clicked)
    *locked = !*locked;
  const ImVec2 mn = ImGui::GetItemRectMin();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImU32 col = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Text);
  const float u = h / 16.f;
  const ImVec2 bodyMin(mn.x + 3.f * u, mn.y + 7.5f * u);
  const ImVec2 bodyMax(mn.x + 13.f * u, mn.y + 14.f * u);
  dl->AddRectFilled(bodyMin, bodyMax, col, 1.5f * u);
  // The shackle: over the body when locked, swung open to the right when not.
  const float cx = *locked ? mn.x + 8.f * u : mn.x + 11.f * u;
  dl->PathArcTo(ImVec2(cx, mn.y + 6.f * u), 3.2f * u, 3.14159265f, 2.f * 3.14159265f);
  dl->PathStroke(col, ImDrawFlags_None, 1.6f * u);
  dl->AddLine(ImVec2(cx - 3.2f * u, mn.y + 6.f * u), ImVec2(cx - 3.2f * u, bodyMin.y), col, 1.6f * u);
  if (*locked)
    dl->AddLine(ImVec2(cx + 3.2f * u, mn.y + 6.f * u), ImVec2(cx + 3.2f * u, bodyMin.y), col, 1.6f * u);
  ItemHelpTooltip(*locked ? "Locked: a creation dialog cannot change this layer."
                          : "Unlocked: a creation dialog may choose another layer.");
}

void DrawObjectLayersTab(StagedDrawingSettings& s, const DialogLayout& L, const AppCommandState& cmd) {
  const float h = ImGui::GetFrameHeight();
  ImGui::AlignTextToFramePadding();  // REQ-375: one row, kept even for a standalone drawing
  if (s.proj && s.proj->hasDefaults) {
    ImGui::TextUnformatted("Layers for new objects:");
    InheritTag(s, ProjectDefaultKey::ObjectLayers, -1.f);
  } else {
    ImGui::TextUnformatted("");
  }
  const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuter |
                                ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
  if (ImGui::BeginTable("##ol_rows", 5, flags)) {
    ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthFixed, L.olObjectW);
    ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Modifier", ImGuiTableColumnFlags_WidthFixed, L.olModifierW);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, L.olValueW);
    ImGui::TableSetupColumn("Locked", ImGuiTableColumnFlags_WidthFixed, h);
    ImGui::TableHeadersRow();
    for (int i = 0; i < kObjectLayerKindCount; ++i) {
      ObjectLayerRow& row = s.settings.objectLayers[static_cast<size_t>(i)];
      ImGui::PushID(i);
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      const ImVec2 at = ImGui::GetCursorScreenPos();
      DrawObjectLayerIcon(ImGui::GetWindowDrawList(), static_cast<ObjectLayerKind>(i), at,
                          ImVec2(at.x + h, at.y + h));
      ImGui::Dummy(ImVec2(h, h));
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(kObjectLayerNames[i]);

      // Layer: typed, or picked from the drawing's layers with the arrow beside it.
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - h);
      ImGui::InputText("##layer", &row.layer);
      ItemHelpTooltip("The layer new objects of this kind are created on. Type a new name, or pick one; "
                      "a layer the drawing does not have is created with the first such object.");
      ImGui::SameLine(0.f, 0.f);
      if (ImGui::BeginCombo("##pick", nullptr, ImGuiComboFlags_NoPreview | ImGuiComboFlags_PopupAlignLeft)) {
        for (const CadLayerRow& layer : cmd.drawingLayerTable)
          if (ImGui::Selectable(layer.name.c_str(), layer.name == row.layer))
            row.layer = layer.name;
        ImGui::EndCombo();
      }

      ImGui::TableSetColumnIndex(2);
      int modifier = static_cast<int>(row.modifier);
      ImGui::SetNextItemWidth(-1.f);
      if (ImGui::Combo("##mod", &modifier, kModifiers, IM_ARRAYSIZE(kModifiers)))
        row.modifier = static_cast<ObjectLayerRow::Modifier>(std::clamp(modifier, 0, 2));

      ImGui::TableSetColumnIndex(3);
      ImGui::BeginDisabled(row.modifier == ObjectLayerRow::Modifier::None);
      ImGui::SetNextItemWidth(-1.f);
      ImGui::InputText("##value", &row.value);
      ImGui::EndDisabled();
      ItemHelpTooltip("Added before (Prefix) or after (Suffix) the layer; each * is the object's name.");

      ImGui::TableSetColumnIndex(4);
      PadlockToggle("##lock", &row.locked);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::Spacing();
  ImGui::TextWrapped("%s", kObjectLayersInfo);
  ImGui::BeginDisabled();
  bool displayComponents = false;
  ImGui::Checkbox(kCheckDisplayComponents, &displayComponents);
  ImGui::EndDisabled();
  GreyedWithReason(kNotImplemented);

  // One message line, kept even when empty so the window never changes size.
  const std::string msg = ValidateObjectLayers(s.settings);
  if (msg.empty())
    ImGui::TextUnformatted("");
  else
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.30f, 1.f), "%s", msg.c_str());
}

void DrawTransformationTab(StagedDrawingSettings& s, const DialogLayout& L, AppCommandState& cmd,
                           std::vector<std::string>& log, bool* picking) {
  DrawingSettings::Transform& t = s.settings.transform;
  const DrawingTransformFactors& f = StagedFactors(s);
  const std::function<void(const char*)> row = [&](const char* label) {
    const float x0 = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(x0 + L.txLabelW);
    ImGui::SetNextItemWidth(L.txFieldW);
  };

  // Zone (read-only), its unit, and the switch every other control waits for (item 1).
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Zone description:");
  ImGui::SameLine(L.txLabelW);
  ImGui::SetNextItemWidth(-1.f);
  ReadOnlyField("##tx_zone", s.details.description);
  ImGui::Text("Zone units are in %s.", f.ok ? f.zoneUnitName.c_str() : "Unknown");
  ImGui::BeginDisabled(!f.ok);
  ImGui::Checkbox(kCheckApplyTransform, &t.apply);
  ImGui::EndDisabled();
  ItemHelpTooltip("On: every grid and latitude/longitude computation in this drawing goes through this "
                  "transformation. No geometry moves.");

  ImGui::BeginDisabled(!f.ok || !t.apply);
  // Two columns; a table cell clips its separators, a group would not.
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.f));
  if (ImGui::BeginTable("##tx_cols", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX)) {
    ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthFixed, L.txColW);
    ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthFixed, L.txColW);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    DrawFactorsAndRotation(s, f, L, cmd, log, picking, row);
    ImGui::TableSetColumnIndex(1);
    ImGui::PushID("right");
    DrawPointGroup("Reference Point", true, s, L, cmd, log, picking, row);
    ImGui::BeginDisabled(t.rotation != DrawingSettings::Transform::Rotation::RotationPoint);
    DrawPointGroup("Rotation Point", false, s, L, cmd, log, picking, row);
    ImGui::EndDisabled();
    ImGui::PopID();
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();
  ImGui::EndDisabled();

  // One message line, kept even when empty so the window never changes size.
  std::string msg = s.transformMessage;
  if (msg.empty() && !geo::DictionariesLoaded())
    msg = geo::DictionaryError();
  if (msg.empty() && !f.ok && s.settings.Geolocated())
    msg = f.error;
  if (msg.empty() && f.ok && t.apply && !f.transformOk)
    msg = f.transformError;
  if (msg.empty())
    ImGui::TextUnformatted("");
  else
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.30f, 1.f), "%s", msg.c_str());
}

}  // namespace

void DrawDrawingSettingsWindow(AppCommandState& cmd, std::vector<std::string>& log) {
  static StagedDrawingSettings staged;
  static bool wasOpen = false;

  // REQ-375: the same window edits the project's settings (PROJECTSETTINGS) — the Units and Zone and
  // Object Layers tabs, whose values become the project's unit, zone and defaults.
  const bool projectMode = cmd.projectSettingsUid != 0;
  const auto closeRequest = [&] {
    if (projectMode)
      cmd.projectSettingsUid = 0;
    else
      cmd.showDrawingSettingsWindow = false;
    wasOpen = false;
  };
  if (!projectMode && !cmd.showDrawingSettingsWindow) {
    wasOpen = false;
    return;
  }
  if (cmd.activeDrawingIdx == 0) {  // REQ-308: the Start tab owns no drawing
    closeRequest();
    log.push_back(projectMode ? "PROJECTSETTINGS — open a drawing of the project first."
                              : "DRAWINGSETTINGS — open or create a drawing first.");
    return;
  }
  AppCommandState::ProjectSession* session = nullptr;
  if (projectMode) {
    for (auto& ps : cmd.openProjects)
      if (ps.uid == cmd.projectSettingsUid)
        session = &ps;
    if (!session || !session->settings) {
      closeRequest();
      return;
    }
  }
  // REQ-360: hidden while a Transformation pick runs in the drawing; the staged values wait.
  if (!projectMode && cmd.active == AppCommandState::Kind::DrawingSettingsPick)
    return;
  if (!wasOpen || staged.drawingIdx != cmd.activeDrawingIdx || staged.projectMode != projectMode) {
    // a tab switched mid-pick: start over
    if (projectMode)
      SeedFromProject(cmd, *session->settings, staged);
    else
      SeedFromDrawing(cmd, staged);
    cmd.drawingSettingsPick = {};
    wasOpen = true;
  }
  staged.proj = projectMode ? nullptr : ProjectSettingsForTab(cmd, cmd.activeDrawingIdx);
  const bool resumed = ConsumePick(cmd, staged);

  const std::string name = cmd.activeDrawingIdx < static_cast<int>(cmd.drawingTabs.size())
                               ? cmd.drawingTabs[static_cast<size_t>(cmd.activeDrawingIdx)].name
                               : std::string("Drawing");
  const std::string title = projectMode ? "Project Settings - " + session->project.name + kPopupId
                                        : "Drawing Settings - " + name + kPopupId;
  if (!ImGui::IsPopupOpen(kPopupId))  // "###" makes the id independent of the drawing name
    ImGui::OpenPopup(title.c_str());

  const DialogLayout layout = MeasureLayout();
  ImGui::SetNextWindowSize(layout.size, ImGuiCond_Always);  // fixed: sized to its content
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  PushProductDialogAccent();
  bool open = true;
  if (!ImGui::BeginPopupModal(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                                  ImGuiWindowFlags_NoSavedSettings)) {
    PopProductDialogAccent();
    closeRequest();  // closed by the title-bar [X]: Cancel
    return;
  }
  PaintProductDialogAccentFrame();
  BeginStyledDialog();

  // [X] and Esc are Cancel. Esc only while this window has focus: an open combo list is its own
  // window, and Esc there must close the list, not the whole dialog.
  // The Esc that just cancelled a pick is not a second Esc for the window (REQ-360).
  bool close = !open || (!resumed && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                         ImGui::IsKeyPressed(ImGuiKey_Escape));

  bool picking = false;  // a pick button started DRAWINGSETTINGS: hide until it ends
  const float footerH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
  if (ImGui::BeginChild("##ds_body", ImVec2(0.f, -footerH), ImGuiChildFlags_None,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    if (ImGui::BeginTabBar("##ds_tabs")) {
      // REQ-359: Edit Location asks for this tab explicitly.
      const ImGuiTabItemFlags unitsFlags =
          cmd.drawingSettingsShowUnitsAndZone ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
      cmd.drawingSettingsShowUnitsAndZone = false;
      if (ImGui::BeginTabItem("Units and Zone", nullptr, unitsFlags)) {
        ImGui::Spacing();
        DrawUnitsAndZoneTab(staged, layout);
        ImGui::EndTabItem();
      }
      const ImGuiTabItemFlags txFlags =
          staged.selectTransformation ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
      staged.selectTransformation = false;
      if (!projectMode && ImGui::BeginTabItem("Transformation", nullptr, txFlags)) {  // REQ-360
        ImGui::Spacing();
        DrawTransformationTab(staged, layout, cmd, log, &picking);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Object Layers")) {  // REQ-361
        ImGui::Spacing();
        DrawObjectLayersTab(staged, layout, cmd);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::EndChild();

  float scale = 0.f;
  const bool scaleValid = StagedScale(staged, &scale);
  const bool transformValid = projectMode || ValidateDrawingTransform(staged.settings).empty();  // REQ-360
  const bool layersValid = ValidateObjectLayers(staged.settings).empty();         // REQ-361
  const bool valid = scaleValid && transformValid && layersValid;
  const float btnW = layout.btnW;
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  ImGui::Separator();
  if (!valid) {  // beside the buttons, so the message needs no row of its own
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.30f, 1.f), "%s",
                       !scaleValid ? kCustomScaleError : !transformValid ? kTransformError : kObjectLayersError);
    ImGui::SameLine();
  }
  ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - (btnW * 3.f + spacing * 2.f));
  ImGui::BeginDisabled(!valid);
  const bool ok = ImGui::Button("OK", ImVec2(btnW, 0.f));
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(btnW, 0.f)))
    close = true;
  ImGui::SameLine();
  ImGui::BeginDisabled(!valid);
  const bool apply = ImGui::Button("Apply", ImVec2(btnW, 0.f));
  ImGui::EndDisabled();

  if (ok || apply) {
    bool applied = false;
    if (projectMode) {  // REQ-375: written to the .gsproj
      ProjectSettings ps;
      ps.insUnits = staged.insUnits;
      ps.zoneCode = staged.settings.zoneCode;
      ps.hasDefaults = true;
      ps.plotScale = scale;
      ps.defaults = staged.settings;
      ps.defaults.overridden = 0;
      applied = SaveProjectSettings(cmd, cmd.projectSettingsUid, ps, log);
      if (applied)
        SeedFromProject(cmd, *session->settings, staged);
    } else {
      // A value the user changed is an override exactly when it differs from the project's (REQ-375).
      for (unsigned k = 0; k < kProjectDefaultKeyCount; ++k) {
        const auto key = static_cast<ProjectDefaultKey>(k);
        if (staged.proj && staged.proj->hasDefaults)
          staged.settings.SetOverridden(key, KeyOverridden(staged, key));
      }
      applied = ApplyDrawingSettings(cmd, staged.insUnits, scale, staged.settings, log);
      if (applied)
        SeedFromDrawing(cmd, staged);
    }
    if (applied && ok)
      close = true;
  }

  if (close) {
    if (!projectMode && cmd.active == AppCommandState::Kind::DrawingSettingsPick)
      CancelActiveCommand(cmd, log);  // started this frame, then Cancel/Esc: the pick goes too
    cmd.drawingSettingsPick = {};
    closeRequest();
    ImGui::CloseCurrentPopup();
  } else if (picking) {
    ImGui::CloseCurrentPopup();  // reopened (with the staged values) when the pick ends
  }
  ImGui::EndPopup();
  PopProductDialogAccent();
}
