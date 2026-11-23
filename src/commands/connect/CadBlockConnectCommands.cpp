#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "ProjectWarnings.hpp"  // REQ-383: CheckClipboardPaste, the paste guard PASTEBLOCK shares with PASTE
#include <GoSurvey/Core/StringUtil.hpp>
#include <GoSurvey/Renderer/CadSolidDisplay.hpp>
#include "GoSurvey/Document/cadpiperun.hpp"  // CadPipeRunEndPorts (issue #486, "pipe end" connection port)
#include "GsIo.hpp"
#include "DxfIo.hpp"
#include "DwgIo.hpp"
#include <GoSurvey/UI/FileDialogs.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "CadBlockConnectInternal.hpp"

void BConnectModeStart(AppCommandState& st, const std::string& connNameArg, std::vector<std::string>& log) {
  using Ph = AppCommandState::BConnectModePhase;
  if (st.blockEditorName.empty()) {
    log.push_back("BCONNECTMODE — open a block with BEDIT first.");
    return;
  }
  stepengine::BeginSession(st, std::make_unique<stepengine::BConnectModeSession>(), /*recordAsLastCommand=*/false);
  st.bconnectModePhase = Ph::WaitConnName;
  st.bconnectModeConnName.clear();
  st.bconnectModeModeName.clear();
  st.bconnectModeEditingExisting = false;
  st.bconnectModeTargetPending = CadConnectionModeTarget::GenericPort;
  st.bconnectModeRolePending = CadBlockConnectionRole::Inlet;
  st.bconnectModeEngagementPending = 0.f;
  st.bconnectModeCompatTagPending.clear();
  if (!connNameArg.empty()) {
    // Power-user shortcut (also how the ribbon button and BCONNECTMODE <name> jump straight past
    // the first prompt): reuse the phase handler itself so the two entry points can't drift apart.
    BConnectModeSubmitLine(st, connNameArg, log);
    return;
  }
  log.push_back("BCONNECTMODE — connection point name:");
}

