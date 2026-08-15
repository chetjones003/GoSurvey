#include "GsMigrate.hpp"

namespace {

// No migrations yet: `.gs` is at version 1, and every change through REQ-044…REQ-076 was additive
// and handled by the tolerant-key pattern (ADR-030 (f)), which stays the right tool for those.
//
// The first entry here will be added by whichever change cannot be expressed additively — a
// renamed field, a changed unit, a restructured store. When that happens: bump kGsFormatVersion
// in GsIo.cpp, add a step with fromVersion equal to the OLD version, and add a test that loads a
// document in the old shape and asserts the new one.
const GsMigrationStep kSteps[] = {
    // { 1, "example: circles gain a Z coordinate", &MigrateV1ToV2 },
    {0, nullptr, nullptr},   // placeholder so the array is never zero-length; count is 0 below
};

}  // namespace

const GsMigrationStep* GsMigrationTable(size_t& countOut)
{
  countOut = 0;   // deliberately 0 — kSteps holds only the placeholder entry
  return kSteps;
}

bool ApplyGsMigrations(nlohmann::json&           doc,
                       int                       fileVersion,
                       int                       targetVersion,
                       const GsMigrationStep*    steps,
                       size_t                    stepCount,
                       std::vector<std::string>& log,
                       std::string&              err)
{
  err.clear();

  if (fileVersion <= 0)
  {
    err = "the file does not record a usable format version";
    return false;
  }
  if (fileVersion > targetVersion)
  {
    // A downgrade, not a corruption. Named precisely, because "unsupported version" sends people
    // looking for a damaged file when the answer is that they need a newer GoSurvey.
    err = "this drawing was saved by a newer version of GoSurvey (file format " +
          std::to_string(fileVersion) + "; this build understands up to " +
          std::to_string(targetVersion) + ")";
    return false;
  }
  if (fileVersion == targetVersion)
    return true;   // current: no migration, and the resave stays byte-identical

  // Walk one version at a time so a file several versions old composes through every intermediate
  // step in order (ADR-030 (c)).
  for (int v = fileVersion; v < targetVersion; ++v)
  {
    const GsMigrationStep* step = nullptr;
    for (size_t i = 0; i < stepCount; ++i)
    {
      if (steps[i].fromVersion == v)
      {
        step = &steps[i];
        break;
      }
    }
    if (!step || !step->apply)
    {
      // A gap in the chain is a programming error, not a user error, but it still has to be
      // reported rather than silently skipped — skipping would hand the typed loader a document
      // in a shape it does not expect.
      err = "no migration available from .gs format version " + std::to_string(v) + " to " +
            std::to_string(v + 1);
      return false;
    }

    std::string stepErr;
    if (!step->apply(doc, stepErr))
    {
      err = "migrating .gs format version " + std::to_string(v) + " to " + std::to_string(v + 1) +
            " failed: " + stepErr;
      return false;
    }
    log.push_back(".gs: migrated format version " + std::to_string(v) + " to " +
                  std::to_string(v + 1) +
                  (step->description ? std::string(" (") + step->description + ")" : ""));
  }
  return true;
}

bool MigrateGsDocument(nlohmann::json&           doc,
                       int                       fileVersion,
                       int                       targetVersion,
                       std::vector<std::string>& log,
                       std::string&              err)
{
  size_t                 count = 0;
  const GsMigrationStep* steps = GsMigrationTable(count);
  return ApplyGsMigrations(doc, fileVersion, targetVersion, steps, count, log, err);
}
