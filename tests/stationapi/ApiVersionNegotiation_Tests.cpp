#include "catch.hpp"

#include "stationchat/ChatEnums.hpp"
#include "stationchat/StationChatConfig.hpp"
#include "stationchat/protocol/SetApiVersion.hpp"
#include "Serialization.hpp"

#include <sstream>

namespace {

struct NegotiationResult {
    ChatResultCode result;
    uint32_t negotiatedVersion;
    uint32_t capabilityMask;
};

NegotiationResult NegotiateVersion(const StationChatConfig& config, uint32_t clientVersion) {
    NegotiationResult response{};
    response.negotiatedVersion = config.ResolveApiVersionForClient(clientVersion);
    response.capabilityMask = config.CapabilityMaskForVersion(response.negotiatedVersion);
    response.result = config.ShouldAcceptApiVersion(clientVersion)
        ? ChatResultCode::SUCCESS
        : ChatResultCode::WRONGCHATSERVERFORREQUEST;
    return response;
}

} // namespace

SCENARIO("api version negotiation", "[stationchat][apiversion]") {
    GIVEN("a server supporting V2 and V3 with default V2") {
        StationChatConfig config;
        config.apiMinVersion = StationChatConfig::kLegacyApiVersion;
        config.apiMaxVersion = StationChatConfig::kEnhancedApiVersion;
        config.apiDefaultVersion = StationChatConfig::kLegacyApiVersion;

        WHEN("a legacy V2 client negotiates") {
            auto response = NegotiateVersion(config, StationChatConfig::kLegacyApiVersion);

            THEN("the legacy compatibility path is unchanged") {
                REQUIRE(response.result == ChatResultCode::SUCCESS);
                REQUIRE(response.negotiatedVersion == StationChatConfig::kLegacyApiVersion);
                REQUIRE(response.capabilityMask == 0);
            }
        }

        WHEN("a V3 client negotiates") {
            auto response = NegotiateVersion(config, StationChatConfig::kEnhancedApiVersion);

            THEN("V3 is negotiated and capability flags are included") {
                REQUIRE(response.result == ChatResultCode::SUCCESS);
                REQUIRE(response.negotiatedVersion == StationChatConfig::kEnhancedApiVersion);
                REQUIRE(response.capabilityMask == StationChatConfig::kCapabilityMaskForV3);
            }
        }

        WHEN("an unsupported client version negotiates") {
            auto response = NegotiateVersion(config, 5);

            THEN("the request is rejected and fallback metadata is still deterministic") {
                REQUIRE(response.result == ChatResultCode::WRONGCHATSERVERFORREQUEST);
                REQUIRE(response.negotiatedVersion == config.apiDefaultVersion);
                REQUIRE(response.capabilityMask == 0);
            }
        }
    }

    GIVEN("a server configured for V2-only support") {
        StationChatConfig config;
        config.apiMinVersion = StationChatConfig::kLegacyApiVersion;
        config.apiMaxVersion = StationChatConfig::kLegacyApiVersion;
        config.apiDefaultVersion = StationChatConfig::kLegacyApiVersion;

        WHEN("a V3 client connects") {
            auto response = NegotiateVersion(config, StationChatConfig::kEnhancedApiVersion);

            THEN("V3 negotiation is not allowed") {
                REQUIRE(response.result == ChatResultCode::WRONGCHATSERVERFORREQUEST);
                REQUIRE(response.negotiatedVersion == StationChatConfig::kLegacyApiVersion);
                REQUIRE(response.capabilityMask == 0);
            }
        }
    }
}


SCENARIO("setapiversion response serialization", "[stationchat][apiversion][serialization]") {
    GIVEN("a V2 negotiated response") {
        ResSetApiVersion response{1};
        response.result = ChatResultCode::SUCCESS;
        response.version = StationChatConfig::kLegacyApiVersion;
        response.capabilityMask = StationChatConfig::kCapabilityMaskForV3;

        std::stringstream stream(std::ios_base::out | std::ios_base::binary);

        WHEN("the response is serialized") {
            write(stream, response);

            THEN("capability bits are omitted for compatibility") {
                const auto v2Size = stream.str().size();
                REQUIRE(v2Size > 0);
                REQUIRE(v2Size % sizeof(uint32_t) == 0);
            }
        }
    }

    GIVEN("a V3 negotiated response") {
        ResSetApiVersion response{1};
        response.result = ChatResultCode::SUCCESS;
        response.version = StationChatConfig::kEnhancedApiVersion;
        response.capabilityMask = StationChatConfig::kCapabilityMaskForV3;

        std::stringstream stream(std::ios_base::out | std::ios_base::binary);

        WHEN("the response is serialized") {
            write(stream, response);

            THEN("capability bits are included") {
                const auto v3Size = stream.str().size();
                ResSetApiVersion v2Response{1};
                v2Response.result = ChatResultCode::SUCCESS;
                v2Response.version = StationChatConfig::kLegacyApiVersion;
                v2Response.capabilityMask = StationChatConfig::kCapabilityMaskForV3;
                std::stringstream v2Stream(std::ios_base::out | std::ios_base::binary);
                write(v2Stream, v2Response);

                REQUIRE(v3Size == v2Stream.str().size() + sizeof(uint32_t));
            }
        }
    }
}