void BConnectModeSubmitLine(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  using Ph = AppCommandState::BConnectModePhase;
  const std::string line = StringUtil::trimCopy(lineIn);
  const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
  if (di < 0) {
    log.push_back("BCONNECTMODE — the block being edited no longer exists.");
    stepengine::EndSession(st);
    return;
  }
  CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];

  if (st.bconnectModePhase == Ph::WaitConnName) {
    if (line.empty()) {
      log.push_back("BCONNECTMODE — connection point name:");
      return;
    }
    const int ci = CadBlockFindConnection(def, line);
    if (ci < 0) {
      log.push_back("BCONNECTMODE — no connection named \"" + line + "\" on \"" + st.blockEditorName + "\".");
      stepengine::EndSession(st);
      return;
    }
    st.bconnectModeConnName = line;
    const CadBlockConnection& conn = def.connections[static_cast<size_t>(ci)];
    if (!conn.modes.empty()) {
      std::string existing = "BCONNECTMODE — \"" + line + "\" already has: ";
      for (size_t i = 0; i < conn.modes.size(); ++i) {
        if (i)
          existing += ", ";
        existing += conn.modes[i].name;
        if (conn.modes[i].isDefault)
          existing += " (default)";
      }
      log.push_back(existing);
    }
    st.bconnectModePhase = Ph::WaitModeName;
    log.push_back("BCONNECTMODE — mode name (existing name to edit/remove, new name to add):");
    return;
  }

  const int ci = CadBlockFindConnection(def, st.bconnectModeConnName);
  if (ci < 0) {
    log.push_back("BCONNECTMODE — connection \"" + st.bconnectModeConnName + "\" is gone; canceled.");
    stepengine::EndSession(st);
    return;
  }
  CadBlockConnection& conn = def.connections[static_cast<size_t>(ci)];

  if (st.bconnectModePhase == Ph::WaitModeName) {
    if (line.empty()) {
      log.push_back("BCONNECTMODE — mode name (existing name to edit/remove, new name to add):");
      return;
    }
    st.bconnectModeModeName = line;
    int mi = -1;
    for (int i = 0; i < static_cast<int>(conn.modes.size()); ++i) {
      if (CadBlockEqCi(conn.modes[static_cast<size_t>(i)].name, line)) {
        mi = i;
        break;
      }
    }
    st.bconnectModeEditingExisting = mi >= 0;
    if (mi >= 0) {
      const CadBlockConnectionMode& m = conn.modes[static_cast<size_t>(mi)];
      st.bconnectModeTargetPending = m.target;
      st.bconnectModeRolePending = m.role;
      st.bconnectModeEngagementPending = m.engagementLength;
      st.bconnectModeCompatTagPending = m.compatibilityTag;
      st.bconnectModePhase = Ph::WaitRemoveConfirm;
      log.push_back("BCONNECTMODE — remove mode \"" + line + "\"? (y/N):");
      return;
    }
    st.bconnectModePhase = Ph::WaitTarget;
    log.push_back("BCONNECTMODE — snap target for \"" + line + "\" (" + BConnectModeTargetPromptList() + "):");
    return;
  }

  if (st.bconnectModePhase == Ph::WaitRemoveConfirm) {
    if (CadBlockEqCi(line, "y") || CadBlockEqCi(line, "yes")) {
      int mi = -1;
      for (int i = 0; i < static_cast<int>(conn.modes.size()); ++i) {
        if (CadBlockEqCi(conn.modes[static_cast<size_t>(i)].name, st.bconnectModeModeName)) {
          mi = i;
          break;
        }
      }
      if (mi >= 0) {
        conn.modes.erase(conn.modes.begin() + mi);
        st.blockEditorDirty = true;
        log.push_back("BCONNECTMODE — removed mode \"" + st.bconnectModeModeName + "\".");
      }
      stepengine::EndSession(st);
      return;
    }
    st.bconnectModePhase = Ph::WaitTarget;
    log.push_back("BCONNECTMODE — snap target for \"" + st.bconnectModeModeName + "\" [" +
                  std::string(CadConnectionModeTargetTag(st.bconnectModeTargetPending)) + "] (" +
                  BConnectModeTargetPromptList() + ", Enter to keep current):");
    return;
  }

  if (st.bconnectModePhase == Ph::WaitTarget) {
    if (!line.empty())
      st.bconnectModeTargetPending = ParseCadConnectionModeTarget(line);
    st.bconnectModePhase = Ph::WaitRole;
    log.push_back("BCONNECTMODE — role [" + std::string(CadBlockConnectionRoleTag(st.bconnectModeRolePending)) +
                  "] (" + BConnectModeRolePromptList() + ", Enter to keep current):");
    return;
  }

  if (st.bconnectModePhase == Ph::WaitRole) {
    if (!line.empty())
      st.bconnectModeRolePending = ParseCadBlockConnectionRole(line);
    st.bconnectModePhase = Ph::WaitEngagement;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", st.bconnectModeEngagementPending);
    log.push_back(std::string("BCONNECTMODE — engagement length [") + buf + "] (Enter to keep current):");
    return;
  }

  if (st.bconnectModePhase == Ph::WaitEngagement) {
    if (!line.empty()) {
      float v = 0.f;
      if (!TryParseF(line, v)) {
        log.push_back("BCONNECTMODE — engagement length must be a number.");
        return;
      }
      st.bconnectModeEngagementPending = v;
    }
    st.bconnectModePhase = Ph::WaitCompatTag;
    log.push_back("BCONNECTMODE — compatibility tag [" + st.bconnectModeCompatTagPending +
                  "] (Enter to keep current, type \"-\" to clear):");
    return;
  }

  if (st.bconnectModePhase == Ph::WaitCompatTag) {
    if (line == "-")
      st.bconnectModeCompatTagPending.clear();
    else if (!line.empty())
      st.bconnectModeCompatTagPending = line;
    st.bconnectModePhase = Ph::WaitIsDefault;
    log.push_back("BCONNECTMODE — make \"" + st.bconnectModeModeName + "\" the default mode? (y/N):");
    return;
  }

  if (st.bconnectModePhase == Ph::WaitIsDefault) {
    const bool makeDefault = (CadBlockEqCi(line, "y") || CadBlockEqCi(line, "yes"));
    BConnectModeCommit(st, conn);
    int mi = -1;
    for (int i = 0; i < static_cast<int>(conn.modes.size()); ++i) {
      if (CadBlockEqCi(conn.modes[static_cast<size_t>(i)].name, st.bconnectModeModeName)) {
        mi = i;
        break;
      }
    }
    if (makeDefault && mi >= 0) {
      for (CadBlockConnectionMode& m : conn.modes)
        m.isDefault = false;
      conn.modes[static_cast<size_t>(mi)].isDefault = true;
    }
    // Exactly one default per connection point once any modes exist (issue #496 AC).
    if (!conn.modes.empty() &&
        std::none_of(conn.modes.begin(), conn.modes.end(), [](const CadBlockConnectionMode& m) { return m.isDefault; }))
      conn.modes.front().isDefault = true;
    log.push_back("BCONNECTMODE — set mode \"" + st.bconnectModeModeName + "\" on \"" + st.bconnectModeConnName +
                  "\".");
    stepengine::EndSession(st);
    return;
  }
}

