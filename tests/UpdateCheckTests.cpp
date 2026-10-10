// REQ-077 / REQ-078 — version ordering, manifest parsing, and the update decision.
//
// These exist because the failure they guard against has no symptom: if version ordering is
// wrong, users are simply never offered an update, and nothing anywhere reports a problem.

#include <catch2/catch_test_macros.hpp>

#include "update/UpdateCheck.hpp"

using namespace update;

namespace {

Version V(const std::string& s)
{
  Version v;
  REQUIRE(ParseVersion(s, v));
  return v;
}

}  // namespace

TEST_CASE("A plain release version parses into its three components", "[update]")
{
  const Version v = V("0.5.0");
  CHECK(v.major == 0);
  CHECK(v.minor == 5);
  CHECK(v.patch == 0);
  CHECK_FALSE(v.isPrerelease());
}

TEST_CASE("A prerelease version parses its label and number", "[update]")
{
  const Version v = V("0.5.0-beta.7");
  CHECK(v.major == 0);
  CHECK(v.minor == 5);
  CHECK(v.patch == 0);
  CHECK(v.prereleaseLabel == "beta");
  CHECK(v.prereleaseNumber == 7);
  CHECK(v.isPrerelease());
}

TEST_CASE("Malformed versions are refused, not coerced", "[update]")
{
  Version v;
  CHECK_FALSE(ParseVersion("", v));
  CHECK_FALSE(ParseVersion("0.5", v));           // incomplete core
  CHECK_FALSE(ParseVersion("0.5.0.1", v));       // four components
  CHECK_FALSE(ParseVersion("v0.5.0", v));        // tag form, not a version
  CHECK_FALSE(ParseVersion("0.5.0-beta", v));    // label with no number
  CHECK_FALSE(ParseVersion("0.5.0-beta.", v));   // number missing after the dot
  CHECK_FALSE(ParseVersion("0.5.0-beta.7x", v)); // trailing garbage
  CHECK_FALSE(ParseVersion("0.5.0 ", v));        // trailing space
  CHECK_FALSE(ParseVersion("0.05.0", v));        // leading zero
  CHECK_FALSE(ParseVersion("abc", v));
}

TEST_CASE("Versions order by major, then minor, then patch", "[update]")
{
  CHECK(CompareVersions(V("1.0.0"), V("0.9.9")) > 0);
  CHECK(CompareVersions(V("0.5.0"), V("0.4.9")) > 0);
  CHECK(CompareVersions(V("0.5.1"), V("0.5.0")) > 0);
  CHECK(CompareVersions(V("0.5.0"), V("0.5.0")) == 0);
  CHECK(CompareVersions(V("0.4.0"), V("0.5.0")) < 0);
}

// The REQ-077 acceptance condition, stated as one chain. This is the ordering that decides
// whether a beta user is ever offered the release that supersedes their build.
TEST_CASE("Beta ordering: 0.5.0-beta.2 < 0.5.0-beta.10 < 0.5.0", "[update]")
{
  CHECK(CompareVersions(V("0.5.0-beta.2"), V("0.5.0-beta.10")) < 0);
  CHECK(CompareVersions(V("0.5.0-beta.10"), V("0.5.0")) < 0);
  CHECK(CompareVersions(V("0.5.0-beta.2"), V("0.5.0")) < 0);

  // The specific mistake this pins: comparing "10" and "2" as text puts them the wrong way round.
  CHECK(CompareVersions(V("0.5.0-beta.10"), V("0.5.0-beta.2")) > 0);
}

TEST_CASE("A release outranks its own prereleases but not the next version's", "[update]")
{
  CHECK(CompareVersions(V("0.5.0"), V("0.5.0-beta.99")) > 0);
  // A beta of the NEXT version is still newer than the current release.
  CHECK(CompareVersions(V("0.6.0-beta.1"), V("0.5.0")) > 0);
}

TEST_CASE("Prereleases of the same core order by label, then number", "[update]")
{
  CHECK(CompareVersions(V("0.5.0-beta.1"), V("0.5.0-dev.1")) < 0);   // "beta" < "dev"
  CHECK(CompareVersions(V("0.5.0-beta.1"), V("0.5.0-beta.1")) == 0);
}

