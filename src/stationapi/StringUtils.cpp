#include "StringUtils.hpp"

#include <codecvt>
#include <locale>
#include <optional>

std::string FromWideString(const std::u16string& str) {
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
    return converter.to_bytes(str);
}

std::u16string ToWideString(const std::string& str) {
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
    return converter.from_bytes(str);
}

std::optional<std::u16string> NullableUtf8ToWide(const unsigned char* buffer) {
    if (!buffer) {
        return std::nullopt;
    }

    const auto utf8 = reinterpret_cast<const char*>(buffer);
    return ToWideString(utf8);
}
