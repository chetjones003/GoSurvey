// Precompiled header — REQ-205 / D-2026-08-31-a (GitHub issue #142).
//
// STABLE, heavy, near-universal headers only. This file is a BUILD-TIME DEVICE:
//   * it changes no produced artifact (MSVC PCH output is deterministic, REQ-200);
//   * every translation unit must still compile with the PCH disabled
//     (`-DGOSURVEY_USE_PCH=OFF`) — do not let a real `#include` disappear from a
//     .cpp just because the symbol happens to be reachable through here.
//
// Do NOT add:
//   * <Windows.h> — its min/max and other macros would leak into the pure
//     geometry TUs in gosurvey_domain (geom2d, tinbuild, …).
//   * project headers — they change often; a PCH edit then rebuilds the world.
//   * imgui.h — not on GoSurveyTests' include path (ADR-002).
//
// nlohmann/json.hpp is the single most expensive header in the tree (~25k LOC,
// re-parsed in dozens of TUs); it resolves on every target that includes this
// PCH because ${NLOHMANN_JSON_INCLUDE} is on all of their include paths.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
