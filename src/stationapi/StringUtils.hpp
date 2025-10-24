#pragma once

#include <optional>
#include <string>

std::string FromWideString(const std::u16string& str);
std::u16string ToWideString(const std::string& str);
std::optional<std::u16string> NullableUtf8ToWide(const unsigned char* buffer);

