#include "WhatsNewLogic.hpp"

bool WhatsNewShouldAutoOpen(std::string_view runningVersion,
                            std::string_view dismissedVersion,
                            const bool alreadyAutoOpenedThisLaunch) {
  if (alreadyAutoOpenedThisLaunch)
    return false;
  if (runningVersion.empty())
    return false;
  if (!dismissedVersion.empty() && dismissedVersion == runningVersion)
    return false;
  return true;
}

bool WhatsNewDismissCheckboxChecked(std::string_view runningVersion,
                                    std::string_view dismissedVersion) {
  if (runningVersion.empty() || dismissedVersion.empty())
    return false;
  return dismissedVersion == runningVersion;
}
