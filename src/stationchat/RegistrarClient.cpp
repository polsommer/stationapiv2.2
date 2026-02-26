#include "RegistrarClient.hpp"

#include "ChatEnums.hpp"
#include "RegistrarNode.hpp"
#include "Serialization.hpp"
#include "StationChatConfig.hpp"
#include "StringUtils.hpp"

#include "protocol/RegistrarGetChatServer.hpp"
#include "RequestFailureHandling.hpp"

#include "easylogging++.h"

#include <cstring>
#include <iterator>

namespace {

constexpr uint32_t kMaxRegistrarHostnameLength = 1024;

bool TryParseRegistrarLookupPayload(const std::string& payload, bool byteSwap,
    bool hasTrack, ReqRegistrarGetChatServer& request, bool& consumedAllPayload) {
    consumedAllPayload = false;
    size_t cursor = 0;

    const auto readU32 = [&payload, &cursor, byteSwap](uint32_t& value) {
        if (payload.size() - cursor < sizeof(uint32_t)) {
            return false;
        }

        std::memcpy(&value, payload.data() + cursor, sizeof(uint32_t));
        if (byteSwap) {
            value = ByteSwapIntegral(value);
        }

        cursor += sizeof(uint32_t);
        return true;
    };

    const auto readU16 = [&payload, &cursor, byteSwap](uint16_t& value) {
        if (payload.size() - cursor < sizeof(uint16_t)) {
            return false;
        }

        std::memcpy(&value, payload.data() + cursor, sizeof(uint16_t));
        if (byteSwap) {
            value = ByteSwapIntegral(value);
        }

        cursor += sizeof(uint16_t);
        return true;
    };

    if (hasTrack) {
        if (!readU32(request.track)) {
            return false;
        }

        if (cursor == payload.size()) {
            consumedAllPayload = true;
            return true;
        }
    } else {
        request.track = 0;
    }

    uint32_t hostnameLength = 0;
    if (!readU32(hostnameLength)) {
        return false;
    }

    if (hostnameLength > kMaxRegistrarHostnameLength) {
        return false;
    }

    const auto requiredBytes = static_cast<size_t>(hostnameLength) * sizeof(uint16_t)
        + sizeof(uint16_t);
    if (payload.size() - cursor < requiredBytes) {
        return false;
    }

    request.hostname.resize(hostnameLength);
    for (uint32_t index = 0; index < hostnameLength; ++index) {
        uint16_t ch;
        if (!readU16(ch)) {
            return false;
        }

        request.hostname[index] = ch;
    }

    if (!readU16(request.port)) {
        return false;
    }

    consumedAllPayload = (cursor == payload.size());
    return true;
}

bool TryReadRegistrarLookupRequest(std::istringstream& istream, ReqRegistrarGetChatServer& request, bool& payloadByteSwap) {
    const auto preferredByteSwap = GetSerializationByteSwap(istream);
    std::string payload{std::istreambuf_iterator<char>(istream), std::istreambuf_iterator<char>()};

    if (payload.empty()) {
        LOG(WARNING) << "Dropping registrar packet: payload too short for track";
        return false;
    }

    struct ParseAttempt {
        bool byteSwap;
        bool hasTrack;
    };

    const ParseAttempt attempts[] = {
        {preferredByteSwap, true},
        {preferredByteSwap, false},
        {!preferredByteSwap, true},
        {!preferredByteSwap, false},
    };

    for (const auto& attempt : attempts) {
        ReqRegistrarGetChatServer parsedRequest{};
        bool consumedAllPayload = false;
        if (!TryParseRegistrarLookupPayload(payload, attempt.byteSwap, attempt.hasTrack,
                parsedRequest, consumedAllPayload)) {
            continue;
        }

        request.track = parsedRequest.track;
        request.hostname = std::move(parsedRequest.hostname);
        request.port = parsedRequest.port;

        if (attempt.byteSwap != preferredByteSwap) {
            LOG(WARNING) << "Parsed registrar packet using alternate payload byte order";
        }

        payloadByteSwap = attempt.byteSwap;

        if (!attempt.hasTrack) {
            LOG(WARNING) << "Parsed registrar packet using legacy no-track payload format";
        }

        if (!consumedAllPayload) {
            LOG(WARNING) << "Registrar packet contained " << (payload.size())
                         << " payload bytes; trailing bytes were ignored";
        }

        return true;
    }

    LOG(ERROR) << "Dropping registrar packet: unrecognized REGISTRAR_GETCHATSERVER payload format ("
               << payload.size() << " bytes)";
    return false;
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
        char endpoint[64] = {0};
        GetConnection()->GetDestinationIp().GetAddress(endpoint);
        constexpr uint16_t kRegistrarRequestType = static_cast<uint16_t>(ChatRequestType::REGISTRAR_GETCHATSERVER);

        ReqRegistrarGetChatServer request{};
        bool payloadByteSwap = was_byteswapped;
        if (!TryReadRegistrarLookupRequest(istream, request, payloadByteSwap)) {
            LOG(WARNING) << "Registrar handler decode failure"
                         << " request_type=" << kRegistrarRequestType
                         << " remote=" << endpoint << ":" << GetConnection()->GetDestinationPort()
                         << " failure_category=decode";
            RegistrarGetChatServer::ResponseType response{request.track};
            response.result = ChatResultCode::INVALID_INPUT;
            Send(response);
            return;
        }

        SetConnectionByteSwap(payloadByteSwap);

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
