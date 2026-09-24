#pragma once

#include "util/cadblock.hpp"
#include "util/ray3d.hpp"
#include "util/solidpick.hpp"

#include <filesystem>
#include <istream>
#include <string>
#include <string_view>
#include <vector>

struct AppCommandState;
struct EntityAttributes;

bool CadBlocksTryIdleCommand(AppCommandState& st, const std::string& plotTok, std::istream& args,
                             std::vector<std::string>& log);

int PickCadBlockRefAt(float wx, float wy, const AppCommandState& st, float orthoHalfHeightWorld);

/// Import block definitions from a .dxf / .dwg path without replacing the current drawing.
/// WBLOCK-style files (geometry in model space, empty BLOCKS table) become one definition named
/// after the file stem.
bool ImportCadBlocksFromPath(AppCommandState& dest, const char* pathUtf8, std::vector<std::string>& log);

/// Open the native block file picker and import the chosen file. Returns false if the user cancelled.
bool CadBlocksImportWithPicker(AppCommandState& dest, std::vector<std::string>& log);

/// Place one INSERT (or explode it). Applies INSUNITS scale. Returns false on a missing name.
bool CadBlockPlaceInsert(AppCommandState& st, std::string_view name, CadBlockXform xf, bool explode,
                         std::vector<std::string>& log);
/// Same as `CadBlockPlaceInsert`, without exploding, and WITHOUT pushing its own undo snapshot —
/// for a caller (PIPERUN's auto-fitting, issue #486 increment B5) that places several elbows as
/// part of one larger multi-step operation already covered by its own single `PushUndoSnapshot`.
bool CadBlockPlaceInsertNoUndo(AppCommandState& st, std::string_view name, CadBlockXform xf,
                               std::vector<std::string>& log);

void StartInsertBlockCommand(AppCommandState& st, std::vector<std::string>& log);
void StartBlockCreateDialog(AppCommandState& st, std::vector<std::string>& log);

/// Write one block definition out as its own `.dwg` (issue #284). The single write path shared by
/// the typed `WBLOCK <name>, <path.dwg>` form and the save dialog; it reports its own failures.
bool CadBlocksWriteBlockToFile(AppCommandState& st, std::string_view name, const char* pathUtf8,
                               std::vector<std::string>& log);
/// Open the WBLOCK save dialog (bare `WBLOCK`). A no-op, with a stated reason, when the drawing has
/// no block definitions.
void StartWblockDialog(AppCommandState& st, std::vector<std::string>& log);
/// Write the dialog's chosen block to its chosen path. The dialog stays open if the write fails.
void CommitWblockDialog(AppCommandState& st, std::vector<std::string>& log);
void CancelWblockDialog(AppCommandState& st, std::vector<std::string>& log);
void SubmitBlockCreateBasePointPick(AppCommandState& st, float wx, float wy, float wz, std::vector<std::string>& log);
void SubmitBlockCreateBasePointPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
void CommitBlockCreateDialog(AppCommandState& st, std::vector<std::string>& log);
void CancelBlockCreateDialog(AppCommandState& st, std::vector<std::string>& log);
/// Seed INSERT defaults for the current \p insertBlockName (matchlines default to 90°).
void CadBlocksApplyInsertNameDefaults(AppCommandState& st);
void SubmitInsertBlockPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
void SubmitInsertBlockPick(AppCommandState& st, float wx, float wy, float wz, std::vector<std::string>& log);
/// The transform an INSERT preview ghost should be drawn at for the current on-screen pick phase.
/// \p curX / \p curY is the (snapped) cursor. Result includes the block-unit scale, so it matches
/// what CadBlockPlaceInsert will commit within REQ-101 (REQ-107, D-2026-08-29-i). Returns false
/// when no definition is selected or no on-screen pick is in progress.
bool CadBlockInsertPreviewXform(const AppCommandState& st, float curX, float curY, CadBlockXform* out);
bool CadBlockInsertPreviewXform(const AppCommandState& st, float curX, float curY, float curZ, CadBlockXform* out);
/// Wireframe ghost for INSERT on-screen picks: 2D linework + transformed B-rep edges (issue #475).
void AppendInsertBlockGhostRubber(const AppCommandState& st, const CadBlockXform& xf,
                                  std::vector<float>& rubberLines);
/// Face pick during WaitAlignFace — sets rotX/rotY from the face outward normal (planar faces only).
bool SubmitInsertBlockAlignFacePick(AppCommandState& st, const ray3d::Ray& ray, const solidpick::Tolerance& tol,
                                    std::vector<std::string>& log);
/// Target connection pick during WaitConnectorTarget (issue #475 inc5).
bool SubmitInsertBlockConnectorPick(AppCommandState& st, float wx, float wy, float wz, std::vector<std::string>& log);
/// Face pick during BCONNECT authoring in BEDIT.
bool SubmitBconnectFacePick(AppCommandState& st, const ray3d::Ray& ray, const solidpick::Tolerance& tol,
                            std::vector<std::string>& log);
