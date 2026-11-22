// --- Alignments (REQ-480..485, 501; GitHub issue #121 Phase 1; CadCommands_Alignment.cpp) ----------------
// Typed-argument commands, dispatched from ProcessCommandLineSubmit. Geometry lives in alignment/Alignment.hpp.
void RunAlignmentCreate(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentAdd(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentList(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentDelete(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
/// REQ-517: CREATEVIEWFRAMES <alignment>, <start>, <end>, <sheet>, <scale>, <overlap>[, CENTRED|LEFT|RIGHT].
void RunCreateViewFrames(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
/// REQ-517.7: VIEWFRAMEDELETE <alignment>. Deletes the alignment's view frame groups in one undo step.
void RunViewFrameDelete(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
/// REQ-518: CREATESHEETS <alignment>[, <frames>[, <title block DWG>]]. One rotated paper layout per frame, one undo step.
void RunCreateSheets(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
/// REQ-519: CREATESAMPLELINES <alignment>, <start>, <end>, <interval>, <left>, <right>[, <surface>]. One undo step.
void RunCreateSampleLines(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
/// REQ-519: CREATESECTIONSHEETS <alignment>, <sample lines>, <sections per sheet>, <axis elevation>. One undo step.
void RunCreateSectionSheets(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunCreateDesignProfile(AppCommandState& st, const std::string& args, std::vector<std::string>& log);  // REQ-521
void RunProfilePvi(AppCommandState& st, const std::string& args, std::vector<std::string>& log);  // REQ-521
void RunProfileDelPvi(AppCommandState& st, const std::string& args, std::vector<std::string>& log);  // REQ-521
void RunProfileQuery(AppCommandState& st, const std::string& args, std::vector<std::string>& log);  // REQ-521
void RunProfileList(AppCommandState& st, const std::string& args, std::vector<std::string>& log);  // REQ-521
void RunProfileDelete(AppCommandState& st, const std::string& args, std::vector<std::string>& log);  // REQ-521
/// REQ-517.8/.9: keeps view frame groups in step with the alignments. Logs a group it could not regenerate.
void RefreshViewFrameGroups(AppCommandState& st, std::vector<std::string>& log);
/// True when the active tab's survey-point project is read-only (REQ-517.6 uses the same rule as paste).
bool ActiveProjectPointsReadOnly(const AppCommandState& st);
void RunGetStationOffset(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
/// REQ-484.4: reports the station and offset of one picked point (drawing-local) while GETSTATIONOFFSET picks.
void CommitStationOffsetPick(AppCommandState& st, double lx, double ly, std::vector<std::string>& log);
void RunGetPointFromStationOffset(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunCreateAlignmentFromObjects(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
// REQ-485 interactive creation: ALIGNMENTLAYOUT picks PIs, CREATEALIGNMENTFROMOBJECTS without a name picks objects.
void RunAlignmentLayout(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentCurveSettings(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
/// A click or snapped point for Kind::AlignmentLayout, or an object pick for Kind::AlignmentFromObjects (drawing-local).
bool SubmitAlignmentCommandPoint(AppCommandState& st, double localX, double localY, std::vector<std::string>& log);
/// Typed input (including a bare Enter) for the two alignment Kinds; false when neither is active.
bool HandleAlignmentCommandText(AppCommandState& st, const std::string& line, std::vector<std::string>& log);
/// ESC: the layout keeps what was built (as Civil 3D does); picking objects is abandoned.
void CancelAlignmentCommand(AppCommandState& st, std::vector<std::string>& log);
/// Command-line prompt for the two alignment Kinds, or nullptr.
const char* AlignmentCommandPrompt(const AppCommandState& st);
/// "Alignment - (n)" with the first n not already used, like Civil 3D's default name.
std::string NextAlignmentName(const AppCommandState& st);
/// `base`, or "base (2)", "base (3)"... - the first not used; names are unique ignoring case (REQ-480, REQ-487 AC4).
std::string UniqueAlignmentName(const AppCommandState& st, const std::string& base);
/// STATIONFORMAT [DELIMITER=][, DIGITS=][, DECIMALS=][, PREFIX=][, SUFFIX=] | DEFAULT - REQ-483.4.
void RunStationFormat(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
/// ALIGNMENTMETA <alignment>, SET, <name>, <value> | REMOVE, <name> | LIST - REQ-480.1 metadata.
void RunAlignmentMeta(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
/// At the Reverse prompt of CREATEALIGNMENTFROMOBJECTS, the alignment the picks would make (for the direction arrows).
/// False at other times or when the picks cannot make one.
bool AlignmentFromObjectsPreview(const AppCommandState& st, alignment::Alignment* out);
/// The picked start and Reverse answer as CREATEALIGNMENTFROMOBJECTS fields (", PICK=<x> <y>[, REVERSE]"), for the
/// Create Alignment from Objects dialog to submit with its own fields.
std::string AlignmentFromObjectsPickArgs(const AppCommandState& st);
/// The curve radius the layout and Create from Objects use: the settings radius, else 100 ft (30 m metric).
double EffectiveAlignmentCurveRadius(const AppCommandState& st);
/// REQ-488.3: rebuilds every linked offset alignment from its parent (a vanished parent detaches it).
void RefreshAlignmentOffsets(AppCommandState& st, std::vector<std::string>& log);
void RunAlignmentEdit(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentLock(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentInsert(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentRemove(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentContinuity(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunReverseAlignment(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentStation(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentStyle(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentLabelSet(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentLabelStyle(AppCommandState& st, const std::string& args, std::vector<std::string>& log);  // REQ-492.5
void RunAlignmentReport(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunOffsetAlignment(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentDetach(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentIntersect(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunEditAlignment(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunEditAlignmentStyle(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunFeatureLineFromAlignment(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentProps(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentInverse(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentBearing(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentDistance(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentCheckSet(AppCommandState& st, const std::string& args, std::vector<std::string>& log);  // REQ-502
void RunAlignmentCriteria(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentSpeed(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentCheck(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
/// REQ-500: QUICKPROFILE <surface>, <alignment>. False only when no alignment has that name.
bool RunQuickProfileAlongAlignment(AppCommandState& st, const std::string& surfaceName, const std::string& alignmentName,
                                   std::vector<std::string>& log);
/// REQ-498.2: rebuilds the feature lines made from alignments and logs any link it drops.
void RefreshFeatureLineLinks(AppCommandState& st, std::vector<std::string>& log);
/// Offsets and dynamic tables only: the form the erase path uses, so feature links are checked (and logged) once.
void RefreshAlignmentOffsetsOnly(AppCommandState& st, std::vector<std::string>& log);
void RunAlignmentSurface(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
/// REQ-492.5 viewport drag of a station label. Begin returns false when no label lies within \p tolWorld of (lx,ly).
[[nodiscard]] bool TryBeginAlignmentLabelDrag(AppCommandState& st, float lx, float ly, float tolWorld);
void ApplyAlignmentLabelDrag(AppCommandState& st, float lx, float ly);
/// Commit (\p commit true) takes one undo step for the whole drag; cancel puts the label back where it was.
void FinishAlignmentLabelDrag(AppCommandState& st, bool commit, std::vector<std::string>& log);
void RunAlignmentLabel(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunAlignmentTable(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
/// REQ-501.2: "STA 25+42.35  OFF -12.40  N 45d12'30" E" for the alignment nearest the LOCAL point (lx, ly), or an
/// empty string when no alignment lies within \p maxOffset (drawing units). Read-only; used by the status bar.
[[nodiscard]] std::string AlignmentCursorReadout(const AppCommandState& st, double lx, double ly, double maxOffset);
/// REQ-490 station snap: resolve \p displayed on the alignment whose station point is nearest
/// (\p preferLocalX, \p preferLocalY) in storage coordinates.
[[nodiscard]] bool ResolveAlignmentStationSnapPoint(const AppCommandState& st, double displayed, float preferLocalX,
                                                    float preferLocalY, float* outLx, float* outLy);
/// True when \p raw is `STATION …` point entry (case-insensitive prefix).
[[nodiscard]] bool LooksLikeAlignmentStationSnapInput(const std::string& raw);
/// Parses `STATION 12+45.32` into storage coordinates. Returns false if \p raw is not a station line.
[[nodiscard]] bool TryParseAlignmentStationSnapStoragePoint(AppCommandState& st, const std::string& raw, float* lx,
                                                            float* ly, double* outWorldZ, bool allowRelative,
                                                            float baseLocalX, float baseLocalY);
