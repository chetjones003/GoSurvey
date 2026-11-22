
#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include <string>
#include <vector>

namespace {

std::string BConnectModeTargetPromptList() {
  return "pipe-end, flange-face, or generic-port";
}

std::string BConnectModeRolePromptList() {
  return "inlet, outlet, or branch";
}

/// Writes \p pending (with any fields left as-typed) into the named mode on \p conn, adding it if
/// new, and enforces the single-default invariant (issue #496 AC: exactly one default per point).
void BConnectModeCommit(AppCommandState& st, CadBlockConnection& conn) {
  CadBlockConnectionMode mode;
  mode.name = st.bconnectModeModeName;
  mode.target = st.bconnectModeTargetPending;
  mode.role = st.bconnectModeRolePending;
  mode.engagementLength = st.bconnectModeEngagementPending;
  mode.compatibilityTag = st.bconnectModeCompatTagPending;
  int mi = -1;
  for (int i = 0; i < static_cast<int>(conn.modes.size()); ++i) {
    if (CadBlockEqCi(conn.modes[static_cast<size_t>(i)].name, mode.name)) {
      mi = i;
      break;
    }
  }
  mode.isDefault = mi >= 0 ? conn.modes[static_cast<size_t>(mi)].isDefault : false;
  if (mi >= 0)
    conn.modes[static_cast<size_t>(mi)] = mode;
  else
    conn.modes.push_back(mode);
  st.blockEditorDirty = true;
}

} // namespace
