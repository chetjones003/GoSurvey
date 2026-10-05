#pragma once

// REQ-375 (issue #696 P2) — a project's settings, and how a project drawing resolves against them.
//
// Pure: no window, no app state. The project stores its settings as JSON text inside the .gsproj
// (gsproj::Project::settingsJson); this turns that text into a struct and back, and decides what a
// drawing's coordinate system, unit and inheritable settings must be. D-2026-10-05-e fixes which
// settings are which:
//   * enforced      — the coordinate system (zone) and the drawing unit;
//   * inheritable   — ProjectDefaultKey's six settings (a drawing may override each one);
//   * always the drawing's own — transformation, geographic marker, online map.

#include "CadCommands.hpp"

#include <string>
#include <vector>

struct ProjectSettings {
  /// Drawing unit (INSUNITS code) every drawing in the project uses; -1 = the project fixes none.
  int insUnits = -1;
  /// CS-MAP code every drawing in the project uses; empty = the project fixes none, so each drawing
  /// keeps its own (a project written before P2, or one with no coordinate system chosen yet).
  std::string zoneCode;
  /// False = the project supplies no defaults yet, so no setting is inherited.
  bool hasDefaults = false;
  /// Plot scale default (model units per plotted inch) and the other inheritable defaults.
  float           plotScale = 50.f;
  DrawingSettings defaults;
};

/// Reads the `.gsproj` settings text. Empty, "{}" or an object without our members → no enforcement
/// and no defaults. False (with \p err) only when the text is not a JSON object.
bool ParseProjectSettings(const std::string& settingsJson, ProjectSettings* out, std::string* err);

/// The settings text for \p p. Members of \p existingJson that this code does not own are kept
/// (additive evolution, REQ-373 clause 3).
std::string WriteProjectSettings(const std::string& existingJson, const ProjectSettings& p);

/// What ApplyProjectToDrawing changed.
struct ProjectApplyResult {
  bool changed = false;
  bool unitsReplaced = false;  ///< the drawing's unit differed from the project's
  bool zoneReplaced = false;   ///< the drawing's coordinate system differed from the project's
  int  oldInsUnits = 0;
  std::string oldZone;
};

/// Makes a project drawing agree with its project: enforced values are set; every inheritable setting
/// the drawing has not overridden takes the project's default. A replaced zone also resets the
/// geographic marker and transformation, which belong to it (REQ-359 item 4, REQ-360).
ProjectApplyResult ApplyProjectToDrawing(const ProjectSettings& p, int* insUnits, float* plotScale,
                                         DrawingSettings* ds);

/// True when the drawing's value of \p key differs from the project's default.
bool DiffersFromProjectDefault(const ProjectSettings& p, ProjectDefaultKey key, float plotScale,
                               const DrawingSettings& ds);

/// Copies the project's default for \p key into the drawing's values (Reset to project value). Does
/// not touch the override bit.
void CopyProjectDefault(const ProjectSettings& p, ProjectDefaultKey key, float* plotScale, DrawingSettings* ds);

/// The settings of the project drawing tab \p tabIdx belongs to, or null (standalone tab / Start tab).
const ProjectSettings* ProjectSettingsForTab(const AppCommandState& st, int tabIdx);

/// Called after the user changes the active drawing's plot scale: it becomes an override when it
/// differs from the project default, and inherited again when it matches.
void NoteUserPlotScale(AppCommandState& st);

/// Every frame: makes every project drawing tab agree with its project (enforced zone and unit,
/// inherited defaults). Cheap when nothing differs. A replaced zone or unit is reported (REQ-201).
void EnforceProjectSettings(AppCommandState& st, std::vector<std::string>& log);