/// OK on the Insert dialog: place now, or start on-screen point/scale/rotation picks.
void CadBlocksCommitInsertDialog(AppCommandState& st, std::vector<std::string>& log);
void CadBlocksCommitInsertAttrDialog(AppCommandState& st, std::vector<std::string>& log);
void CadBlocksPlacePendingInsert(AppCommandState& st, std::vector<std::string>& log);
/// Arm a dynamic INSERT grip. Returns false when the grip is a click-toggle (flip), so no drag.
bool CadBlockArmDynGrip(AppCommandState& st, int refIndex, int which);
void CadBlockRestoreDynGripOrig(AppCommandState& st, CadBlockRef* r);

/// Merge bundled `resources/blocks/*.{gs,dxf}` into \p dest. Skips names that already exist.
void LoadBundledBlockLibrary(AppCommandState& dest, std::vector<std::string>& log);

/// One row in the INSERT dialog library pane (drawing defs + bundled files not yet imported).
/// `partType`/`nominalSize`/`pressureClass` (issue #486 increment A5) come from the definition
/// itself when already imported, or from a LIBEXPORT `.json` sidecar beside an unimported file —
/// read without importing, so the library pane can filter before the user picks anything.
struct CadBlockLibraryEntry {
  std::string name;
  std::string path;
  bool imported = false;
  bool isFitting = false;
  CadPipePartType partType = CadPipePartType::None;
  std::string nominalSize;
  CadPipePressureClass pressureClass = CadPipePressureClass::None;
};

void CadBlocksCollectLibraryEntries(const AppCommandState& st, std::vector<CadBlockLibraryEntry>* out);
/// Catalog lookup (issue #486 increment B4 / REQ-345): maps (size, class, part type) to a block
/// definition name in the bundled/user fittings library, importing it into \p st.blockDefs if it
/// was found but not yet imported. Refuses (returns false, reason pushed to \p log) when nothing
/// matches, or when the match is ambiguous (more than one candidate) — never guesses among several.
/// \p pressureClass may be `None` to mean "no restriction"; a specific class prefers an exact-tagged
/// part and falls back to a class-agnostic one only if no exact match exists, mirroring
/// `CadBlockResolveMode`'s own exact-then-default precedent.
bool CadPipeCatalogFind(AppCommandState& st, CadPipePartType partType, const std::string& nominalSize,
                        CadPipePressureClass pressureClass, std::string* outBlockName,
                        std::vector<std::string>& log);
/// `<UserDataDirectory>/blocks/fittings` — where LIBEXPORT writes user-authored fitting parts
/// (issue #486 increment A3). Empty if the user data directory cannot be determined.
[[nodiscard]] std::filesystem::path CadFittingLibraryExportDir();
/// Import a bundled library file when the user picks an entry that is not yet in \p st.blockDefs.
bool CadBlocksImportLibraryEntry(AppCommandState& st, const CadBlockLibraryEntry& entry, std::vector<std::string>& log);

/// One right-hand tab of the Pipe Fittings palette (REQ-350 (b), D-2026-09-24-a (1)). A tab is a
/// CATEGORY, grouping several `CadPipePartType` values, because one tab per part type is ten vertical
/// tabs in a tall window of which most are empty in any real library.
enum class CadPipePaletteCategory : std::uint8_t { Fittings = 0, Flanges, Valves, Nozzles, Other };
inline constexpr int kCadPipePaletteCategoryCount = 5;

/// The tab's label ("Flanges"), and the lower-case plural it reads as in prose ("flanges") for the
/// empty-tab sentence. Two functions rather than one label plus `toLower` at the call site, because
/// "Other" pluralises as "other parts", not "others".
[[nodiscard]] std::string_view CadPipePaletteCategoryLabel(CadPipePaletteCategory c);
[[nodiscard]] std::string_view CadPipePaletteCategoryPlural(CadPipePaletteCategory c);

/// Which tab \p t files under. Total by construction — `None` and anything the enum gains later
/// answer `Other`, so a part type added without touching this function lists somewhere visible
/// instead of vanishing from every tab.
[[nodiscard]] CadPipePaletteCategory CadPipePaletteCategoryOf(CadPipePartType t);

