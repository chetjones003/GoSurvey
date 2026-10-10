// BUG-013 — the per-application GPU preference.
//
// **What these tests can and cannot cover, stated up front.** The bug's root cause was two missing
// exported symbols, and no unit test can observe which GPU a driver handed the process: that needs
// a GL context, real hardware and an external instrument. It was verified by measurement instead —
// the same scene runs at 1.46 ms on the discrete GPU and 12.42 ms on the integrated one, with
// `nvidia-smi` confirming which was busy — and that evidence is stronger than anything assertable
// here.
//
// What IS testable, and what regresses silently, is the half that carries the user's choice: the
// value written for Windows to read at the next launch. A typo in the property string or the value
// name would leave the checkbox looking like it worked while doing nothing at all, and nobody would
// notice until they measured. That is what these tests pin down.
//
// They write to HKCU for the TEST executable's own path — never GoSurvey's — and restore whatever
// was there when they finish, so running the suite cannot change how the application starts.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "platform/GpuPreference.hpp"

using platform::GpuPreference;

namespace {

/// Restores the test executable's preference on the way out, whatever the test did or however it
/// failed. Without this a failing assertion would leave the machine's registry modified.
struct PreferenceGuard {
  GpuPreference original = platform::ReadGpuPreferenceForThisExe();
  ~PreferenceGuard() {
    std::string ignored;
    platform::SetGpuPreferenceForThisExe(original, &ignored);
  }
};

} // namespace

TEST_CASE("A GPU preference round-trips through Windows' own setting", "[gpu]") {
  PreferenceGuard guard;
  std::string err;

  REQUIRE(platform::SetGpuPreferenceForThisExe(GpuPreference::HighPerformance, &err));
  CHECK(err.empty());
  CHECK(platform::ReadGpuPreferenceForThisExe() == GpuPreference::HighPerformance);

  REQUIRE(platform::SetGpuPreferenceForThisExe(GpuPreference::PowerSaving, &err));
  CHECK(platform::ReadGpuPreferenceForThisExe() == GpuPreference::PowerSaving);
}

TEST_CASE("Clearing the preference removes it rather than recording a third state", "[gpu]") {
  // "Let Windows decide" is spelled by the value being ABSENT. Writing "GpuPreference=0;" would
  // leave the application listed on the Settings page as though it had an opinion it does not have.
  PreferenceGuard guard;
  std::string err;

  REQUIRE(platform::SetGpuPreferenceForThisExe(GpuPreference::PowerSaving, &err));
  REQUIRE(platform::ReadGpuPreferenceForThisExe() == GpuPreference::PowerSaving);

  REQUIRE(platform::SetGpuPreferenceForThisExe(GpuPreference::SystemDefault, &err));
  CHECK(platform::ReadGpuPreferenceForThisExe() == GpuPreference::SystemDefault);

  // Clearing an already-absent preference is success, not an error: it is the state asked for.
  CHECK(platform::SetGpuPreferenceForThisExe(GpuPreference::SystemDefault, &err));
  CHECK(err.empty());
}

TEST_CASE("Setting the preference twice is idempotent", "[gpu]") {
  // The settings checkbox writes on every toggle, including toggling back to where it started.
  PreferenceGuard guard;
  std::string err;

  REQUIRE(platform::SetGpuPreferenceForThisExe(GpuPreference::HighPerformance, &err));
  REQUIRE(platform::SetGpuPreferenceForThisExe(GpuPreference::HighPerformance, &err));
  CHECK(platform::ReadGpuPreferenceForThisExe() == GpuPreference::HighPerformance);
}