void BConnectStart(AppCommandState& st, const std::string& nameArg, std::vector<std::string>& log) {
  using Ph = AppCommandState::BConnectPhase;
  if (st.blockEditorName.empty()) {
    log.push_back("BCONNECT — open a block with BEDIT first.");
    return;
  }
  stepengine::BeginSession(st, std::make_unique<stepengine::BConnectSession>(), /*recordAsLastCommand=*/false);
  st.bconnectPhase = Ph::WaitName;
  st.bconnectNameBuf[0] = '\0';
  st.bconnectSizeBuf[0] = '\0';
  st.bconnectRolePending = CadBlockConnectionRole::Inlet;
  st.bconnectEngagementPending = 0.f;
  st.bconnectCompatTagPending.clear();
  if (!nameArg.empty()) {
    BConnectSubmitLine(st, nameArg, log);
    return;
  }
  log.push_back("BCONNECT — connection point name:");
}

void BConnectSubmitLine(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  using Ph = AppCommandState::BConnectPhase;
  const std::string line = StringUtil::trimCopy(lineIn);

  if (st.bconnectPhase == Ph::WaitName) {
    if (line.empty()) {
      log.push_back("BCONNECT — connection point name:");
      return;
    }
    std::snprintf(st.bconnectNameBuf, sizeof(st.bconnectNameBuf), "%s", line.c_str());
    st.bconnectPhase = Ph::WaitNominalSize;
    log.push_back("BCONNECT — nominal size (e.g. 2in, Enter to skip):");
    return;
  }
  if (st.bconnectPhase == Ph::WaitNominalSize) {
    std::snprintf(st.bconnectSizeBuf, sizeof(st.bconnectSizeBuf), "%s", line.c_str());
    st.bconnectPhase = Ph::WaitRole;
    log.push_back("BCONNECT — role (" + BConnectModeRolePromptList() + ", Enter for inlet):");
    return;
  }
  if (st.bconnectPhase == Ph::WaitRole) {
    if (!line.empty())
      st.bconnectRolePending = ParseCadBlockConnectionRole(line);
    st.bconnectPhase = Ph::WaitEngagement;
    log.push_back("BCONNECT — engagement length (Enter for 0):");
    return;
  }
  if (st.bconnectPhase == Ph::WaitEngagement) {
    if (!line.empty()) {
      float v = 0.f;
      if (!TryParseF(line, v)) {
        log.push_back("BCONNECT — engagement length must be a number.");
        return;
      }
      st.bconnectEngagementPending = v;
    }
    st.bconnectPhase = Ph::WaitCompatTag;
    log.push_back("BCONNECT — compatibility tag (Enter to skip):");
    return;
  }
  if (st.bconnectPhase == Ph::WaitCompatTag) {
    if (!line.empty())
      st.bconnectCompatTagPending = line;
    st.bconnectPhase = Ph::WaitPoint;
    // Arm the face pick immediately (issue #496 follow-up) rather than waiting for an extra blank
    // Enter — a click in the viewport works right away, and typing coordinates instead still works
    // because BConnectSubmitLine keeps routing here first (see the K::BConnect check in
    // ProcessCommandLineSubmit) as long as st.active hasn't been cleared yet.
    st.bconnectAwaitingFace = true;
    log.push_back("BCONNECT — pick a flat solid face in the viewport, or type x,y,z,nx,ny,nz:");
    return;
  }
  if (st.bconnectPhase == Ph::WaitPoint) {
    if (line.empty()) {
      log.push_back("BCONNECT — pick a flat solid face in the viewport, or type x,y,z,nx,ny,nz:");
      return;
    }
    std::istringstream ss(line);
    const std::vector<std::string> f = SplitCommaRest(ss);
    float x = 0.f, y = 0.f, z = 0.f, nx = 0.f, ny = 0.f, nz = 0.f;
    if (f.size() != 6 || !TryParseF(f[0], x) || !TryParseF(f[1], y) || !TryParseF(f[2], z) ||
        !TryParseF(f[3], nx) || !TryParseF(f[4], ny) || !TryParseF(f[5], nz)) {
      log.push_back("BCONNECT — type 6 comma-separated numbers x,y,z,nx,ny,nz, or click a flat face.");
      return;
    }
    st.bconnectAwaitingFace = false;
    const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
    if (di < 0) {
      log.push_back("BCONNECT — the block being edited no longer exists.");
      stepengine::EndSession(st);
      return;
    }
    CadBlockConnection conn;
    conn.name = st.bconnectNameBuf;
    conn.nominalSize = st.bconnectSizeBuf;
    conn.x = x;
    conn.y = y;
    conn.z = z;
    conn.nx = nx;
    conn.ny = ny;
    conn.nz = nz;
    conn.role = st.bconnectRolePending;
    conn.engagementLength = st.bconnectEngagementPending;
    conn.compatibilityTag = st.bconnectCompatTagPending;
    const std::string addedName = conn.name;
    st.drawing.blockDefs[static_cast<size_t>(di)].connections.push_back(std::move(conn));
    st.blockEditorDirty = true;
    log.push_back("BCONNECT — added connection \"" + addedName + "\".");
    stepengine::EndSession(st);
    return;
  }
}

