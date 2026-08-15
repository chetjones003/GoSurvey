#pragma once

/// Point-group membership rules (REQ-067) — the pure half.
///
/// Its own translation unit, like `EntityId.cpp` and `DwgProbe.cpp`: it depends on nothing but
/// `<string>` and `<vector>`, so `GoSurveyTests` links it without the GUI stack. The semantics here
/// are the part the user made an explicit decision about (union, not intersection — decision log
/// 2026-08-15), and the part most easily got wrong, so they must be testable in isolation.

#include <string>
#include <vector>

/// One numeric id range, inclusive at both ends. `lo <= hi` always holds after parsing.
struct PointIdRange {
  int lo = 0;
  int hi = 0;
};

/// What makes a point a member of a group. **Criteria combine as a union (OR)** — a point joins if
/// it matches any filled-in criterion (REQ-067, clarified 2026-08-15).
///
/// An **empty** criterion contributes nothing. In particular a rule with everything empty matches
/// **no** points, deliberately: treating "no filter" as "match everything" is how an entire drawing
/// would end up in a surface by accident.
struct PointGroupRule {
  /// User-typed ranges, e.g. `"1-500, 1200, 1400-1450"`. Stored as typed so the editor round-trips
  /// exactly what the user wrote; parsed on demand by \ref ParseIdRanges.
  std::string idRangesText;
  /// Wildcard against `SurveyPoint::description`. Empty = unused.
  std::string descriptionMatch;
  /// Wildcard against `SurveyPoint::rawDescription` (REQ-066). Empty = unused.
  std::string rawDescriptionMatch;
  /// Point ids picked by hand in the drawing. Always members, whatever the other criteria say.
  std::vector<int> explicitIds;

  [[nodiscard]] bool empty() const {
    return idRangesText.empty() && descriptionMatch.empty() && rawDescriptionMatch.empty() &&
           explicitIds.empty();
  }
};

/// A named, drawing-owned point group (REQ-067).
///
/// **Not an entity**: no geometry, no layer, no colour, never drawn, never selectable in the
/// viewport, never exported. It is a *rule* the drawing stores, and its membership is computed on
/// demand from the current point set — deliberately never cached, so a point imported after the
/// group was defined joins it with no edit, and a deleted point leaves nothing behind.
struct PointGroup {
  std::string name;  ///< Unique within the drawing.
  PointGroupRule rule;
};

/// Case-insensitive wildcard match. `*` matches any run (including empty), `?` exactly one
/// character. Everything else is literal.
///
/// Case-insensitive because field codes are typed by whoever is holding the data collector, and
/// `eg` meaning something different from `EG` would be a trap rather than a feature.
[[nodiscard]] bool WildcardMatchCI(const std::string& pattern, const std::string& text);

/// Parse `"1-10, 20, 30-25"` into ranges. Reversed input is normalised (30-25 → 25-30).
///
/// \param badTokens optional; receives every token that could not be parsed, so the caller can
///        report them instead of absorbing them (REQ-201). Unparseable tokens are skipped, not
///        treated as matching nothing *and* not treated as matching everything.
[[nodiscard]] std::vector<PointIdRange> ParseIdRanges(const std::string& text,
                                                      std::vector<std::string>* badTokens = nullptr);

/// True if \p id falls in any range. Both ends are inclusive.
[[nodiscard]] bool IdInRanges(const std::vector<PointIdRange>& ranges, int id);

/// Does one point satisfy \p rule?
///
/// \param parsedRanges pass the result of \ref ParseIdRanges once for the whole resolve rather than
///        re-parsing the text per point — the rule text is fixed across a resolution pass.
[[nodiscard]] bool PointMatchesRule(const PointGroupRule& rule,
                                    const std::vector<PointIdRange>& parsedRanges, int pointId,
                                    const std::string& description, const std::string& rawDescription);
