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
  if (st.blockEditorName.empty()) {
    log.push_back("BCONNECTMODE — open a block with BEDIT first.");
    return;
  }
  stepengine::BeginSession(st, std::make_unique<stepengine::BConnectModeSession>(), /*recordAsLastCommand=*/false);
  if (!connNameArg.empty()) {
    // Power-user shortcut (also how the ribbon button and BCONNECTMODE <name> jump straight past
    // the first prompt): reuse the phase handler itself so the two entry points can't drift apart.
    BConnectModeSubmitLine(st, connNameArg, log);
    return;
  }
  log.push_back("BCONNECTMODE — connection point name:");
}

void BConnectModeSubmitLine(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  using Ph = stepengine::BConnectModePhase;
  auto* sess = st.sessionAs<stepengine::BConnectModeSession>();
  if (!sess)
    return;
  const std::string line = StringUtil::trimCopy(lineIn);
  const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
  if (di < 0) {
    log.push_back("BCONNECTMODE — the block being edited no longer exists.");
    stepengine::EndSession(st);
    return;
  }
  CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];

  if (sess->phase == Ph::WaitConnName) {
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
    sess->connName = line;
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
    sess->phase = Ph::WaitModeName;
    log.push_back("BCONNECTMODE — mode name (existing name to edit/remove, new name to add):");
    return;
  }

  const int ci = CadBlockFindConnection(def, sess->connName);
  if (ci < 0) {
    log.push_back("BCONNECTMODE — connection \"" + sess->connName + "\" is gone; canceled.");
    stepengine::EndSession(st);
    return;
  }
  CadBlockConnection& conn = def.connections[static_cast<size_t>(ci)];

  if (sess->phase == Ph::WaitModeName) {
    if (line.empty()) {
      log.push_back("BCONNECTMODE — mode name (existing name to edit/remove, new name to add):");
      return;
    }
    sess->modeName = line;
    int mi = -1;
    for (int i = 0; i < static_cast<int>(conn.modes.size()); ++i) {
      if (CadBlockEqCi(conn.modes[static_cast<size_t>(i)].name, line)) {
        mi = i;
        break;
      }
    }
    sess->editingExisting = mi >= 0;
    if (mi >= 0) {
      const CadBlockConnectionMode& m = conn.modes[static_cast<size_t>(mi)];
      sess->targetPending = m.target;
      sess->rolePending = m.role;
      sess->engagementPending = m.engagementLength;
      sess->compatTagPending = m.compatibilityTag;
      sess->phase = Ph::WaitRemoveConfirm;
      log.push_back("BCONNECTMODE — remove mode \"" + line + "\"? (y/N):");
      return;
    }
    sess->phase = Ph::WaitTarget;
    log.push_back("BCONNECTMODE — snap target for \"" + line + "\" (" + BConnectModeTargetPromptList() + "):");
    return;
  }

  if (sess->phase == Ph::WaitRemoveConfirm) {
    if (CadBlockEqCi(line, "y") || CadBlockEqCi(line, "yes")) {
      int mi = -1;
      for (int i = 0; i < static_cast<int>(conn.modes.size()); ++i) {
        if (CadBlockEqCi(conn.modes[static_cast<size_t>(i)].name, sess->modeName)) {
          mi = i;
          break;
        }
      }
      if (mi >= 0) {
        conn.modes.erase(conn.modes.begin() + mi);
        st.blockEditorDirty = true;
        log.push_back("BCONNECTMODE — removed mode \"" + sess->modeName + "\".");
      }
      stepengine::EndSession(st);
      return;
    }
    sess->phase = Ph::WaitTarget;
    log.push_back("BCONNECTMODE — snap target for \"" + sess->modeName + "\" [" +
                  std::string(CadConnectionModeTargetTag(sess->targetPending)) + "] (" +
                  BConnectModeTargetPromptList() + ", Enter to keep current):");
    return;
  }

  if (sess->phase == Ph::WaitTarget) {
    if (!line.empty())
      sess->targetPending = ParseCadConnectionModeTarget(line);
    sess->phase = Ph::WaitRole;
    log.push_back("BCONNECTMODE — role [" + std::string(CadBlockConnectionRoleTag(sess->rolePending)) +
                  "] (" + BConnectModeRolePromptList() + ", Enter to keep current):");
    return;
  }

  if (sess->phase == Ph::WaitRole) {
    if (!line.empty())
      sess->rolePending = ParseCadBlockConnectionRole(line);
    sess->phase = Ph::WaitEngagement;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", sess->engagementPending);
    log.push_back(std::string("BCONNECTMODE — engagement length [") + buf + "] (Enter to keep current):");
    return;
  }

  if (sess->phase == Ph::WaitEngagement) {
    if (!line.empty()) {
      float v = 0.f;
      if (!TryParseF(line, v)) {
        log.push_back("BCONNECTMODE — engagement length must be a number.");
        return;
      }
      sess->engagementPending = v;
    }
    sess->phase = Ph::WaitCompatTag;
    log.push_back("BCONNECTMODE — compatibility tag [" + sess->compatTagPending +
                  "] (Enter to keep current, type \"-\" to clear):");
    return;
  }

  if (sess->phase == Ph::WaitCompatTag) {
    if (line == "-")
      sess->compatTagPending.clear();
    else if (!line.empty())
      sess->compatTagPending = line;
    sess->phase = Ph::WaitIsDefault;
    log.push_back("BCONNECTMODE — make \"" + sess->modeName + "\" the default mode? (y/N):");
    return;
  }

  if (sess->phase == Ph::WaitIsDefault) {
    const bool makeDefault = (CadBlockEqCi(line, "y") || CadBlockEqCi(line, "yes"));
    BConnectModeCommit(st, *sess, conn);
    int mi = -1;
    for (int i = 0; i < static_cast<int>(conn.modes.size()); ++i) {
      if (CadBlockEqCi(conn.modes[static_cast<size_t>(i)].name, sess->modeName)) {
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
    log.push_back("BCONNECTMODE — set mode \"" + sess->modeName + "\" on \"" + sess->connName +
                  "\".");
    stepengine::EndSession(st);
    return;
  }
}

void BConnectStart(AppCommandState& st, const std::string& nameArg, std::vector<std::string>& log) {
  if (st.blockEditorName.empty()) {
    log.push_back("BCONNECT — open a block with BEDIT first.");
    return;
  }
  stepengine::BeginSession(st, std::make_unique<stepengine::BConnectSession>(), /*recordAsLastCommand=*/false);
  if (!nameArg.empty()) {
    BConnectSubmitLine(st, nameArg, log);
    return;
  }
  log.push_back("BCONNECT — connection point name:");
}

void BConnectSubmitLine(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  using Ph = stepengine::BConnectPhase;
  auto* sess = st.sessionAs<stepengine::BConnectSession>();
  if (!sess)
    return;
  const std::string line = StringUtil::trimCopy(lineIn);

  if (sess->phase == Ph::WaitName) {
    if (line.empty()) {
      log.push_back("BCONNECT — connection point name:");
      return;
    }
    sess->name = line;
    sess->phase = Ph::WaitNominalSize;
    log.push_back("BCONNECT — nominal size (e.g. 2in, Enter to skip):");
    return;
  }
  if (sess->phase == Ph::WaitNominalSize) {
    sess->nominalSize = line;
    sess->phase = Ph::WaitRole;
    log.push_back("BCONNECT — role (" + BConnectModeRolePromptList() + ", Enter for inlet):");
    return;
  }
  if (sess->phase == Ph::WaitRole) {
    if (!line.empty())
      sess->rolePending = ParseCadBlockConnectionRole(line);
    sess->phase = Ph::WaitEngagement;
    log.push_back("BCONNECT — engagement length (Enter for 0):");
    return;
  }
  if (sess->phase == Ph::WaitEngagement) {
    if (!line.empty()) {
      float v = 0.f;
      if (!TryParseF(line, v)) {
        log.push_back("BCONNECT — engagement length must be a number.");
        return;
      }
      sess->engagementPending = v;
    }
    sess->phase = Ph::WaitCompatTag;
    log.push_back("BCONNECT — compatibility tag (Enter to skip):");
    return;
  }
  if (sess->phase == Ph::WaitCompatTag) {
    if (!line.empty())
      sess->compatTagPending = line;
    sess->phase = Ph::WaitPoint;
    // Arm the face pick immediately (issue #496 follow-up) rather than waiting for an extra blank
    // Enter — a click in the viewport works right away, and typing coordinates instead still works
    // because BConnectSubmitLine keeps routing here first while BConnectSession is active.
    sess->awaitingFace = true;
    log.push_back("BCONNECT — pick a flat solid face in the viewport, or type x,y,z,nx,ny,nz:");
    return;
  }
  if (sess->phase == Ph::WaitPoint) {
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
    sess->awaitingFace = false;
    const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
    if (di < 0) {
      log.push_back("BCONNECT — the block being edited no longer exists.");
      stepengine::EndSession(st);
      return;
    }
    CadBlockConnection conn;
    conn.name = sess->name;
    conn.nominalSize = sess->nominalSize;
    conn.x = x;
    conn.y = y;
    conn.z = z;
    conn.nx = nx;
    conn.ny = ny;
    conn.nz = nz;
    conn.role = sess->rolePending;
    conn.engagementLength = sess->engagementPending;
    conn.compatibilityTag = sess->compatTagPending;
    const std::string addedName = conn.name;
    st.drawing.blockDefs[static_cast<size_t>(di)].connections.push_back(std::move(conn));
    st.blockEditorDirty = true;
    log.push_back("BCONNECT — added connection \"" + addedName + "\".");
    stepengine::EndSession(st);
    return;
  }
}

void BConnectEditStart(AppCommandState& st, const std::string& connNameArg, std::vector<std::string>& log) {
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
  using Ph = stepengine::BConnectEditPhase;
  auto* sess = st.sessionAs<stepengine::BConnectEditSession>();
  if (!sess)
    return;
  const std::string line = StringUtil::trimCopy(lineIn);
  const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
  if (di < 0) {
    log.push_back("BCONNECTEDIT — the block being edited no longer exists.");
    stepengine::EndSession(st);
    return;
  }
  CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];

  if (sess->phase == Ph::WaitConnName) {
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
    sess->connName = line;
    const CadBlockConnection& c = def.connections[static_cast<size_t>(ci)];
    sess->nominalSizePending = c.nominalSize;
    sess->rolePending = c.role;
    sess->engagementPending = c.engagementLength;
    sess->compatTagPending = c.compatibilityTag;
    sess->phase = Ph::WaitRemoveConfirm;
    log.push_back("BCONNECTEDIT — remove \"" + line + "\"? (y/N):");
    return;
  }

  const int ci = CadBlockFindConnection(def, sess->connName);
  if (ci < 0) {
    log.push_back("BCONNECTEDIT — connection \"" + sess->connName + "\" is gone; canceled.");
    stepengine::EndSession(st);
    return;
  }
  CadBlockConnection& c = def.connections[static_cast<size_t>(ci)];

  if (sess->phase == Ph::WaitRemoveConfirm) {
    if (CadBlockEqCi(line, "y") || CadBlockEqCi(line, "yes")) {
      def.connections.erase(def.connections.begin() + ci);
      st.blockEditorDirty = true;
      log.push_back("BCONNECTEDIT — removed \"" + sess->connName + "\".");
      stepengine::EndSession(st);
      return;
    }
    sess->phase = Ph::WaitNominalSize;
    log.push_back("BCONNECTEDIT — nominal size [" + sess->nominalSizePending +
                  "] (Enter to keep current):");
    return;
  }

  if (sess->phase == Ph::WaitNominalSize) {
    if (!line.empty())
      sess->nominalSizePending = line;
    sess->phase = Ph::WaitRole;
    log.push_back("BCONNECTEDIT — role [" + std::string(CadBlockConnectionRoleTag(sess->rolePending)) +
                  "] (" + BConnectModeRolePromptList() + ", Enter to keep current):");
    return;
  }

  if (sess->phase == Ph::WaitRole) {
    if (!line.empty())
      sess->rolePending = ParseCadBlockConnectionRole(line);
    sess->phase = Ph::WaitEngagement;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", sess->engagementPending);
    log.push_back(std::string("BCONNECTEDIT — engagement length [") + buf + "] (Enter to keep current):");
    return;
  }

  if (sess->phase == Ph::WaitEngagement) {
    if (!line.empty()) {
      float v = 0.f;
      if (!TryParseF(line, v)) {
        log.push_back("BCONNECTEDIT — engagement length must be a number.");
        return;
      }
      sess->engagementPending = v;
    }
    sess->phase = Ph::WaitCompatTag;
    log.push_back("BCONNECTEDIT — compatibility tag [" + sess->compatTagPending +
                  "] (Enter to keep current, type \"-\" to clear):");
    return;
  }

  if (sess->phase == Ph::WaitCompatTag) {
    if (line == "-")
      sess->compatTagPending.clear();
    else if (!line.empty())
      sess->compatTagPending = line;
    c.nominalSize = sess->nominalSizePending;
    c.role = sess->rolePending;
    c.engagementLength = sess->engagementPending;
    c.compatibilityTag = sess->compatTagPending;
    st.blockEditorDirty = true;
    log.push_back("BCONNECTEDIT — updated \"" + sess->connName + "\".");
    stepengine::EndSession(st);
    return;
  }
}
