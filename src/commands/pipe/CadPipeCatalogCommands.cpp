#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "GoSurvey/Document/cadpiperun.hpp"
#include <GoSurvey/Core/StringUtil.hpp>
#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

std::string_view CadPipePaletteCategoryLabel(CadPipePaletteCategory c) {
  switch (c) {
    case CadPipePaletteCategory::Flanges: return "Flanges";
    case CadPipePaletteCategory::Valves: return "Valves";
    case CadPipePaletteCategory::Nozzles: return "Nozzles";
    case CadPipePaletteCategory::Other: return "Other";
    case CadPipePaletteCategory::Fittings:
    default: return "Fittings";
  }
}

std::string_view CadPipePaletteCategoryPlural(CadPipePaletteCategory c) {
  switch (c) {
    case CadPipePaletteCategory::Flanges: return "flanges";
    case CadPipePaletteCategory::Valves: return "valves";
    case CadPipePaletteCategory::Nozzles: return "nozzles";
    case CadPipePaletteCategory::Other: return "other parts";
    case CadPipePaletteCategory::Fittings:
    default: return "fittings";
  }
}

CadPipePaletteCategory CadPipePaletteCategoryOf(CadPipePartType t) {
  switch (t) {
    case CadPipePartType::Elbow90:
    case CadPipePartType::Elbow45:
    case CadPipePartType::Tee:
    case CadPipePartType::Cross:
    case CadPipePartType::Reducer:
    case CadPipePartType::Coupling:
      return CadPipePaletteCategory::Fittings;
    case CadPipePartType::Flange:
      return CadPipePaletteCategory::Flanges;
    case CadPipePartType::Valve:
      return CadPipePaletteCategory::Valves;
    case CadPipePartType::Nozzle:
      return CadPipePaletteCategory::Nozzles;
    case CadPipePartType::Cap:
    case CadPipePartType::Other:
    case CadPipePartType::None:
    default:
      return CadPipePaletteCategory::Other;
  }
}

void CadPipePaletteCollectRows(const std::vector<CadBlockLibraryEntry>& entries,
                               std::string_view runNominalSize, CadPipePressureClass runClass,
                               CadPipePaletteCategory category, std::vector<CadBlockLibraryEntry>* out) {
  assert(out != nullptr);
  out->clear();
  double runNps = 0.0;
  if (!CadParsePipeNominalSizeInches(runNominalSize, &runNps))
    return;  // no usable size — offer nothing rather than the whole library

  for (const CadBlockLibraryEntry& e : entries) {
    // An ordinary (non-fitting) block is not a catalog part, and an untagged fitting has no category
    // to file under — neither belongs in a palette that exists to answer "what fits this pipe?".
    if (!e.isFitting || e.partType == CadPipePartType::None)
      continue;
    if (CadPipePaletteCategoryOf(e.partType) != category)
      continue;
    double nps = 0.0;
    if (!CadParsePipeNominalSizeInches(e.nominalSize, &nps))
      continue;
    if (std::fabs(nps - runNps) > 1e-9)
      continue;
    out->push_back(e);
  }

  if (runClass == CadPipePressureClass::None)
    return;  // the run states no class, so every size match stands

  // `CadPipeCatalogFind`'s precedence, scoped per part type (see the header's note on why not per
  // tab). O(n^2) over the handful of parts one tab shows, which is cheaper to read than a grouping
  // pass and measurably free at this size.
  std::vector<CadBlockLibraryEntry> kept;
  kept.reserve(out->size());
  for (const CadBlockLibraryEntry& e : *out) {
    if (e.pressureClass == runClass) {
      kept.push_back(e);
      continue;
    }
    if (e.pressureClass != CadPipePressureClass::None)
      continue;  // tagged for a DIFFERENT class — never a match, same as the catalog lookup
    bool exactExists = false;
    for (const CadBlockLibraryEntry& o : *out) {
      if (o.partType == e.partType && o.pressureClass == runClass) {
        exactExists = true;
        break;
      }
    }
    if (!exactExists)
      kept.push_back(e);
  }
  *out = std::move(kept);
}

std::string CadPipePaletteEmptyReason(CadPipePaletteCategory category, std::string_view runNominalSize) {
  const std::string size = StringUtil::trimCopy(std::string(runNominalSize));
  if (size.empty())
    return "No pipe size yet — start PIPERUN and choose a size to see the parts that fit it.";
  return "No " + size + " " + std::string(CadPipePaletteCategoryPlural(category)) +
         " in the fittings library. Tag a part with BLOCKFITTING and LIBEXPORT it, or drop a .json "
         "sidecar beside the file (see resources/blocks/fittings/README.md).";
}

void CadPipePaletteSetOpen(AppCommandState& st, bool open, std::vector<std::string>& log) {
  if (st.pipeFittingPaletteOpen == open)
    return;
  st.pipeFittingPaletteOpen = open;
  log.push_back(open ? "Pipe Fittings palette opened (PIPEPALETTE closes it)."
                     : "Pipe Fittings palette closed (PIPEPALETTE reopens it).");
}