/// The rows one palette tab shows (REQ-350 (c)/(d)): the library entries that are piping parts, file
/// under \p category, and match \p runNominalSize EXACTLY — compared through
/// `CadParsePipeNominalSizeInches` on both sides, so `2in`, `2 in` and `2.0in` are one size and a
/// string compare cannot make them three.
///
/// Pressure class follows `CadPipeCatalogFind`'s own recorded precedence rather than a second rule
/// invented here: with a class on the run, a part tagged with that exact class wins, a part tagged
/// class-agnostic is accepted only when no exact-class part of **the same part type** exists, and a
/// part tagged with a DIFFERENT class never matches. Per part type, not per tab — a Fittings tab
/// holding a CS150 elbow and an untagged tee must still show the tee, which a per-tab scope would
/// hide.
///
/// An unparsable or empty \p runNominalSize yields NO rows rather than every row: "we don't know the
/// size" must not read as "here is the whole library" (REQ-201's spirit — never present a guess as an
/// answer). Clears \p out first.
void CadPipePaletteCollectRows(const std::vector<CadBlockLibraryEntry>& entries,
                               std::string_view runNominalSize, CadPipePressureClass runClass,
                               CadPipePaletteCategory category, std::vector<CadBlockLibraryEntry>* out);

/// The sentence an empty tab shows instead of a blank pane (REQ-350 (g)) — it names the size that was
/// filtered on and what to do about it, because "your library has no 2in valve" and "this window is
/// broken" look identical otherwise.
[[nodiscard]] std::string CadPipePaletteEmptyReason(CadPipePaletteCategory category,
                                                   std::string_view runNominalSize);

/// REQ-350 (a) — open or close the Pipe Fittings palette (the PIPEPALETTE command, and the same call
/// PIPERUN makes to open it).
void CadPipePaletteSetOpen(AppCommandState& st, bool open, std::vector<std::string>& log);

/// REQ-350 (f) — arm \p entry for placement from the palette: import it if the library has not been
/// read into this drawing yet, then put INSERT into its single-click, no-scale/rotation-prompt state
/// with the pipe-splice flag set. The next viewport pick places it — spliced into a run if it lands on
/// one, free-standing if it does not.
///
/// Returns false, with the reason logged, when the part cannot be imported (REQ-201: a named refusal,
/// never a silently armed command that would place the wrong thing or nothing at all).
bool CadPipePaletteArmPart(AppCommandState& st, const CadBlockLibraryEntry& entry,
                           std::vector<std::string>& log);
/// Block-unit scale for INSERT: honours \c insertBlockUnitsBuf when set (issue #475 inc6).
[[nodiscard]] float CadBlockInsertUnitsScale(const AppCommandState& st, const CadBlockDefinition& def);

/// Civil 3D-style “Edit Block Definition” picker (BEDIT with no name, ribbon BEDIT).
void CadBlocksCollectEditPickerNames(const AppCommandState& st, std::vector<std::string>* names);
void CadBlocksOpenEditPicker(AppCommandState& st, std::vector<std::string>& log);
void CadBlocksEnterNamedEditor(AppCommandState& st, std::string_view name, std::vector<std::string>& log);
void CadBlocksCommitEditPicker(AppCommandState& st, std::vector<std::string>& log);

/// BCONNECTMODE wizard (issue #496): one prompt per value instead of a comma-separated line.
/// Called from the idle command dispatcher to start the wizard (\p connNameArg pre-fills the first
/// prompt when the caller already has it, e.g. `BCONNECTMODE P1` typed directly).
void BConnectModeStart(AppCommandState& st, const std::string& connNameArg, std::vector<std::string>& log);
/// Routes one typed line to whichever \c BConnectModePhase is active. Call only when
/// `st.active == AppCommandState::Kind::BConnectMode`.
void BConnectModeSubmitLine(AppCommandState& st, const std::string& line, std::vector<std::string>& log);

/// BCONNECT wizard (issue #496): one prompt per value, ending in either a face pick (hands off to
/// the existing \ref SubmitBconnectFacePick pick mechanism) or typed coordinates.
void BConnectStart(AppCommandState& st, const std::string& nameArg, std::vector<std::string>& log);
/// Routes one typed line to whichever \c BConnectPhase is active. Call only when
/// `st.active == AppCommandState::Kind::BConnect`.
void BConnectSubmitLine(AppCommandState& st, const std::string& line, std::vector<std::string>& log);

/// BCONNECTEDIT wizard (issue #496): one prompt per value, with the connection point's current
/// value shown and Enter-to-keep, plus a remove option.
void BConnectEditStart(AppCommandState& st, const std::string& connNameArg, std::vector<std::string>& log);
/// Routes one typed line to whichever \c BConnectEditPhase is active. Call only when
/// `st.active == AppCommandState::Kind::BConnectEdit`.
void BConnectEditSubmitLine(AppCommandState& st, const std::string& line, std::vector<std::string>& log);

/// BLOCKFITTING wizard (issue #496): one prompt per value, with the current value shown and
/// Enter-to-keep.
void BlockFittingStart(AppCommandState& st, std::vector<std::string>& log);
/// Routes one typed line to whichever \c BlockFittingPhase is active. Call only when
/// `st.active == AppCommandState::Kind::BlockFitting`.
void BlockFittingSubmitLine(AppCommandState& st, const std::string& line, std::vector<std::string>& log);
