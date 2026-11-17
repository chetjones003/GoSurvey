#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include <GoSurvey/Network/Network.hpp>

// Link smoke for the Network system library (#1252): the public include root resolves and the
// compiled library links with only its allowed dependencies (Core, Events).
TEST_CASE("Network library exposes its system name through the public include root",
          "[network][library]") {
  CHECK(std::string_view(GoSurvey::Network::SystemName()) == "Network");
}