TEST_CASE("A well-formed manifest parses every field", "[update]")
{
  const std::string json = R"({
    "version": "0.5.0",
    "channel": "stable",
    "installerUrl": "https://example.invalid/GoSurvey-0.5.0-Installer.exe",
    "sha256": "abc123",
    "size": 5369344,
    "releasedAt": "2026-08-15T12:00:00Z",
    "mandatory": false,
    "notes": "Surfaces and point groups."
  })";

  Manifest    m;
  std::string err;
  REQUIRE(ParseManifest(json, m, err));
  CHECK(err.empty());
  CHECK(m.version == "0.5.0");
  CHECK(m.channel == "stable");
  CHECK(m.installerUrl == "https://example.invalid/GoSurvey-0.5.0-Installer.exe");
  CHECK(m.sha256 == "abc123");
  CHECK(m.size == 5369344);
  CHECK_FALSE(m.mandatory);
  CHECK(m.notes == "Surfaces and point groups.");
}

TEST_CASE("A manifest missing a required field is rejected with a reason", "[update]")
{
  Manifest    m;
  std::string err;

  // No version.
  CHECK_FALSE(ParseManifest(R"({"installerUrl":"u","sha256":"s"})", m, err));
  CHECK_FALSE(err.empty());

  // No sha256 — accepting this would push the failure past the integrity check REQ-078 needs.
  CHECK_FALSE(ParseManifest(R"({"version":"0.5.0","installerUrl":"u"})", m, err));
  CHECK_FALSE(err.empty());

  // Present but wrong type, and present but empty.
  CHECK_FALSE(ParseManifest(R"({"version":null,"installerUrl":"u","sha256":"s"})", m, err));
  CHECK_FALSE(ParseManifest(R"({"version":"","installerUrl":"u","sha256":"s"})", m, err));
}

TEST_CASE("Malformed JSON is rejected rather than throwing", "[update]")
{
  Manifest    m;
  std::string err;
  CHECK_FALSE(ParseManifest("{not json", m, err));
  CHECK_FALSE(err.empty());
  CHECK_FALSE(ParseManifest("", m, err));
  CHECK_FALSE(ParseManifest("[1,2,3]", m, err));   // valid JSON, wrong shape
}

TEST_CASE("Optional manifest fields fall back instead of failing", "[update]")
{
  Manifest    m;
  std::string err;
  REQUIRE(ParseManifest(R"({"version":"0.5.0","installerUrl":"u","sha256":"s"})", m, err));
  CHECK(m.notes.empty());
  CHECK(m.channel.empty());
  CHECK(m.size == 0);
  CHECK_FALSE(m.mandatory);
}

TEST_CASE("Stable and beta resolve to their permanent, distinct manifest URLs", "[update]")
{
  // Stable goes through `releases/latest/...`, which GitHub defines to exclude prereleases —
  // this is what makes a stable install structurally unable to see a beta (ADR-029 (d)).
  CHECK(ManifestUrlForChannel(Channel::Stable, "chetjones003/GoSurvey") ==
        "https://github.com/chetjones003/GoSurvey/releases/latest/download/latest.json");
  CHECK(ManifestUrlForChannel(Channel::Beta, "chetjones003/GoSurvey") ==
        "https://github.com/chetjones003/GoSurvey/releases/download/channel-beta/latest.json");
}

namespace {

Manifest ManifestForVersion(const std::string& version)
{
  Manifest m;
  m.version      = version;
  m.installerUrl = "https://example.invalid/setup.exe";
  m.sha256       = "abc";
  return m;
}

}  // namespace

TEST_CASE("A newer version is offered", "[update]")
{
  std::string reason;
  CHECK(DecideUpdate("0.4.0", ManifestForVersion("0.5.0"), "", reason) == Decision::Offer);
  CHECK_FALSE(reason.empty());
}

TEST_CASE("An equal or older version is not offered", "[update]")
{
  std::string reason;
  CHECK(DecideUpdate("0.5.0", ManifestForVersion("0.5.0"), "", reason) == Decision::NoUpdate);
  CHECK(DecideUpdate("0.5.0", ManifestForVersion("0.4.0"), "", reason) == Decision::NoUpdate);

  // A developer build carrying the version it is working toward must not be offered the
  // release of that same version.
  CHECK(DecideUpdate("0.5.0", ManifestForVersion("0.5.0-beta.9"), "", reason) == Decision::NoUpdate);
}

