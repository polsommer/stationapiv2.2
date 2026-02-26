#include "RegistrarClient.hpp"

#include "ChatEnums.hpp"
#include "RegistrarNode.hpp"
#include "Serialization.hpp"
#include "StationChatConfig.hpp"
#include "StringUtils.hpp"

#include "protocol/RegistrarGetChatServer.hpp"

#include "easylogging++.h"

namespace {

constexpr uint32_t kMaxRegistrarHostnameLength = 1024;

bool TryReadRegistrarLookupRequest(std::istringstream& istream, ReqRegistrarGetChatServer& request) {
    auto* buffer = istream.rdbuf();
    if (buffer->in_avail() < static_cast<std::streamsize>(sizeof(uint32_t))) {
        LOG(WARNING) << "Dropping registrar packet: payload too short for track";
        return false;
    }

    read(istream, request.track);

    if (buffer->in_avail() == 0) {
        return true;
    }

    if (buffer->in_avail() < static_cast<std::streamsize>(sizeof(uint32_t))) {
        LOG(WARNING) << "Dropping registrar packet: payload too short for hostname length";
        return false;
    }

    uint32_t hostnameLength = 0;
    read(istream, hostnameLength);

    if (hostnameLength > kMaxRegistrarHostnameLength) {
        LOG(ERROR) << "Dropping registrar packet: hostname length too large (" << hostnameLength << ")";
        return false;
    }

    const auto requiredBytes = static_cast<std::streamsize>(hostnameLength * sizeof(uint16_t) + sizeof(uint16_t));
    if (buffer->in_avail() < requiredBytes) {
        LOG(WARNING) << "Dropping registrar packet: payload too short for hostname/port";
        return false;
    }

    request.hostname.resize(hostnameLength);
    for (uint32_t index = 0; index < hostnameLength; ++index) {
        uint16_t ch;
        read(istream, ch);
        request.hostname[index] = ch;
    }

    read(istream, request.port);
    return true;
}

} // namespace

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

    SetSerializationByteSwap(istream, was_byteswapped);
    SetConnectionByteSwap(was_byteswapped);

    switch (normalized_request_type) {
    case ChatRequestType::LOGOUTAVATAR:
        LOG(WARNING) << "Received legacy registrar request type alias (1); "
                     << "handling as REGISTRAR_GETCHATSERVER";
        [[fallthrough]];
    case ChatRequestType::REGISTRAR_GETCHATSERVER: {
        ReqRegistrarGetChatServer request{};
        if (!TryReadRegistrarLookupRequest(istream, request)) {
            return;
        }

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
