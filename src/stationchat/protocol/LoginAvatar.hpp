
#pragma once

#include "ChatAvatar.hpp"
#include "ChatEnums.hpp"

#include <string>

class ChatAvatarService;
class ChatRoomService;
class GatewayClient;

/** Begin LOGINAVATAR */

struct ReqLoginAvatar {
    const ChatRequestType type = ChatRequestType::LOGINAVATAR;
    uint32_t track;
    uint32_t userId;
    std::u16string name;
    std::u16string address;
    std::u16string loginLocation;
    int32_t loginPriority;
    int32_t loginAttributes;
    std::u16string authNonce;
    std::u16string authProof;
    std::u16string authSessionToken;

    bool HasV3AuthProof() const {
        return !authNonce.empty() || !authProof.empty() || !authSessionToken.empty();
    }
};

template <typename StreamT>
void read(StreamT& ar, ReqLoginAvatar& data) {
    read(ar, data.track);
    read(ar, data.userId);
    read(ar, data.name);
    read(ar, data.address);
    read(ar, data.loginLocation);
    read(ar, data.loginPriority);
    read(ar, data.loginAttributes);

    if (ar.peek() != std::char_traits<char>::eof()) {
        read(ar, data.authNonce);
        read(ar, data.authProof);
        read(ar, data.authSessionToken);
    }
}

struct LoginAuthValidationResult {
    bool accepted{false};
    std::string reason;
};

class LoginAuthValidator {
public:
    virtual ~LoginAuthValidator() = default;
    virtual LoginAuthValidationResult Validate(const ReqLoginAvatar& request) const = 0;
};

inline LoginAuthValidationResult ValidateLoginAvatarAuth(const ReqLoginAvatar& request,
    bool allowLegacyLogin, const LoginAuthValidator* validator) {
    if (!request.HasV3AuthProof()) {
        if (allowLegacyLogin) {
            return {true, "legacy_login_fallback"};
        }

        return {false, "legacy_login_disabled"};
    }

    if (request.authNonce.empty()) {
        return {false, "missing_auth_nonce"};
    }

    if (request.authProof.empty()) {
        return {false, "missing_auth_proof"};
    }

    if (request.authSessionToken.empty()) {
        return {false, "missing_auth_session_token"};
    }

    if (!validator) {
        return {true, "validated_by_default_v3_presence_check"};
    }

    return validator->Validate(request);
}

/** Begin LOGINAVATAR */

struct ResLoginAvatar {
    explicit ResLoginAvatar(uint32_t track_)
        : track{track_}
        , result{ChatResultCode::SUCCESS} {}

    const ChatResponseType type = ChatResponseType::LOGINAVATAR;
    uint32_t track;
    ChatResultCode result;
    const ChatAvatar* avatar;
};

template <typename StreamT>
void write(StreamT& ar, const ResLoginAvatar& data) {
    write(ar, data.type);
    write(ar, data.track);
    write(ar, data.result);

    if (data.result == ChatResultCode::SUCCESS) {
        write(ar, data.avatar);
    }
}

class LoginAvatar {
public:
    using RequestType = ReqLoginAvatar;
    using ResponseType = ResLoginAvatar;

    LoginAvatar(GatewayClient* client, const RequestType& request, ResponseType& response);

private:
    ChatAvatarService* avatarService_;
    ChatRoomService* roomService_;
};
