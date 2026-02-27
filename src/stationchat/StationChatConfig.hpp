
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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
    std::string databaseHost{"mysql73.unoeuro.com"};
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

    // Maintain compatibility with existing Star Wars Galaxies chat clients,
    // which expect protocol version 2 during the SETAPIVERSION handshake.
    const uint32_t version = 2;
    std::string gatewayAddress{"192.168.88.6"};
    uint16_t gatewayPort{5001};
    std::string registrarAddress{"192.168.88.6"};
    uint16_t registrarPort{5000};
    std::string chatDatabaseHost{"mysql73.unoeuro.com"};
    uint16_t chatDatabasePort{3306};
    std::string chatDatabaseUser{"swgplus_com"};
    std::string chatDatabasePassword;
    std::string chatDatabaseSchema{"swgplus_com_db"};
    std::string chatDatabaseSocket;
    std::string loggerConfig;
    bool bindToIp{false};
    WebsiteIntegrationConfig websiteIntegration;
    std::vector<GatewayClusterEndpoint> gatewayCluster;

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
