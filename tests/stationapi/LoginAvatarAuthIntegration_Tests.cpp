#include "catch.hpp"

#include "stationchat/protocol/LoginAvatar.hpp"
#include "Serialization.hpp"

#include <sstream>

namespace {

struct StaticValidator : LoginAuthValidator {
    explicit StaticValidator(LoginAuthValidationResult result_)
        : result(result_) {}

    LoginAuthValidationResult Validate(const ReqLoginAvatar&) const override {
        return result;
    }

    LoginAuthValidationResult result;
};

ReqLoginAvatar BuildBaseRequest() {
    ReqLoginAvatar request{};
    request.track = 42;
    request.userId = 77;
    request.name = u"TestUser";
    request.address = u"chat.example";
    request.loginLocation = u"station";
    request.loginPriority = 1;
    request.loginAttributes = 3;
    return request;
}

} // namespace

TEST_CASE("legacy v2 login is accepted when compatibility mode is enabled", "[stationchat][loginauth]") {
    const auto request = BuildBaseRequest();

    const auto result = ValidateLoginAvatarAuth(request, true, nullptr);

    REQUIRE(result.accepted);
    REQUIRE(result.reason == "legacy_login_fallback");
}

TEST_CASE("legacy v2 login is rejected in strict v3 mode", "[stationchat][loginauth]") {
    const auto request = BuildBaseRequest();

    const auto result = ValidateLoginAvatarAuth(request, false, nullptr);

    REQUIRE_FALSE(result.accepted);
    REQUIRE(result.reason == "legacy_login_disabled");
}

TEST_CASE("v3 login is accepted when proof fields exist and validator approves", "[stationchat][loginauth]") {
    auto request = BuildBaseRequest();
    request.authNonce = u"nonce";
    request.authProof = u"hmac";
    request.authSessionToken = u"token";

    StaticValidator validator{{true, "validator_ok"}};
    const auto result = ValidateLoginAvatarAuth(request, false, &validator);

    REQUIRE(result.accepted);
    REQUIRE(result.reason == "validator_ok");
}

TEST_CASE("v3 login is rejected when validator rejects", "[stationchat][loginauth]") {
    auto request = BuildBaseRequest();
    request.authNonce = u"nonce";
    request.authProof = u"hmac";
    request.authSessionToken = u"token";

    StaticValidator validator{{false, "bad_signature"}};
    const auto result = ValidateLoginAvatarAuth(request, true, &validator);

    REQUIRE_FALSE(result.accepted);
    REQUIRE(result.reason == "bad_signature");
}

TEST_CASE("login request decoding supports both v2 and v3 payload shapes", "[stationchat][loginauth]") {
    auto legacyReq = BuildBaseRequest();

    std::stringstream legacyStream(std::ios_base::out | std::ios_base::in | std::ios_base::binary);
    write(legacyStream, legacyReq.track);
    write(legacyStream, legacyReq.userId);
    write(legacyStream, legacyReq.name);
    write(legacyStream, legacyReq.address);
    write(legacyStream, legacyReq.loginLocation);
    write(legacyStream, legacyReq.loginPriority);
    write(legacyStream, legacyReq.loginAttributes);
    legacyStream.seekg(0);

    ReqLoginAvatar decodedLegacy{};
    read(legacyStream, decodedLegacy);

    REQUIRE_FALSE(legacyStream.fail());
    REQUIRE(decodedLegacy.track == legacyReq.track);
    REQUIRE(decodedLegacy.authNonce.empty());
    REQUIRE(decodedLegacy.authProof.empty());
    REQUIRE(decodedLegacy.authSessionToken.empty());

    auto v3Req = BuildBaseRequest();
    v3Req.authNonce = u"nonce";
    v3Req.authProof = u"hmac";
    v3Req.authSessionToken = u"token";

    std::stringstream v3Stream(std::ios_base::out | std::ios_base::in | std::ios_base::binary);
    write(v3Stream, v3Req.track);
    write(v3Stream, v3Req.userId);
    write(v3Stream, v3Req.name);
    write(v3Stream, v3Req.address);
    write(v3Stream, v3Req.loginLocation);
    write(v3Stream, v3Req.loginPriority);
    write(v3Stream, v3Req.loginAttributes);
    write(v3Stream, v3Req.authNonce);
    write(v3Stream, v3Req.authProof);
    write(v3Stream, v3Req.authSessionToken);
    v3Stream.seekg(0);

    ReqLoginAvatar decodedV3{};
    read(v3Stream, decodedV3);

    REQUIRE_FALSE(v3Stream.fail());
    REQUIRE(decodedV3.authNonce == v3Req.authNonce);
    REQUIRE(decodedV3.authProof == v3Req.authProof);
    REQUIRE(decodedV3.authSessionToken == v3Req.authSessionToken);
}
