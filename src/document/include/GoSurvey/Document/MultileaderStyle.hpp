#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

/// Active multileader style for new MLEADER callouts (issue #619).
struct MultileaderStyle {
  std::string name = "Standard";
  float textSizeInches = 0.10f;
  std::string textFont;  ///< empty = app / active text style default
  float arrowSizeInches = 0.10f;
  float landingGapInches = 0.05f;
  bool annotativeDefault = false;
  /// Leader format / structure / content settings of the Modify Multileader Style window, keyed by name.
  std::map<std::string, std::string> props;

  bool operator==(const MultileaderStyle& o) const {
    return name == o.name && textSizeInches == o.textSizeInches && textFont == o.textFont &&
           arrowSizeInches == o.arrowSizeInches && landingGapInches == o.landingGapInches &&
           annotativeDefault == o.annotativeDefault && props == o.props;
  }
  bool operator!=(const MultileaderStyle& o) const { return !(*this == o); }
};

namespace MultileaderStyles {

inline MultileaderStyle Default() { return {}; }

inline std::vector<MultileaderStyle> DefaultStyles() { return {Default()}; }

inline const MultileaderStyle* Find(const std::vector<MultileaderStyle>& styles, const std::string& name) {
  const auto it = std::find_if(styles.begin(), styles.end(), [&name](const MultileaderStyle& style) {
    return style.name == name;
  });
  return it == styles.end() ? nullptr : &*it;
}

}  // namespace MultileaderStyles