bool CadPipePaletteArmPart(AppCommandState& st, const CadBlockLibraryEntry& entry,
                           std::vector<std::string>& log) {
  if (entry.name.empty()) {
    log.push_back("Pipe Fittings — that row has no block name.");
    return false;
  }
  // A library row may not be in this drawing's block table yet. Import it now, exactly as picking it
  // in the INSERT dialog does — and refuse by name if that fails, rather than arming a command that
  // would have nothing to place (REQ-201).
  if (CadBlockFindDef(st.drawing.blockDefs, entry.name) < 0 && !CadBlocksImportLibraryEntry(st, entry, log)) {
    log.push_back("Pipe Fittings — could not import \"" + entry.name + "\" from the library.");
    return false;
  }

  // Picking a part takes the command away from whatever holds it. If that is an open PIPERUN,
  // FINISH the route first (D-2026-09-24-d) — before the route was a real entity this threw away the
  // pipe the user had just drawn, which is precisely the reported "the pipe run disappears".
  (void)CadPipeRunFinishForHandoff(st, log);

  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "%s", entry.name.c_str());
  // One click places it: no scale, rotation or face-align prompt, so `InsertAdvanceAfterPoint` goes
  // straight to placing. A fitting's size comes from the catalog, not from a dragged scale, and its
  // orientation comes from the port it snaps to.
  st.insertBlockSpecifyScale = false;
  st.insertBlockSpecifyRot = false;
  st.insertBlockSpecifyAlignFace = false;
  st.insertBlockExplode = false;
  // Every port on the part is a candidate for the connector snap the click may take
  // (`SubmitInsertBlockPick`), so whichever one's own configured mode matches what is under the
  // cursor wins. A name left behind in the INSERT dialog would pin the search to that one port
  // instead — and on a part it does not even belong to, refuse the snap outright.
  st.insertBlockConnectorName[0] = '\0';
  st.insertBlockSpecifyConnectorSnap = false;  // the palette drives the snap itself, per click
  st.insertBlockRotXDeg = 0.f;
  st.insertBlockRotYDeg = 0.f;
  st.insertBlockSx = 1.f;
  st.insertBlockSy = 1.f;
  st.insertBlockSz = 1.f;
  st.insertBlockUniformScale = true;
  st.insertBlockRotDeg = 0.f;
  st.insertBlockDialogOpen = false;
  st.insertBlockPipeSpliceArmed = true;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitInsertPoint;
  st.active = AppCommandState::Kind::InsertBlock;
  log.push_back("Pipe Fittings — \"" + entry.name +
                "\": click on a pipe run to splice it in, on a connection port or pipe end to snap "
                "onto it, or anywhere else to place it (ESC cancels).");
  return true;
}

bool CadPipeCatalogFind(AppCommandState& st, CadPipePartType partType, const std::string& nominalSize,
                        CadPipePressureClass pressureClass, std::string* outBlockName,
                        std::vector<std::string>& log) {
  if (!outBlockName)
    return false;
  outBlockName->clear();
  if (partType == CadPipePartType::None) {
    log.push_back("PIPECATALOG - a part type is required.");
    return false;
  }
  if (nominalSize.empty()) {
    log.push_back("PIPECATALOG - a nominal size is required.");
    return false;
  }

  std::vector<CadBlockLibraryEntry> entries;
  CadBlocksCollectLibraryEntries(st, &entries);

  std::vector<const CadBlockLibraryEntry*> sizeTypeMatches;
  for (const CadBlockLibraryEntry& e : entries) {
    if (e.isFitting && e.partType == partType && e.nominalSize == nominalSize)
      sizeTypeMatches.push_back(&e);
  }

  const std::string partTag(CadPipePartTypeTag(partType));
  const std::string classDesc = pressureClass == CadPipePressureClass::None
                                    ? std::string()
                                    : (" " + std::string(CadPipePressureClassTag(pressureClass)));
  const auto refuseNoMatch = [&]() {
    log.push_back("PIPECATALOG - no " + partTag + " found for " + nominalSize + classDesc +
                  ". Import or LIBEXPORT a matching fitting first, or PIPECATALOG with a different size/class.");
    return false;
  };
  if (sizeTypeMatches.empty())
    return refuseNoMatch();

  // A requested class prefers an EXACT match; if none is tagged for that class, fall back to a
  // class-agnostic part (pressureClass == None on the library entry) rather than refusing outright
  // — the same exact-then-default precedent `CadBlockResolveMode` already uses for connection modes.
  // No requested class (None) means no restriction at all: every size/type match qualifies.
  std::vector<const CadBlockLibraryEntry*> chosen;
  if (pressureClass != CadPipePressureClass::None) {
    for (const CadBlockLibraryEntry* e : sizeTypeMatches)
      if (e->pressureClass == pressureClass)
        chosen.push_back(e);
    if (chosen.empty()) {
      for (const CadBlockLibraryEntry* e : sizeTypeMatches)
        if (e->pressureClass == CadPipePressureClass::None)
          chosen.push_back(e);
    }
  } else {
    chosen = sizeTypeMatches;
  }
  if (chosen.empty())
    return refuseNoMatch();

  if (chosen.size() > 1) {
    std::string names;
    for (size_t i = 0; i < chosen.size(); ++i) {
      if (i)
        names += ", ";
      names += chosen[i]->name;
    }
    // REQ-201: nothing invalid — or ambiguous — is ever guessed. A duplicate-classed catalog is a
    // library authoring problem for the user to fix, not something this lookup silently resolves.
    log.push_back("PIPECATALOG - ambiguous: " + std::to_string(chosen.size()) + " parts match " +
                  partTag + " " + nominalSize + classDesc + " (" + names +
                  "). Narrow the pressure class, or remove/retag the duplicate in the library.");
    return false;
  }

  const CadBlockLibraryEntry entry = *chosen.front();  // copy: CadBlocksImportLibraryEntry mutates st
  if (!CadBlocksImportLibraryEntry(st, entry, log)) {
    log.push_back("PIPECATALOG - found \"" + entry.name + "\" but it failed to import.");
    return false;
  }
  *outBlockName = entry.name;
  log.push_back("PIPECATALOG - " + partTag + " " + nominalSize + classDesc + " -> \"" + entry.name + "\".");
  return true;
}
