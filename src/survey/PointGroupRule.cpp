#include "PointGroupRule.hpp"

#include <algorithm>
#include <cctype>

namespace {

char LowerAscii(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

/// Iterative wildcard match with backtracking — no recursion, so a pathological pattern like
/// `"*a*a*a*a*"` cannot blow the stack on a long description.
bool WildcardMatchImpl(const std::string& pat, const std::string& txt) {
  size_t p = 0, t = 0;
  size_t starP = std::string::npos;  // last '*' seen in the pattern
  size_t starT = 0;                  // where in the text that '*' was allowed to start matching

  while (t < txt.size()) {
    if (p < pat.size() && (pat[p] == '?' || LowerAscii(pat[p]) == LowerAscii(txt[t]))) {
      ++p;
      ++t;
    } else if (p < pat.size() && pat[p] == '*') {
      starP = p++;
      starT = t;
    } else if (starP != std::string::npos) {
      // Mismatch, but a '*' is open behind us: let it swallow one more character and retry.
      p = starP + 1;
      t = ++starT;
    } else {
      return false;
    }
  }
  while (p < pat.size() && pat[p] == '*')
    ++p;
  return p == pat.size();
}

/// Trim ASCII whitespace from both ends.
std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
    ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    --b;
  return s.substr(a, b - a);
}

/// Strict integer parse: the whole token must be a number, optionally signed. `std::stoi` is not
/// used because it accepts trailing garbage ("12abc" → 12), which would silently widen a range.
bool ParseIntStrict(const std::string& s, int* out) {
  if (s.empty())
    return false;
  size_t i = 0;
  bool neg = false;
  if (s[0] == '+' || s[0] == '-') {
    neg = (s[0] == '-');
    i = 1;
    if (s.size() == 1)
      return false;
  }
  long long v = 0;
  for (; i < s.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(s[i])))
      return false;
    v = v * 10 + (s[i] - '0');
    if (v > 2147483647LL)  // clamp rather than wrap; a point number this large is junk input
      return false;
  }
  *out = static_cast<int>(neg ? -v : v);
  return true;
}

} // namespace

bool WildcardMatchCI(const std::string& pattern, const std::string& text) {
  if (pattern.empty())
    return false;  // an unused criterion matches nothing — never everything (REQ-067)
  return WildcardMatchImpl(pattern, text);
}

std::vector<PointIdRange> ParseIdRanges(const std::string& text, std::vector<std::string>* badTokens) {
  std::vector<PointIdRange> out;
  size_t pos = 0;
  while (pos <= text.size()) {
    const size_t comma = text.find(',', pos);
    const std::string token = Trim(text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
    pos = (comma == std::string::npos) ? text.size() + 1 : comma + 1;
    if (token.empty())
      continue;  // "1,,2" and a trailing comma are tolerated, not errors

    // A '-' that is not the leading sign separates a range. Searching from index 1 keeps a
    // negative single id ("-5") from being read as an empty-to-5 range.
    const size_t dash = token.find('-', 1);
    if (dash == std::string::npos) {
      int v = 0;
      if (ParseIntStrict(token, &v))
        out.push_back({v, v});
      else if (badTokens)
        badTokens->push_back(token);
      continue;
    }
    int lo = 0, hi = 0;
    const std::string loS = Trim(token.substr(0, dash));
    const std::string hiS = Trim(token.substr(dash + 1));
    if (ParseIntStrict(loS, &lo) && ParseIntStrict(hiS, &hi)) {
      if (lo > hi)
        std::swap(lo, hi);  // "30-25" is a typo with an obvious intent, not an error
      out.push_back({lo, hi});
    } else if (badTokens) {
      badTokens->push_back(token);
    }
  }
  return out;
}

bool IdInRanges(const std::vector<PointIdRange>& ranges, int id) {
  for (const PointIdRange& r : ranges)
    if (id >= r.lo && id <= r.hi)
      return true;
  return false;
}

bool PointMatchesRule(const PointGroupRule& rule, const std::vector<PointIdRange>& parsedRanges,
                      int pointId, const std::string& description, const std::string& rawDescription) {
  // Union (OR): any one filled criterion is enough. Ordered cheapest-first — the id tests are
  // integer compares, the wildcards walk strings.
  if (!rule.explicitIds.empty() &&
      std::find(rule.explicitIds.begin(), rule.explicitIds.end(), pointId) != rule.explicitIds.end())
    return true;
  if (IdInRanges(parsedRanges, pointId))
    return true;
  if (WildcardMatchCI(rule.descriptionMatch, description))
    return true;
  // Falls back to `description` when the point predates REQ-066 and carries no raw code — so a
  // raw-description rule still finds points imported before the field existed, rather than
  // silently skipping every one of them.
  const std::string& raw = rawDescription.empty() ? description : rawDescription;
  if (WildcardMatchCI(rule.rawDescriptionMatch, raw))
    return true;
  return false;
}
