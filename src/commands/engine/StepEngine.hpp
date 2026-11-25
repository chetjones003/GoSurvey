#pragma once
// ADR-0012 / Issue #1578: Shared step engine entry points.

#include "engine/CommandSession.hpp"
#include "CadDynInput.hpp"

#include <memory>
#include <string>
#include <vector>

struct AppCommandState;

namespace stepengine {

/// Starts a new active command session. Silently resets any previous active session first,
/// clears pending one-shot snaps, sets st.active = session->kind(), and records st.lastCommand
/// when the command is repeatable.
void BeginSession(AppCommandState& st, std::unique_ptr<CommandSession> session,
                  bool recordAsLastCommand = true);

/// Ends the active command session cleanly on normal completion, dropping its state struct.
void EndSession(AppCommandState& st);

/// Silently drops any active session (calling session->onReset(st)) without logging.
void ResetActiveSession(AppCommandState& st);

/// Cancels the active session (calling session->onCancel(st, log)) and drops its state struct.
bool CancelActiveSession(AppCommandState& st, std::vector<std::string>& log);

/// Shared selection-box close-out used by SelectObjects steps and idle selection.
void FinishSelectionBox(AppCommandState& st, double wx, double wy, bool windowSelectionSubtract,
                        bool fenceLeftToRightWindowMode, bool includeSurvey,
                        std::vector<std::string>& log);
inline void FinishSelectionBox(AppCommandState& st, const StepPickInput& pick, bool includeSurvey,
                               std::vector<std::string>& log) {
  FinishSelectionBox(st, pick.localX, pick.localY, pick.windowSelectionSubtract,
                     pick.fenceLeftToRightWindowMode, includeSurvey, log);
}

/// Queries the active session's click route (or IdleSelection if no session is active).
[[nodiscard]] StepClickRoute ActiveClickRoute(const AppCommandState& st);

/// True when the active session wants unsnapped world coordinates for a selection rectangle.
[[nodiscard]] bool ActiveUseRawWorldForSelectionRect(const AppCommandState& st);

/// True when the active session's selection box is ZOOM WINDOW's region box (not object selection).
[[nodiscard]] bool ActiveIsZoomRegionBox(const AppCommandState& st);

/// Dynamic input prompt metadata derived from the active session's current step.
[[nodiscard]] bool ActiveExpectsPointEntry(const AppCommandState& st);
[[nodiscard]] bool ActiveUcsPolarPromptBase(const AppCommandState& st, ray3d::Vec3* baseWorld);
[[nodiscard]] CadDynInputPrompt ActiveDynInputPromptFor(const AppCommandState& st);

/// Single source of truth for the active command's prompt / footer hint.
[[nodiscard]] std::string ActivePromptHint(const AppCommandState& st);

/// Shared unparsed-input reporter (checks if the user typed another command's name).
void ReportUnparsedInput(const AppCommandState& st, const std::string& line,
                         const std::string& fallback, std::vector<std::string>& log,
                         std::size_t markBeforeHandler);

/// Shared step-kind parsers used by ScriptedStepSession and command modules.
[[nodiscard]] bool ParseDistanceToken(const std::string& raw, bool allowZero, bool allowNegative,
                                      double* outDistance);
[[nodiscard]] bool ParseAngleToken(const std::string& raw, double* outDegrees, double* outRadians);
[[nodiscard]] const KeywordOption* MatchKeywordOption(const std::string& raw,
                                                      const std::vector<KeywordOption>& options);

/// Routes one command-line submit through the active session (one Enter = one submit) or,
/// when idle, through CommandRegistry.
void SubmitCommandLine(AppCommandState& st, const std::string& rawLine,
                       std::vector<std::string>& log);

/// Routes one viewport pick through the active session (or idle selection box).
void SubmitViewportPick(AppCommandState& st, const StepPickInput& pick,
                        std::vector<std::string>& log);

} // namespace stepengine

/// Single public prompt/footer hint query for the UI (replaces the 12 *FooterHint functions).
std::string ActiveCommandPromptHint(const AppCommandState& st);
