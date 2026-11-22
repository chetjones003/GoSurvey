/// REQ-087: start a feature line — named 3D linework committing to its own store (ADR-035 (g)).
void StartFeatureLineCommand(AppCommandState& st, const std::string& name, std::vector<std::string>& log);

/// Append one vertex to the feature-line draft. \p isElevPoint marks it an elevation point rather
/// than a PI — geometrically it lies on the line either way (ADR-035 (a)).
bool SubmitFeatureLineVertex(AppCommandState& st, float x, float y, bool isElevPoint,
                             std::vector<std::string>& log);

/// Take an X,Y that arrived without an elevation — a viewport click, or a typed `X,Y` — and hold it
/// while FEATURELINE prompts for one (TASK-082). A typed `X,Y,Z` bypasses this and commits directly.
bool SubmitFeatureLinePoint(AppCommandState& st, float x, float y, std::vector<std::string>& log);

/// Resolve the point held by \ref SubmitFeatureLinePoint at elevation \p z and add it to the draft.
void CommitFeatureLinePendingPoint(AppCommandState& st, float z, std::vector<std::string>& log);

/// Commit the feature-line draft into the store as one entity. \p closed joins last vertex to first.
void CommitFeatureLineDraft(AppCommandState& st, bool closed, std::vector<std::string>& log);

// REQ-088 — feature line elevation editing. The table is DERIVED, never stored (ADR-035 (e)); the
// edits write elevations back into the stride-3 vertex array. `flNumber` and `pointNumber` are
// 1-based, matching what FEATURELINELIST and FLELEV print.

/// Build feature line \p fi's elevation table: station, elevation, length ahead, grade back/ahead.
/// False if \p fi is not a feature line with at least two points. Grades are NaN where no such
/// segment exists — see FeatureLineElevRow.
bool BuildFeatureLineElevTable(const AppCommandState& st, int fi, std::vector<FeatureLineElevRow>* out);

bool SetFeatureLinePointElevation(AppCommandState& st, int flNumber, int pointNumber, float elevation,
                                  std::vector<std::string>& log);
/// Sets the grade of the segment AHEAD, which moves the NEXT point and leaves this one alone.
bool SetFeatureLineGradeAhead(AppCommandState& st, int flNumber, int pointNumber, double gradePct,
                              std::vector<std::string>& log);
/// Sets the grade of the segment BEHIND, which moves THIS point and holds the previous one.
bool SetFeatureLineGradeBack(AppCommandState& st, int flNumber, int pointNumber, double gradePct,
                             std::vector<std::string>& log);
/// Raises (or, with a negative delta, lowers) every point of the feature line as a set.
bool RaiseFeatureLineElevations(AppCommandState& st, int flNumber, float delta,
                                std::vector<std::string>& log);
/// Adds an elevation point at plan \p station, interpolating its position so it lies ON the line.
bool InsertFeatureLineElevationPoint(AppCommandState& st, int flNumber, double station, float elevation,
                                     std::vector<std::string>& log);
/// Removes an elevation point. Refuses a PI — that is geometry editing, not an elevation edit.
bool DeleteFeatureLineElevationPoint(AppCommandState& st, int flNumber, int pointNumber,
                                     std::vector<std::string>& log);
