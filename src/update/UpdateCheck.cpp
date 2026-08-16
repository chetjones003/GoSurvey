#include "UpdateCheck.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdlib>

namespace update {
namespace {

/// Reads a run of ASCII digits into \p out. Returns false on an empty run, on a non-digit, or
/// on a leading zero in a multi-digit run ("01" is not a version component). Deliberately does
/// not use std::stoi: that throws on garbage and silently stops at the first non-digit, and
/// both behaviours are wrong here.
bool ParseUInt(const std::string& s, size_t& i, int& out)
{
  const size_t start = i;
  long long    value = 0;
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
  {
    value = value * 10 + (s[i] - '0');
    if (value > 1000000)   // no legitimate version component is this large; refuse rather than overflow
      return false;
    ++i;
  }
  if (i == start)
    return false;
  if (i - start > 1 && s[start] == '0')
    return false;
  out = static_cast<int>(value);
  return true;
}

}  // namespace

bool ParseVersion(const std::string& text, Version& out)
{
  out = Version{};
  size_t i = 0;

  if (!ParseUInt(text, i, out.major)) return false;
  if (i >= text.size() || text[i] != '.') return false;
  ++i;
  if (!ParseUInt(text, i, out.minor)) return false;
  if (i >= text.size() || text[i] != '.') return false;
  ++i;
  if (!ParseUInt(text, i, out.patch)) return false;

  if (i == text.size())
    return true;   // a plain release, e.g. "0.5.0"

  // Only a prerelease suffix may follow, and it must be complete: "-<label>.<number>".
  if (text[i] != '-')
    return false;
  ++i;

  const size_t labelStart = i;
  while (i < text.size() && (std::isalpha(static_cast<unsigned char>(text[i])) || text[i] == '-'))
    ++i;
  if (i == labelStart)
    return false;
  out.prereleaseLabel = text.substr(labelStart, i - labelStart);

  if (i >= text.size() || text[i] != '.')
    return false;
  ++i;
  if (!ParseUInt(text, i, out.prereleaseNumber))
    return false;

  // Trailing garbage means we did not understand the string, and a version we do not
  // understand must not be treated as one we do.
  return i == text.size();
}

int CompareVersions(const Version& a, const Version& b)
{
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;

  // Same core version. A release outranks any prerelease of it: 0.5.0 > 0.5.0-beta.9.
  const bool aPre = a.isPrerelease();
  const bool bPre = b.isPrerelease();
  if (!aPre && !bPre) return 0;
  if (!aPre) return 1;
  if (!bPre) return -1;

  if (a.prereleaseLabel != b.prereleaseLabel)
    return a.prereleaseLabel < b.prereleaseLabel ? -1 : 1;

  // Numeric, not lexicographic: beta.10 is newer than beta.2.
  if (a.prereleaseNumber != b.prereleaseNumber)
    return a.prereleaseNumber < b.prereleaseNumber ? -1 : 1;
  return 0;
}

bool ParseManifest(const std::string& json, Manifest& out, std::string& errorOut)
{
  out     = Manifest{};
  errorOut.clear();

  nlohmann::json j = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded())
  {
    errorOut = "manifest is not valid JSON";
    return false;
  }
  if (!j.is_object())
  {
    errorOut = "manifest is not a JSON object";
    return false;
  }

  // Required fields. Each is checked for presence AND type: a manifest with version:null or
  // sha256:12345 is malformed, and accepting it would push the failure to the hash check or,
  // worse, past it.
  struct Required { const char* key; std::string* dest; };
  const Required required[] = {
      {"version", &out.version},
      {"installerUrl", &out.installerUrl},
      {"sha256", &out.sha256},
  };
  for (const Required& r : required)
  {
    if (!j.contains(r.key) || !j[r.key].is_string() || j[r.key].get<std::string>().empty())
    {
      errorOut = std::string("manifest is missing required string field '") + r.key + "'";
      return false;
    }
    *r.dest = j[r.key].get<std::string>();
  }

  // Optional fields; absence is normal, so each falls back rather than failing.
  if (j.contains("channel") && j["channel"].is_string())
    out.channel = j["channel"].get<std::string>();
  if (j.contains("notes") && j["notes"].is_string())
    out.notes = j["notes"].get<std::string>();
  if (j.contains("size") && j["size"].is_number_integer())
    out.size = j["size"].get<long long>();
  if (j.contains("mandatory") && j["mandatory"].is_boolean())
    out.mandatory = j["mandatory"].get<bool>();
  if (j.contains("gsFormatVersion") && j["gsFormatVersion"].is_number_integer())
    out.gsFormatVersion = j["gsFormatVersion"].get<int>();

  // Compatibility block. Absent in manifests written before this field existed, which must read
  // as "nothing declared" rather than as a failure — an older manifest is not a broken one.
  if (j.contains("compatibility") && j["compatibility"].is_object())
  {
    const nlohmann::json& c = j["compatibility"];
    if (c.contains("breaksExistingDrawings") && c["breaksExistingDrawings"].is_boolean())
      out.breaksExistingDrawings = c["breaksExistingDrawings"].get<bool>();
    if (c.contains("warning") && c["warning"].is_string())
      out.compatibilityWarning = c["warning"].get<std::string>();
  }

  return true;
}

CompatibilityRisk ClassifyCompatibility(const Manifest& manifest, int runningGsFormatVersion)
{
  // An explicit declaration outranks anything inferred: a semantic break need not move the
  // format version at all, which is exactly why it cannot be detected automatically.
  if (manifest.breaksExistingDrawings)
    return CompatibilityRisk::Breaking;

  // 0 means the manifest predates the field. Treat as "unknown, assume fine" rather than warning
  // on every update from an older release.
  if (manifest.gsFormatVersion > 0 && manifest.gsFormatVersion > runningGsFormatVersion)
    return CompatibilityRisk::ForwardOnly;

  return CompatibilityRisk::None;
}

std::string ManifestUrlForChannel(Channel channel, const std::string& ownerRepo)
{
  if (channel == Channel::Beta)
    return "https://github.com/" + ownerRepo + "/releases/download/channel-beta/latest.json";
  return "https://github.com/" + ownerRepo + "/releases/latest/download/latest.json";
}

Decision DecideUpdate(const std::string& runningVersion,
                      const Manifest&    manifest,
                      const std::string& skippedVersion,
                      std::string&       reasonOut)
{
  Version running;
  if (!ParseVersion(runningVersion, running))
  {
    reasonOut = "running version '" + runningVersion + "' is unparseable";
    return Decision::NoUpdate;
  }

  Version remote;
  if (!ParseVersion(manifest.version, remote))
  {
    reasonOut = "manifest version '" + manifest.version + "' is unparseable";
    return Decision::NoUpdate;
  }

  if (CompareVersions(remote, running) <= 0)
  {
    reasonOut = "up to date (running " + runningVersion + ", offered " + manifest.version + ")";
    return Decision::NoUpdate;
  }

  // Compare the parsed forms, not the raw strings: "0.5.0" and "0.05.0" would not match as
  // text, and a skip the user actually performed must hold.
  if (!skippedVersion.empty())
  {
    Version skipped;
    if (ParseVersion(skippedVersion, skipped) && CompareVersions(skipped, remote) == 0)
    {
      reasonOut = "version " + manifest.version + " was skipped by the user";
      return Decision::NoUpdate;
    }
  }

  reasonOut = "update available: " + manifest.version;
  return Decision::Offer;
}

}  // namespace update
