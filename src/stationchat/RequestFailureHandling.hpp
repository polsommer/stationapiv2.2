#pragma once

#include "ChatEnums.hpp"
#include "MariaDB.hpp"

#include <exception>

namespace stationchat {

constexpr ChatResultCode kInternalProtocolError = ChatResultCode::ROOM_UNKNOWNFAILURE;

enum class FailureCategory {
    NONE,
    CHAT_RESULT,
    DATABASE,
    STD_EXCEPTION,
    UNKNOWN,
};

inline const char* ToString(FailureCategory category) {
    switch (category) {
    case FailureCategory::NONE:
        return "none";
    case FailureCategory::CHAT_RESULT:
        return "chat_result";
    case FailureCategory::DATABASE:
        return "database";
    case FailureCategory::STD_EXCEPTION:
        return "std_exception";
    case FailureCategory::UNKNOWN:
        return "unknown";
    }

    return "unknown";
}

template <typename ResponseT, typename HandlerT>
FailureCategory ExecuteHandlerWithFallbacks(ResponseT& response, HandlerT&& handler) {
    try {
        handler();
        return FailureCategory::NONE;
    } catch (const ChatResultException& e) {
        response.result = e.code;
        return FailureCategory::CHAT_RESULT;
    } catch (const MariaDBException&) {
        response.result = ChatResultCode::DATABASE;
        return FailureCategory::DATABASE;
    } catch (const std::exception&) {
        response.result = kInternalProtocolError;
        return FailureCategory::STD_EXCEPTION;
    } catch (...) {
        response.result = kInternalProtocolError;
        return FailureCategory::UNKNOWN;
    }
}

} // namespace stationchat

