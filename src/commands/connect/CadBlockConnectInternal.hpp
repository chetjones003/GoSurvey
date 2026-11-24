
#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include <GoSurvey/Core/StringUtil.hpp>
#include <cctype>
#include <istream>
#include <string>
#include <vector>
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
void BConnectModeCommit(AppCommandState& st, const stepengine::BConnectModeSession& sess,
                        CadBlockConnection& conn) {
  CadBlockConnectionMode mode;
  mode.name = sess.modeName;
  mode.target = sess.targetPending;
  mode.role = sess.rolePending;
  mode.engagementLength = sess.engagementPending;
  mode.compatibilityTag = sess.compatTagPending;
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

namespace {

std::vector<std::string> SplitCommaRest(std::istream& in) {
  std::string rest;
  std::getline(in, rest);
  std::vector<std::string> f;
  std::string cur;
  for (char c : rest) {
    if (c == ',') {
      f.push_back(StringUtil::trimCopy(cur));
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty() || !rest.empty())
    f.push_back(StringUtil::trimCopy(cur));
  if (!f.empty() && f[0].empty())
    f.erase(f.begin());
  return f;
}

// Parse a float without throwing. Returns false (and leaves out untouched) when
// the token is not a clean number — callers report a usage error instead of
// letting std::invalid_argument/out_of_range escape the command dispatcher.
bool TryParseF(const std::string& s, float& out) {
  try {
    size_t used = 0;
    const float v = std::stof(s, &used);
    while (used < s.size() && std::isspace(static_cast<unsigned char>(s[used])))
      ++used;
    if (used != s.size())
      return false;
    out = v;
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace
