#include <catch2/catch_test_macros.hpp>

#include "WhatsNewLogic.hpp"

TEST_CASE("WhatsNewShouldAutoOpen when not dismissed and not yet shown", "[req336]") {
  REQUIRE(WhatsNewShouldAutoOpen("1.2.3", "", false));
  REQUIRE(WhatsNewShouldAutoOpen("1.2.3-beta.1", "1.2.2", false));
}

TEST_CASE("WhatsNewShouldAutoOpen suppressed when dismissed for this version", "[req336]") {
  REQUIRE_FALSE(WhatsNewShouldAutoOpen("1.2.3", "1.2.3", false));
  REQUIRE_FALSE(WhatsNewShouldAutoOpen("1.2.3-beta.7", "1.2.3-beta.7", false));
}

TEST_CASE("WhatsNewShouldAutoOpen suppressed after already opening this launch", "[req336]") {
  REQUIRE_FALSE(WhatsNewShouldAutoOpen("1.2.3", "", true));
  REQUIRE_FALSE(WhatsNewShouldAutoOpen("1.2.3", "1.2.2", true));
}

TEST_CASE("WhatsNewShouldAutoOpen returns after version change", "[req336]") {
  REQUIRE(WhatsNewShouldAutoOpen("1.2.4", "1.2.3", false));
}

TEST_CASE("WhatsNewDismissCheckboxChecked matches exact version only", "[req336]") {
  REQUIRE(WhatsNewDismissCheckboxChecked("1.2.3", "1.2.3"));
  REQUIRE_FALSE(WhatsNewDismissCheckboxChecked("1.2.3", "1.2.4"));
  REQUIRE_FALSE(WhatsNewDismissCheckboxChecked("1.2.3", ""));
  REQUIRE_FALSE(WhatsNewDismissCheckboxChecked("", "1.2.3"));
}
