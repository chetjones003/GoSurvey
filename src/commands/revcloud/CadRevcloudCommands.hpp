// --- REQ-444 REVCLOUD ----------------------------------------------------------------------------
/// Open the command: pick a first corner, or `O` to convert a closed shape already drawn.
void StartRevcloudCommand(AppCommandState& st, std::vector<std::string>& log);
/// The prompt line, echoing the arc length and options in force.
[[nodiscard]] std::string CadRevcloudPromptText(const AppCommandState& st);
/// Handle one typed line: a coordinate, Enter, or one of `A R D O C U`. \neturn false if not consumed.
bool HandleRevcloudTextInput(const std::string& line, AppCommandState& st, std::vector<std::string>& log);
/// Handle a viewport click: a corner, or — at the `O`bject prompt — the shape to convert.
void SubmitRevcloudViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
/// Drop the draft path, keeping the remembered arc length and options.
void CancelRevcloudCommand(AppCommandState& st);
/// The cloud the cursor currently proposes (the draft plus \p cursor, closed), built by the SAME
/// function the commit uses. Empty when the draft is too short for one.
[[nodiscard]] std::vector<double> CadRevcloudPreviewPolyline(const AppCommandState& st, double curX, double curY,
                                                             std::vector<float>* outBulge);
