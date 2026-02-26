#include "RegistrarClient.hpp"

#include "ChatEnums.hpp"
#include "RegistrarNode.hpp"
#include "Serialization.hpp"
#include "StationChatConfig.hpp"
#include "StringUtils.hpp"

#include "protocol/RegistrarGetChatServer.hpp"

#include "easylogging++.h"

RegistrarClient::RegistrarClient(UdpConnection* connection, RegistrarNode* node)
    : NodeClient(connection)
    , node_{node} {
    connection->SetHandler(this);
}

RegistrarClient::~RegistrarClient() {}

RegistrarNode* RegistrarClient::GetNode() { return node_; }

void RegistrarClient::OnIncoming(std::istringstream& istream) {
    if (istream.rdbuf()->in_avail() < static_cast<std::streamsize>(sizeof(ChatRequestType))) {
        LOG(WARNING) << "Dropping registrar packet: payload too short for request type";
        return;
    }

    ChatRequestType request_type = ::read<ChatRequestType>(istream);
    ChatRequestType normalized_request_type;
    bool was_byteswapped = false;
    if (!TryNormalizeChatRequestType(request_type, normalized_request_type, was_byteswapped)) {
        LOG(ERROR) << "Invalid registrar message type received: "
                   << static_cast<uint16_t>(request_type);
        return;
    }

    if (was_byteswapped) {
        LOG(WARNING) << "Registrar request type required byte swap: "
                     << static_cast<uint16_t>(request_type) << " -> "
                     << static_cast<uint16_t>(normalized_request_type);
    }

    switch (normalized_request_type) {
    case ChatRequestType::REGISTRAR_GETCHATSERVER: {
        auto request = ::read<ReqRegistrarGetChatServer>(istream);
        RegistrarGetChatServer::ResponseType response{request.track};

        try {
            RegistrarGetChatServer(this, request, response);
        } catch (const ChatResultException& e) {
            response.result = e.code;
            LOG(ERROR) << "ChatAPI Error: [" << static_cast<uint32_t>(e.code) << "] " << e.message;
        }

        Send(response);
    } break;
    default:
        LOG(ERROR) << "Invalid registrar message type received after normalization: "
                   << static_cast<uint16_t>(normalized_request_type);
        break;
    }
}
