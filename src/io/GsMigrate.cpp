#include "GsMigrate.hpp"

namespace {

// v1 -> v2 (REQ-314 B2b-1, D-2026-09-02-h): a `brep` solid edge may now be `CurveKind::Ellipse`
// (kind 2), carrying a second radius `r2`. A v1 document has no such edge — every solid edge is a
// line or an arc — so carrying it forward is a pure relabel and the resave is byte-identical. The
// version bump exists so an *older* GoSurvey refuses an ellipse-edge drawing by name (via the
// downgrade branch above) rather than silently mis-reading the edge.
bool MigrateV1ToV2(nlohmann::json& /*doc*/, std::string& /*err*/) { return true; }

const GsMigrationStep kSteps[] = {
    {1, "solids may carry elliptical intersection edges (B2b-1)", &MigrateV1ToV2},
};

}  // namespace

const GsMigrationStep* GsMigrationTable(size_t& countOut)
{
  countOut = sizeof(kSteps) / sizeof(kSteps[0]);
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