TEST_CASE("Skipping a version suppresses that version only", "[update]")
{
  std::string reason;

  // Skipped: not offered.
  CHECK(DecideUpdate("0.4.0", ManifestForVersion("0.5.0"), "0.5.0", reason) == Decision::NoUpdate);

  // A LATER version still prompts — this is what stops "skip" becoming "never update again".
  CHECK(DecideUpdate("0.4.0", ManifestForVersion("0.6.0"), "0.5.0", reason) == Decision::Offer);

  // An EARLIER skip does not suppress the newer offer either.
  CHECK(DecideUpdate("0.4.0", ManifestForVersion("0.5.0"), "0.4.5", reason) == Decision::Offer);
}

TEST_CASE("An unparseable version on either side yields no update, with a reason", "[update]")
{
  std::string reason;

  CHECK(DecideUpdate("0.4.0", ManifestForVersion("not-a-version"), "", reason) == Decision::NoUpdate);
  CHECK_FALSE(reason.empty());

  CHECK(DecideUpdate("garbage", ManifestForVersion("0.5.0"), "", reason) == Decision::NoUpdate);
  CHECK_FALSE(reason.empty());
}

TEST_CASE("Compatibility: a matching format version raises nothing", "[update]")
{
  Manifest m = ManifestForVersion("0.6.0");
  m.gsFormatVersion = 1;
  CHECK(ClassifyCompatibility(m, 1) == CompatibilityRisk::None);
}

TEST_CASE("Compatibility: a newer .gs format warns forward-only", "[update]")
{
  // Old drawings still open in the new build (that is what migration is for); the loss is that
  // drawings saved afterwards will not open in the version installed today.
  Manifest m = ManifestForVersion("0.6.0");
  m.gsFormatVersion = 2;
  CHECK(ClassifyCompatibility(m, 1) == CompatibilityRisk::ForwardOnly);
}

TEST_CASE("Compatibility: a declared break outranks the format version", "[update]")
{
  // A semantic break need not move the format version, which is why it must be declarable.
  Manifest m = ManifestForVersion("0.6.0");
  m.gsFormatVersion        = 1;   // unchanged
  m.breaksExistingDrawings = true;
  m.compatibilityWarning   = "Surfaces built before 0.6 must be rebuilt.";
  CHECK(ClassifyCompatibility(m, 1) == CompatibilityRisk::Breaking);
}

TEST_CASE("Compatibility: a manifest with no format version warns about nothing", "[update]")
{
  // Manifests published before the field existed must not make every update look risky.
  Manifest m = ManifestForVersion("0.6.0");
  CHECK(m.gsFormatVersion == 0);
  CHECK(ClassifyCompatibility(m, 1) == CompatibilityRisk::None);
}

TEST_CASE("Compatibility: an older .gs format on the offered build warns about nothing", "[update]")
{
  Manifest m = ManifestForVersion("0.6.0");
  m.gsFormatVersion = 1;
  CHECK(ClassifyCompatibility(m, 3) == CompatibilityRisk::None);
}

TEST_CASE("The compatibility block parses, and is optional", "[update]")
{
  Manifest    m;
  std::string err;

  REQUIRE(ParseManifest(R"({
    "version":"0.6.0","installerUrl":"u","sha256":"s",
    "gsFormatVersion": 2,
    "compatibility": { "breaksExistingDrawings": true, "warning": "Old surfaces must be rebuilt." }
  })", m, err));
  CHECK(m.gsFormatVersion == 2);
  CHECK(m.breaksExistingDrawings);
  CHECK(m.compatibilityWarning == "Old surfaces must be rebuilt.");

  // Absent block: parses fine, declares nothing.
  REQUIRE(ParseManifest(R"({"version":"0.6.0","installerUrl":"u","sha256":"s"})", m, err));
  CHECK_FALSE(m.breaksExistingDrawings);
  CHECK(m.compatibilityWarning.empty());
  CHECK(m.gsFormatVersion == 0);
}

TEST_CASE("A beta user is offered the next beta", "[update]")
{
  std::string reason;
  CHECK(DecideUpdate("0.5.0-beta.2", ManifestForVersion("0.5.0-beta.10"), "", reason) ==
        Decision::Offer);
  // ...and eventually the release that supersedes the whole beta line.
  CHECK(DecideUpdate("0.5.0-beta.10", ManifestForVersion("0.5.0"), "", reason) == Decision::Offer);
}
