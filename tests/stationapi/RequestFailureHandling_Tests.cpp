#include "catch.hpp"

#include "stationchat/RequestFailureHandling.hpp"

#include <stdexcept>

namespace {

struct FakeResponse {
    ChatResultCode result = ChatResultCode::SUCCESS;
};

} // namespace

TEST_CASE("handler failure mapping uses protocol-safe fallbacks", "[stationchat][errors]") {
    FakeResponse response{};

    const auto chatResultCategory = stationchat::ExecuteHandlerWithFallbacks(response, []() {
        throw ChatResultException{ChatResultCode::INVALID_INPUT, "bad request"};
    });
    REQUIRE(chatResultCategory == stationchat::FailureCategory::CHAT_RESULT);
    REQUIRE(response.result == ChatResultCode::INVALID_INPUT);

    response.result = ChatResultCode::SUCCESS;
    const auto databaseCategory = stationchat::ExecuteHandlerWithFallbacks(response, []() {
        throw MariaDBException{MARIADB_ERROR, "db down"};
    });
    REQUIRE(databaseCategory == stationchat::FailureCategory::DATABASE);
    REQUIRE(response.result == ChatResultCode::DATABASE);

    response.result = ChatResultCode::SUCCESS;
    const auto stdExceptionCategory = stationchat::ExecuteHandlerWithFallbacks(response, []() {
        throw std::runtime_error{"boom"};
    });
    REQUIRE(stdExceptionCategory == stationchat::FailureCategory::STD_EXCEPTION);
    REQUIRE(response.result == stationchat::kInternalProtocolError);

    response.result = ChatResultCode::SUCCESS;
    const auto unknownCategory = stationchat::ExecuteHandlerWithFallbacks(response, []() {
        throw 7;
    });
    REQUIRE(unknownCategory == stationchat::FailureCategory::UNKNOWN);
    REQUIRE(response.result == stationchat::kInternalProtocolError);
}

TEST_CASE("handler execution continues after synthetic exceptions", "[stationchat][errors]") {
    FakeResponse response{};
    int invocationCount = 0;

    const auto firstCategory = stationchat::ExecuteHandlerWithFallbacks(response, [&]() {
        ++invocationCount;
        throw std::runtime_error{"first call fails"};
    });

    const auto secondCategory = stationchat::ExecuteHandlerWithFallbacks(response, [&]() {
        ++invocationCount;
        response.result = ChatResultCode::SUCCESS;
    });

    REQUIRE(firstCategory == stationchat::FailureCategory::STD_EXCEPTION);
    REQUIRE(secondCategory == stationchat::FailureCategory::NONE);
    REQUIRE(invocationCount == 2);
    REQUIRE(response.result == ChatResultCode::SUCCESS);
}
