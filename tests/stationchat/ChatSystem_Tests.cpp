#include "catch.hpp"

#include "stationchat/ChatEnums.hpp"
#include "stationchat/ChatSystem.hpp"

TEST_CASE("Chat system detection defaults to spatial", "[chat][system]") {
    auto type = DetermineChatSystem(u"Cantina", u"swg+server+cantina");
    REQUIRE(type == ChatSystemType::SPATIAL);
}

TEST_CASE("Chat system detection recognizes planet channels", "[chat][system]") {
    auto type = DetermineChatSystem(u"Planet Naboo", u"swg+planet+naboo");
    REQUIRE(type == ChatSystemType::PLANET);
}

TEST_CASE("Chat system detection recognizes galaxy channels", "[chat][system]") {
    auto type = DetermineChatSystem(u"Galaxy Broadcast", u"swg+galaxy+broadcast");
    REQUIRE(type == ChatSystemType::GALAXY);
}

TEST_CASE("Chat system node level normalization", "[chat][system]") {
    REQUIRE(ChatSystemFromNodeLevel(static_cast<uint32_t>(ChatSystemType::GALAXY))
        == ChatSystemType::GALAXY);
    REQUIRE(ChatSystemFromNodeLevel(42) == ChatSystemType::SPATIAL);
    REQUIRE(NodeLevelFromChatSystem(ChatSystemType::PLANET) == 1u);
}
