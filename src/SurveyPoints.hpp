#pragma once

#include <string>
#include <vector>

struct SurveyPoint {
  int id = 0;
  float easting = 0.f;
  float northing = 0.f;
  float elevation = 0.f;
  std::string description;
  std::string layer;
};

enum class SurveyDuplicatePolicy { Notify, Renumber, Merge, Overwrite };

struct CreatePointsOptions {
  int startNumber = 1;
  bool sequentialNumbering = true;
  int pointNumberOffset = 1;
  int sequenceNumbersFrom = 1;
  std::string layer = "0";
  std::string defaultDescription;
  float defaultElevation = 0.f;
  SurveyDuplicatePolicy duplicatePolicy = SurveyDuplicatePolicy::Notify;
};

struct AppCommandState;

/// Half of horizontal span of the X in world units from paper span (inches) and plot scale (MUP).

float SurveyPointCrossHalfWorldFromPaper(float crossSpanPlottedInches, float modelUnitsPerPlottedInch);

/// Appends two GL_LINES segments (x,y,z triplets) forming an X centered at the point.
void AppendSurveyPointCrossVertices(float easting, float northing, float elevationZ, float halfExtentWorld,
                                    std::vector<float>* outLines);

void AppendAllSurveyPointMarkers(float crossHalfWorld, const std::vector<SurveyPoint>& pts,
                                 std::vector<float>* outLines);

void ResetCreatePointsNextIdFromSettings(AppCommandState& st);

/// Places a survey point using create-points options & duplicate policy. Updates next ID when sequential.

bool TryPlaceSurveyPoint(AppCommandState& st, float easting, float northing, float elevation,
                         std::vector<std::string>& log);

/// Copies viewport-selected survey rows by (\p dx, \p dy), applying \p policy when IDs collide with other points.
void DuplicateSelectedSurveyPointsTranslated(AppCommandState& st, float dx, float dy, SurveyDuplicatePolicy policy,
                                             std::vector<std::string>& log);

/// Same ID policy as translated copy; positions are rotated about (\p bx,\p by) by \p rad radians.
void DuplicateSelectedSurveyPointsRotated(AppCommandState& st, float bx, float by, float rad,
                                          SurveyDuplicatePolicy policy, std::vector<std::string>& log);

void RemoveSurveyPointAt(AppCommandState& st, size_t index);

bool SaveSurveyPointsToJsonFile(const AppCommandState& st, const char* path, std::vector<std::string>& log);

bool LoadSurveyPointsFromJsonFile(AppCommandState& st, const char* path, std::vector<std::string>& log);

void StartCreatePointsCommand(AppCommandState& st, std::vector<std::string>& log);

void StartViewPointsCommand(AppCommandState& st, std::vector<std::string>& log);

void StartImportPointsCommand(AppCommandState& st, std::vector<std::string>& log);

void StartExportPointsCommand(AppCommandState& st, std::vector<std::string>& log);
