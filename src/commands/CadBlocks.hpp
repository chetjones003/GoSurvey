#pragma once

#include "util/cadblock.hpp"

#include <istream>
#include <string>
#include <string_view>
#include <vector>

struct AppCommandState;
struct EntityAttributes;

bool CadBlocksTryIdleCommand(AppCommandState& st, const std::string& plotTok, std::istream& args,
                             std::vector<std::string>& log);

int PickCadBlockRefAt(float wx, float wy, const AppCommandState& st, float orthoHalfHeightWorld);

/// Import block definitions from a .gs / .dxf / .dwg path without replacing the current drawing.
/// WBLOCK-style files (geometry in model space, empty BLOCKS table) become one definition named
/// after the file stem.
bool ImportCadBlocksFromPath(AppCommandState& dest, const char* pathUtf8, std::vector<std::string>& log);

/// Open the native block file picker and import the chosen file. Returns false if the user cancelled.
bool CadBlocksImportWithPicker(AppCommandState& dest, std::vector<std::string>& log);

/// Place one INSERT (or explode it). Applies INSUNITS scale. Returns false on a missing name.
bool CadBlockPlaceInsert(AppCommandState& st, std::string_view name, CadBlockXform xf, bool explode,
                         std::vector<std::string>& log);

void StartInsertBlockCommand(AppCommandState& st, std::vector<std::string>& log);
/// Seed INSERT defaults for the current \p insertBlockName (matchlines default to 90°).
void CadBlocksApplyInsertNameDefaults(AppCommandState& st);
void SubmitInsertBlockPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
/// The transform an INSERT preview ghost should be drawn at for the current on-screen pick phase.
/// \p curX / \p curY is the (snapped) cursor. Result includes the block-unit scale, so it matches
/// what CadBlockPlaceInsert will commit within REQ-101 (REQ-107, D-2026-08-29-i). Returns false
/// when no definition is selected or no on-screen pick is in progress.
bool CadBlockInsertPreviewXform(const AppCommandState& st, float curX, float curY, CadBlockXform* out);
/// OK on the Insert dialog: place now, or start on-screen point/scale/rotation picks.
void CadBlocksCommitInsertDialog(AppCommandState& st, std::vector<std::string>& log);
void CadBlocksCommitInsertAttrDialog(AppCommandState& st, std::vector<std::string>& log);
void CadBlocksPlacePendingInsert(AppCommandState& st, std::vector<std::string>& log);
/// Arm a dynamic INSERT grip. Returns false when the grip is a click-toggle (flip), so no drag.
bool CadBlockArmDynGrip(AppCommandState& st, int refIndex, int which);
void CadBlockRestoreDynGripOrig(AppCommandState& st, CadBlockRef* r);

/// Merge bundled `resources/blocks/*.{gs,dxf}` into \p dest. Skips names that already exist.
void LoadBundledBlockLibrary(AppCommandState& dest, std::vector<std::string>& log);

/// Civil 3D-style “Edit Block Definition” picker (BEDIT with no name, ribbon BEDIT).
void CadBlocksCollectEditPickerNames(const AppCommandState& st, std::vector<std::string>* names);
void CadBlocksOpenEditPicker(AppCommandState& st, std::vector<std::string>& log);
void CadBlocksEnterNamedEditor(AppCommandState& st, std::string_view name, std::vector<std::string>& log);
void CadBlocksCommitEditPicker(AppCommandState& st, std::vector<std::string>& log);