void BConnectEditStart(AppCommandState& st, const std::string& connNameArg, std::vector<std::string>& log) {
  using Ph = AppCommandState::BConnectEditPhase;
  if (st.blockEditorName.empty()) {
    log.push_back("BCONNECTEDIT — open a block with BEDIT first.");
    return;
  }
  const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
  if (di < 0)
    return;
  const CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];
  if (def.connections.empty()) {
    log.push_back("BCONNECTEDIT — \"" + st.blockEditorName + "\" has no connection points yet (use BCONNECT).");
    return;
  }
  stepengine::BeginSession(st, std::make_unique<stepengine::BConnectEditSession>(), /*recordAsLastCommand=*/false);
  st.bconnectEditPhase = Ph::WaitConnName;
  st.bconnectEditConnName.clear();
  st.bconnectEditNominalSizePending.clear();
  st.bconnectEditRolePending = CadBlockConnectionRole::Inlet;
  st.bconnectEditEngagementPending = 0.f;
  st.bconnectEditCompatTagPending.clear();
  if (!connNameArg.empty()) {
    BConnectEditSubmitLine(st, connNameArg, log);
    return;
  }
  std::string names = "BCONNECTEDIT — connection point name (";
  for (size_t i = 0; i < def.connections.size(); ++i) {
    if (i)
      names += ", ";
    names += def.connections[i].name;
  }
  names += "):";
  log.push_back(names);
}

