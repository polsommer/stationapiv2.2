
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

class LoginAuthValidator;

struct GatewayClusterEndpoint {
    std::string address;
    uint16_t port{0};
    uint16_t weight{1};

    bool Matches(const std::string& otherAddress, uint16_t otherPort) const {
        return address == otherAddress && port == otherPort;
    }
};

struct WebsiteIntegrationConfig {
    bool enabled{true};
    std::string userLinkTable{"web_user_avatar"};
    std::string onlineStatusTable{"web_avatar_status"};
    std::string mailTable{"web_persistent_message"};
    bool useSeparateDatabase{false};
    std::string databaseHost{"127.0.0.1"};
    uint16_t databasePort{3306};
    std::string databaseUser{"swgplus_com"};
    std::string databasePassword;
    std::string databaseSchema{"swgplus_com_db"};
    std::string databaseSocket;
};

struct StationChatConfig {
    StationChatConfig() = default;
    StationChatConfig(const std::string& gatewayAddress_, uint16_t gatewayPort_,
        const std::string& registrarAddress_, uint16_t registrarPort_, const std::string& chatDatabaseHost_,
        uint16_t chatDatabasePort_, const std::string& chatDatabaseUser_, const std::string& chatDatabasePassword_,
        const std::string& chatDatabaseSchema_, bool bindToIp_, WebsiteIntegrationConfig websiteIntegration_ = {})
        : gatewayAddress{gatewayAddress_}
        , gatewayPort{gatewayPort_}
        , registrarAddress{registrarAddress_}
        , registrarPort{registrarPort_}
        , chatDatabaseHost{chatDatabaseHost_}
        , chatDatabasePort{chatDatabasePort_}
        , chatDatabaseUser{chatDatabaseUser_}
        , chatDatabasePassword{chatDatabasePassword_}
        , chatDatabaseSchema{chatDatabaseSchema_}
        , bindToIp{bindToIp_}
        , websiteIntegration{std::move(websiteIntegration_)} {}

    std::string BuildDatabaseConnectionString() const {
        std::ostringstream stream;
        stream << "host=" << chatDatabaseHost;
        stream << ";port=" << chatDatabasePort;
        stream << ";user=" << chatDatabaseUser;
        stream << ";password=" << chatDatabasePassword;
        stream << ";database=" << chatDatabaseSchema;
        if (!chatDatabaseSocket.empty()) {
            stream << ";socket=" << chatDatabaseSocket;
        }
        return stream.str();
    }

    static constexpr uint32_t kLegacyApiVersion = 2;
    static constexpr uint32_t kEnhancedApiVersion = 3;
    static constexpr uint32_t kCapabilityMaskForV3 = 0x1;

    uint32_t apiMinVersion{kLegacyApiVersion};
    uint32_t apiMaxVersion{kEnhancedApiVersion};
    // Keep version 2 as the default for compatibility unless explicitly configured.
    uint32_t apiDefaultVersion{kLegacyApiVersion};
    bool allowLegacyLogin{true};
    std::string gatewayAddress{"0.0.0.0"};
    uint16_t gatewayPort{5001};
    std::string registrarAddress{"0.0.0.0"};
    uint16_t registrarPort{5000};
    std::string chatDatabaseHost{"127.0.0.1"};
    uint16_t chatDatabasePort{3306};
    std::string chatDatabaseUser{"swgplus_com"};
    std::string chatDatabasePassword;
    std::string chatDatabaseSchema{"swgplus_com_db"};
    std::string chatDatabaseSocket;
    std::string loggerConfig;
    bool bindToIp{false};
    WebsiteIntegrationConfig websiteIntegration;
    const LoginAuthValidator* loginAuthValidator{nullptr};
    std::vector<GatewayClusterEndpoint> gatewayCluster;

    bool SupportsApiVersion(uint32_t version) const {
        return version >= apiMinVersion && version <= apiMaxVersion;
    }

    uint32_t ResolveApiVersionForClient(uint32_t clientVersion) const {
        if (clientVersion == kEnhancedApiVersion && SupportsApiVersion(kEnhancedApiVersion)) {
            return kEnhancedApiVersion;
        }

        if (clientVersion == kLegacyApiVersion && SupportsApiVersion(kLegacyApiVersion)) {
            return kLegacyApiVersion;
        }

        if (SupportsApiVersion(clientVersion)) {
            return clientVersion;
        }

        if (SupportsApiVersion(apiDefaultVersion)) {
            return apiDefaultVersion;
        }

        return apiMinVersion;
    }

    bool ShouldAcceptApiVersion(uint32_t clientVersion) const {
        return SupportsApiVersion(clientVersion);
    }

    uint32_t CapabilityMaskForVersion(uint32_t version) const {
        return version >= kEnhancedApiVersion ? kCapabilityMaskForV3 : 0;
    }

    void NormalizeClusterGateways() {
        for (auto& endpoint : gatewayCluster) {
            if (endpoint.weight == 0) {
                endpoint.weight = 1;
            }
        }

        GatewayClusterEndpoint selfEndpoint{gatewayAddress, gatewayPort, 1};

        std::vector<GatewayClusterEndpoint> uniqueEndpoints;
        uniqueEndpoints.reserve(gatewayCluster.size() + 1);

        auto accumulateEndpoint = [&uniqueEndpoints](const GatewayClusterEndpoint& endpoint) {
            auto existing = std::find_if(std::begin(uniqueEndpoints), std::end(uniqueEndpoints),
                [&endpoint](const GatewayClusterEndpoint& value) {
                    return value.Matches(endpoint.address, endpoint.port);
                });
            if (existing == std::end(uniqueEndpoints)) {
                uniqueEndpoints.push_back(endpoint);
            } else {
                auto totalWeight = static_cast<uint32_t>(existing->weight) + endpoint.weight;
                if (totalWeight > std::numeric_limits<uint16_t>::max()) {
                    existing->weight = std::numeric_limits<uint16_t>::max();
                } else {
                    existing->weight = static_cast<uint16_t>(totalWeight);
                }
            }
        };

        for (const auto& endpoint : gatewayCluster) {
            accumulateEndpoint(endpoint);
        }

        if (std::none_of(std::begin(uniqueEndpoints), std::end(uniqueEndpoints),
                [&selfEndpoint](const GatewayClusterEndpoint& endpoint) {
                    return endpoint.Matches(selfEndpoint.address, selfEndpoint.port);
                })) {
            uniqueEndpoints.push_back(selfEndpoint);
        }

        gatewayCluster = std::move(uniqueEndpoints);
    }
};
