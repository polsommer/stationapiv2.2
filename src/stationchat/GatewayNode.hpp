
#pragma once

#include "Node.hpp"
#include "GatewayClient.hpp"

#include <map>
#include <memory>

class ChatAvatarService;
class ChatRoomService;
class PersistentMessageService;
class WebsiteIntegrationService;
struct StationChatConfig;
struct MariaDBConnection;

class GatewayNode : public Node<GatewayNode, GatewayClient> {
public:
    explicit GatewayNode(StationChatConfig& config);
    ~GatewayNode();

    ChatAvatarService* GetAvatarService();
    ChatRoomService* GetRoomService();
    PersistentMessageService* GetMessageService();
    WebsiteIntegrationService* GetWebsiteIntegrationService();
    StationChatConfig& GetConfig();

    void RegisterClientAddress(const std::u16string& address, GatewayClient* client);

    template<typename MessageT>
    void SendTo(const std::u16string& address, const MessageT& message) {
        auto find_iter = clientAddressMap_.find(address);
        if (find_iter != std::end(clientAddressMap_)) {
            find_iter->second->Send(message);
        }
    }

private:
    void OnTick() override;

    std::unique_ptr<ChatAvatarService> avatarService_;
    std::unique_ptr<ChatRoomService> roomService_;
    std::unique_ptr<PersistentMessageService> messageService_;
    std::unique_ptr<WebsiteIntegrationService> websiteIntegrationService_;
    std::map<std::u16string, GatewayClient*> clientAddressMap_;
    StationChatConfig& config_;
    MariaDBConnection* db_;
};
