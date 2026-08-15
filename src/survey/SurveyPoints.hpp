#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "PointGroupRule.hpp"

enum class SurveyPointLabelStyle : uint8_t {
  None = 0,
  NumberDesc,
  NumberOnly,
  DescOnly,
  NumberElev,
  NumberElevDesc,
  NumberNorthEast,
  NorthEast,
  NumberNorthEastElev,
};

/// Placeholders: {id} {desc} {elev} {north} {east} — use a real newline in the string for line breaks.
struct SurveyLabelStyleTemplates {
  std::string numberDesc = "{id}\n{desc}";
  std::string numberOnly = "{id}";
  std::string descOnly = "{desc}";
  std::string numberElev = "{id}\nZ={elev}";
  std::string numberElevDesc = "{id}\nZ={elev}\n{desc}";
  std::string numberNorthEast = "{id}\nN={north}\nE={east}";
  std::string northEast = "N={north}\nE={east}";
  std::string numberNorthEastElev = "{id}\nN={north}\nE={east}\nZ={elev}";
};

struct SurveyPoint {
  int id = 0;
  float easting = 0.f;
  float northing = 0.f;
  float elevation = 0.f;
  std::string description;
  /// The field code as collected, never rewritten by an edit to \ref description (REQ-066).
  ///
  /// The two are independent on purpose: a crew codes `EG`, the office expands or edits the
  /// description, and a point group keyed on the raw code must still match afterwards (REQ-067).
  /// **Empty means "this point predates REQ-066"** — every drawing written before it, and every
  /// pre-REQ-066 DXF — and consumers fall back to \ref description rather than skipping the point.
  std::string rawDescription;
  std::string layer;
  SurveyPointLabelStyle labelStyle = SurveyPointLabelStyle::NumberDesc;
  /// Stable id (\ref EntityAttributes::id) of this point's linked MTEXT label, or 0 for none.
  ///
  /// **An id, not an index** (REQ-076 / architecture §11.9). It was `labelMtextAnnIndex`, and
  /// keeping it correct cost a decrement loop inside `EraseCadAnnotationAtIndex` — erasing any
  /// annotation had to walk every survey point and renumber. That loop is deleted; an id needs no
  /// fix-up because it never means anything but the entity it was issued to.
  std::uint64_t labelMtextAnnId = 0;
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

/// Camera right/up as world-space unit vectors, used to build screen-facing markers (REQ-058).
///
/// Passed in as plain components rather than as a `Camera`: survey is a domain module and the
/// camera lives in the renderer, so taking one here would invert the layering (architecture §11).
/// The default is world +X / +Y, which is exactly plan view — so an un-plumbed caller keeps the
/// pre-3D geometry.
struct MarkerBillboardBasis {
  float rightX = 1.f, rightY = 0.f, rightZ = 0.f;
  float upX = 0.f, upY = 1.f, upZ = 0.f;
};

/// Appends two GL_LINES segments (x,y,z triplets) forming an X centered at the point.
///
/// The X is built in the \p basis plane, not in world XY: it is a marker, not geometry, so it must
/// stay readable at any orientation instead of foreshortening to an edge near a horizontal view
/// (REQ-058 / GAP-2).
void AppendSurveyPointCrossVertices(float easting, float northing, float elevationZ, float halfExtentWorld,
                                    std::vector<float>* outLines, const MarkerBillboardBasis& basis = {});

void AppendAllSurveyPointMarkers(float crossHalfWorld, const std::vector<SurveyPoint>& pts,
                                 std::vector<float>* outLines, const MarkerBillboardBasis& basis = {});

/// Resolve a survey point's label link to an index into \c st.cadAnnotations, or -1 if there is none.
///
/// The link is stored as a stable entity id (REQ-076), so this is the one place that turns it back
/// into an index — and the index is a return value, never something a caller stores.
[[nodiscard]] int FindSurveyLabelAnnIndex(const AppCommandState& st, const SurveyPoint& p);

/// Resolve a label MTEXT's back-link to an index into \c st.surveyPoints, or -1 if there is none.
///
/// The argument is a \c SurveyPoint::id (the point number), not an array position — that is what
/// \c CadAnnotation::surveyPointLabelForId now holds (REQ-076).
[[nodiscard]] int SurveyPointIndexForId(const AppCommandState& st, int surveyPointId);

/// Indices into \c st.surveyPoints of every point matching \p group (REQ-067).
///
/// Computed fresh on every call — a group stores a rule, never a member list (ASSUMPTION-3). Returns
/// indices rather than ids because callers immediately want the points; ids are what get *stored*.
///
/// \param log optional; receives a line when the rule names ranges it could not parse, and when a
///        configured rule resolves to nothing — both are things the user needs told (REQ-201).
[[nodiscard]] std::vector<int> ResolvePointGroup(const AppCommandState& st, const PointGroup& group,
                                                 std::vector<std::string>* log = nullptr);

/// Index of the group named \p name (case-insensitive), or -1.
[[nodiscard]] int FindPointGroupIndex(const AppCommandState& st, const std::string& name);

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

/// \p worldOriginX/Y are added to the point's local easting/northing so {north}/{east} render world
/// (state-plane) coordinates rather than the internal local-space values.
[[nodiscard]] std::string FormatSurveyPointLabelPlain(const SurveyPoint& p, SurveyPointLabelStyle style,
                                                      const SurveyLabelStyleTemplates& templates, int precision,
                                                      double worldOriginX = 0.0, double worldOriginY = 0.0);

void EnsureSurveyPointLabelMtext(AppCommandState& st, size_t pointIndex, std::vector<std::string>* log);

void RepositionSurveyLabelMtextForPoint(AppCommandState& st, size_t pointIndex);

/// Repositions every linked survey MTEXT (e.g. after plot scale / PSCALE changes).
void RepositionAllSurveyPointLabels(AppCommandState& st);

/// Rebuilds every linked survey MTEXT's text and position. Call after the document origin changes so
/// {north}/{east} labels show world coordinates relative to the new origin.
void RegenerateAllSurveyPointLabels(AppCommandState& st);

/// Adds conflicting world-coordinate survey points to the document. \p overwrite replaces the existing same-ID point;
/// otherwise each point's ID gets \p offset added (then bumped to the next free ID if still taken).
void ResolveConflictingWorldSurveyPoints(AppCommandState& st, const std::vector<SurveyPoint>& conflictsWorld,
                                         bool overwrite, int offset, std::vector<std::string>* log);

bool SaveSurveyPointsToJsonFile(const AppCommandState& st, const char* path, std::vector<std::string>& log);

bool LoadSurveyPointsFromJsonFile(AppCommandState& st, const char* path, std::vector<std::string>& log);

void StartCreatePointsCommand(AppCommandState& st, std::vector<std::string>& log);

void StartViewPointsCommand(AppCommandState& st, std::vector<std::string>& log);

void StartImportPointsCommand(AppCommandState& st, std::vector<std::string>& log);

void StartExportPointsCommand(AppCommandState& st, std::vector<std::string>& log);