void BConnectEditSubmitLine(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  using Ph = AppCommandState::BConnectEditPhase;
  const std::string line = StringUtil::trimCopy(lineIn);
  const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
  if (di < 0) {
    log.push_back("BCONNECTEDIT — the block being edited no longer exists.");
    stepengine::EndSession(st);
    return;
  }
  CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];

  if (st.bconnectEditPhase == Ph::WaitConnName) {
    if (line.empty()) {
      log.push_back("BCONNECTEDIT — connection point name:");
      return;
    }
    const int ci = CadBlockFindConnection(def, line);
    if (ci < 0) {
      log.push_back("BCONNECTEDIT — no connection named \"" + line + "\" on \"" + st.blockEditorName + "\".");
      stepengine::EndSession(st);
      return;
    }
    st.bconnectEditConnName = line;
    const CadBlockConnection& c = def.connections[static_cast<size_t>(ci)];
    st.bconnectEditNominalSizePending = c.nominalSize;
    st.bconnectEditRolePending = c.role;
    st.bconnectEditEngagementPending = c.engagementLength;
    st.bconnectEditCompatTagPending = c.compatibilityTag;
    st.bconnectEditPhase = Ph::WaitRemoveConfirm;
    log.push_back("BCONNECTEDIT — remove \"" + line + "\"? (y/N):");
    return;
  }

  const int ci = CadBlockFindConnection(def, st.bconnectEditConnName);
  if (ci < 0) {
    log.push_back("BCONNECTEDIT — connection \"" + st.bconnectEditConnName + "\" is gone; canceled.");
    stepengine::EndSession(st);
    return;
  }
  CadBlockConnection& c = def.connections[static_cast<size_t>(ci)];

  if (st.bconnectEditPhase == Ph::WaitRemoveConfirm) {
    if (CadBlockEqCi(line, "y") || CadBlockEqCi(line, "yes")) {
      def.connections.erase(def.connections.begin() + ci);
      st.blockEditorDirty = true;
      log.push_back("BCONNECTEDIT — removed \"" + st.bconnectEditConnName + "\".");
      stepengine::EndSession(st);
      return;
    }
    st.bconnectEditPhase = Ph::WaitNominalSize;
    log.push_back("BCONNECTEDIT — nominal size [" + st.bconnectEditNominalSizePending +
                  "] (Enter to keep current):");
    return;
  }

  if (st.bconnectEditPhase == Ph::WaitNominalSize) {
    if (!line.empty())
      st.bconnectEditNominalSizePending = line;
    st.bconnectEditPhase = Ph::WaitRole;
    log.push_back("BCONNECTEDIT — role [" + std::string(CadBlockConnectionRoleTag(st.bconnectEditRolePending)) +
                  "] (" + BConnectModeRolePromptList() + ", Enter to keep current):");
    return;
  }

  if (st.bconnectEditPhase == Ph::WaitRole) {
    if (!line.empty())
      st.bconnectEditRolePending = ParseCadBlockConnectionRole(line);
    st.bconnectEditPhase = Ph::WaitEngagement;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", st.bconnectEditEngagementPending);
    log.push_back(std::string("BCONNECTEDIT — engagement length [") + buf + "] (Enter to keep current):");
    return;
  }

  if (st.bconnectEditPhase == Ph::WaitEngagement) {
    if (!line.empty()) {
      float v = 0.f;
      if (!TryParseF(line, v)) {
        log.push_back("BCONNECTEDIT — engagement length must be a number.");
        return;
      }
      st.bconnectEditEngagementPending = v;
    }
    st.bconnectEditPhase = Ph::WaitCompatTag;
    log.push_back("BCONNECTEDIT — compatibility tag [" + st.bconnectEditCompatTagPending +
                  "] (Enter to keep current, type \"-\" to clear):");
    return;
  }

  if (st.bconnectEditPhase == Ph::WaitCompatTag) {
    if (line == "-")
      st.bconnectEditCompatTagPending.clear();
    else if (!line.empty())
      st.bconnectEditCompatTagPending = line;
    c.nominalSize = st.bconnectEditNominalSizePending;
    c.role = st.bconnectEditRolePending;
    c.engagementLength = st.bconnectEditEngagementPending;
    c.compatibilityTag = st.bconnectEditCompatTagPending;
    st.blockEditorDirty = true;
    log.push_back("BCONNECTEDIT — updated \"" + st.bconnectEditConnName + "\".");
    stepengine::EndSession(st);
    return;
  }
}
