
#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

struct WebsiteIntegrationConfig {
    bool enabled{false};
    std::string userLinkTable{"web_user_avatar"};
    std::string onlineStatusTable{"web_avatar_status"};
    std::string mailTable{"web_persistent_message"};
    bool useSeparateDatabase{false};
    std::string databaseHost;
    uint16_t databasePort{0};
    std::string databaseUser;
    std::string databasePassword;
    std::string databaseSchema;
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
    std::string gatewayAddress{"127.0.0.1"};
    uint16_t gatewayPort{5001};
    std::string registrarAddress{"127.0.0.1"};
    uint16_t registrarPort{5000};
    std::string chatDatabaseHost{"127.0.0.1"};
    uint16_t chatDatabasePort{3306};
    std::string chatDatabaseUser{"stationchat"};
    std::string chatDatabasePassword;
    std::string chatDatabaseSchema{"stationchat"};
    std::string chatDatabaseSocket;
    std::string loggerConfig;
    bool bindToIp{false};
    WebsiteIntegrationConfig websiteIntegration;
};
