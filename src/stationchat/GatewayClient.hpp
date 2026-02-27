
#pragma once

#include "ChatEnums.hpp"
#include "NodeClient.hpp"
#include "MariaDB.hpp"
#include "RequestFailureHandling.hpp"
#include "easylogging++.h"

class ChatAvatar;
class ChatAvatarService;
class ChatRoom;
class ChatRoomService;
class GatewayNode;
class PersistentMessageService;
class UdpConnection;

struct PersistentHeader;

struct ReqSetAvatarAttributes;
struct ReqGetAnyAvatar;

class GatewayClient : public NodeClient {
public:
    GatewayClient(UdpConnection* connection, GatewayNode* node);
    virtual ~GatewayClient();

    GatewayNode* GetNode() { return node_; }

    void SendFriendLoginUpdate(const ChatAvatar* srcAvatar, const ChatAvatar* destAvatar);
    void SendFriendLoginUpdates(const ChatAvatar* avatar);
    void SendFriendLogoutUpdates(const ChatAvatar* avatar);
    void SendDestroyRoomUpdate(const ChatAvatar* srcAvatar, uint32_t roomId, std::vector<std::u16string> targets);
    void SendInstantMessageUpdate(const ChatAvatar* srcAvatar, const ChatAvatar* destAvatar, const std::u16string& message, const std::u16string& oob);
    void SendRoomMessageUpdate(const ChatAvatar* srcAvatar, const ChatRoom* room, uint32_t messageId, const std::u16string& message, const std::u16string& oob);
    void SendEnterRoomUpdate(const ChatAvatar* srcAvatar, const ChatRoom* room);
    void SendLeaveRoomUpdate(const std::vector<std::u16string>& addresses, uint32_t srcAvatarId, uint32_t roomId);
    void SendPersistentMessageUpdate(const ChatAvatar* destAvatar, const PersistentHeader& header);
    void SendKickAvatarUpdate(const std::vector<std::u16string>& addresses, const ChatAvatar* srcAvatar, const ChatAvatar* destAvatar, const ChatRoom* room);

private:
    void OnIncoming(std::istringstream& istream) override;

    template<typename HandlerT, typename StreamT>
    void HandleIncomingMessage(StreamT& istream) {
        typedef typename HandlerT::RequestType RequestT;
        typedef typename HandlerT::ResponseType ResponseT;

        char endpoint[64] = {0};
        GetConnection()->GetDestinationIp().GetAddress(endpoint);
        const auto requestType = static_cast<uint16_t>(RequestT{}.type);

        RequestT request{};
        read(istream, request);
        if (istream.fail() || istream.bad()) {
            LOG(WARNING) << "Gateway handler decode failure"
                         << " request_type=" << requestType
                         << " remote=" << endpoint << ":" << GetConnection()->GetDestinationPort()
                         << " failure_category=decode";
            ResponseT response(request.track);
            response.result = ChatResultCode::INVALID_INPUT;
            Send(response);
            return;
        }

        ResponseT response(request.track);

        const auto failureCategory = stationchat::ExecuteHandlerWithFallbacks(
            response,
            [&]() { HandlerT(this, request, response); });
        if (failureCategory != stationchat::FailureCategory::NONE) {
            LOG(ERROR) << "Gateway handler execution failure"
                       << " request_type=" << requestType
                       << " remote=" << endpoint << ":" << GetConnection()->GetDestinationPort()
                       << " failure_category=" << stationchat::ToString(failureCategory)
                       << " result=" << ToString(response.result);
        }

        Send(response);
    }
    
    GatewayNode* node_;
    ChatAvatarService* avatarService_;
    ChatRoomService* roomService_;
    PersistentMessageService* messageService_;
    bool hasLoggedWideRequestTypeCompatibility_ = false;
};
