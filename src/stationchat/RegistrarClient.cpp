#include "RegistrarClient.hpp"

#include "ChatEnums.hpp"
#include "RegistrarNode.hpp"
#include "Serialization.hpp"
#include "StationChatConfig.hpp"
#include "StringUtils.hpp"

#include "protocol/RegistrarGetChatServer.hpp"
#include "RequestFailureHandling.hpp"

#include "easylogging++.h"

namespace {

bool TryReadNormalizedRequestType(
    std::istringstream& istream,
    ChatRequestType& normalizedRequestType,
    bool& requestTypeByteSwap,
    bool& usedWideRequestType) {
    usedWideRequestType = false;

    if (istream.rdbuf()->in_avail() < static_cast<std::streamsize>(sizeof(uint16_t))) {
        return false;
    }

    const auto canNormalizeCode = [](uint16_t code, ChatRequestType& normalized, bool& swapped) {
        return TryNormalizeChatRequestType(static_cast<ChatRequestType>(code), normalized, swapped);
    };

    if (istream.rdbuf()->in_avail() >= static_cast<std::streamsize>(sizeof(uint32_t))) {
        const auto lowWord = peekAt<uint16_t>(istream, 0);
        const auto highWord = peekAt<uint16_t>(istream, sizeof(uint16_t));

        if (highWord == 0 && canNormalizeCode(lowWord, normalizedRequestType, requestTypeByteSwap)) {
            (void)::read<uint32_t>(istream);
            usedWideRequestType = true;
            return true;
        }

        if (lowWord == 0 && canNormalizeCode(highWord, normalizedRequestType, requestTypeByteSwap)) {
            (void)::read<uint32_t>(istream);
            usedWideRequestType = true;
            return true;
        }
    }

    const auto narrowType = ::read<uint16_t>(istream);
    return canNormalizeCode(narrowType, normalizedRequestType, requestTypeByteSwap);
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
    ChatRequestType normalized_request_type;
    bool was_byteswapped = false;
    bool usedWideRequestType = false;
    if (!TryReadNormalizedRequestType(istream, normalized_request_type, was_byteswapped, usedWideRequestType)) {
        LOG(ERROR) << "Invalid registrar message type received";
        return;
    }

    if (usedWideRequestType && !hasLoggedWideRequestTypeCompatibility_) {
        LOG(WARNING) << "Registrar request used 32-bit request type framing; enabling compatibility mode"
                     << " (further warnings suppressed)";
        hasLoggedWideRequestTypeCompatibility_ = true;
    }

    SetSerializationByteSwap(istream, was_byteswapped);
    SetConnectionByteSwap(was_byteswapped);

    switch (normalized_request_type) {
    case ChatRequestType::REGISTRAR_GETCHATSERVER: {
        char endpoint[64] = {0};
        GetConnection()->GetDestinationIp().GetAddress(endpoint);
        constexpr uint16_t kRegistrarRequestType = static_cast<uint16_t>(ChatRequestType::REGISTRAR_GETCHATSERVER);

        ReqRegistrarGetChatServer request{};
        read(istream, request);
        if (istream.fail() || istream.bad()) {
            LOG(WARNING) << "Registrar handler decode failure"
                         << " request_type=" << kRegistrarRequestType
                         << " remote=" << endpoint << ":" << GetConnection()->GetDestinationPort()
                         << " failure_category=decode";
            RegistrarGetChatServer::ResponseType response{0};
            response.result = ChatResultCode::INVALID_INPUT;
            Send(response);
            return;
        }

        RegistrarGetChatServer::ResponseType response{request.track};
        const auto failureCategory = stationchat::ExecuteHandlerWithFallbacks(
            response,
            [&]() { RegistrarGetChatServer(this, request, response); });
        if (failureCategory != stationchat::FailureCategory::NONE) {
            LOG(ERROR) << "Registrar handler execution failure"
                       << " request_type=" << kRegistrarRequestType
                       << " remote=" << endpoint << ":" << GetConnection()->GetDestinationPort()
                       << " failure_category=" << stationchat::ToString(failureCategory)
                       << " result=" << ToString(response.result);
        }

        Send(response);
    } break;
    default:
        LOG(ERROR) << "Invalid registrar message type received after normalization: "
                   << static_cast<uint16_t>(normalized_request_type);
        break;
    }
}
