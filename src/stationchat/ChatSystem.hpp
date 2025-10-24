#pragma once

#include "ChatEnums.hpp"
#include "StringUtils.hpp"

#include <algorithm>
#include <cctype>
#include <string>

inline std::string NormalizeChatSystemToken(const std::u16string& value) {
    auto ascii = FromWideString(value);
    std::string normalized(ascii.size(), '\0');
    std::transform(ascii.begin(), ascii.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return normalized;
}

inline bool ContainsChatSystemKeyword(const std::string& haystack, const char* keyword) {
    return haystack.find(keyword) != std::string::npos;
}

inline bool DetectChatSystem(const std::string& token, ChatSystemType& detectedType) {
    if (ContainsChatSystemKeyword(token, "galaxy")) {
        detectedType = ChatSystemType::GALAXY;
        return true;
    }
    if (ContainsChatSystemKeyword(token, "planet")) {
        detectedType = ChatSystemType::PLANET;
        return true;
    }
    if (ContainsChatSystemKeyword(token, "spatial")) {
        detectedType = ChatSystemType::SPATIAL;
        return true;
    }
    return false;
}

inline ChatSystemType DetermineChatSystem(const std::u16string& roomName, const std::u16string& roomAddress) {
    const auto normalizedName = NormalizeChatSystemToken(roomName);
    const auto normalizedAddress = NormalizeChatSystemToken(roomAddress);

    ChatSystemType nameType = ChatSystemType::SPATIAL;
    ChatSystemType addressType = ChatSystemType::SPATIAL;

    const bool hasNameDetection = DetectChatSystem(normalizedName, nameType);
    const bool hasAddressDetection = DetectChatSystem(normalizedAddress, addressType);

    if ((hasNameDetection && nameType == ChatSystemType::GALAXY)
        || (hasAddressDetection && addressType == ChatSystemType::GALAXY)) {
        return ChatSystemType::GALAXY;
    }

    if ((hasNameDetection && nameType == ChatSystemType::PLANET)
        || (hasAddressDetection && addressType == ChatSystemType::PLANET)) {
        return ChatSystemType::PLANET;
    }

    if ((hasNameDetection && nameType == ChatSystemType::SPATIAL)
        || (hasAddressDetection && addressType == ChatSystemType::SPATIAL)) {
        return ChatSystemType::SPATIAL;
    }

    return ChatSystemType::SPATIAL;
}
