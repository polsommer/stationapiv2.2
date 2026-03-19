
#include "GatewayNode.hpp"

#include "ChatAvatarService.hpp"
#include "ChatRoomService.hpp"
#include "MariaDB.hpp"
#include "PersistentMessageService.hpp"
#include "StationChatConfig.hpp"
#include "WebsiteIntegrationService.hpp"

#include <string>

GatewayNode::GatewayNode(StationChatConfig& config)
    : Node(this, config.gatewayAddress, config.gatewayPort, config.bindToIp)
    , config_{config} {
    auto connectionString = config.BuildDatabaseConnectionString();
    auto result = mariadb_open(connectionString.c_str(), &db_);
    if (result != MARIADB_OK) {
        std::string errorMessage = db_ ? mariadb_errmsg(db_) : "Unknown MariaDB error";
        if (db_) {
            mariadb_close(db_);
            db_ = nullptr;
        }
        throw std::runtime_error("Can't open database: " + errorMessage);
    }

    avatarService_ = std::make_unique<ChatAvatarService>(db_);
    roomService_ = std::make_unique<ChatRoomService>(avatarService_.get(), db_);
    messageService_ = std::make_unique<PersistentMessageService>(db_);

    websiteIntegrationService_ = std::make_unique<WebsiteIntegrationService>(db_, config_);
}

GatewayNode::~GatewayNode() {
    if (db_) {
        mariadb_close(db_);
        db_ = nullptr;
    }
}

ChatAvatarService* GatewayNode::GetAvatarService() { return avatarService_.get(); }

ChatRoomService* GatewayNode::GetRoomService() { return roomService_.get(); }

PersistentMessageService* GatewayNode::GetMessageService() {
    return messageService_.get();
}

WebsiteIntegrationService* GatewayNode::GetWebsiteIntegrationService() {
    return websiteIntegrationService_.get();
}

StationChatConfig& GatewayNode::GetConfig() { return config_; }

void GatewayNode::RegisterClientAddress(const std::u16string & address, GatewayClient * client) {
    clientAddressMap_[address] = client;
}

void GatewayNode::OnTick() {}
