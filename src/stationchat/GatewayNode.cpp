
#include "GatewayNode.hpp"

#include "ChatAvatarService.hpp"
#include "ChatRoomService.hpp"
#include "MariaDB.hpp"
#include "PersistentMessageService.hpp"
#include "StationChatConfig.hpp"
#include "WebsiteIntegrationService.hpp"

GatewayNode::GatewayNode(StationChatConfig& config)
    : Node(this, config.gatewayAddress, config.gatewayPort, config.bindToIp)
    , config_{config} {
    auto connectionString = config.BuildDatabaseConnectionString();
    if (mariadb_open(connectionString.c_str(), &db_) != MARIADB_OK) {
        throw std::runtime_error("Can't open database: " + std::string{mariadb_errmsg(db_)});
    }

    avatarService_ = std::make_unique<ChatAvatarService>(db_);
    roomService_ = std::make_unique<ChatRoomService>(avatarService_.get(), db_);
    messageService_ = std::make_unique<PersistentMessageService>(db_);

    websiteIntegrationService_ = std::make_unique<WebsiteIntegrationService>(db_, config_);
}

GatewayNode::~GatewayNode() { mariadb_close(db_); }

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
