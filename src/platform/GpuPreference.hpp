#pragma once

#include <string>

/// Which GPU Windows hands this application on a hybrid ("switchable graphics") machine — the
/// laptops most surveyors carry, with a low-power integrated GPU beside a discrete one.
///
/// BUG-013: GoSurvey shipped without expressing any preference, so the choice fell to Windows'
/// heuristics, which are free to answer differently between launches. On the reference machine they
/// answered "integrated", and the same scene that runs at 1.38 ms on the discrete GPU took 9.27 ms
/// — a 6-9x penalty nobody asked for and nothing reported, because the application was still
/// perfectly usable, just needlessly slow.
namespace platform {

/// Mirrors the values Windows itself stores; the numbers are Microsoft's, not ours.
enum class GpuPreference {
  SystemDefault = 0,    ///< No preference recorded — Windows decides. What GoSurvey used to do.
  PowerSaving = 1,      ///< The integrated GPU. Longer battery life, far less capable.
  HighPerformance = 2,  ///< The discrete GPU.
};

/// Reads the preference Windows has recorded for THIS executable.
///
/// Returns `SystemDefault` when no preference is recorded, and also when the key cannot be read at
/// all — the two are indistinguishable to a caller that only wants to know what will happen next
/// launch, and neither is an error worth reporting to a user.
[[nodiscard]] GpuPreference ReadGpuPreferenceForThisExe();

/// Records \p pref for THIS executable, so Windows applies it at the next launch.
///
/// This writes the same per-application setting the Settings → System → Display → Graphics page
/// writes, keyed by the executable's full path, under `HKCU`. Deliberately that mechanism rather
/// than something of our own: it is the one Windows actually consults, the user can see and change
/// it outside our application, and it survives our uninstall as an inert registry value rather than
/// as behaviour they cannot find the source of.
///
/// **Takes effect at the next launch**, never the current one — the GPU is bound when the process
/// starts. A caller that does not say so in its UI is telling the user something false.
///
/// \param error  on failure, receives a human-readable reason (REQ-201: this is user-initiated, so
///               it reports rather than fails quietly).
/// \returns true on success.
bool SetGpuPreferenceForThisExe(GpuPreference pref, std::string* error);

} // namespace platform
